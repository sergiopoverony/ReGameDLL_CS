#include "precompiled.h"
#include "cz_npc.h"

// item_generic: animated decoration prop used all over the CZ:DS maps.
// monster_npc : civilian / hostage style actor playing an idle animation,
//               fires "usetarget" when the player presses +use.

static bool Cz_IsNullName(const char *s)
{
	if (!s || !s[0])
		return true;

	return !Q_stricmp(s, "none") || !Q_stricmp(s, "null") ||
		!Q_stricmp(s, "nullnullnull") || !Q_stricmp(s, "null_null_null_null");
}

// ---------------------------------------------------------------------------
class CItemGeneric: public CBaseAnimating
{
public:
	virtual void Spawn();
	virtual void Precache();
	virtual void KeyValue(KeyValueData *pkvd);
	virtual int Save(CSave &save);
	virtual int Restore(CRestore &restore);
	virtual int ObjectCaps() { return (CBaseAnimating::ObjectCaps() & ~FCAP_ACROSS_TRANSITION); }
	virtual void Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value);

public:
	static TYPEDESCRIPTION m_SaveData[];

	void ApplySequence(const char *name);
	void EXPORT HideIfAppearTarget();
	void EXPORT AnimThink();
	void EXPORT BombSprThink();
	bool m_bAnimated = false;

	string_t m_sequenceName;
	int m_removeOnUse;
	int m_spawned;
	int m_iLightMul = 0;
};

TYPEDESCRIPTION CItemGeneric::m_SaveData[] =
{
	DEFINE_FIELD(CItemGeneric, m_iLightMul, FIELD_INTEGER),
	DEFINE_FIELD(CItemGeneric, m_sequenceName, FIELD_STRING),
	DEFINE_FIELD(CItemGeneric, m_removeOnUse, FIELD_INTEGER),
};

static cvar_t cz_bomb_fwd = { "cz_bomb_fwd", "2", 0, 2.0f, nullptr };
static cvar_t cz_bomb_right = { "cz_bomb_right", "0", 0, 0.0f, nullptr };
static cvar_t cz_bomb_up = { "cz_bomb_up", "0", 0, 0.0f, nullptr };
static bool g_czBombCvars = false;

LINK_ENTITY_TO_CLASS(item_generic, CItemGeneric, CCSItemGeneric)
IMPLEMENT_SAVERESTORE(CItemGeneric, CBaseAnimating)

void CItemGeneric::KeyValue(KeyValueData *pkvd)
{
	if (FStrEq(pkvd->szKeyName, "sequencename"))
	{
		m_sequenceName = ALLOC_STRING(pkvd->szValue);
		pkvd->fHandled = TRUE;

		if (m_spawned)
			ApplySequence(pkvd->szValue);
	}
	else if (FStrEq(pkvd->szKeyName, "removeonuse"))
	{
		m_removeOnUse = Q_atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "lightmultiplier"))
	{
		// CZ: per-model light boost = renderfx 25 (Xash kRenderFxLightMultiplier), value in iuser4 (light *= iuser4 / 10)
		m_iLightMul = Q_atoi(pkvd->szValue);
		ALERT(at_console, "item_generic: key lightmultiplier=%d\n", m_iLightMul);
		pkvd->fHandled = TRUE;
	}
	else
		CBaseAnimating::KeyValue(pkvd);
}

void CItemGeneric::ApplySequence(const char *name)
{
	int seq = -1;

	if (name && name[0])
		seq = LookupSequence(name);

	if (seq < 0)
		seq = 0;

	pev->sequence = seq;
	pev->frame = 0;
	ResetSequenceInfo();
}

void CItemGeneric::Precache()
{
	// light *= iuser4 / 10 in the engine: values <= 10 would only darken the model, so they are ignored
	if (m_iLightMul > 10)
	{
		pev->renderfx = kRenderFxLightMultiplier;
		pev->iuser4 = 10 + m_iLightMul; // Xash: light *= iuser4/10, 10 = neutral
		// iuser4 may not survive the entity delta: the multiplier also travels in renderamt (unused for rendermode normal)
		pev->renderamt = (10 + m_iLightMul) > 255 ? 255 : (10 + m_iLightMul);
	}
	if (!FStringNull(pev->model))
		PRECACHE_MODEL((char *)STRING(pev->model));
}

void CItemGeneric::Spawn()
{
	if (FStringNull(pev->model))
	{
		UTIL_Remove(this);
		return;
	}

	Precache();
	SET_MODEL(ENT(pev), STRING(pev->model));

	pev->movetype = MOVETYPE_NONE;
	pev->solid = SOLID_NOT;
	pev->takedamage = DAMAGE_NO;

	ALERT(at_console, "item_generic: spawn %s lightmul=%d\n", STRING(pev->model), m_iLightMul);
	if (m_iLightMul > 10)
	{
		pev->renderfx = kRenderFxLightMultiplier;
		pev->iuser4 = 10 + m_iLightMul; // Xash: light *= iuser4/10, 10 = neutral
		pev->renderamt = (10 + m_iLightMul) > 255 ? 255 : (10 + m_iLightMul);
		ALERT(at_console, "item_generic: %s lightmul=%d renderfx=%d renderamt=%.0f\n", STRING(pev->model), m_iLightMul, pev->renderfx, pev->renderamt);
	}

	InitBoneControllers();
	UTIL_SetOrigin(pev, pev->origin);

	ApplySequence(FStringNull(m_sequenceName) ? "idle" : STRING(m_sequenceName));
	m_spawned = 1;

	// multi-frame sequences are advanced by the server (the client does not animate a prop on its own)
	{
		studiohdr_t *pstudiohdr = (studiohdr_t *)GET_MODEL_PTR(ENT(pev));
		if (pstudiohdr && pev->sequence >= 0 && pev->sequence < pstudiohdr->numseq)
		{
			mstudioseqdesc_t *pseq = (mstudioseqdesc_t *)((byte *)pstudiohdr + pstudiohdr->seqindex) + pev->sequence;
			m_bAnimated = pseq->numframes > 1 && pev->framerate > 0.0f;
		}
	}

	if (m_bAnimated)
	{
		SetThink(&CItemGeneric::AnimThink);
		pev->nextthink = gpGlobals->time + 0.1f;
	}

	// the defuse bomb (train03): its light sprites are nudged with cz_bomb_fwd/right/up (live tuning)
	if (!FStringNull(pev->targetname) && FStrEq(STRING(pev->targetname), "bomb") && Q_strstr(STRING(pev->model), "w_c4"))
	{
		if (!g_czBombCvars)
		{
			g_czBombCvars = true;
			CVAR_REGISTER(&cz_bomb_fwd);
			CVAR_REGISTER(&cz_bomb_right);
			CVAR_REGISTER(&cz_bomb_up);
		}
		SetThink(&CItemGeneric::BombSprThink);
		pev->nextthink = gpGlobals->time + 0.5f;
		return;
	}

	// display items that a map shows later with an env_render ("appear") start hidden
	if (!FStringNull(pev->targetname))
	{
		SetThink(&CItemGeneric::HideIfAppearTarget);
		pev->nextthink = gpGlobals->time + 0.3f;
	}
}

void CItemGeneric::AnimThink()
{
	StudioFrameAdvance();

	if (m_fSequenceFinished && !m_fSequenceLoops)
	{
		pev->frame = 0;
		ResetSequenceInfo();
	}

	pev->nextthink = gpGlobals->time + 0.1f;
}

void CItemGeneric::BombSprThink()
{
	Vector f, r, u;
	UTIL_MakeVectorsPrivate(pev->angles, f, r, u);
	Vector off = f * cz_bomb_fwd.value + r * cz_bomb_right.value + u * cz_bomb_up.value;

	static const char *names[2] = { "bomb_on", "bomb_off" };
	for (int i = 0; i < 2; i++)
	{
		CBaseEntity *e = nullptr;
		while ((e = UTIL_FindEntityByString(e, "targetname", names[i])) != nullptr)
		{
			if (FStrEq(STRING(e->pev->classname), "env_sprite") == FALSE)
				continue;
			if (e->pev->iuser3 != 7771)
			{
				e->pev->iuser3 = 7771;
				e->pev->vuser1 = e->pev->origin; // original map position
			}
			UTIL_SetOrigin(e->pev, e->pev->vuser1 + off);
		}
	}
	pev->nextthink = gpGlobals->time + 0.5f;
}

