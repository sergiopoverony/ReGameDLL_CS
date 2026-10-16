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
	EHANDLE m_hPlat;
	Vector m_platOfs;
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
LINK_ENTITY_TO_CLASS(monster_ct_gign_pistol, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_gign_smg, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_gign_mp, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_gign_mp5, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_gign_shotgun, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_gign_assaultrifle, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_gign_sniperrifle, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_gign_machinegun, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_gign_grenader, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_gign_law, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_gign_melee, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_gign_kamakazi, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_gsg9_pistol, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_gsg9_smg, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_gsg9_mp, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_gsg9_mp5, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_gsg9_shotgun, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_gsg9_assaultrifle, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_gsg9_sniperrifle, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_gsg9_machinegun, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_gsg9_grenader, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_gsg9_law, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_gsg9_melee, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_gsg9_kamakazi, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_sas_pistol, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_sas_smg, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_sas_mp, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_sas_mp5, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_sas_shotgun, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_sas_assaultrifle, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_sas_sniperrifle, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_sas_machinegun, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_sas_grenader, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_sas_law, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_sas_melee, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_sas_kamakazi, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_spetsnaz_pistol, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_spetsnaz_smg, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_spetsnaz_mp, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_spetsnaz_mp5, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_spetsnaz_shotgun, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_spetsnaz_assaultrifle, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_spetsnaz_sniperrifle, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_spetsnaz_machinegun, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_spetsnaz_grenader, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_spetsnaz_law, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_spetsnaz_melee, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_spetsnaz_kamakazi, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_ct_swat_mp, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_arctic_pistol, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_arctic_smg, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_arctic_mp, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_arctic_mp5, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_arctic_shotgun, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_arctic_assaultrifle, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_arctic_sniperrifle, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_arctic_machinegun, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_arctic_grenader, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_arctic_law, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_arctic_melee, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_arctic_kamakazi, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_asian_pistol, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_asian_smg, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_asian_mp, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_asian_mp5, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_asian_shotgun, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_asian_assaultrifle, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_asian_sniperrifle, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_asian_machinegun, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_asian_grenader, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_asian_law, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_asian_melee, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_asian_kamakazi, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_desert_pistol, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_desert_smg, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_desert_mp, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_desert_mp5, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_desert_shotgun, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_desert_assaultrifle, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_desert_sniperrifle, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_desert_machinegun, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_desert_grenader, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_desert_law, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_desert_melee, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_desert_kamakazi, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_jungle_pistol, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_jungle_smg, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_jungle_mp, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_jungle_mp5, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_jungle_shotgun, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_jungle_assaultrifle, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_jungle_sniperrifle, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_jungle_machinegun, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_jungle_grenader, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_jungle_law, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_jungle_melee, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_jungle_kamakazi, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_russian_pistol, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_russian_smg, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_russian_mp, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_russian_mp5, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_russian_shotgun, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_russian_assaultrifle, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_russian_sniperrifle, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_russian_machinegun, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_russian_grenader, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_russian_law, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_russian_melee, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_russian_kamakazi, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_urban_pistol, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_urban_smg, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_urban_mp, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_urban_mp5, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_urban_shotgun, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_urban_assaultrifle, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_urban_sniperrifle, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_urban_machinegun, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_urban_grenader, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_urban_law, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_urban_melee, CCZSoldier, CCSCzSoldier)
LINK_ENTITY_TO_CLASS(monster_terrorist_urban_kamakazi, CCZSoldier, CCSCzSoldier)

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
	{ "mp",           0, "idle1" },
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
	PRECACHE_MODEL("models/terror.mdl");
	PRECACHE_SOUND("ct/ct_blinded_am.wav");
	PRECACHE_SOUND("ct/ct_incoming_am.wav");
	PRECACHE_SOUND("ct/ct_damnsmoke_am.wav");
	PRECACHE_SOUND("t/t_coughc_am.wav");
}

void CCZSoldier::Spawn()
{
	Precache();

	m_react = 0;
	const bool isTerror = Q_strstr(STRING(pev->classname), "terrorist") != nullptr;
	const char *mdl = isTerror ? "models/terror.mdl" : "models/seal.mdl";
	pev->model = MAKE_STRING(mdl);
	SET_MODEL(ENT(pev), mdl);

	pev->movetype = MOVETYPE_NONE;
	pev->solid = SOLID_SLIDEBOX;
	pev->takedamage = DAMAGE_NO;
	pev->health = 100;
	pev->skin = isTerror ? 0 : (Q_strstr(STRING(pev->classname), "_swat_") ? 3 : 0); // SWAT uniform

	UTIL_SetSize(pev, Vector(-16, -16, 0), Vector(16, 16, 72));
	UTIL_SetOrigin(pev, pev->origin);

	// CZ: soldier made inside a moving cage/lift (train02): ride on that platform
	m_hPlat = nullptr;
	{
		CBaseEntity *best = nullptr;
		float bestD = 99999.0f;
		CBaseEntity *pEnt = nullptr;
		while ((pEnt = UTIL_FindEntityInSphere(pEnt, pev->origin, 256.0f)) != nullptr)
		{
			if (FNullEnt(pEnt->edict()) || pEnt->pev == pev || !pEnt->pev->classname || pEnt->pev->movetype != MOVETYPE_PUSH)
				continue;

			const char *cn = STRING(pEnt->pev->classname);
			if (Q_strncmp(cn, "func_door", 9) && Q_strcmp(cn, "func_plat") && Q_strcmp(cn, "func_train") && Q_strcmp(cn, "func_platrot"))
				continue;

			const Vector &o = pev->origin;
			if (o.x < pEnt->pev->absmin.x - 24 || o.x > pEnt->pev->absmax.x + 24 || o.y < pEnt->pev->absmin.y - 24 || o.y > pEnt->pev->absmax.y + 24)
				continue;
			if (o.z < pEnt->pev->absmin.z - 72 || o.z > pEnt->pev->absmax.z + 16)
				continue;

			float d = fabsf(pEnt->pev->absmax.z - o.z);
			if (d < bestD)
			{
				bestD = d;
				best = pEnt;
			}
		}

		if (best)
		{
			m_hPlat = best;
			m_platOfs = pev->origin - best->pev->origin;
			ALERT(at_console, "soldier: rides on %s (%s) ofs z %.0f\n", STRING(best->pev->classname), STRING(best->pev->targetname), m_platOfs.z);
		}
		else
			ALERT(at_console, "soldier: no platform under %.0f %.0f %.0f\n", pev->origin.x, pev->origin.y, pev->origin.z);
	}

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

	CBaseEntity *plat = m_hPlat;
	if (plat)
	{
		UTIL_SetOrigin(pev, plat->pev->origin + m_platOfs);
		pev->nextthink = gpGlobals->time + 0.02f;
		return;
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
