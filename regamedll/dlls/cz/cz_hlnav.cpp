#include "precompiled.h"
#include "cz_npc.h"

// ---------------------------------------------------------------------------
// Half-Life monster navigation for the CZ:DS actors (monster_npc, monster_generic, the soldiers).
//
// The original cz.so carries the Half-Life SDK AI (CGraph, CTestHull::BuildNodeGraph, CBaseMonster::BuildRoute,
// CheckLocalMove, FTriangulate, Move, MoveExecute ... - same names in cz.so). ReGameDLL has none of it (CS has no
// monsters). This is a port of the parts the actors need, following the SDK code (dlls/nodes.cpp,
// dlls/monsters.cpp):
//   - node graph: info_node origins put 8 units over the floor, linked when they see each other and a human
//     hull can WALK_MOVE (WALKMOVE_WORLDONLY, 16 unit steps) from one to the other (CTestHull::BuildNodeGraph);
//     a brush entity between two nodes (doors) is remembered on the link (LinkVisibleNodes)
//   - FindNearestNode: the closest node that is visible from the point (CGraph::FindNearestNode / CheckNode)
//   - FindShortestPath: Dijkstra over the human hull links
//   - route: BuildRoute = straight CheckLocalMove, else FTriangulate around the obstacle, else the node path;
//     Move = look ahead (CheckLocalMove up to 200 units), triangulate/refresh when blocked, advance at 8 units,
//     MoveExecute = UTIL_MoveToOrigin in steps of at most 16 units along ideal_yaw (the engine monster step)
// No collision-free steps and no jumps: when no way is left the actor stops (the caller decides what follows).
// ---------------------------------------------------------------------------

#ifndef MOVE_NORMAL
#define MOVE_NORMAL 0
#endif
#define HLNAV_NODE_HEIGHT     8.0f
#define HLNAV_HULL_STEP       16.0f
#define HLNAV_LOCAL_STEP      16.0f
#define HLNAV_CUT_CORNER      8.0f
#define HLNAV_DIST_TO_CHECK   200.0f
#define HLNAV_MAX_LINK_DIST   1536.0f

struct czNavLink
{
	int dest;
	float len;
	std::string linkModel; // brush entity on the connection (door), by its model "*N" (like the SDK m_szLinkEntModelname)
};

struct czNavNode
{
	Vector origin;            // 8 units over the floor, like the SDK
	std::vector<czNavLink> links;
};

static std::vector<czNavNode> g_hlNodes;
static std::string g_hlMap;
static bool g_hlBuilt = false;
static float g_hlBuildTime = 0;

static CBaseEntity *HlNav_LinkEnt(const czNavLink &l)
{
	if (l.linkModel.empty())
		return nullptr;
	return UTIL_FindEntityByString(nullptr, "model", l.linkModel.c_str());
}