void CItemGeneric::HideIfAppearTarget()
{
	CBaseEntity *pRender = nullptr;
	while ((pRender = UTIL_FindEntityByClassname(pRender, "env_render")) != nullptr)
	{
		if (FStringNull(pRender->pev->target) || !FStrEq(STRING(pRender->pev->target), STRING(pev->targetname)))
			continue;

		if (pRender->pev->renderamt >= 200.0f)
		{
			pev->rendermode = kRenderTransTexture;
			pev->renderamt = 0;
			break;
		}
	}

	if (m_bAnimated)
	{
		SetThink(&CItemGeneric::AnimThink);
		pev->nextthink = gpGlobals->time + 0.1f;
	}
	else
		pev->nextthink = 0;
}

// monster_npc_dead: a corpse in a fixed pose (deadanim), head bodygroup from "head"
class CNpcDead: public CItemGeneric
{
public:
	virtual void Spawn();
	virtual void KeyValue(KeyValueData *pkvd);
	virtual int Save(CSave &save);
	virtual int Restore(CRestore &restore);
	static TYPEDESCRIPTION m_SaveData[];
	int m_iHeadKey = 0;
};

TYPEDESCRIPTION CNpcDead::m_SaveData[] =
{
	DEFINE_FIELD(CNpcDead, m_iHeadKey, FIELD_INTEGER),
};

LINK_ENTITY_TO_CLASS(monster_npc_dead, CNpcDead, CCSNpcDead)
IMPLEMENT_SAVERESTORE(CNpcDead, CItemGeneric)

void CNpcDead::KeyValue(KeyValueData *pkvd)
{
	if (FStrEq(pkvd->szKeyName, "deadanim"))
	{
		m_sequenceName = ALLOC_STRING(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "head"))
	{
		m_iHeadKey = Q_atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else
		CItemGeneric::KeyValue(pkvd);
}

void CNpcDead::Spawn()
{
	if (FStringNull(pev->model))
		pev->model = ALLOC_STRING("models/sealB.mdl");
	if (FStringNull(m_sequenceName))
		m_sequenceName = ALLOC_STRING("deadstomach");
	CItemGeneric::Spawn();
	SetBodygroup(1, m_iHeadKey);
	m_bAnimated = false;
	pev->nextthink = 0;
	pev->framerate = 0.0f; // frozen on the dead pose
	pev->frame = 0.0f; // FGD: the custom death pose is the first frame
}

void CItemGeneric::Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value)
{
	if (m_removeOnUse)
		UTIL_Remove(this);
}


// ---------------------------------------------------------------------------
// Navigation: the maps carry info_node entities (the original NPCs path-find over them).
// Nodes are connected when a human hull can walk between them in a straight line.
// ---------------------------------------------------------------------------
static std::vector<Vector> g_navNodes;
static std::vector<std::vector<int> > g_navAdj;
static std::string g_navMap;

// one walking step like the engine does for monsters: step up (<= 18), move, step down (<= 18)
static bool Nav_Step(const Vector &from, float x, float y, Vector &out)
{
	const float STEP = 18.0f;
	const float CENTER = 36.0f; // human hull center above the floor

	Vector s = from + Vector(0, 0, STEP + CENTER);
	Vector e(x, y, s.z);

	TraceResult tr;
	UTIL_TraceHull(s, e, ignore_monsters, human_hull, nullptr, &tr);
	if (tr.fStartSolid || tr.fAllSolid || tr.flFraction < 1.0f)
		return false;

	Vector d = e - Vector(0, 0, STEP * 2);
	UTIL_TraceHull(e, d, ignore_monsters, human_hull, nullptr, &tr);
	if (tr.fStartSolid || tr.fAllSolid || tr.flFraction >= 1.0f)
		return false; // ledge or stuck

	out = Vector(x, y, tr.vecEndPos.z - CENTER);
	return true;
}

static bool Nav_Clear(const Vector &a, const Vector &b)
{
	Vector cur = a;
	Vector flat(b.x - a.x, b.y - a.y, 0);
	float len = flat.Length();
	if (len < 1.0f)
		return true;

	Vector dir = flat * (1.0f / len);
	for (float t = 12.0f; ; t += 12.0f)
	{
		bool last = (t >= len);
		float tt = last ? len : t;
		Vector nxt;
		if (!Nav_Step(cur, a.x + dir.x * tt, a.y + dir.y * tt, nxt))
			return false;

		cur = nxt;
		if (last)
			break;
	}

	return fabsf(cur.z - b.z) < 40.0f;
}

static void Nav_Load()
{
	const char *map = STRING(gpGlobals->mapname);
	if (g_navMap == map)
		return;

	g_navMap = map;
	g_navNodes.clear();
	g_navAdj.clear();

	CBaseEntity *pNode = nullptr;
	while ((pNode = UTIL_FindEntityByClassname(pNode, "info_node")) != nullptr)
	{
		// put the node on the floor
		Vector p = pNode->pev->origin;
		TraceResult tr;
		UTIL_TraceLine(p + Vector(0, 0, 16), p + Vector(0, 0, -200), ignore_monsters, nullptr, &tr);
		if (tr.flFraction < 1.0f && !tr.fStartSolid)
			p.z = tr.vecEndPos.z;
		g_navNodes.push_back(p);
	}

	int n = (int)g_navNodes.size();
	g_navAdj.resize(n);

	for (int i = 0; i < n && n <= 600; i++)
	{
		for (int j = i + 1; j < n; j++)
		{
			if ((g_navNodes[i] - g_navNodes[j]).Length() > 700.0f)
				continue;

			if (Nav_Clear(g_navNodes[i], g_navNodes[j]))
			{
				g_navAdj[i].push_back(j);
				g_navAdj[j].push_back(i);
			}
		}
	}

	ALERT(at_aiconsole, "nav: %d info_node loaded\n", n);
}

// returns the number of waypoints written to out (not including 'from'; the last one is 'to'), 0 = go straight
static int Nav_FindPath(const Vector &from, const Vector &to, Vector *out, int maxOut)
{
	if (Nav_Clear(from, to))
		return 0;

	Nav_Load();
	int n = (int)g_navNodes.size();
	if (n == 0)
		return 0;

	// Dijkstra over nodes; start/end are connected to every visible node
	std::vector<float> dist(n, 1e9f);
	std::vector<int> prev(n, -1);
	std::vector<char> done(n, 0);

	for (int i = 0; i < n; i++)
	{
		float d = (g_navNodes[i] - from).Length();
		if (d < 900.0f && Nav_Clear(from, g_navNodes[i]))
			dist[i] = d;
	}

	int best = -1;
	float bestCost = 1e9f;

	for (;;)
	{
		int u = -1;
		for (int i = 0; i < n; i++)
			if (!done[i] && dist[i] < 1e8f && (u < 0 || dist[i] < dist[u]))
				u = i;

		if (u < 0)
			break;

		done[u] = 1;

		// can the target be reached from this node?
		float toEnd = (g_navNodes[u] - to).Length();
		if (dist[u] + toEnd < bestCost && Nav_Clear(g_navNodes[u], to))
		{
			bestCost = dist[u] + toEnd;
			best = u;
		}

		for (size_t k = 0; k < g_navAdj[u].size(); k++)
		{
			int v = g_navAdj[u][k];
			float nd = dist[u] + (g_navNodes[u] - g_navNodes[v]).Length();
			if (nd < dist[v])
			{
				dist[v] = nd;
				prev[v] = u;
			}
		}
	}

	if (best < 0)
		return 0;

	// rebuild: to <- best <- prev ...
	int chain[64];
	int len = 0;
	for (int c = best; c >= 0 && len < 64; c = prev[c])
		chain[len++] = c;

	int count = 0;
	for (int i = len - 1; i >= 0 && count < maxOut - 1; i--)
		out[count++] = g_navNodes[chain[i]];

	out[count++] = to;
	return count;
}

// ---------------------------------------------------------------------------
// (class CMonsterNPC lives in cz_npc.h)

static cvar_t cz_npc_sit_offset = { "cz_npc_sit_offset", "3", 0, 3.0f, nullptr };
static bool g_czNpcCvars = false;

