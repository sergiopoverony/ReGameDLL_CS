#include "precompiled.h"

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

	string_t m_sequenceName;
	int m_removeOnUse;
	int m_spawned;
};

TYPEDESCRIPTION CItemGeneric::m_SaveData[] =
{
	DEFINE_FIELD(CItemGeneric, m_sequenceName, FIELD_STRING),
	DEFINE_FIELD(CItemGeneric, m_removeOnUse, FIELD_INTEGER),
};

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

	InitBoneControllers();
	UTIL_SetOrigin(pev, pev->origin);

	ApplySequence(FStringNull(m_sequenceName) ? "idle" : STRING(m_sequenceName));
	m_spawned = 1;

	// display items that a map shows later with an env_render ("appear") start hidden
	if (!FStringNull(pev->targetname))
	{
		SetThink(&CItemGeneric::HideIfAppearTarget);
		pev->nextthink = gpGlobals->time + 0.3f;
	}
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
}

void CItemGeneric::Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value)
{
	if (m_removeOnUse)
		UTIL_Remove(this);
}

// ---------------------------------------------------------------------------
class CMonsterNPC: public CBaseAnimating
{
public:
	virtual void Spawn();
	virtual void Precache();
	virtual void KeyValue(KeyValueData *pkvd);
	virtual int Save(CSave &save);
	virtual int Restore(CRestore &restore);
	virtual int ObjectCaps() { return (CBaseAnimating::ObjectCaps() | FCAP_IMPULSE_USE); }
	virtual void Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value);

public:
	static TYPEDESCRIPTION m_SaveData[];

	void ApplyIdle(const char *name);
	void EXPORT AnimThink();
	bool PlaySeq(const char *name);
	void ApplySitOffset();
	void StartMove(const Vector &target, float yaw, bool run);
	void StopMove();
	bool MoveStep(float stopDist, bool checkWalls);
	void StartFollow(CBaseEntity *leader);
	void StopFollow();
	void FollowStep();

	string_t m_idleAnim;
	string_t m_useTarget;
	int m_head;
	int m_spawned;
	bool m_bScripted; // driven by scripted_sequence
	bool m_bMoving;   // walking to m_vMoveTarget (scripted)
	bool m_bArrived;
	bool m_bFollow;   // follows the player after +use
	bool m_bRun;
	bool m_bSitting;
	float m_flSitApplied;
	float m_flMoveYaw;
	float m_flLastMove;
	float m_flStuckSince;
	Vector m_vMoveTarget;
	EHANDLE m_hLeader;
};

static cvar_t cz_npc_sit_offset = { "cz_npc_sit_offset", "3", 0, 3.0f, nullptr };
static bool g_czNpcCvars = false;

TYPEDESCRIPTION CMonsterNPC::m_SaveData[] =
{
	DEFINE_FIELD(CMonsterNPC, m_idleAnim, FIELD_STRING),
	DEFINE_FIELD(CMonsterNPC, m_useTarget, FIELD_STRING),
	DEFINE_FIELD(CMonsterNPC, m_head, FIELD_INTEGER),
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
		if (m_spawned)
			ApplyIdle(pkvd->szValue);
	}
	else if (FStrEq(pkvd->szKeyName, "usetarget"))
	{
		m_useTarget = ALLOC_STRING(pkvd->szValue);
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
	if (ExtractBbox(pev->sequence, mins, maxs))
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
		PRECACHE_MODEL((char *)STRING(pev->model));
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

	if (m_bMoving)
		MoveStep(0, false);
	else if (m_bFollow && !m_bScripted)
		FollowStep();

	// animations that are not flagged as looping stop at the last frame: restart them
	if (m_fSequenceFinished && !m_fSequenceLoops && !m_bScripted)
	{
		pev->frame = 0;
		ResetSequenceInfo();
	}

	// the cvar can be changed live to tune the chair offset
	if (m_bSitting && !m_bMoving)
		ApplySitOffset();

	// while walking think every frame and keep velocity non-zero: that switches the engine's
	// 10 Hz studio "slerp" off and the client interpolates the origin normally
	pev->nextthink = gpGlobals->time + ((m_bMoving || m_bFollow) ? 0.0f : 0.1f);
}