static void HlNav_Build()
{
	const char *map = STRING(gpGlobals->mapname);
	// same map and not a fresh start of it (time goes back on a new spawn)
	if (g_hlMap == map && g_hlBuilt && gpGlobals->time >= g_hlBuildTime)
		return;
	g_hlBuildTime = gpGlobals->time;

	g_hlMap = map;
	g_hlBuilt = true;
	g_hlNodes.clear();

	CBaseEntity *pNode = nullptr;
	while ((pNode = UTIL_FindEntityByClassname(pNode, "info_node")) != nullptr)
	{
		czNavNode n;
		n.origin = pNode->pev->origin;
		// CTestHull::BuildNodeGraph: trace to the ground, pop up NODE_HEIGHT
		TraceResult tr, trEnt;
		UTIL_TraceLine(n.origin, n.origin - Vector(0, 0, 384), ignore_monsters, nullptr, &tr);
		UTIL_TraceLine(n.origin, n.origin - Vector(0, 0, 384), dont_ignore_monsters, nullptr, &trEnt);
		if (trEnt.flFraction < tr.flFraction && trEnt.pHit && (trEnt.pHit->v.flags & FL_WORLDBRUSH))
			tr.vecEndPos = trEnt.vecEndPos;
		if (tr.flFraction < 1.0f && !tr.fStartSolid)
			n.origin.z = tr.vecEndPos.z + HLNAV_NODE_HEIGHT;
		g_hlNodes.push_back(n);
	}

	int count = (int)g_hlNodes.size();
	if (!count)
	{
		ALERT(at_console, "hlnav: no info_node on %s\n", map);
		return;
	}

	// the walking test hull (CTestHull: human hull, MOVETYPE_STEP)
	edict_t *hull = CREATE_ENTITY();
	entvars_t *hp = VARS(hull);
	hp->classname = MAKE_STRING("cz_testhull");
	hp->solid = SOLID_NOT;
	hp->movetype = MOVETYPE_STEP;
	hp->effects = EF_NODRAW;
	SET_MODEL(hull, "models/player.mdl");
	UTIL_SetSize(hp, Vector(-16, -16, 0), Vector(16, 16, 72)); // VEC_HUMAN_HULL_MIN/MAX of the SDK

	int links = 0;
	for (int i = 0; i < count; i++)
	{
		for (int j = 0; j < count; j++)
		{
			if (i == j)
				continue;

			const Vector &a = g_hlNodes[i].origin, &b = g_hlNodes[j].origin;
			if ((a - b).Length() > HLNAV_MAX_LINK_DIST)
				continue;

			// LinkVisibleNodes: visible, or only one brush entity (not the world) in the way
			TraceResult tr;
			UTIL_TraceLine(a, b, ignore_monsters, nullptr, &tr);
			if (tr.fStartSolid)
				continue;
			edict_t *linkEnt = nullptr;
			if (tr.flFraction != 1.0f)
			{
				edict_t *hit = tr.pHit;
				UTIL_TraceLine(b, a, ignore_monsters, nullptr, &tr);
				if (tr.pHit == hit && hit && !FClassnameIs(&hit->v, "worldspawn"))
					linkEnt = hit;
				else
					continue;
			}

			// BuildNodeGraph walk rejection: the human hull steps from node i to node j
			UTIL_SetOrigin(hp, a);
			hp->flags |= FL_ONGROUND;
			float yaw = UTIL_VecToYaw(b - a);
			float dist = (b - a).Length2D();
			bool failed = false;
			for (float step = 0; step < dist && !failed; step += HLNAV_HULL_STEP)
			{
				float stepSize = HLNAV_HULL_STEP;
				if (step + stepSize >= dist - 1)
					stepSize = (dist - step) - 1;
				if (stepSize <= 0)
					break;
				if (!WALK_MOVE(hull, yaw, stepSize, WALKMOVE_WORLDONLY))
					failed = true;
			}
			if (!failed && (hp->origin - b).Length() > 64.0f)
				failed = true; // "bogus walk"
			if (failed)
				continue;

			czNavLink l;
			l.dest = j;
			l.len = (b - a).Length();
			if (linkEnt && linkEnt->v.model)
				l.linkModel = STRING(linkEnt->v.model);
			g_hlNodes[i].links.push_back(l);
			links++;
		}
	}

	REMOVE_ENTITY(hull);
	ALERT(at_console, "hlnav: %s: %d nodes, %d walkable links\n", map, count, links);
}

// built in the first think of the first actor (still on the loading / black intro screen), not in the middle of the game
void HlNav_EnsureBuilt()
{
	HlNav_Build();
}

// CGraph::FindNearestNode: the closest node that can be seen from the point
static int HlNav_FindNearestNode(const Vector &org, edict_t *ignore)
{
	HlNav_Build();
	int best = -1;
	float bestDist = 1e9f;
	for (int i = 0; i < (int)g_hlNodes.size(); i++)
	{
		float d = (g_hlNodes[i].origin - org).Length();
		if (d >= bestDist || d > 1024.0f)
			continue;
		TraceResult tr;
		UTIL_TraceLine(org + Vector(0, 0, 8), g_hlNodes[i].origin, ignore_monsters, ignore, &tr);
		if (tr.flFraction < 1.0f)
			continue;
		best = i;
		bestDist = d;
	}
	return best;
}