TYPEDESCRIPTION CMonsterNPC::m_SaveData[] =
{
	DEFINE_FIELD(CMonsterNPC, m_idleAnim, FIELD_STRING),
	DEFINE_FIELD(CMonsterNPC, m_useTarget, FIELD_STRING),
	DEFINE_FIELD(CMonsterNPC, m_deathAnim, FIELD_STRING),
	DEFINE_FIELD(CMonsterNPC, m_flDeathAngle, FIELD_FLOAT),
	DEFINE_FIELD(CMonsterNPC, m_bHasDeathAngle, FIELD_CHARACTER),
	DEFINE_FIELD(CMonsterNPC, m_bDead, FIELD_CHARACTER),
	DEFINE_FIELD(CMonsterNPC, m_bDeadSwapped, FIELD_CHARACTER),
	DEFINE_FIELD(CMonsterNPC, m_head, FIELD_INTEGER),
	DEFINE_FIELD(CMonsterNPC, m_spawned, FIELD_INTEGER),
	DEFINE_FIELD(CMonsterNPC, m_bScripted, FIELD_CHARACTER),
	DEFINE_FIELD(CMonsterNPC, m_bMoving, FIELD_CHARACTER),
	DEFINE_FIELD(CMonsterNPC, m_bArrived, FIELD_CHARACTER),
	DEFINE_FIELD(CMonsterNPC, m_bFollow, FIELD_CHARACTER),
	DEFINE_FIELD(CMonsterNPC, m_bRun, FIELD_CHARACTER),
	DEFINE_FIELD(CMonsterNPC, m_bSitting, FIELD_CHARACTER),
	DEFINE_FIELD(CMonsterNPC, m_bGhost, FIELD_CHARACTER),
	DEFINE_FIELD(CMonsterNPC, m_bPatrolInit, FIELD_CHARACTER),
	DEFINE_FIELD(CMonsterNPC, m_bPatrolOn, FIELD_CHARACTER),
	DEFINE_FIELD(CMonsterNPC, m_bPatrolHeading, FIELD_CHARACTER),
	DEFINE_FIELD(CMonsterNPC, m_bNoPatrol, FIELD_CHARACTER),
	DEFINE_FIELD(CMonsterNPC, m_hPatrol, FIELD_EHANDLE),
	DEFINE_FIELD(CMonsterNPC, m_flPatrolNext, FIELD_TIME),
	DEFINE_FIELD(CMonsterNPC, m_flGhostUntil, FIELD_TIME),
	DEFINE_FIELD(CMonsterNPC, m_flLiftApplied, FIELD_FLOAT),
	DEFINE_FIELD(CMonsterNPC, m_iMoveSeq, FIELD_INTEGER),
	DEFINE_FIELD(CMonsterNPC, m_bAllowGhost, FIELD_CHARACTER),
	DEFINE_FIELD(CMonsterNPC, m_bBaseYawSet, FIELD_CHARACTER),
	DEFINE_FIELD(CMonsterNPC, m_bReplanned, FIELD_CHARACTER),
	DEFINE_FIELD(CMonsterNPC, m_bHoldEnd, FIELD_CHARACTER),
	DEFINE_FIELD(CMonsterNPC, m_bNoSit, FIELD_CHARACTER),
	DEFINE_FIELD(CMonsterNPC, m_flSitApplied, FIELD_FLOAT),
	DEFINE_FIELD(CMonsterNPC, m_flMoveYaw, FIELD_FLOAT),
	DEFINE_FIELD(CMonsterNPC, m_flBiasD0, FIELD_FLOAT),
	DEFINE_FIELD(CMonsterNPC, m_flBiasCur, FIELD_FLOAT),
	DEFINE_FIELD(CMonsterNPC, m_flYawBias, FIELD_FLOAT),
	DEFINE_FIELD(CMonsterNPC, m_nWalks, FIELD_INTEGER),
	DEFINE_FIELD(CMonsterNPC, m_flLastMove, FIELD_TIME),
	DEFINE_FIELD(CMonsterNPC, m_flStuckSince, FIELD_TIME),
	DEFINE_FIELD(CMonsterNPC, m_flProgDist, FIELD_FLOAT),
	DEFINE_FIELD(CMonsterNPC, m_flProgTime, FIELD_TIME),
	DEFINE_FIELD(CMonsterNPC, m_flFaceUntil, FIELD_TIME),
	DEFINE_FIELD(CMonsterNPC, m_flBaseYaw, FIELD_FLOAT),
	DEFINE_ARRAY(CMonsterNPC, m_path, FIELD_VECTOR, 16),
	DEFINE_FIELD(CMonsterNPC, m_nPath, FIELD_INTEGER),
	DEFINE_FIELD(CMonsterNPC, m_iPath, FIELD_INTEGER),
	DEFINE_FIELD(CMonsterNPC, m_flNextPath, FIELD_TIME),
	DEFINE_FIELD(CMonsterNPC, m_vMoveTarget, FIELD_VECTOR),
	DEFINE_FIELD(CMonsterNPC, m_hLeader, FIELD_EHANDLE),
};

LINK_ENTITY_TO_CLASS(monster_npc, CMonsterNPC, CCSMonsterNPC)
LINK_ENTITY_TO_CLASS(monster_generic, CMonsterNPC, CCSMonsterNPC)
IMPLEMENT_SAVERESTORE(CMonsterNPC, CBaseAnimating)

void CMonsterNPC::KeyValue(KeyValueData *pkvd)
{
	if (FStrEq(pkvd->szKeyName, "idleanim"))
	{
		m_idleAnim = ALLOC_STRING(pkvd->szValue);
		pkvd->fHandled = TRUE;

		// trigger_changekeyvalue can switch the animation at run time
		if (m_spawned && !m_bDead) // a corpse keeps its death pose (the map's death trigger also switches idleanim)
			ApplyIdle(pkvd->szValue);
	}
	else if (FStrEq(pkvd->szKeyName, "usetarget"))
	{
		m_useTarget = ALLOC_STRING(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "deathanim"))
	{
		m_deathAnim = ALLOC_STRING(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "deathangle"))
	{
		m_flDeathAngle = Q_atof(pkvd->szValue);
		m_bHasDeathAngle = true;
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "head"))
	{
		m_head = Q_atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else
		CBaseAnimating::KeyValue(pkvd);
}

// trigger_killtarget on a model with a death animation: play it and stay (damaged machinery, debris ...)
bool Cz_KillGeneric(CBaseEntity *pEnt)
{
	if (!pEnt)
		return false;
	// monster_generic and soldiers (the helicopter crew in cz_recoil-intro) play their "deathanim" and stay
	const char *cn = STRING(pEnt->pev->classname);
	if (!FClassnameIs(pEnt->pev, "monster_generic") && Q_strncmp(cn, "monster_ct_", 11) && Q_strncmp(cn, "monster_terrorist_", 18))
		return false;
	CMonsterNPC *m = (CMonsterNPC *)pEnt;
	if (FStringNull(m->m_deathAnim))
		return false;
	int seq = m->LookupSequence(STRING(m->m_deathAnim));
	if (seq < 0)
		seq = m->LookupSequence("explode");
	if (seq < 0)
		seq = m->LookupSequence("death");
	if (seq < 0)
	{
		ALERT(at_console, "monster_generic '%s': death anim '%s' NOT FOUND in model\n", STRING(m->pev->targetname), STRING(m->m_deathAnim));
		return false;
	}
	if (m->m_bHasDeathAngle)
		m->pev->angles.y = m->m_flDeathAngle;
	m->m_bDead = true;
	m->pev->deadflag = DEAD_DEAD; // dead for everybody (grenade reactions, targeting)
	m->pev->takedamage = DAMAGE_NO;
	m->m_bScripted = false;
	m->m_bMoving = false;
	m->m_bFollow = false;
	m->pev->sequence = seq;
	m->pev->frame = 0;
	m->pev->framerate = 1.0f;
	m->ResetSequenceInfo();
	if (m->pev->framerate <= 0.0f)
		m->pev->framerate = 1.0f;
	m->pev->effects &= ~EF_NODRAW;
	m->pev->solid = SOLID_NOT;
	// make sure the animation keeps advancing (stays on the last frame afterwards)
	m->SetThink(&CMonsterNPC::AnimThink);
	m->pev->nextthink = gpGlobals->time + 0.01f;
	ALERT(at_console, "monster_generic '%s': death anim '%s' (seq %d, fps %.1f)\n", STRING(m->pev->targetname), STRING(m->m_deathAnim), seq, m->pev->framerate);
	return true;
}