void CMonsterNPC::StartMove(const Vector &target, float yaw, bool run)
{
	m_vMoveTarget = target;
	m_flMoveYaw = yaw;
	m_bRun = run;
	m_bMoving = true;
	m_bArrived = false;
	m_flLastMove = gpGlobals->time;
	m_flStuckSince = 0.0f;

	if (!PlaySeq(run ? "run" : "walk"))
		PlaySeq("idle1");

	m_bSitting = false;
	ApplySitOffset();

	if (m_flGroundSpeed < 1.0f)
		m_flGroundSpeed = run ? 200.0f : 70.0f;

	pev->movetype = MOVETYPE_NONE; // we move the entity ourselves every frame
}

void CMonsterNPC::StopMove()
{
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

	Vector to = m_vMoveTarget - pev->origin;
	Vector flat(to.x, to.y, 0);
	float d = flat.Length();
	float step = m_flGroundSpeed * dt;

	float ideal = (d > 1.0f) ? UTIL_VecToYaw(flat) : m_flMoveYaw;
	pev->angles.y = UTIL_ApproachAngle(ideal, pev->angles.y, 540.0f * dt);

	if (d <= stopDist || d <= step + 1.0f)
	{
		if (stopDist <= 0)
		{
			UTIL_SetOrigin(pev, m_vMoveTarget);
			pev->angles.y = m_flMoveYaw;
			m_bArrived = true;
			StopMove();
		}
		return true;
	}

	// no navigation nodes here: walk straight to the target, steer around solid things
	Vector dir(flat.x / d, flat.y / d, 0);
	Vector n = pev->origin + dir * step;
	{
		static const float angles[] = { 0, 35, -35, 70, -70, 100, -100 };
		bool found = false;
		float baseYaw = UTIL_VecToYaw(dir);

		for (int i = 0; i < 7 && !found; i++)
		{
			Vector tdir;
			if (angles[i] == 0)
				tdir = dir;
			else
			{
				float a = (baseYaw + angles[i]) * (M_PI / 180.0f);
				tdir = Vector(cos(a), sin(a), 0);
			}

			Vector cand = pev->origin + tdir * (step + 4.0f);
			TraceResult wall;
			UTIL_TraceHull(pev->origin + Vector(0, 0, 20), cand + Vector(0, 0, 20), ignore_monsters, human_hull, ENT(pev), &wall);
			if (wall.flFraction >= 1.0f && !wall.fStartSolid)
			{
				dir = tdir;
				n = pev->origin + tdir * step;
				found = true;
			}
		}

		if (!found)
		{
			// boxed in: if this lasts, give up following / jump to the mark
			if (m_flStuckSince == 0.0f)
				m_flStuckSince = gpGlobals->time;
			else if (gpGlobals->time - m_flStuckSince > 2.5f && stopDist <= 0)
			{
				UTIL_SetOrigin(pev, m_vMoveTarget);
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

	// stay on the floor (stairs, ramps): the marks only give the final height
	TraceResult tr;
	UTIL_TraceLine(n + Vector(0, 0, 40), n + Vector(0, 0, -60), ignore_monsters, ENT(pev), &tr);
	if (tr.flFraction < 1.0f && !tr.fStartSolid)
		n.z = tr.vecEndPos.z + m_flSitApplied;
	else
		n.z = pev->origin.z + to.z * (step / d);

	pev->flags |= FL_ONGROUND;
	pev->velocity = Vector(dir.x * m_flGroundSpeed, dir.y * m_flGroundSpeed, 0);
	UTIL_SetOrigin(pev, n);
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

CMonsterNPC *CCZScriptedSequence::FindNPC()
{
	if (FStringNull(m_iszEntity))
		return nullptr;

	CBaseEntity *pEnt = nullptr;
	while ((pEnt = UTIL_FindEntityByTargetname(pEnt, STRING(m_iszEntity))))
	{
		if (FClassnameIs(pEnt->pev, "monster_npc"))
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

	CMonsterNPC *npc = FindNPC();
	if (!npc)
	{
		ALERT(at_console, "scripted_sequence: can't find monster_npc '%s'\n", STRING(m_iszEntity));
		return;
	}

	m_hNpc = npc;
	npc->StopFollow();
	npc->m_bScripted = true;
	m_flLast = gpGlobals->time;

	if (m_fMoveTo == 1 || m_fMoveTo == 2)
	{
		npc->StartMove(pev->origin, pev->angles.y, m_fMoveTo == 2);

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