// CGraph::FindShortestPath (Dijkstra; the SDK uses it when the static routing tables are not built)
static int HlNav_FindShortestPath(int *path, int maxPath, int start, int dest)
{
	int n = (int)g_hlNodes.size();
	if (start < 0 || dest < 0 || start >= n || dest >= n)
		return 0;
	if (start == dest)
	{
		path[0] = start;
		return 1;
	}

	std::vector<float> dist(n, 1e9f);
	std::vector<int> prev(n, -1);
	std::vector<char> done(n, 0);
	dist[start] = 0;
	for (;;)
	{
		int u = -1;
		for (int i = 0; i < n; i++)
			if (!done[i] && dist[i] < 1e8f && (u < 0 || dist[i] < dist[u]))
				u = i;
		if (u < 0 || u == dest)
			break;
		done[u] = 1;
		for (const czNavLink &l : g_hlNodes[u].links)
		{
			// a locked door on the link blocks it (HandleLinkEnt), an unlocked one is opened on the way
			CBaseEntity *e = HlNav_LinkEnt(l);
			if (e && e->IsLockedByMaster())
				continue;
			float nd = dist[u] + l.len;
			if (nd < dist[l.dest])
			{
				dist[l.dest] = nd;
				prev[l.dest] = u;
			}
		}
	}
	if (prev[dest] < 0)
		return 0;

	int rev[256];
	int len = 0;
	for (int c = dest; c >= 0 && len < 256; c = prev[c])
		rev[len++] = c;
	int count = 0;
	for (int i = len - 1; i >= 0 && count < maxPath; i--)
		path[count++] = rev[i];
	return count;
}

// ---------------------------------------------------------------------------
// CBaseMonster::CheckLocalMove (SDK monsters.cpp): walk the monster's own hull in 16 unit steps from vecStart
// to vecEnd (WALKMOVE_CHECKONLY, nothing is linked), then put it back. pTarget: hitting it is a valid move.
// ---------------------------------------------------------------------------
int CMonsterNPC::HlCheckLocalMove(const Vector &vecStart, const Vector &vecEnd, CBaseEntity *pTarget, float *pflDist)
{
	Vector startPos = pev->origin;
	float yaw = UTIL_VecToYaw(vecEnd - vecStart);
	float dist = (vecEnd - vecStart).Length2D();
	int ret = HLNAV_LOCALMOVE_VALID;

	UTIL_SetOrigin(pev, vecStart);
	DROP_TO_FLOOR(ENT(pev));
	pev->flags |= FL_ONGROUND;

	for (float step = 0; step < dist; step += HLNAV_LOCAL_STEP)
	{
		float stepSize = HLNAV_LOCAL_STEP;
		if (step + HLNAV_LOCAL_STEP >= dist - 1)
			stepSize = (dist - step) - 1;
		if (stepSize <= 0)
			break;

		if (!WALK_MOVE(ENT(pev), yaw, stepSize, WALKMOVE_CHECKONLY))
		{
			if (pflDist)
				*pflDist = step;

			// "if this step hits target ent, the move is legal" (the engine global trace_ent is not set by
			// Xash for this step, so look what is in front)
			bool hitTarget = false;
			if (pTarget)
			{
				float a = yaw * (M_PI / 180.0f);
				TraceResult tr;
				TRACE_MONSTER_HULL(ENT(pev), pev->origin, pev->origin + Vector(cos(a), sin(a), 0) * (stepSize + 8.0f), dont_ignore_monsters, ENT(pev), &tr);
				hitTarget = (tr.pHit == pTarget->edict());
			}
			ret = hitTarget ? HLNAV_LOCALMOVE_VALID : HLNAV_LOCALMOVE_INVALID;
			break;
		}
	}

	if (ret == HLNAV_LOCALMOVE_VALID && (!pTarget || (pTarget->pev->flags & FL_ONGROUND)))
	{
		// can get under the target, but not to it: go to the node graph
		if (fabs(vecEnd.z - pev->origin.z) > 64.0f)
			ret = HLNAV_LOCALMOVE_INVALID_DONT_TRIANGULATE;
	}

	UTIL_SetOrigin(pev, startPos);
	return ret;
}

// CBaseMonster::FTriangulate: points left / right of the obstacle, pushed further out 8 times
bool CMonsterNPC::HlTriangulate(const Vector &vecStart, const Vector &vecEnd, float flDist, CBaseEntity *pTarget, Vector *pApex)
{
	float sizeX = pev->size.x;
	if (sizeX < 24.0f) sizeX = 24.0f;
	else if (sizeX > 48.0f) sizeX = 48.0f;

	Vector forward = (vecEnd - vecStart).Normalize();
	Vector dir = CrossProduct(forward, Vector(0, 0, 1));

	Vector left = pev->origin + forward * (flDist + sizeX) - dir * (sizeX * 3);
	Vector right = pev->origin + forward * (flDist + sizeX) + dir * (sizeX * 3);
	Vector farSide = (m_iPath < m_nPath) ? m_path[m_iPath] : vecEnd;
	dir = dir * sizeX * 2;

	for (int i = 0; i < 8; i++)
	{
		if (HlCheckLocalMove(pev->origin, right, pTarget, nullptr) == HLNAV_LOCALMOVE_VALID
			&& HlCheckLocalMove(right, farSide, pTarget, nullptr) == HLNAV_LOCALMOVE_VALID)
		{
			*pApex = right;
			return true;
		}
		if (HlCheckLocalMove(pev->origin, left, pTarget, nullptr) == HLNAV_LOCALMOVE_VALID
			&& HlCheckLocalMove(left, farSide, pTarget, nullptr) == HLNAV_LOCALMOVE_VALID)
		{
			*pApex = left;
			return true;
		}
		right = right + dir;
		left = left - dir;
	}
	return false;
}