void CMonsterNPC::ApplyIdle(const char *name)
{
	int seq = -1;

	if (name && name[0])
		seq = LookupSequence(name);

	if (seq < 0)
		seq = LookupSequence("idle1");

	if (seq < 0)
		seq = 0;

	pev->sequence = seq;
	pev->frame = 0;
	ResetSequenceInfo();
	ApplySitOffset();
}

// sitting poses are authored with the model origin on the floor: lift them onto the chair
void CMonsterNPC::ApplySitOffset()
{
	float mins[3], maxs[3];
	m_bSitting = false;
	if (!m_bNoSit && ExtractBbox(pev->sequence, mins, maxs))
		m_bSitting = (maxs[2] < 56.0f);

	float want = m_bSitting ? CVAR_GET_FLOAT("cz_npc_sit_offset") : 0.0f;
	if (want != m_flSitApplied)
	{
		Vector o = pev->origin;
		o.z += want - m_flSitApplied;
		m_flSitApplied = want;
		UTIL_SetOrigin(pev, o);
	}
}

// CZ:DS voice lines (names from the .seq sentence groups: NPC_OK_AM, NPC_WAIT_AM, H_THANK_AM)
struct czVoice_t { const char *kind; const char *file; };
static const czVoice_t g_czVoices[] =
{
	{ "ok", "npc/npc_m_yesdo_am.wav" }, { "ok", "npc/npc_m_right_am.wav" }, { "ok", "npc/npc_m_ok_am.wav" },
	{ "help", "h/h_m_getmeout_am.wav" }, { "help", "h/h_m_okletsgo_am.wav" },
	{ "help", "h/h_m_gethellout_am.wav" }, { "help", "h/h_m_ohlets_am.wav" },
	{ "wait", "npc/npc_m_waithere_am.wav" }, { "wait", "npc/npc_m_stayhere_am.wav" },
	{ "thank", "h/h_m_thanksave_am.wav" }, { "thank", "h/h_m_owelife_am.wav" },
	{ "thank", "h/h_m_gladover_am.wav" }, { "thank", "h/h_m_thanks_am.wav" },
};

void Cz_PrecacheVoices()
{
	for (size_t i = 0; i < ARRAYSIZE(g_czVoices); i++)
		PRECACHE_SOUND((char *)g_czVoices[i].file);
}

void Cz_Say(edict_t *pEdict, const char *kind)
{
	const czVoice_t *pick[8];
	int n = 0;
	for (size_t i = 0; i < ARRAYSIZE(g_czVoices) && n < 8; i++)
		if (FStrEq(g_czVoices[i].kind, kind))
			pick[n++] = &g_czVoices[i];
	if (!n)
		return;
	EMIT_SOUND_DYN2(pEdict, CHAN_VOICE, pick[RANDOM_LONG(0, n - 1)]->file, VOL_NORM, ATTN_NORM, 0, PITCH_NORM);
}

void CMonsterNPC::Precache()
{
	if (!FStringNull(pev->model))
	{
		PRECACHE_MODEL((char *)STRING(pev->model));
		if (!FStringNull(m_deathAnim))
		{
			// "<name>_dead.mdl" replaces the model after the death animation (machinery.mdl -> machinery_dead.mdl)
			char dead[256];
			Q_strncpy(dead, STRING(pev->model), sizeof(dead) - 12);
			dead[sizeof(dead) - 12] = 0;
			char *dot = Q_strrchr(dead, '.');
			if (dot)
			{
				*dot = 0;
				Q_strcat(dead, "_dead.mdl");
				if (g_pFileSystem->FileExists(dead))
					PRECACHE_MODEL(dead);
			}
		}
	}
	Cz_PrecacheVoices();
}

void CMonsterNPC::Spawn()
{
	if (FStringNull(pev->model))
	{
		// CZ default civilian model; models without a "model" key are civilians
		if (g_pFileSystem->FileExists("models/npc-scientist.mdl"))
			pev->model = MAKE_STRING("models/npc-scientist.mdl");
		else if (g_pFileSystem->FileExists("models/npc.mdl"))
			pev->model = MAKE_STRING("models/npc.mdl");
		else
			pev->model = MAKE_STRING("models/scientist.mdl");
	}

	if (!g_czNpcCvars)
	{
		CVAR_REGISTER(&cz_npc_sit_offset);
		g_czNpcCvars = true;
	}

	Precache();
	SET_MODEL(ENT(pev), STRING(pev->model));

	// solid must be set before the entity is linked into the world, otherwise players walk through it
	pev->movetype = MOVETYPE_NONE;
	pev->solid = SOLID_SLIDEBOX;
	pev->takedamage = DAMAGE_NO;
	pev->health = 100;

	UTIL_SetSize(pev, Vector(-16, -16, 0), Vector(16, 16, 72)); // origin at the feet, like HL monsters
	UTIL_SetOrigin(pev, pev->origin);

	InitBoneControllers();

	if (m_head > 0)
		SetBodygroup(1, m_head);

	ApplyIdle(FStringNull(m_idleAnim) ? "idle1" : STRING(m_idleAnim));
	m_spawned = 1;

	SetThink(&CMonsterNPC::AnimThink);
	pev->nextthink = gpGlobals->time + 0.1f;
}

void CMonsterNPC::AnimThink()
{
	StudioFrameAdvance();

	if (pev->iuser3 == 77 && gpGlobals->time >= pev->fuser3)
	{
		pev->iuser3 = 0;
		pev->effects &= ~EF_NOINTERP;
	}

	// death animation finished: swap to the "_dead" model (idle only)
	if (m_bDead && !m_bDeadSwapped && m_fSequenceFinished && !FStringNull(m_deathAnim))
	{
		m_bDeadSwapped = true;
		char dead[256];
		Q_strncpy(dead, STRING(pev->model), sizeof(dead) - 12);
		dead[sizeof(dead) - 12] = 0;
		char *dot = Q_strrchr(dead, '.');
		if (dot)
		{
			*dot = 0;
			Q_strcat(dead, "_dead.mdl");
			if (g_pFileSystem->FileExists(dead))
			{
				pev->model = ALLOC_STRING(dead);
				SET_MODEL(ENT(pev), dead);
				pev->sequence = 0;
				pev->frame = 0;
				ResetSequenceInfo();
				m_fSequenceLoops = TRUE;
				ALERT(at_console, "monster_generic '%s': swapped to %s\n", STRING(pev->targetname), dead);
			}
		}
	}

	if (m_bMoving)
	{
		MoveStep(0, false);

		// patrol: round the corners - when the corner has no wait time, head for the next one before reaching it
		if (m_bMoving && m_bPatrolOn && m_bPatrolHeading && !m_bFollow && !m_bScripted)
		{
			CBaseEntity *c = m_hPatrol;
			if (c && c->GetDelay() <= 0.05f && !FStringNull(c->pev->target))
			{
				Vector dd = c->pev->origin - pev->origin;
				dd.z = 0;
				if (dd.Length() < 36.0f)
				{
					CBaseEntity *next = UTIL_FindEntityByTargetname(nullptr, STRING(c->pev->target));
					if (next)
					{
						m_hPatrol = next;
						m_vMoveTarget = next->pev->origin;
						Vector nd = next->pev->origin - pev->origin;
						m_flMoveYaw = UTIL_VecToYaw(Vector(nd.x, nd.y, 0));
						BuildPath(m_vMoveTarget);
						m_flProgTime = 0.0f;
					}
				}
			}
		}
	}
	else if (m_bFollow && !m_bScripted)
		FollowStep();
	else if (!m_bScripted && !m_bDead && !m_bNoPatrol)
		PatrolStep();

	// animations that are not flagged as looping stop at the last frame: restart them
	if (m_fSequenceFinished && !m_fSequenceLoops && !m_bScripted && !m_bDead && !m_bHoldEnd)
	{
		pev->frame = 0;
		ResetSequenceInfo();
	}

	// the cvar can be changed live to tune the chair offset
	if (m_bSitting && !m_bMoving)
		ApplySitOffset();

	// speaking NPC turns its HEAD (bone controller 0) to the player, like the original, then returns
	bool turning = false;
	{
		float want = 0;
		if (gpGlobals->time < m_flFaceUntil)
		{
			CBaseEntity *pl = UTIL_PlayerByIndex(1);
			if (pl)
			{
				Vector d = pl->pev->origin - pev->origin;
				float yaw = UTIL_VecToYaw(Vector(d.x, d.y, 0));
				want = UTIL_AngleDiff(yaw, pev->angles.y);
				if (want > 60.0f) want = 60.0f;
				if (want < -60.0f) want = -60.0f;
			}
		}
		if (fabs(want - m_flBaseYaw) > 0.5f)
		{
			float step = 180.0f * 0.03f;
			float dlt = want - m_flBaseYaw;
			if (dlt > step) dlt = step;
			if (dlt < -step) dlt = -step;
			m_flBaseYaw += dlt;
			SetBoneController(0, m_flBaseYaw);
			turning = true;
		}
	}

	// while walking think every frame and keep velocity non-zero: that switches the engine's
	// 10 Hz studio "slerp" off and the client interpolates the origin normally
	pev->nextthink = gpGlobals->time + ((m_bMoving || m_bFollow) ? 0.0f : (turning ? 0.03f : 0.1f));
}

