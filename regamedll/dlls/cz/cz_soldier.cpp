#include "precompiled.h"

// CZ:DS soldiers (monster_<team>_<faction>_<weapon>).
// First step: a static soldier with the right model, skin and weapon (the real AI comes later).
// models/seal.mdl: bodygroup 1 = head (4), bodygroup 2 = weapon:
//   0 mp5, 1 shotgun, 2 law, 3 scout, 4 m4a1, 5 usp, 6 m60, 7 mac-10, 8 blank; skin 3 = SWAT uniform.

class CCZSoldier: public CBaseAnimating
{
public:
	virtual void Spawn();
	virtual void Precache();
	virtual void KeyValue(KeyValueData *pkvd);
	virtual int ObjectCaps() { return (CBaseAnimating::ObjectCaps() & ~FCAP_ACROSS_TRANSITION); }

public:
	void EXPORT AnimThink();
	void PlayAnim(const char *name);
	void React(int type); // 1 = flashbang, 2 = smoke grenade

	int m_head;
	int m_react;
	int m_step;
	float m_stepTime;
};

LINK_ENTITY_TO_CLASS(monster_ct_swat_pistol, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_swat_smg, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_swat_mp5, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_swat_shotgun, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_swat_assaultrifle, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_swat_sniperrifle, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_swat_machinegun, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_swat_grenader, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_swat_law, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_swat_melee, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_swat_kamakazi, CCZSoldier, CCSCzSoldier)

struct SoldierWeapon
{
	const char *name;
	int body;        // seal.mdl weapon bodygroup value
	const char *idle;
};

static const SoldierWeapon g_soldierWeapons[] =
{
	{ "pistol",       5, "idle1" },
	{ "smg",          7, "idle1" },
	{ "mp5",          0, "idle1" },
	{ "shotgun",      1, "idle1" },
	{ "assaultrifle", 4, "idle1" },
	{ "sniperrifle",  3, "idle1" },
	{ "machinegun",   6, "idle1" },
	{ "grenader",     0, "idle1" },
	{ "law",          2, "idle1" },
	{ "melee",        8, "idle1" },
	{ "kamakazi",     8, "idle1" },
};

static const SoldierWeapon *Soldier_FindWeapon(const char *classname)
{
	const char *p = Q_strrchr(classname, '_');
	if (!p)
		return &g_soldierWeapons[4];

	for (const SoldierWeapon &w : g_soldierWeapons)
	{
		if (!Q_strcmp(p + 1, w.name))
			return &w;
	}

	return &g_soldierWeapons[4];
}

void CCZSoldier::KeyValue(KeyValueData *pkvd)
{
	if (FStrEq(pkvd->szKeyName, "head"))
	{
		m_head = Q_atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "cantmove") || FStrEq(pkvd->szKeyName, "usetarget")
		|| FStrEq(pkvd->szKeyName, "TriggerTarget") || FStrEq(pkvd->szKeyName, "TriggerCondition")
		|| FStrEq(pkvd->szKeyName, "alertradius") || FStrEq(pkvd->szKeyName, "alertable")
		|| FStrEq(pkvd->szKeyName, "classtype"))
	{
		pkvd->fHandled = TRUE; // AI keys, not used yet
	}
	else
		CBaseAnimating::KeyValue(pkvd);
}

void CCZSoldier::Precache()
{
	PRECACHE_MODEL("models/seal.mdl");
	PRECACHE_SOUND("ct/ct_blinded_am.wav");
	PRECACHE_SOUND("ct/ct_incoming_am.wav");
	PRECACHE_SOUND("ct/ct_damnsmoke_am.wav");
	PRECACHE_SOUND("t/t_coughc_am.wav");
}

void CCZSoldier::Spawn()
{
	Precache();

	m_react = 0;
	pev->model = MAKE_STRING("models/seal.mdl");
	SET_MODEL(ENT(pev), "models/seal.mdl");

	pev->movetype = MOVETYPE_NONE;
	pev->solid = SOLID_SLIDEBOX;
	pev->takedamage = DAMAGE_NO;
	pev->health = 100;
	pev->skin = 3; // SWAT uniform

	UTIL_SetSize(pev, Vector(-16, -16, 0), Vector(16, 16, 72));
	UTIL_SetOrigin(pev, pev->origin);

	InitBoneControllers();

	const SoldierWeapon *w = Soldier_FindWeapon(STRING(pev->classname));

	SetBodygroup(1, m_head);
	SetBodygroup(2, w->body);

	int seq = LookupSequence(w->idle);
	if (seq < 0)
		seq = LookupSequence("combatidle");
	if (seq < 0)
		seq = 0;

	pev->sequence = seq;
	pev->frame = 0;
	ResetSequenceInfo();

	SetThink(&CCZSoldier::AnimThink);
	pev->nextthink = gpGlobals->time + 0.1f;
}

void CCZSoldier::PlayAnim(const char *name)
{
	int seq = LookupSequence(name);
	if (seq < 0)
		return;

	pev->sequence = seq;
	pev->frame = 0;
	ResetSequenceInfo();
}

static void Soldier_Say(edict_t *pEdict, const char *file)
{
	EMIT_SOUND_DYN2(pEdict, CHAN_VOICE, file, VOL_NORM, ATTN_NORM, 0, PITCH_NORM);
}

void CCZSoldier::React(int type)
{
	m_react = type;
	m_step = 0;

	if (type == 1)
	{
		// flashbang: "blinded", twitching
		Soldier_Say(edict(), "ct/ct_blinded_am.wav");
		PlayAnim("smflinch");
		m_stepTime = gpGlobals->time + 6.0f;
	}
	else
	{
		// smoke: "Incoming!", cover the head, then "Damn, smoke!" and coughing
		Soldier_Say(edict(), "ct/ct_incoming_am.wav");
		PlayAnim("cower");
		m_stepTime = gpGlobals->time + 2.5f;
	}
}