// CBaseMonster::BuildRoute: straight, triangulated, or over the node graph. The route is m_path[0..m_nPath-1],
// the last waypoint is the goal.
bool CMonsterNPC::HlBuildRoute(const Vector &vecGoal, CBaseEntity *pTarget)
{
	m_iPath = 0;
	m_nPath = 1;
	m_path[0] = vecGoal;

	float flDist = 0;
	int local = HlCheckLocalMove(pev->origin, vecGoal, pTarget, &flDist);
	if (local == HLNAV_LOCALMOVE_VALID)
		return true;

	Vector apex;
	if (local != HLNAV_LOCALMOVE_INVALID_DONT_TRIANGULATE && HlTriangulate(pev->origin, vecGoal, flDist, pTarget, &apex))
	{
		m_path[0] = apex;
		m_path[1] = vecGoal;
		m_nPath = 2;
		return true;
	}

	// FGetNodeRoute
	int src = HlNav_FindNearestNode(pev->origin, ENT(pev));
	int dst = HlNav_FindNearestNode(vecGoal, ENT(pev));
	if (src >= 0 && dst >= 0)
	{
		int path[64];
		int n = HlNav_FindShortestPath(path, 64, src, dst);
		if (n > 0)
		{
			int max = ARRAYSIZE(m_path) - 1;
			if (n > max)
				n = max;
			for (int i = 0; i < n; i++)
				m_path[i] = g_hlNodes[path[i]].origin;
			m_path[n] = vecGoal;
			m_nPath = n + 1;
			HlRouteSimplify(pTarget);
			return true;
		}
		ALERT(at_aiconsole, "hlnav: no path from node %d to %d\n", src, dst);
	}

	return false; // m_path keeps the goal: Move() finds out what is in the way
}

// CBaseMonster::RouteSimplify: skip waypoints that can be reached straight from the previous one
void CMonsterNPC::HlRouteSimplify(CBaseEntity *pTarget)
{
	int count = m_nPath - m_iPath;
	if (count < 2)
		return;

	Vector out[ARRAYSIZE(m_path)];
	int outCount = 0;
	Vector start = pev->origin;
	int i;
	for (i = 0; i < count - 1; i++)
	{
		const Vector &cur = m_path[m_iPath + i], &next = m_path[m_iPath + i + 1];
		if (HlCheckLocalMove(start, next, pTarget, nullptr) == HLNAV_LOCALMOVE_VALID)
			continue; // skip this one
		if (outCount < (int)ARRAYSIZE(m_path) - 1)
			out[outCount++] = cur;
		start = cur;
	}
	out[outCount++] = m_path[m_iPath + i];

	for (i = 0; i < outCount; i++)
		m_path[i] = out[i];
	m_iPath = 0;
	m_nPath = outCount;
}

// CBaseMonster::InsertWaypoint: a detour point before the current waypoint
void CMonsterNPC::HlInsertWaypoint(const Vector &where)
{
	if (m_nPath >= (int)ARRAYSIZE(m_path))
		m_nPath = ARRAYSIZE(m_path) - 1;
	for (int i = m_nPath; i > m_iPath; i--)
		m_path[i] = m_path[i - 1];
	m_path[m_iPath] = where;
	m_nPath++;
}