// patrol along a path_corner chain given by the monster's "target" (generic for every actor)
void CMonsterNPC::PatrolStep()
{
	if (!m_bPatrolInit)
	{
		m_bPatrolInit = true;
		if (!FStringNull(pev->target))
		{
			CBaseEntity *c = UTIL_FindEntityByTargetname(nullptr, STRING(pev->target));
			if (c && FClassnameIs(c->pev, "path_corner"))
			{
				m_bPatrolOn = true;
				m_hPatrol = c;
				m_flPatrolNext = gpGlobals->time + 0.5f;
				ALERT(at_console, "patrol: '%s' starts along path_corner '%s'\n", STRING(pev->classname), STRING(pev->target));
			}
		}
	}

	if (!m_bPatrolOn || m_bMoving || m_bFollow)
		return;

	CBaseEntity *c = m_hPatrol;
	if (m_bPatrolHeading)
	{
		// reached the corner: wait its "wait" time, then take the next one
		m_bPatrolHeading = false;
		float wait = c ? c->GetDelay() : 0.0f;
		CBaseEntity *next = nullptr;
		if (c && !FStringNull(c->pev->target))
			next = UTIL_FindEntityByTargetname(nullptr, STRING(c->pev->target));
		m_hPatrol = next;
		m_flPatrolNext = gpGlobals->time + wait + 0.05f;
		if (!next)
			m_bPatrolOn = false;
		if (wait > 0.3f)
		{
			if (!FStringNull(m_idleAnim))
				ApplyIdle(STRING(m_idleAnim));
			else if (!PlaySeq("idle1"))
				PlaySeq("combatidle");
		}
		return;
	}

	if (gpGlobals->time < m_flPatrolNext)
		return;
	if (!c)
	{
		m_bPatrolOn = false;
		return;
	}

	Vector d = c->pev->origin - pev->origin;
	StartMove(c->pev->origin, UTIL_VecToYaw(Vector(d.x, d.y, 0)), false);
	m_bPatrolHeading = true;
}

void CMonsterNPC::BuildPath(const Vector &goal)
{
	m_iPath = 0;
	m_nPath = Nav_FindPath(pev->origin, goal, m_path, 16);
}

void CMonsterNPC::StartMove(const Vector &target, float yaw, bool run)
{
	int oldSeq = pev->sequence;
	float oldFrame = pev->frame;
	m_vMoveTarget = target;
	BuildPath(target);
	m_flMoveYaw = yaw;
	m_bRun = run;
	m_bMoving = true;
	m_bArrived = false;
	m_flLastMove = gpGlobals->time;
	m_flStuckSince = 0.0f;
	m_flProgTime = 0.0f;
	m_bReplanned = false;

	{
		int found = -1;
		const char *how = "";
		if (PlaySeq(run ? "run" : "walk"))
			how = "name";
		else
		{
			int a = LookupActivity(run ? ACT_RUN : ACT_WALK);
			if (a > 0 || (a == 0 && false))
			{
				found = a;
				how = "activity";
			}
			else
			{
				// scan sequence names, then fall back to any sequence with real ground movement
				studiohdr_t *hdr = (studiohdr_t *)GET_MODEL_PTR(ENT(pev));
				if (hdr)
				{
					static const char *keys[2][3] = { { "walk", "patrol", "run" }, { "run", "sprint", "walk" } };
					mstudioseqdesc_t *sd = (mstudioseqdesc_t *)((byte *)hdr + hdr->seqindex);
					for (int k = 0; k < 3 && found < 0; k++)
						for (int i = 0; i < hdr->numseq && found < 0; i++)
						{
							char low[40];
							Q_strncpy(low, sd[i].label, sizeof(low) - 1);
							low[sizeof(low) - 1] = 0;
							for (char *p = low; *p; p++) *p = tolower((unsigned char)*p);
							if (strstr(low, keys[run ? 1 : 0][k]) && sd[i].linearmovement.Length() > 0.0f)
								found = i, how = "scan-name";
						}
					for (int i = 0; i < hdr->numseq && found < 0; i++)
						if (sd[i].linearmovement.Length() > 10.0f && (sd[i].flags & STUDIO_LOOPING))
							found = i, how = "scan-move";
				}
			}
			if (found >= 0)
			{
				pev->sequence = found;
				pev->frame = 0;
				ResetSequenceInfo();
			}
			else
				PlaySeq("idle1");
		}
		if (pev->sequence == oldSeq)
			pev->frame = oldFrame; // keep the walk cycle running between legs
		pev->framerate = 1.0f;
		if (m_flFrameRate < 0.1f)
			m_flFrameRate = 1.0f;
		m_iMoveSeq = pev->sequence;
	}

	m_bSitting = false;
	ApplySitOffset();

	if (m_flGroundSpeed < 1.0f)
		m_flGroundSpeed = run ? 200.0f : 70.0f;

	pev->movetype = MOVETYPE_NONE; // we move the entity ourselves every frame
}

void CMonsterNPC::StopMove()
{
	m_flGhostUntil = 0.0f;
	// arrived: leave the walk/run cycle (otherwise the actor keeps running on the spot)
	if (m_bMoving && m_iMoveSeq >= 0 && pev->sequence == m_iMoveSeq && !m_bFollow && !m_bDead && !m_bAllowGhost)
	{
		m_iMoveSeq = -1;
		if (!FStringNull(m_idleAnim))
			ApplyIdle(STRING(m_idleAnim));
		else if (!PlaySeq("idle1"))
			PlaySeq("combatidle");
	}

	m_bMoving = false;
	pev->movetype = MOVETYPE_NONE;
	pev->velocity = g_vecZero;
}