void CCZSoldier::AnimThink()
{
	StudioFrameAdvance();

	if (m_react == 1)
	{
		if (gpGlobals->time >= m_stepTime)
		{
			m_react = 0;
			const SoldierWeapon *w = Soldier_FindWeapon(STRING(pev->classname));
			PlayAnim(w->idle);
		}
		else if (m_fSequenceFinished)
		{
			static const char *flinch[] = { "smflinch", "leftarmflinch", "rightarmflinch", "leftlegsmflinch", "rightlegsmflinch" };
			PlayAnim(flinch[RANDOM_LONG(0, 4)]);
		}
	}
	else if (m_react == 2)
	{
		if (m_step == 0 && gpGlobals->time >= m_stepTime)
		{
			m_step = 1;
			Soldier_Say(edict(), "ct/ct_damnsmoke_am.wav");
			PlayAnim("choking");
			m_stepTime = gpGlobals->time + 1.5f;
		}
		else if (m_step == 1 && gpGlobals->time >= m_stepTime)
		{
			m_step = 2;
			Soldier_Say(edict(), "t/t_coughc_am.wav");
			m_stepTime = gpGlobals->time + 2.5f;
		}
		else if (m_step == 2 && gpGlobals->time >= m_stepTime)
		{
			m_react = 0;
			const SoldierWeapon *w = Soldier_FindWeapon(STRING(pev->classname));
			PlayAnim(w->idle);
		}
	}
	else if (m_fSequenceFinished && !m_fSequenceLoops)
	{
		pev->frame = 0;
		ResetSequenceInfo();
	}

	pev->nextthink = gpGlobals->time + 0.1f;
}

// called from CGrenade: type 1 = flashbang exploded, 2 = smoke grenade exploded
void Cz_GrenadeEvent(const Vector &origin, int type)
{
	CBaseEntity *pEnt = nullptr;
	while ((pEnt = UTIL_FindEntityInSphere(pEnt, origin, 900.0f)) != nullptr)
	{
		if (FNullEnt(pEnt->edict()) || !pEnt->pev->classname)
			continue;

		if (Q_strncmp(STRING(pEnt->pev->classname), "monster_ct_swat_", 16))
			continue;

		static_cast<CCZSoldier *>(pEnt)->React(type);
	}
}

// ---------------------------------------------------------------------------
// monstermaker: creates "monstercount" monsters of class "monstertype" at its origin when used.
// "netname" = targetname given to the spawned monsters, "delay" = time between spawns.
// ---------------------------------------------------------------------------
class CCZMonsterMaker: public CBaseDelay
{
public:
	virtual void Spawn();
	virtual void KeyValue(KeyValueData *pkvd);
	virtual int ObjectCaps() { return (CBaseDelay::ObjectCaps() & ~FCAP_ACROSS_TRANSITION); }
	virtual void Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value);

public:
	void EXPORT MakerThink();
	void MakeMonster();

	string_t m_iszMonsterType;
	int m_cNumMonsters;
	int m_cMonstersMade;
	float m_flSpawnDelay;
};

LINK_ENTITY_TO_CLASS(monstermaker, CCZMonsterMaker, CCSMonsterMaker)

void CCZMonsterMaker::KeyValue(KeyValueData *pkvd)
{
	if (FStrEq(pkvd->szKeyName, "monstertype"))
	{
		m_iszMonsterType = ALLOC_STRING(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "monstercount"))
	{
		m_cNumMonsters = Q_atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "delay"))
	{
		m_flSpawnDelay = Q_atof(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_imaxlivechildren"))
	{
		pkvd->fHandled = TRUE;
	}
	else
		CBaseDelay::KeyValue(pkvd);
}

void CCZMonsterMaker::Spawn()
{
	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NONE;
	m_cMonstersMade = 0;

	if (m_cNumMonsters <= 0)
		m_cNumMonsters = 1;

	SetThink(&CCZMonsterMaker::MakerThink);
	SetUse(&CCZMonsterMaker::Use);

	// models can only be precached while the map loads: do it now for the monster we will create later
	if (!FStringNull(m_iszMonsterType))
		UTIL_PrecacheOther(STRING(m_iszMonsterType));

	// CZ: maker with no trigger starts on its own (nobody targets spawn_CT in train02)
	if (!FStringNull(m_iszMonsterType))
		pev->nextthink = gpGlobals->time + 0.5f;
}

void CCZMonsterMaker::Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value)
{
	if (FStringNull(m_iszMonsterType))
		return;

	m_cMonstersMade = 0;
	pev->nextthink = gpGlobals->time + 0.05f;
}

void CCZMonsterMaker::MakerThink()
{
	MakeMonster();

	if (m_cMonstersMade < m_cNumMonsters)
		pev->nextthink = gpGlobals->time + Q_max(m_flSpawnDelay, 0.1f);
	else
		pev->nextthink = 0;
}

void CCZMonsterMaker::MakeMonster()
{
	if (FStringNull(m_iszMonsterType))
		return;

	CBaseEntity *pNew = CBaseEntity::Create(STRING(m_iszMonsterType), pev->origin, pev->angles, edict());
	if (!pNew)
	{
		ALERT(at_console, "monstermaker: can't create '%s'\n", STRING(m_iszMonsterType));
		m_cMonstersMade = m_cNumMonsters;
		return;
	}

	pNew->pev->owner = nullptr;
	if (!FStringNull(pev->netname))
		pNew->pev->targetname = pev->netname;

	m_cMonstersMade++;
}