// a door on the link between two nodes: open it like CBaseMonster::OpenDoorAndWait
void CMonsterNPC::HlOpenDoorOnLink(const Vector &from, const Vector &to)
{
	int a = HlNav_FindNearestNode(from, ENT(pev));
	int b = HlNav_FindNearestNode(to, ENT(pev));
	if (a < 0 || b < 0)
		return;
	for (const czNavLink &l : g_hlNodes[a].links)
	{
		if (l.dest != b)
			continue;
		CBaseEntity *door = HlNav_LinkEnt(l);
		if (door && !door->IsLockedByMaster() && door->pev->classname && !Q_strncmp(STRING(door->pev->classname), "func_door", 9))
		{
			door->Use(this, this, USE_ON, 0);
			m_flMoveWaitFinished = gpGlobals->time + Q_max(0.0f, door->pev->nextthink - door->pev->ltime);
		}
		break;
	}
}

// CBaseMonster::Move + MoveExecute. Returns HLNAV_MOVE_* .
int CMonsterNPC::HlMove(float flInterval, CBaseEntity *pTarget)
{
	if (m_nPath <= 0 || m_iPath >= m_nPath)
		return HLNAV_MOVE_DONE;

	if (m_flMoveWaitFinished > gpGlobals->time)
		return HLNAV_MOVE_WAIT;

	Vector wp = m_path[m_iPath];
	// 3D like the SDK: on stairs the look-ahead end point climbs with them. (A flat end point - my first
	// version - made CheckLocalMove report "goal more than 64 units under/over the end" on every stair case:
	// the actors stopped at the bottom of stairs, test 30.10.)
	Vector dir = (wp - pev->origin).Normalize();
	float wpDist = (wp - pev->origin).Length2D();

	// MakeIdealYaw + ChangeYaw
	pev->ideal_yaw = UTIL_VecToYaw(Vector(wp.x - pev->origin.x, wp.y - pev->origin.y, 0));
	pev->angles.y = UTIL_ApproachAngle(pev->ideal_yaw, pev->angles.y, 540.0f * flInterval);

	float checkDist = Q_min(wpDist, HLNAV_DIST_TO_CHECK);
	float flDist = 0;
	CBaseEntity *target = (m_iPath == m_nPath - 1) ? pTarget : nullptr;
	if (checkDist > 1.0f && HlCheckLocalMove(pev->origin, pev->origin + dir * checkDist, target, &flDist) != HLNAV_LOCALMOVE_VALID)
	{
		// SDK Move(): a moving blocker (a door that is still opening, a train), not the player: wait for it
		{
			TraceResult bt;
			TRACE_MONSTER_HULL(ENT(pev), pev->origin, pev->origin + dir * (flDist + 16.0f), dont_ignore_monsters, ENT(pev), &bt);
			CBaseEntity *blocker = CBaseEntity::Instance(bt.pHit);
			if (blocker && blocker->edict() != INDEXENT(0) && !blocker->IsPlayer()
				&& (blocker->pev->velocity.Length() > 0.1f || blocker->pev->avelocity.Length() > 0.1f)
				&& m_nBlockWaits < 12)
			{
				m_nBlockWaits++;
				m_flMoveWaitFinished = gpGlobals->time + 0.5f;
				return HLNAV_MOVE_WAIT;
			}
		}

		Vector apex;
		if (HlTriangulate(pev->origin, wp, flDist, target, &apex))
		{
			HlInsertWaypoint(apex);
			HlRouteSimplify(target);
		}
		else
		{
			// refresh the route (FRefreshRoute) a few times with a short wait, then give up (TaskFail)
			if (m_nRouteRefresh < 3)
			{
				m_nRouteRefresh++;
				Vector goal = m_path[m_nPath - 1];
				HlBuildRoute(goal, pTarget);
				m_flMoveWaitFinished = gpGlobals->time + 0.4f;
				return HLNAV_MOVE_WAIT;
			}
			return HLNAV_MOVE_FAILED;
		}
		wp = m_path[m_iPath];
		dir = (wp - pev->origin).Normalize();
		wpDist = (wp - pev->origin).Length2D();
		pev->ideal_yaw = UTIL_VecToYaw(Vector(wp.x - pev->origin.x, wp.y - pev->origin.y, 0));
		checkDist = Q_min(wpDist, HLNAV_DIST_TO_CHECK);
	}

	// ShouldAdvanceRoute / AdvanceRoute: switch to the next waypoint a bit before reaching this one
	if (wpDist <= HLNAV_CUT_CORNER && m_iPath < m_nPath - 1)
	{
		HlOpenDoorOnLink(m_path[m_iPath], m_path[m_iPath + 1]);
		m_iPath++;
		m_nRouteRefresh = 0;
		m_nBlockWaits = 0;
		if (m_flMoveWaitFinished > gpGlobals->time)
			return HLNAV_MOVE_WAIT;
		wp = m_path[m_iPath];
		pev->ideal_yaw = UTIL_VecToYaw(Vector(wp.x - pev->origin.x, wp.y - pev->origin.y, 0));
		wpDist = (wp - pev->origin).Length2D();
		checkDist = Q_min(wpDist, HLNAV_DIST_TO_CHECK);
	}

	// "this is a hack to quit moving farther than it has looked ahead"
	float speed = (m_flGroundSpeed > 1.0f) ? m_flGroundSpeed : 100.0f;
	float total = speed * (pev->framerate > 0 ? pev->framerate : 1.0f) * flInterval;
	if (m_iPath == m_nPath - 1 && total > wpDist)
		total = wpDist; // the goal: do not run past it
	if (checkDist > 1.0f && total > checkDist)
		total = checkDist;

	// MoveExecute: UTIL_MoveToOrigin in steps of at most 16 units ("or stairs stop working")
	pev->flags |= FL_ONGROUND;
	while (total > 0.001f)
	{
		float stepLen = Q_min(16.0f, total);
		MOVE_TO_ORIGIN(ENT(pev), &wp.x, stepLen, MOVE_NORMAL);
		total -= stepLen;
	}

	if (m_iPath == m_nPath - 1 && (wp - pev->origin).Length2D() < 8.0f)
		return HLNAV_MOVE_DONE; // MovementComplete
	return HLNAV_MOVE_OK;
}