// moves towards m_vMoveTarget; returns true when the target is reached
bool CMonsterNPC::MoveStep(float stopDist, bool checkWalls)
{
	float dt = gpGlobals->time - m_flLastMove;
	m_flLastMove = gpGlobals->time;
	if (dt < 0.001f) dt = 0.001f;
	if (dt > 0.25f) dt = 0.25f;

	bool finalLeg = (m_iPath >= m_nPath);
	Vector goal = finalLeg ? m_vMoveTarget : m_path[m_iPath];
	Vector to = goal - pev->origin;
	Vector flat(to.x, to.y, 0);
	float d = flat.Length();
	float step = m_flGroundSpeed * dt;

	if (!finalLeg && d < (m_bAllowGhost ? 40.0f : 24.0f))
	{
		m_iPath++; // reached a waypoint: continue with the next one
		return false;
	}

	if (m_flYawBias > 0.0f)
	{
		// walk off to the left of the goal until half of the way is done, then turn to it
		if (m_flBiasD0 <= 0.0f)
			m_flBiasD0 = d;
		// smooth blend: full bias until 65% of the leg remains, none at 35%
		float t = (d - m_flBiasD0 * 0.35f) / (m_flBiasD0 * 0.30f);
		t = t < 0 ? 0 : (t > 1 ? 1 : t);
		if (d < 96.0f)
			t = 0;
		m_flBiasCur = m_flYawBias * t * t * (3.0f - 2.0f * t);
	}
	else
		m_flBiasCur = 0.0f;

	float ideal = (d > (m_bPatrolHeading ? 12.0f : 1.0f)) ? UTIL_VecToYaw(flat) + m_flBiasCur : pev->angles.y;
	pev->angles.y = UTIL_ApproachAngle(ideal, pev->angles.y, (m_flYawBias > 0.0f ? 140.0f : (m_bPatrolHeading ? 160.0f : 540.0f)) * dt);

	if (finalLeg && (d <= stopDist || d <= step + 1.0f))
	{
		if (stopDist <= 0)
		{
			UTIL_SetOrigin(pev, Vector(m_vMoveTarget.x, m_vMoveTarget.y, pev->origin.z));
			if (!m_bPatrolHeading)
				pev->angles.y = m_flMoveYaw; // patrol keeps its heading: no snap at the corners
			m_bArrived = true;
			StopMove();
		}
		return true;
	}

	if (m_bGhost || gpGlobals->time < m_flGhostUntil)
	{
		// red-shirt worker: follows his path through props (no collision of his own), but stays solid for the player
		float a = (UTIL_VecToYaw(flat) + m_flBiasCur) * (M_PI / 180.0f);
		Vector dir2(cos(a), sin(a), 0);
		Vector np = pev->origin + dir2 * step;

		CBaseEntity *pl = UTIL_FindEntityByClassname(nullptr, "player");
		if (pl && pl->IsAlive())
		{
			Vector dp = pl->pev->origin - np;
			dp.z = 0;
			if (dp.Length() < 34.0f && fabs(pl->pev->origin.z - np.z) < 72.0f)
			{
				pev->velocity = Vector(0, 0, 1); // wait for the player to step aside
				return false;
			}
		}

		// combat soldiers may squeeze past door frames/props, but never through world walls
		if (!m_bGhost && !m_bScripted)
		{
			if (m_flWallSince != 0.0f && gpGlobals->time - m_flWallSince > 3.0f)
				m_flWallSince = 0.0f;
			TraceResult wt;
			UTIL_TraceLine(pev->origin + Vector(0, 0, 36), Vector(np.x, np.y, pev->origin.z + 36), ignore_monsters, ENT(pev), &wt);
			if (wt.flFraction < 1.0f)
			{
				m_flGhostUntil = 0.0f;
				pev->velocity = Vector(0, 0, 1);
				if (m_flWallSince == 0.0f)
					m_flWallSince = gpGlobals->time;
				else if (gpGlobals->time - m_flWallSince > 1.0f)
				{
					// path leads through a wall: hop to the next free waypoint (or give up on this one)
					m_flWallSince = 0.0f;
					if (!finalLeg)
					{
						TraceResult ht;
						UTIL_TraceHull(m_path[m_iPath] + Vector(0, 0, 36), m_path[m_iPath] + Vector(0, 0, 36), ignore_monsters, human_hull, ENT(pev), &ht);
						if (!ht.fStartSolid)
						{
							UTIL_SetOrigin(pev, m_path[m_iPath]);
							DROP_TO_FLOOR(ENT(pev));
						}
						m_iPath++;
					}
					else
						BuildPath(m_vMoveTarget);
				}
				return false;
			}
		}

		// keep the same height above the floor as now, following the floor (steps included) under the new spot
		TraceResult f0, f1;
		UTIL_TraceLine(pev->origin + Vector(0, 0, 40), pev->origin - Vector(0, 0, 100), ignore_monsters, ENT(pev), &f0);
		UTIL_TraceLine(Vector(np.x, np.y, pev->origin.z + 40), Vector(np.x, np.y, pev->origin.z - 100), ignore_monsters, ENT(pev), &f1);
		np.z = pev->origin.z;
		if (f0.flFraction < 1.0f && f1.flFraction < 1.0f && !f1.fStartSolid && !f0.fStartSolid)
		{
			float dz = f1.vecEndPos.z - f0.vecEndPos.z;
			if (dz > -40.0f && dz < 40.0f)
				np.z += dz;
		}

		pev->flags |= FL_ONGROUND;
		pev->velocity = Vector(dir2.x * m_flGroundSpeed, dir2.y * m_flGroundSpeed, 0);
		UTIL_SetOrigin(pev, np);
		return false;
	}

	// progress watchdog: no real approach to the current goal for 1.2 s -> replan, then skip ahead
	if (m_flProgTime == 0.0f || d < m_flProgDist - 30.0f)
	{
		m_flProgTime = gpGlobals->time;
		m_flProgDist = d;
	}
	else if (finalLeg && stopDist <= 0 && d < 72.0f && gpGlobals->time - m_flProgTime > 0.35f && d > m_flProgDist - 4.0f)
	{
		// pressed against the target (e.g. a wall panel/button): count as arrived instead of running in place
		pev->angles.y = m_flMoveYaw;
		m_bArrived = true;
		StopMove();
		return true;
	}
	else if (gpGlobals->time - m_flProgTime > (m_bAllowGhost ? 0.5f : 1.2f))
	{
		m_flProgTime = gpGlobals->time;
		m_flProgDist = d;
		if (m_bAllowGhost && !m_bScripted)
		{
			// soldier stuck (door frame, prop): squeeze through without collision for a moment instead of replanning/teleporting
			m_flGhostUntil = gpGlobals->time + 1.6f;
			return false;
		}
		if (!finalLeg)
			m_iPath++;               // give up this waypoint
		else if (stopDist <= 0 && !m_bReplanned)
		{
			m_bReplanned = true; // replan once from here; next stall teleports
			BuildPath(m_vMoveTarget);
		}
		else if (stopDist <= 0)
		{
			UTIL_SetOrigin(pev, m_vMoveTarget);
			DROP_TO_FLOOR(ENT(pev));
			pev->angles.y = m_flMoveYaw;
			m_bArrived = true;
			StopMove();
			return true;
		}
		return false;
	}

	// walk with the engine's monster movement (stairs, ledges, collision with the real hull);
	// if the straight direction is blocked try to steer around
	Vector dir(flat.x / d, flat.y / d, 0);
	Vector origin0 = pev->origin;
	{
		static const float angles[] = { 0, 25, -25, 50, -50, 75, -75, 100, -100 };
		bool found = false;
		float baseYaw = UTIL_VecToYaw(dir) + m_flBiasCur;

		pev->flags |= FL_ONGROUND;

		for (int i = 0; i < 9 && !found; i++)
		{
			float yaw = baseYaw + angles[i];

			// look ahead ~56 units (anything solid, incl. props and other monsters) so we turn before touching it
			float a2 = yaw * (M_PI / 180.0f);
			Vector st = pev->origin + Vector(0, 0, 56.0f);
			Vector en = st + Vector(cos(a2), sin(a2), 0) * 56.0f;
			TraceResult tr;
			TRACE_MONSTER_HULL(ENT(pev), st, en, dont_ignore_monsters, ENT(pev), &tr);
			if (tr.flFraction < 1.0f && !tr.fStartSolid)
			{
				if (i == 0 && tr.pHit && m_flStuckSince != 0.0f)
					ALERT(at_aiconsole, "npc blocked by %s\n", STRING(tr.pHit->v.classname));
				if (i < 8)
					continue;
			}

			if (WALK_MOVE(ENT(pev), yaw, step, WALKMOVE_NORMAL))
			{
				float a = yaw * (M_PI / 180.0f);
				dir = Vector(cos(a), sin(a), 0);
				found = true;
			}
		}

		if (!found)
		{
			// boxed in: if this lasts, give up following / jump to the mark
			pev->velocity = Vector(0, 0, 1);
			if (m_bAllowGhost)
			{
				m_flGhostUntil = gpGlobals->time + 0.6f; // hull blocked: step without collision, the node path avoids walls
				return false;
			}
			if (m_flStuckSince == 0.0f)
				m_flStuckSince = gpGlobals->time;
			else if (gpGlobals->time - m_flStuckSince > 2.5f && stopDist <= 0)
			{
				UTIL_SetOrigin(pev, m_vMoveTarget);
				DROP_TO_FLOOR(ENT(pev));
				pev->angles.y = m_flMoveYaw;
				m_flStuckSince = 0;
				m_bArrived = true;
				StopMove();
				return true;
			}
			return false;
		}

		m_flStuckSince = 0.0f;
	}

	{
		// WALK_MOVE "succeeded" but the actor did not really move (stuck in the floor/geometry): go ghost
		Vector mv = pev->origin - origin0;
		mv.z = 0;
		bool stuck = mv.Length() < step * 0.3f;
		if (stuck && m_bAllowGhost)
		{
			m_flGhostUntil = gpGlobals->time + 0.6f;
			return false;
		}
	}

	pev->flags |= FL_ONGROUND;
	pev->velocity = Vector(dir.x * m_flGroundSpeed, dir.y * m_flGroundSpeed, 0);
	UTIL_SetOrigin(pev, pev->origin); // relink
	return false;
}

