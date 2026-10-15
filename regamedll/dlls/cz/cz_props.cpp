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

	string_t m_idleAnim;
	string_t m_useTarget;
	int m_head;
	int m_spawned;
	bool m_bScripted; // driven by scripted_sequence
};

TYPEDESCRIPTION CMonsterNPC::m_SaveData[] =
{
	DEFINE_FIELD(CMonsterNPC, m_idleAnim, FIELD_STRING),
	DEFINE_FIELD(CMonsterNPC, m_useTarget, FIELD_STRING),
	DEFINE_FIELD(CMonsterNPC, m_head, FIELD_INTEGER),
};

LINK_ENTITY_TO_CLASS(monster_npc, CMonsterNPC, CCSMonsterNPC)
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
}

void CMonsterNPC::Precache()
{
	if (!FStringNull(pev->model))
		PRECACHE_MODEL((char *)STRING(pev->model));
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

	Precache();
	SET_MODEL(ENT(pev), STRING(pev->model));

	UTIL_SetSize(pev, Vector(-16, -16, 0), Vector(16, 16, 72)); // origin at the feet, like HL monsters
	UTIL_SetOrigin(pev, pev->origin);

	pev->movetype = MOVETYPE_NONE;
	pev->solid = SOLID_SLIDEBOX;
	pev->takedamage = DAMAGE_NO;
	pev->health = 100;

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

	// animations that are not flagged as looping stop at the last frame: restart them
	if (m_fSequenceFinished && !m_fSequenceLoops && !m_bScripted)
	{
		pev->frame = 0;
		ResetSequenceInfo();
	}

	pev->nextthink = gpGlobals->time + 0.1f;
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
	if (FStringNull(m_useTarget) || Cz_IsNullName(STRING(m_useTarget)))
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

	// no targetname: starts by itself
	if (FStringNull(pev->targetname))
	{
		SetThink(&CCZScriptedSequence::AutoStart);
		pev->nextthink = gpGlobals->time + 1.0f;
	}
}

void CCZScriptedSequence::AutoStart()
{
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
	npc->m_bScripted = true;
	m_flLast = gpGlobals->time;

	if (m_fMoveTo == 1 || m_fMoveTo == 2)
	{
		if (!npc->PlaySeq(m_fMoveTo == 2 ? "run" : "walk"))
			npc->PlaySeq("idle1");

		if (npc->m_flGroundSpeed < 1.0f)
			npc->m_flGroundSpeed = (m_fMoveTo == 2) ? 200.0f : 70.0f;

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
		Vector to = pev->origin - npc->pev->origin;
		Vector flat = Vector(to.x, to.y, 0);
		float dist2d = flat.Length();
		float step = npc->m_flGroundSpeed * dt;

		if (dist2d <= step + 1.0f)
		{
			UTIL_SetOrigin(npc->pev, pev->origin);
			npc->pev->angles.y = pev->angles.y;
			StartPlay();
			return;
		}

		float frac = step / dist2d;
		Vector newOrg = npc->pev->origin + Vector(to.x * frac, to.y * frac, to.z * frac);
		npc->pev->angles.y = UTIL_VecToYaw(flat);
		UTIL_SetOrigin(npc->pev, newOrg);
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
