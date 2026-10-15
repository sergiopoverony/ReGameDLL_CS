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

	string_t m_idleAnim;
	string_t m_useTarget;
	int m_head;
	int m_spawned;
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
		if (g_pFileSystem->FileExists("models/npc.mdl"))
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
	if (m_fSequenceFinished && !m_fSequenceLoops)
	{
		pev->frame = 0;
		ResetSequenceInfo();
	}

	pev->nextthink = gpGlobals->time + 0.1f;
}

void CMonsterNPC::Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value)
{
	if (FStringNull(m_useTarget) || Cz_IsNullName(STRING(m_useTarget)))
		return;

	FireTargets(STRING(m_useTarget), pActivator, this, USE_TOGGLE, 0);
}