void CMonsterNPC::StartFollow(CBaseEntity *leader)
{
	m_hLeader = leader;
	m_bFollow = true;
	m_flLastMove = gpGlobals->time;
}

void CMonsterNPC::StopFollow()
{
	if (!m_bFollow)
		return;

	m_bFollow = false;
	StopMove();
	ApplyIdle(FStringNull(m_idleAnim) ? "idle1" : STRING(m_idleAnim));
}

void CMonsterNPC::FollowStep()
{
	CBaseEntity *leader = m_hLeader;
	if (!leader || !leader->IsAlive())
	{
		StopFollow();
		return;
	}

	Vector flat = leader->pev->origin - pev->origin;
	flat.z = 0;
	float d = flat.Length();

	if (d > 110.0f)
	{
		bool run = d > 320.0f;
		int curWalk = LookupSequence(run ? "run" : "walk");
		if (!m_bMoving || m_bRun != run || pev->sequence != curWalk)
		{
			StartMove(leader->pev->origin, pev->angles.y, run);
			m_bArrived = false;
		}

		m_vMoveTarget = leader->pev->origin;

		if (gpGlobals->time >= m_flNextPath)
		{
			m_flNextPath = gpGlobals->time + 1.0f;
			BuildPath(leader->pev->origin);
		}

		MoveStep(90.0f, true);
	}
	else if (m_bMoving)
	{
		StopMove();
		ApplyIdle("idle1");
	}
}

bool CMonsterNPC::PlaySeq(const char *name)
{
	int seq = (name && name[0]) ? LookupSequence(name) : -1;
	if (seq < 0)
		return false;

	pev->sequence = seq;
	pev->frame = 0;
	ResetSequenceInfo();
	return true;
}

void CMonsterNPC::Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value)
{
	if (FClassnameIs(pev, "monster_generic"))
		return; // plain decoration (models/idleanim from the map), never a helper

	// an npc without a "usetarget" key is a helper: +use makes it follow the player (and again stops it)
	if (FStringNull(m_useTarget))
	{
		if (m_bScripted)
			return;

		if (m_bFollow)
		{
			StopFollow();
			Cz_Say(edict(), "wait");
		}
		else if (pActivator && pActivator->IsPlayer())
		{
			StartFollow(pActivator);
			Cz_Say(edict(), "ok");
		}

		return;
	}

	if (Cz_IsNullName(STRING(m_useTarget)))
		return;

	FireTargets(STRING(m_useTarget), pActivator, this, USE_TOGGLE, 0);
}


// ---------------------------------------------------------------------------
// scripted_sequence: makes a monster_npc walk/run to the entity origin and play
// an animation there, then fires its target (chains of these make NPCs patrol).
// Keys: m_iszEntity (npc targetname), m_iszPlay, m_iszIdle, m_fMoveTo
// (0 stay, 1 walk, 2 run, 4 teleport), m_flRadius. Flags: 4 repeatable.
// ---------------------------------------------------------------------------
class CCZScriptedSequence: public CBaseEntity
{
public:
	virtual int Save(CSave &save);
	virtual int Restore(CRestore &restore);
	static TYPEDESCRIPTION m_SaveData[];
	virtual void Spawn();
	virtual void KeyValue(KeyValueData *pkvd);
	virtual void Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value);

public:
	void EXPORT AutoStart();
	void EXPORT ScriptThink();
	void StartPlay();
	void Finish();
	CMonsterNPC *FindNPC();

	string_t m_iszEntity;
	string_t m_iszPlay;
	string_t m_iszIdle;
	int m_fMoveTo;
	float m_flRadius;
	int m_state; // 0 idle, 1 move, 2 play
	float m_flLast;
	EHANDLE m_hNpc;
};

TYPEDESCRIPTION CCZScriptedSequence::m_SaveData[] =
{
	DEFINE_FIELD(CCZScriptedSequence, m_iszEntity, FIELD_STRING),
	DEFINE_FIELD(CCZScriptedSequence, m_iszPlay, FIELD_STRING),
	DEFINE_FIELD(CCZScriptedSequence, m_iszIdle, FIELD_STRING),
	DEFINE_FIELD(CCZScriptedSequence, m_fMoveTo, FIELD_INTEGER),
	DEFINE_FIELD(CCZScriptedSequence, m_flRadius, FIELD_FLOAT),
	DEFINE_FIELD(CCZScriptedSequence, m_state, FIELD_INTEGER),
	DEFINE_FIELD(CCZScriptedSequence, m_flLast, FIELD_TIME),
	DEFINE_FIELD(CCZScriptedSequence, m_hNpc, FIELD_EHANDLE),
};
IMPLEMENT_SAVERESTORE(CCZScriptedSequence, CBaseEntity)

LINK_ENTITY_TO_CLASS(scripted_sequence, CCZScriptedSequence, CCSScriptedSequence)