void CMonsterNPC::BuildPath(const Vector &goal)
{
	m_nRouteRefresh = 0;
	m_nBlockWaits = 0;
	if (!HlBuildRoute(goal, nullptr))
		ALERT(at_aiconsole, "hlnav: '%s' no route to %.0f %.0f %.0f\n", STRING(pev->targetname), goal.x, goal.y, goal.z);
}

// one movement step of an actor (scripted walk, follow, chase): the SDK Move() plus what the callers expect
// from the old MoveStepImpl: true = arrived (stopDist <= 0: StopMove + m_bArrived) / close enough (stopDist > 0)
bool CMonsterNPC::HlMoveStep(float stopDist)
{
	float dt = gpGlobals->time - m_flLastMove;
	m_flLastMove = gpGlobals->time;
	if (dt < 0.001f) dt = 0.001f;
	if (dt > 0.25f) dt = 0.25f;

	if (m_nPath <= 0)
	{
		m_path[0] = m_vMoveTarget;
		m_nPath = 1;
		m_iPath = 0;
	}
	m_path[m_nPath - 1] = m_vMoveTarget; // the goal may move (follow / chase)

	float dGoal = (m_vMoveTarget - pev->origin).Length2D();
	if (stopDist > 0 && dGoal <= stopDist)
	{
		pev->velocity = Vector(0, 0, 1);
		return true;
	}

	Vector before = pev->origin;
	CBaseEntity *tgt = m_bFollow ? (CBaseEntity *)m_hLeader : (CBaseEntity *)m_hMoveTargetEnt;
	int r = HlMove(dt, tgt);

	if (r == HLNAV_MOVE_DONE || (stopDist <= 0 && dGoal < 2.0f))
	{
		if (stopDist <= 0)
		{
			if (!m_bPatrolHeading)
				pev->angles.y = m_flMoveYaw;
			m_bArrived = true;
			StopMove();
		}
		return true;
	}

	if (r == HLNAV_MOVE_FAILED)
	{
		pev->velocity = Vector(0, 0, 1);
		if (stopDist <= 0)
		{
			// TaskFail: no way to the mark - no jump, the actor stays and the script goes on
			ALERT(at_console, "npc '%s': can't reach %.0f %.0f %.0f, stays at %.0f %.0f %.0f\n", STRING(pev->targetname),
				m_vMoveTarget.x, m_vMoveTarget.y, m_vMoveTarget.z, pev->origin.x, pev->origin.y, pev->origin.z);
			pev->angles.y = m_flMoveYaw;
			m_bArrived = true;
			StopMove();
			return true;
		}
		return false; // follow / chase: the caller builds a new route a bit later
	}

	// keep the velocity non-zero while walking: the client interpolates the origin (no 10 Hz studio slerp)
	Vector v = (pev->origin - before) * (1.0f / dt);
	v.z = 0;
	pev->velocity = (v.Length() > 1.0f) ? v : Vector(0, 0, 1);
	return false;
}