void CCZScriptedSequence::KeyValue(KeyValueData *pkvd)
{
	if (FStrEq(pkvd->szKeyName, "m_iszEntity"))
	{
		m_iszEntity = ALLOC_STRING(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_iszPlay"))
	{
		m_iszPlay = ALLOC_STRING(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_iszIdle"))
	{
		m_iszIdle = ALLOC_STRING(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_fMoveTo"))
	{
		m_fMoveTo = Q_atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_flRadius"))
	{
		m_flRadius = Q_atof(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_fRepeat") || FStrEq(pkvd->szKeyName, "m_flRepeat")
		|| FStrEq(pkvd->szKeyName, "m_iFinishSchedule"))
	{
		pkvd->fHandled = TRUE; // unused
	}
	else
		CBaseEntity::KeyValue(pkvd);
}

void CCZScriptedSequence::Spawn()
{
	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NONE;
	m_state = 0;

	if (FStringNull(pev->targetname))
	{
		if (m_flRadius > 0 && m_fMoveTo != 0)
		{
			// proximity script: runs when the helper npc that follows the player gets near
			SetThink(&CCZScriptedSequence::AutoStart);
			pev->nextthink = gpGlobals->time + 1.0f;
		}
		else
		{
			// no targetname: starts by itself
			SetThink(&CCZScriptedSequence::AutoStart);
			pev->nextthink = gpGlobals->time + 1.0f;
		}
	}
}

void CCZScriptedSequence::AutoStart()
{
	if (m_flRadius > 0 && m_fMoveTo != 0)
	{
		CMonsterNPC *npc = FindNPC();
		if (npc && npc->m_bFollow && (npc->pev->origin - pev->origin).Length() <= m_flRadius)
		{
			Use(nullptr, nullptr, USE_TOGGLE, 0);
			return;
		}

		pev->nextthink = gpGlobals->time + 0.2f;
		return;
	}

	Use(nullptr, nullptr, USE_TOGGLE, 0);
}

bool Cz_IsSoldierClass(CBaseEntity *e)
{
	if (!e || !e->pev->classname)
		return false;
	const char *c = STRING(e->pev->classname);
	return !Q_strncmp(c, "monster_ct_", 11) || !Q_strncmp(c, "monster_terrorist_", 18);
}

CMonsterNPC *CCZScriptedSequence::FindNPC()
{
	if (FStringNull(m_iszEntity))
		return nullptr;

	CBaseEntity *pEnt = nullptr;
	while ((pEnt = UTIL_FindEntityByTargetname(pEnt, STRING(m_iszEntity))))
	{
		if (FClassnameIs(pEnt->pev, "monster_npc") || Cz_IsSoldierClass(pEnt))
			return static_cast<CMonsterNPC *>(pEnt);
	}

	if (m_flRadius > 0)
	{
		pEnt = nullptr;
		while ((pEnt = UTIL_FindEntityInSphere(pEnt, pev->origin, m_flRadius)))
		{
			if (FClassnameIs(pEnt->pev, "monster_npc") && FClassnameIs(pEnt->pev, STRING(m_iszEntity)))
				return static_cast<CMonsterNPC *>(pEnt);
		}
	}

	return nullptr;
}

void CCZScriptedSequence::Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value)
{
	if (m_state != 0)
		return;

	if (m_state != 0)
		ALERT(at_console, "scripted_sequence '%s': used while busy (state %d)\n", STRING(pev->targetname), m_state);
	CMonsterNPC *npc = FindNPC();
	if (!npc)
	{
		ALERT(at_console, "scripted_sequence '%s': can't find monster_npc '%s'\n", STRING(pev->targetname), STRING(m_iszEntity));
		return;
	}

	ALERT(at_console, "scripted_sequence '%s': '%s' (%s) at %.0f %.0f %.0f %s to %.0f %.0f %.0f play '%s'\n", STRING(pev->targetname), STRING(npc->pev->targetname),
		STRING(npc->pev->classname), npc->pev->origin.x, npc->pev->origin.y, npc->pev->origin.z, m_fMoveTo == 2 ? "runs" : (m_fMoveTo == 1 ? "walks" : "acts"), pev->origin.x, pev->origin.y, pev->origin.z, STRING(m_iszPlay));
	m_hNpc = npc;
	npc->StopFollow();
	npc->m_bScripted = true;
	m_flLast = gpGlobals->time;

	if (m_fMoveTo == 1 || m_fMoveTo == 2)
	{
		npc->StartMove(pev->origin, pev->angles.y, m_fMoveTo == 2);
		if (npc->m_bAllowGhost)
			npc->m_flGhostUntil = gpGlobals->time + 40.0f; // scripted soldier: follow the node path without hull collision (dynamic props/walls)
		// the red-shirt worker of cz_train01 walks the original routes (waypoints measured from the original demo)
		npc->m_flYawBias = 0.0f;
		if (FStrEq(STRING(npc->pev->targetname), "worker_walk1"))
		{
			struct leg_t { const char *dest; int n; float p[3][2]; };
			static const leg_t legs[] =
			{
				{ "worker_sequence1", 0, {} },
				{ "worker_sequence2", 1, { { -250, -154 } } },
				{ "worker_sequence4", 2, { { -105, -200 }, { 34, -91 } } },
				{ "worker_sequence3", 3, { { 140, -70 }, { 0, -105 }, { -130, -75 } } }, // wide arc to the right (user sketch)
				{ "worker_sequence5", 2, { { -220, -41 }, { -452, -174 } } },
			};
			for (const leg_t &l : legs)
			{
				if (!FStrEq(STRING(pev->targetname), l.dest))
					continue;
				npc->m_nPath = l.n;
				npc->m_iPath = 0;
				for (int i = 0; i < l.n; i++)
					npc->m_path[i] = Vector(l.p[i][0], l.p[i][1], npc->pev->origin.z);
				break;
			}
		}
		npc->m_nWalks++;
		npc->m_bGhost = FStrEq(STRING(npc->pev->targetname), "worker_walk1");
		npc->m_flBiasD0 = 0.0f;

		m_state = 1;
		SetThink(&CCZScriptedSequence::ScriptThink);
		pev->nextthink = gpGlobals->time + 0.05f;
		return;
	}

	if (m_fMoveTo == 4)
	{
		UTIL_SetOrigin(npc->pev, pev->origin);
		npc->pev->angles.y = pev->angles.y;
		npc->pev->effects |= EF_NOINTERP;
	}

	StartPlay();
}

void CCZScriptedSequence::StartPlay()
{
	CMonsterNPC *npc = (CMonsterNPC *)(CBaseEntity *)m_hNpc;
	if (!npc)
	{
		Finish();
		return;
	}

	npc->pev->effects &= ~EF_NOINTERP;

	if (FStringNull(m_iszPlay) || !npc->PlaySeq(STRING(m_iszPlay)))
	{
		if (!FStringNull(m_iszPlay))
			ALERT(at_console, "scripted_sequence: sequence '%s' not found\n", STRING(m_iszPlay));
		Finish();
		return;
	}

	m_state = 2;
	SetThink(&CCZScriptedSequence::ScriptThink);
	pev->nextthink = gpGlobals->time + 0.05f;
}

void CCZScriptedSequence::ScriptThink()
{
	CMonsterNPC *npc = (CMonsterNPC *)(CBaseEntity *)m_hNpc;
	if (!npc)
	{
		Finish();
		return;
	}

	float dt = gpGlobals->time - m_flLast;
	m_flLast = gpGlobals->time;
	if (dt > 0.2f) dt = 0.2f;

	if (m_state == 1)
	{
		if (npc->m_bArrived)
		{
			StartPlay();
			return;
		}
	}
	else if (m_state == 2)
	{
		if (npc->m_fSequenceFinished)
		{
			Finish();
			return;
		}
	}

	pev->nextthink = gpGlobals->time + 0.05f;
}

void CCZScriptedSequence::Finish()
{
	CMonsterNPC *npc = (CMonsterNPC *)(CBaseEntity *)m_hNpc;
	if (npc && m_state == 2)
	{
		// the animation carries the actor away from his mark (a roll behind a crate): keep him where it ended
		// instead of snapping back to the start point
		entvars_t *e = npc->pev;
		Vector p0, p1, a;
		float oldFrame = e->frame;
		e->frame = 0;
		GET_BONE_POSITION(ENT(e), 0, p0, a);
		e->frame = 255;
		GET_BONE_POSITION(ENT(e), 0, p1, a);
		e->frame = oldFrame;
		Vector d = p1 - p0;
		d.z = 0;

		studiohdr_t *hdr = (studiohdr_t *)GET_MODEL_PTR(ENT(e));
		if (hdr && e->sequence >= 0 && e->sequence < hdr->numseq)
		{
			mstudioseqdesc_t *sd = (mstudioseqdesc_t *)((byte *)hdr + hdr->seqindex) + e->sequence;
			if (sd->linearmovement.Length() > d.Length())
			{
				UTIL_MakeVectors(e->angles);
				d = gpGlobals->v_forward * sd->linearmovement.x + gpGlobals->v_right * -sd->linearmovement.y;
				d.z = 0;
			}
		}
		if (d.Length() > 8.0f && d.Length() < 400.0f)
		{
			Vector np = e->origin + d;
			TraceResult tr;
			UTIL_TraceLine(e->origin + Vector(0, 0, 36), np + Vector(0, 0, 36), ignore_monsters, ENT(e), &tr);
			if (tr.flFraction >= 1.0f)
			{
				TraceResult fl;
				UTIL_TraceLine(np + Vector(0, 0, 24), np - Vector(0, 0, 64), ignore_monsters, ENT(e), &fl);
				if (fl.flFraction < 1.0f && !fl.fStartSolid)
					np.z = fl.vecEndPos.z - e->mins.z;
				UTIL_SetOrigin(e, np);
				e->effects |= EF_NOINTERP; // no sliding/blinking between the old and the new spot
				e->iuser3 = 77;
				e->fuser3 = gpGlobals->time + 0.3f;
				ALERT(at_console, "scripted_sequence '%s': actor moved %.0f units by the animation\n", STRING(pev->targetname), d.Length());
			}
		}
	}
	if (npc)
	{
		npc->m_bScripted = false;
		npc->ApplyIdle(FStringNull(npc->m_idleAnim) ? "idle1" : STRING(npc->m_idleAnim));
	}

	m_state = 0;
	m_hNpc = nullptr;

	if (pev->spawnflags & 4) // repeatable
		SetThink(nullptr);
	else
	{
		SetThink(&CBaseEntity::SUB_Remove);
		pev->nextthink = gpGlobals->time + 0.1f;
	}

	SUB_UseTargets(this, USE_TOGGLE, 0);
}


// called by trigger_sequence when an entity speaks: make a stationary NPC look at the player
void Cz_NpcSpeak(CBaseEntity *pSpeaker, float seconds)
{
	if (!pSpeaker)
		return;

	const char *cls = STRING(pSpeaker->pev->classname);
	if (Q_strcmp(cls, "monster_npc") && Q_strcmp(cls, "monster_generic"))
		return;

	CMonsterNPC *pNpc = (CMonsterNPC *)pSpeaker;
	pNpc->m_flFaceUntil = gpGlobals->time + seconds;
	if (pNpc->pev->nextthink <= 0 || pNpc->pev->nextthink > gpGlobals->time + 0.05f)
		pNpc->pev->nextthink = gpGlobals->time + 0.03f;
}
