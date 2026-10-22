#include "precompiled.h"
#include "cz_npc.h"

// CZ:DS soldiers (monster_<team>_<faction>_<weapon>).
// First step: a static soldier with the right model, skin and weapon (the real AI comes later).
// models/seal.mdl: bodygroup 1 = head (4), bodygroup 2 = weapon:
//   0 mp5, 1 shotgun, 2 law, 3 scout, 4 m4a1, 5 usp, 6 m60, 7 mac-10, 8 blank; skin 3 = SWAT uniform.

bool g_czMakerSpawn = false; // set by monstermaker while it creates a monster
static cvar_t cz_cage_drop = { "cz_cage_drop", "100", 0, 100.0f, nullptr };
static cvar_t cz_grenade_react = { "cz_grenade_react_radius", "250", 0, 250.0f, nullptr };
static bool g_czCageCvar = false;


class CCZSoldier: public CMonsterNPC
{
public:
	virtual int Save(CSave &save);
	virtual int Restore(CRestore &restore);
	static TYPEDESCRIPTION m_SaveData[];
	virtual void Spawn();
	virtual void Precache();
	virtual void KeyValue(KeyValueData *pkvd);
	virtual int ObjectCaps() { return (CBaseAnimating::ObjectCaps() & ~FCAP_ACROSS_TRANSITION); }
	virtual void Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value) {}
	virtual BOOL TakeDamage(entvars_t *pevInflictor, entvars_t *pevAttacker, float flDamage, int bitsDamageType);
	virtual void Killed(entvars_t *pevAttacker, int iGib);
	virtual int BloodColor() { return BLOOD_COLOR_RED; }

public:
	void EXPORT AnimThink();
	void EXPORT DeadThink();
	void PlayAnim(const char *name);
	void Settle();
	bool m_bPlatMoved;
	bool m_bMade;
	float m_flSettleAt = 0;
	void StandOnFloor();
	void React(int type); // 1 = flashbang, 2 = smoke grenade
	void FireTrigger(CBaseEntity *pActivator);
	void CheckSeePlayer();
	void CombatThink();
	void FireShot(CBaseEntity *pl);

	char m_combat = 0;       // 0 calm, 1 fighting the player
	char m_bHurtByPlayer = 0;
	int m_burstLeft = 0;
	float m_flNextShot = 0;
	float m_flNextMelee = 0;
	float m_flLastSeen = 0;
	float m_flNextChase = 0;
	float m_flFlashOff = 0;

	int m_react;
	int m_step;
	float m_stepTime;

	// AI keys of the map (see cz.fgd)
	string_t m_trigTarget;   // TriggerTarget
	int m_trigCond = 0;      // TriggerCondition: 1 see player, 2 take damage, 3 half health, 4 death, 10/11 see player
	string_t m_deathTrig;    // deathtrigger
	int m_cantMove = 0;
	int m_bInvuln = 0;
	int m_bUnkillable = 0;
	float m_flMaxHealth = 100;
	bool m_bTrigFired = false;
	float m_flNextSee = 0;
	float m_flFadeStart = 0;
	EHANDLE m_hPlat;
	float m_flNextPain = 0;
	Vector m_platOfs;
};

TYPEDESCRIPTION CCZSoldier::m_SaveData[] =
{
	DEFINE_FIELD(CCZSoldier, m_bPlatMoved, FIELD_CHARACTER),
	DEFINE_FIELD(CCZSoldier, m_bMade, FIELD_CHARACTER),
	DEFINE_FIELD(CCZSoldier, m_flSettleAt, FIELD_TIME),
	DEFINE_FIELD(CCZSoldier, m_hPlat, FIELD_EHANDLE),
	DEFINE_FIELD(CCZSoldier, m_platOfs, FIELD_VECTOR),
	DEFINE_FIELD(CCZSoldier, m_react, FIELD_INTEGER),
	DEFINE_FIELD(CCZSoldier, m_step, FIELD_INTEGER),
	DEFINE_FIELD(CCZSoldier, m_stepTime, FIELD_TIME),
	DEFINE_FIELD(CCZSoldier, m_trigTarget, FIELD_STRING),
	DEFINE_FIELD(CCZSoldier, m_trigCond, FIELD_INTEGER),
	DEFINE_FIELD(CCZSoldier, m_deathTrig, FIELD_STRING),
	DEFINE_FIELD(CCZSoldier, m_cantMove, FIELD_INTEGER),
	DEFINE_FIELD(CCZSoldier, m_bInvuln, FIELD_INTEGER),
	DEFINE_FIELD(CCZSoldier, m_bUnkillable, FIELD_INTEGER),
	DEFINE_FIELD(CCZSoldier, m_flMaxHealth, FIELD_FLOAT),
	DEFINE_FIELD(CCZSoldier, m_bTrigFired, FIELD_CHARACTER),
	DEFINE_FIELD(CCZSoldier, m_flNextSee, FIELD_TIME),
	DEFINE_FIELD(CCZSoldier, m_flFadeStart, FIELD_TIME),
	DEFINE_FIELD(CCZSoldier, m_combat, FIELD_CHARACTER),
	DEFINE_FIELD(CCZSoldier, m_bHurtByPlayer, FIELD_CHARACTER),
	DEFINE_FIELD(CCZSoldier, m_burstLeft, FIELD_INTEGER),
	DEFINE_FIELD(CCZSoldier, m_flNextShot, FIELD_TIME),
	DEFINE_FIELD(CCZSoldier, m_flNextMelee, FIELD_TIME),
	DEFINE_FIELD(CCZSoldier, m_flLastSeen, FIELD_TIME),
	DEFINE_FIELD(CCZSoldier, m_flNextChase, FIELD_TIME),
	DEFINE_FIELD(CCZSoldier, m_flFlashOff, FIELD_TIME),
};
IMPLEMENT_SAVERESTORE(CCZSoldier, CMonsterNPC)

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

// terrorists (leet.mdl): bodygroup 2 = weapon (0 mp5, 1 shotgun, 2 law, 3 awm, 4 ak47, 5 glock, 6 machete, 7 m60, 8 tmp, 9 blank)
static const SoldierWeapon g_terrorWeapons[] =
{
	{ "pistol",       5, "pistol_idle1" },
	{ "smg",          8, "idle1" },
	{ "mp5",          0, "idle1" },
	{ "mp",           0, "idle1" },
	{ "shotgun",      1, "idle1" },
	{ "assaultrifle", 4, "idle1" },
	{ "sniperrifle",  3, "idle1" },
	{ "machinegun",   7, "idle1" },
	{ "grenader",     2, "idle1" },
	{ "law",          2, "idle1" },
	{ "melee",        6, "machete_idle1" },
	{ "kamakazi",     9, "idle1" },
};

static const SoldierWeapon *Soldier_FindWeapon(const char *classname)
{
	const char *p = Q_strrchr(classname, '_');
	if (!p)
		return &g_soldierWeapons[4];

	if (Q_strstr(classname, "terrorist"))
	{
		for (const SoldierWeapon &w : g_terrorWeapons)
			if (!Q_strcmp(p + 1, w.name))
				return &w;
		return &g_terrorWeapons[5];
	}

	for (const SoldierWeapon &w : g_soldierWeapons)
	{
		if (!Q_strcmp(p + 1, w.name))
			return &w;
	}

	return &g_soldierWeapons[4];
}

void CCZSoldier::KeyValue(KeyValueData *pkvd)
{
	if (FStrEq(pkvd->szKeyName, "TriggerTarget"))
	{
		m_trigTarget = ALLOC_STRING(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "TriggerCondition"))
	{
		m_trigCond = Q_atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "deathtrigger"))
	{
		m_deathTrig = ALLOC_STRING(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "cantmove"))
	{
		m_cantMove = Q_atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "invulnerable"))
	{
		m_bInvuln = Q_atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "unkillable"))
	{
		m_bUnkillable = Q_atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "alertradius") || FStrEq(pkvd->szKeyName, "alertable")
		|| FStrEq(pkvd->szKeyName, "classtype") || FStrEq(pkvd->szKeyName, "weaponaccuracy")
		|| FStrEq(pkvd->szKeyName, "healthmult") || FStrEq(pkvd->szKeyName, "language"))
	{
		pkvd->fHandled = TRUE; // combat AI keys, not used yet
	}
	else
		CMonsterNPC::KeyValue(pkvd); // head, idleanim, usetarget, deathanim
}

static const char *Soldier_Wav(const char *classname, const char *key, char *buf, size_t sz);
static void Soldier_Voice(CBaseEntity *e, const char *key);

// ---------------------------------------------------------------------------
// Combat: hostile (terrorist) soldiers shoot the player. Training maps stay peaceful.
struct SoldierGun
{
	const char *name;
	float dmg, spread, rate; // per shot damage, aim cone (tan), seconds between shots in a burst
	int burst;
	float pauseMin, pauseMax;
	const char *sound;
	const char *anim;
};

static const SoldierGun g_soldierGuns[] =
{
	{ "pistol",       8, 0.060f, 0.35f, 1, 0.5f, 1.0f, "weapons/glock18-2.wav",      "pistol_combatidlle" },
	{ "smg",          6, 0.070f, 0.09f, 5, 0.7f, 1.3f, "weapons/tmp-1.wav",          "combatidle" },
	{ "mp5",          6, 0.055f, 0.09f, 5, 0.7f, 1.3f, "weapons/mp5-1.wav",         "combatidle" },
	{ "mp",           6, 0.055f, 0.09f, 5, 0.7f, 1.3f, "weapons/mp5-1.wav",         "combatidle" },
	{ "shotgun",     25, 0.090f, 1.20f, 1, 0.3f, 0.6f, "weapons/sbarrel1.wav",       "combatidle" },
	{ "assaultrifle", 9, 0.045f, 0.10f, 4, 0.7f, 1.4f, "weapons/ak47-1.wav",        "combatidle" },
	{ "sniperrifle", 40, 0.008f, 2.20f, 1, 0.5f, 1.0f, "weapons/awp1.wav",          "combatidle" },
	{ "machinegun",   7, 0.080f, 0.08f, 9, 1.0f, 1.8f, "weapons/m249-1.wav",        "combatidle" },
	{ "grenader",     9, 0.050f, 0.10f, 4, 0.8f, 1.5f, "weapons/ak47-1.wav",        "combatidle" },
	{ "law",          9, 0.050f, 0.10f, 4, 0.8f, 1.5f, "weapons/ak47-1.wav",        "combatidle" },
	{ "melee",        0, 0.000f, 1.00f, 1, 0.0f, 0.0f, nullptr,                     "machete_combatidle" },
	{ "kamakazi",     0, 0.000f, 1.00f, 1, 0.0f, 0.0f, nullptr,                     "combatidle" },
};

static const SoldierGun *Soldier_FindGun(const char *classname)
{
	const char *p = Q_strrchr(classname, '_');
	if (p)
	{
		for (const SoldierGun &g : g_soldierGuns)
			if (!Q_strcmp(p + 1, g.name))
				return &g;
	}
	return &g_soldierGuns[5];
}

static bool Soldier_IsHostile(entvars_t *pev)
{
	if (!Q_strstr(STRING(pev->classname), "terrorist"))
		return false;
	return Q_strncmp(STRING(gpGlobals->mapname), "cz_train", 8) != 0; // the training is scripted: nobody shoots
}

void CCZSoldier::FireShot(CBaseEntity *pl)
{
	const SoldierGun *g = Soldier_FindGun(STRING(pev->classname));
	if (!g->sound)
		return;

	UTIL_MakeVectors(pev->angles);
	Vector src = pev->origin + Vector(0, 0, 60) + gpGlobals->v_forward * 16;
	Vector aim = (pl->pev->origin + pl->pev->view_ofs * 0.6f - src).Normalize();
	Vector dir = aim + gpGlobals->v_right * (RANDOM_FLOAT(-1, 1) * g->spread) + gpGlobals->v_up * (RANDOM_FLOAT(-1, 1) * g->spread);
	dir = dir.Normalize();

	TraceResult tr;
	UTIL_TraceLine(src, src + dir * 4096.0f, dont_ignore_monsters, ENT(pev), &tr);

	EMIT_SOUND_DYN(ENT(pev), CHAN_WEAPON, g->sound, 1.0f, ATTN_NORM, 0, 94 + RANDOM_LONG(0, 15));
	pev->effects |= EF_MUZZLEFLASH;
	m_flFlashOff = gpGlobals->time + 0.08f;

	// tracer
	MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, src);
		WRITE_BYTE(TE_TRACER);
		WRITE_COORD(src.x); WRITE_COORD(src.y); WRITE_COORD(src.z);
		WRITE_COORD(tr.vecEndPos.x); WRITE_COORD(tr.vecEndPos.y); WRITE_COORD(tr.vecEndPos.z);
	MESSAGE_END();

	CBaseEntity *hit = CBaseEntity::Instance(tr.pHit);
	if (hit && hit->pev->takedamage != DAMAGE_NO)
	{
		ClearMultiDamage();
		hit->TraceAttack(pev, g->dmg, dir, &tr, DMG_BULLET | DMG_NEVERGIB);
		ApplyMultiDamage(pev, pev);
	}
	else if (tr.flFraction < 1.0f)
	{
		UTIL_GunshotDecalTrace(&tr, DECAL_GUNSHOT1 + RANDOM_LONG(0, 4), false, pev);
	}
}

void CCZSoldier::CombatThink()
{
	if (!Soldier_IsHostile(pev) || m_bScripted || (pev->spawnflags & 128))
		return;

	CBasePlayer *pl = (CBasePlayer *)UTIL_PlayerByIndex(1);
	if (!pl || !pl->IsAlive() || (pl->pev->flags & FL_NOTARGET))
	{
		m_combat = 0;
		return;
	}

	Vector eyes = pev->origin + Vector(0, 0, 64);
	Vector target = pl->pev->origin + pl->pev->view_ofs;
	Vector to = target - eyes;
	float dist = to.Length();

	bool visible = false;
	if (dist < 2400.0f)
	{
		TraceResult tr;
		UTIL_TraceLine(eyes, target, ignore_monsters, ENT(pev), &tr);
		visible = (tr.flFraction >= 1.0f || tr.pHit == pl->edict());
		if (visible && !m_combat && !m_bHurtByPlayer)
		{
			// calm: only notices the player in front (180 degrees)
			UTIL_MakeVectors(pev->angles);
			if (DotProduct(to.Normalize(), gpGlobals->v_forward) < 0.0f)
				visible = false;
		}
	}

	if (!m_combat)
	{
		if (!visible && !m_bHurtByPlayer)
			return;

		m_combat = 1;
		m_bNoPatrol = true;
		m_burstLeft = 0;
		m_flNextShot = gpGlobals->time + RANDOM_FLOAT(0.3f, 0.7f);
		static const char *spot[] = { "heyyou", "intrudov", "theretheya", "attack" };
		Soldier_Voice(this, spot[RANDOM_LONG(0, 3)]);
		FireTrigger(pl);
	}

	if (visible)
		m_flLastSeen = gpGlobals->time;

	const SoldierGun *g = Soldier_FindGun(STRING(pev->classname));
	bool melee = (g->dmg <= 0.0f);

	// face the player
	if (visible || dist < 300.0f)
	{
		float yaw = UTIL_VecToYaw(Vector(to.x, to.y, 0));
		pev->angles.y = UTIL_ApproachAngle(yaw, pev->angles.y, 400.0f * 0.1f);
	}

	// positioning: charge when the player is out of sight / too far, hold ground in view
	float hold = melee ? 40.0f : 600.0f;
	if (!m_cantMove)
	{
		bool want = !visible || dist > hold + 150.0f;
		if (want && !m_bMoving && gpGlobals->time >= m_flNextChase)
		{
			m_flNextChase = gpGlobals->time + 0.8f;
			StartMove(pl->pev->origin, UTIL_VecToYaw(Vector(to.x, to.y, 0)), true);
		}
		else if (m_bMoving && visible && dist < hold)
			StopMove();
		else if (m_bMoving && gpGlobals->time >= m_flNextChase)
		{
			m_flNextChase = gpGlobals->time + 0.8f;
			StartMove(pl->pev->origin, UTIL_VecToYaw(Vector(to.x, to.y, 0)), true); // follow the moving target
		}
	}

	if (!visible)
		return;

	if (!m_bMoving)
	{
		int cs = LookupSequence(g->anim);
		if (cs >= 0 && pev->sequence != cs)
		{
			pev->sequence = cs;
			pev->frame = 0;
			ResetSequenceInfo();
		}
	}

	if (melee)
	{
		if (dist < 72.0f && gpGlobals->time >= m_flNextMelee)
		{
			m_flNextMelee = gpGlobals->time + 1.0f;
			PlayAnim("weapon_butt");
			pl->TakeDamage(pev, pev, 15.0f, DMG_CLUB);
		}
		return;
	}

	if (m_bMoving && dist > 350.0f)
		return; // runs first, shoots when standing

	if (gpGlobals->time >= m_flNextShot)
	{
		if (m_burstLeft <= 0)
			m_burstLeft = g->burst;

		FireShot(pl);
		m_burstLeft--;
		m_flNextShot = gpGlobals->time + (m_burstLeft > 0 ? g->rate : RANDOM_FLOAT(g->pauseMin, g->pauseMax));
	}
}

void CCZSoldier::Precache()
{
	PRECACHE_MODEL("models/seal.mdl");
	PRECACHE_MODEL("models/terror.mdl");
	PRECACHE_MODEL("models/leet.mdl");
	if (!FStringNull(pev->model) && Q_strstr(STRING(pev->model), "leet_lo"))
		PRECACHE_MODEL((char *)STRING(pev->model));
	PRECACHE_SOUND("ct/ct_blinded_am.wav");
	PRECACHE_SOUND("ct/ct_incoming_am.wav");
	PRECACHE_SOUND("ct/ct_damnsmoke_am.wav");
	PRECACHE_SOUND("t/t_coughc_am.wav");
	if (Q_strstr(STRING(pev->classname), "terrorist"))
	{
		if (Soldier_FindGun(STRING(pev->classname))->sound)
			PRECACHE_SOUND((char *)Soldier_FindGun(STRING(pev->classname))->sound);
		static const char *keys[] = { "myeyes", "grenade", "damnsmoke", "coughc", "heyyou", "intrudov", "theretheya", "attack",
			"pain1", "pain2", "pain3", "pain4", "death1", "death2", "death3", "death4" };
		for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
		{
			char buf[96];
			PRECACHE_SOUND((char *)Soldier_Wav(STRING(pev->classname), keys[i], buf, sizeof(buf)));
		}
	}
}

void CCZSoldier::Spawn()
{
	Precache();

	m_react = 0;
	const bool isTerror = Q_strstr(STRING(pev->classname), "terrorist") != nullptr;
	const char *mdl = isTerror ? "models/leet.mdl" : "models/seal.mdl";
	if (isTerror && !FStringNull(pev->model) && Q_strstr(STRING(pev->model), "leet_lo"))
		mdl = "models/leet_lo.mdl"; // the mapper picked the low detail model
	pev->model = MAKE_STRING(mdl);
	SET_MODEL(ENT(pev), mdl);

	pev->movetype = MOVETYPE_NONE;
	pev->solid = SOLID_SLIDEBOX;
	pev->takedamage = DAMAGE_NO;
	pev->health = 100;
	m_flMaxHealth = 100;
	pev->flags |= FL_MONSTER;
	pev->deadflag = DEAD_NO;
	m_bDead = false;
	m_bNoSit = true;
	m_bAllowGhost = true;
	m_bNoPatrol = (m_cantMove != 0) || (pev->spawnflags & 128);
	m_bHoldEnd = true;
	m_spawned = 1;
	if (isTerror)
	{
		// leet.mdl: 4 body skins; faction picks one (guess), the mapper's own "skin" wins
		const char *cn = STRING(pev->classname);
		int sk = 0;
		if (Q_strstr(cn, "_arctic_") || Q_strstr(cn, "_russian_")) sk = 1;
		else if (Q_strstr(cn, "_jungle_")) sk = 2;
		else if (Q_strstr(cn, "_urban_") || Q_strstr(cn, "_asian_")) sk = 3;
		if (pev->skin > 0 && pev->skin <= 3)
			sk = pev->skin;
		pev->skin = sk;
		if (m_head == 0 && !Q_strstr(mdl, "_lo"))
		{
			// heads seen in the original demo (cz_recoil): pistol/melee 3 (sometimes 4), shotgun 1-2
			const char *cw = STRING(pev->classname);
			if (Q_strstr(cw, "_pistol") || Q_strstr(cw, "_melee"))
				m_head = RANDOM_LONG(0, 5) == 0 ? 4 : 3;
			else if (Q_strstr(cw, "_shotgun"))
				m_head = RANDOM_LONG(1, 2);
			else
				m_head = RANDOM_LONG(0, 4); // 5 heads
		}
	}
	else
		pev->skin = Q_strstr(STRING(pev->classname), "_swat_") ? 3 : 0; // SWAT uniform

	UTIL_SetSize(pev, Vector(-16, -16, 0), Vector(16, 16, 72));
	UTIL_SetOrigin(pev, pev->origin);

	m_hPlat = nullptr;
	m_bPlatMoved = false;
	m_bMade = g_czMakerSpawn;
	if (m_bMade)
	{
		pev->solid = SOLID_NOT; // the caged soldier is not solid
		if (!g_czCageCvar)
		{
			g_czCageCvar = true;
			CVAR_REGISTER(&cz_cage_drop);
		}
	}
	Settle();

	if (!m_bMade && !m_bInvuln)
		pev->takedamage = DAMAGE_AIM;

	InitBoneControllers();

	const SoldierWeapon *w = Soldier_FindWeapon(STRING(pev->classname));

	SetBodygroup(1, m_head);
	SetBodygroup(2, w->body);
	if (isTerror && Q_strstr(STRING(pev->classname), "_kamakazi"))
		SetBodygroup(3, 1); // bomb belt
	m_flNextSee = gpGlobals->time + 1.0f;

	int seq = LookupSequence(FStringNull(m_idleAnim) ? w->idle : STRING(m_idleAnim));
	if (seq < 0)
		seq = LookupSequence(w->idle);
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

// CZ: a soldier made inside a moving cage/lift (train02) must stand on its floor and ride it.
// floor under the cage: the firing-range shutters (gunrange_door*) open later, never count them as floor or lift
static bool Cz_IsShutter(CBaseEntity *e)
{
	return e && e->pev->targetname && Q_strstr(STRING(e->pev->targetname), "gunrange");
}

static void Cz_TraceFloor(const Vector &from, float down, edict_t *skip, TraceResult &tr)
{
	CBaseEntity *sh[16];
	int ns = 0;
	CBaseEntity *e = nullptr;
	while (ns < 16 && (e = UTIL_FindEntityByClassname(e, "func_door")) != nullptr)
	{
		if (Cz_IsShutter(e) && e->pev->solid != SOLID_NOT)
		{
			sh[ns++] = e;
			e->pev->iuser3 = e->pev->solid;
			e->pev->solid = SOLID_NOT;
		}
	}
	UTIL_TraceLine(from, from - Vector(0, 0, down), ignore_monsters, skip, &tr);
	for (int i = 0; i < ns; i++)
		sh[i]->pev->solid = sh[i]->pev->iuser3;
}

void CCZSoldier::Settle()
{
	if (m_bMade)
	{
		// made by a monstermaker: stay exactly where the mapper put it (the floor of the lowered cage)
		// and ride the moving brush entity that contains this spot
		m_hPlat = nullptr;
		CBaseEntity *best = nullptr;
		float bestTop = -99999.0f;
		CBaseEntity *e = nullptr;
		while ((e = UTIL_FindEntityByClassname(e, "func_door")) != nullptr)
		{
			if (Cz_IsShutter(e))
				continue;
			Vector mn = e->pev->absmin, mx = e->pev->absmax;
			if (pev->origin.x < mn.x - 8 || pev->origin.x > mx.x + 8 || pev->origin.y < mn.y - 8 || pev->origin.y > mx.y + 8)
				continue;
			if (pev->origin.z < mn.z - 24 || pev->origin.z > mx.z + 24)
				continue;
			if (mx.z > bestTop && mx.z <= pev->origin.z + 24)
			{
				bestTop = mx.z;
				best = e;
			}
		}

		if (!best)
		{
			e = nullptr;
			while ((e = UTIL_FindEntityByClassname(e, "func_door")) != nullptr)
			{
				Vector mn = e->pev->absmin, mx = e->pev->absmax;
				if (pev->origin.x >= mn.x - 8 && pev->origin.x <= mx.x + 8 && pev->origin.y >= mn.y - 8 && pev->origin.y <= mx.y + 8 && pev->origin.z >= mn.z - 24 && pev->origin.z <= mx.z + 24)
				{
					best = e;
					break;
				}
			}
		}

		if (best)
		{
			m_hPlat = best;
			m_platOfs = pev->origin - best->pev->origin;
			ALERT(at_console, "soldier: rides %s (%s) at %.0f %.0f %.0f\n", STRING(best->pev->classname), STRING(best->pev->targetname), pev->origin.x, pev->origin.y, pev->origin.z);
		}
		else
			ALERT(at_console, "soldier: maker spawn at %.0f %.0f %.0f, no door under\n", pev->origin.x, pev->origin.y, pev->origin.z);
		return;
	}

	// get out of solid geometry
	for (int i = 0; i < 32; i++)
	{
		TraceResult t;
		UTIL_TraceHull(pev->origin + Vector(0, 0, 36), pev->origin + Vector(0, 0, 36), ignore_monsters, human_hull, ENT(pev), &t);
		if (!t.fStartSolid && !t.fAllSolid)
			break;
		pev->origin.z += 4;
	}

	UTIL_SetOrigin(pev, pev->origin);
	DROP_TO_FLOOR(ENT(pev));

	m_hPlat = nullptr;

	// what are we standing on?
	TraceResult tr;
	UTIL_TraceHull(pev->origin + Vector(0, 0, 36), pev->origin + Vector(0, 0, 28), ignore_monsters, human_hull, ENT(pev), &tr);
	CBaseEntity *h = CBaseEntity::Instance(tr.pHit);
	if (h && h->edict() != INDEXENT(0) && h->pev->movetype == MOVETYPE_PUSH && h->pev->solid != SOLID_NOT)
	{
		m_hPlat = h;
		m_platOfs = pev->origin - h->pev->origin;
		ALERT(at_console, "soldier: stands on %s (%s), origin %.0f %.0f %.0f\n", STRING(h->pev->classname), STRING(h->pev->targetname), pev->origin.x, pev->origin.y, pev->origin.z);
	}
	else
		ALERT(at_console, "soldier: no moving platform under %.0f %.0f %.0f\n", pev->origin.x, pev->origin.y, pev->origin.z);
}

// put the feet on the floor below (the cage floor), keep riding that brush
void CCZSoldier::StandOnFloor()
{
	TraceResult tr;
	Cz_TraceFloor(pev->origin + Vector(0, 0, 24), 324.0f, ENT(pev), tr);
	if (tr.fStartSolid || tr.flFraction >= 1.0f)
		return;

	pev->origin.z = tr.vecEndPos.z;
	UTIL_SetOrigin(pev, pev->origin);

	CBaseEntity *h = CBaseEntity::Instance(tr.pHit);
	if (h && h->edict() != INDEXENT(0) && h->pev->movetype == MOVETYPE_PUSH && h->pev->solid != SOLID_NOT)
		m_hPlat = h;

	CBaseEntity *plat = m_hPlat;
	if (plat)
		m_platOfs = pev->origin - plat->pev->origin;

	ALERT(at_console, "soldier: stands on floor z %.0f (%s)\n", pev->origin.z, h ? STRING(h->pev->classname) : "?");
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

// voice lines: terrorists speak their own language (russian faction: _ru, others _am), SWAT/CT use ct_*_am
static const char *Soldier_Wav(const char *classname, const char *key, char *buf, size_t sz)
{
	if (Q_strstr(classname, "terrorist"))
	{
		const char *lang = Q_strstr(classname, "russian") ? "ru" : "am";
		const char *k = key;
		if (!Q_strcmp(key, "blinded")) k = "myeyes";
		else if (!Q_strcmp(key, "incoming")) k = "grenade";
		Q_snprintf(buf, sz, "t/t_%s_%s.wav", k, lang);
	}
	else if (!Q_strcmp(key, "coughc"))
		Q_snprintf(buf, sz, "t/t_coughc_am.wav");
	else
		Q_snprintf(buf, sz, "ct/ct_%s_am.wav", key);
	return buf;
}

static void Soldier_Voice(CBaseEntity *e, const char *key)
{
	char buf[96];
	Soldier_Say(e->edict(), Soldier_Wav(STRING(e->pev->classname), key, buf, sizeof(buf)));
}

static void Soldier_VoiceN(CBaseEntity *e, const char *base, int count)
{
	char key[48];
	Q_snprintf(key, sizeof(key), "%s%d", base, RANDOM_LONG(1, count));
	Soldier_Voice(e, key);
}

void CCZSoldier::React(int type)
{
	m_react = type;
	m_step = 0;

	if (type == 1)
	{
		// flashbang: "blinded", twitching
		Soldier_Voice(this, "blinded");
		PlayAnim("smflinch");
		m_stepTime = gpGlobals->time + 6.0f;
	}
	else
	{
		// smoke: "Incoming!", cover the head, then "Damn, smoke!" and coughing
		Soldier_Voice(this, "incoming");
		PlayAnim("cower");
		m_stepTime = gpGlobals->time + 2.5f;
	}
}

void CCZSoldier::AnimThink()
{
	if (m_bDead)
	{
		DeadThink();
		return;
	}

	if (m_react == 0)
	{
		// walking, scripted sequences, idle: the common actor code
		CMonsterNPC::AnimThink();
		CheckSeePlayer();
		CombatThink();
		if (m_flFlashOff > 0 && gpGlobals->time >= m_flFlashOff)
		{
			pev->effects &= ~EF_MUZZLEFLASH;
			m_flFlashOff = 0;
		}
	}
	else
	{
		StudioFrameAdvance();

		if (m_react == 1)
		{
			if (gpGlobals->time >= m_stepTime)
			{
				m_react = 0;
				const SoldierWeapon *w = Soldier_FindWeapon(STRING(pev->classname));
				PlayAnim(FStringNull(m_idleAnim) ? w->idle : STRING(m_idleAnim));
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
				Soldier_Voice(this, "damnsmoke");
				PlayAnim("choking");
				m_stepTime = gpGlobals->time + 1.5f;
			}
			else if (m_step == 1 && gpGlobals->time >= m_stepTime)
			{
				m_step = 2;
				Soldier_Voice(this, "coughc");
				m_stepTime = gpGlobals->time + 2.5f;
			}
			else if (m_step == 2 && gpGlobals->time >= m_stepTime)
			{
				m_react = 0;
				const SoldierWeapon *w = Soldier_FindWeapon(STRING(pev->classname));
				PlayAnim(FStringNull(m_idleAnim) ? w->idle : STRING(m_idleAnim));
			}
		}

		pev->nextthink = gpGlobals->time + 0.1f;
	}

	CBaseEntity *plat = m_hPlat;
	if (plat && plat->edict() == INDEXENT(0))
	{
		m_hPlat = nullptr; // old saves bound soldiers to worldspawn
		plat = nullptr;
	}
	if (plat)
	{
		Vector want = plat->pev->origin + m_platOfs;
		if (m_bMade)
			want.z -= cz_cage_drop.value; // tweakable live: the spot the mapper chose is above the cage floor
		if ((want - pev->origin).Length() > 0.1f)
			UTIL_SetOrigin(pev, want);

		if (!m_bMade)
		{
			if (plat->pev->velocity.Length() > 1.0f)
				m_bPlatMoved = true;
			else if (m_bPlatMoved)
			{
				m_bPlatMoved = false;
				Settle(); // the lift has stopped: stand firmly on its floor
			}
		}

		pev->nextthink = gpGlobals->time + 0.02f;
	}
}

// fires the map's TriggerTarget once
void CCZSoldier::FireTrigger(CBaseEntity *pActivator)
{
	if (m_bTrigFired || FStringNull(m_trigTarget) || !STRING(m_trigTarget)[0])
		return;

	m_bTrigFired = true;
	m_bNoPatrol = true; // alerted: stop patrolling
	ALERT(at_console, "soldier '%s': TriggerCondition %d -> %s\n", STRING(pev->targetname), m_trigCond, STRING(m_trigTarget));
	FireTargets(STRING(m_trigTarget), pActivator ? pActivator : this, this, USE_TOGGLE, 0);
}

// TriggerCondition 1/10/11: the soldier sees the player
void CCZSoldier::CheckSeePlayer()
{
	if (m_bTrigFired || (m_trigCond != 1 && m_trigCond != 10 && m_trigCond != 11))
		return;
	if (pev->spawnflags & 128)
		return; // WaitForScript
	if (gpGlobals->time < m_flNextSee)
		return;
	m_flNextSee = gpGlobals->time + 0.2f;

	CBasePlayer *pl = (CBasePlayer *)UTIL_PlayerByIndex(1);
	if (!pl || !pl->IsAlive() || (pl->pev->flags & FL_NOTARGET))
		return;

	Vector eyes = pev->origin + Vector(0, 0, 64);
	Vector to = pl->pev->origin + pl->pev->view_ofs - eyes;
	if (to.Length() > 2048.0f)
		return;

	UTIL_MakeVectors(pev->angles);
	Vector dir = to.Normalize();
	if (DotProduct(dir, gpGlobals->v_forward) < 0.0f) // 180 degree view
		return;

	TraceResult tr;
	UTIL_TraceLine(eyes, pl->pev->origin + pl->pev->view_ofs, ignore_monsters, ENT(pev), &tr);
	if (tr.flFraction < 1.0f && tr.pHit != pl->edict())
		return;

	{
		static const char *spot[] = { "heyyou", "intrudov", "theretheya", "attack" };
		if (Q_strstr(STRING(pev->classname), "terrorist"))
			Soldier_Voice(this, spot[RANDOM_LONG(0, 3)]);
	}
	FireTrigger(pl);
}

BOOL CCZSoldier::TakeDamage(entvars_t *pevInflictor, entvars_t *pevAttacker, float flDamage, int bitsDamageType)
{
	if (m_bDead || pev->takedamage == DAMAGE_NO || m_bInvuln)
		return FALSE;

	pev->health -= flDamage;
	if (m_bUnkillable && pev->health < 1.0f)
		pev->health = 1.0f;

	CBaseEntity *pAttacker = CBaseEntity::Instance(pevAttacker);

	if (pev->health > 0.0f && gpGlobals->time >= m_flNextPain && Q_strstr(STRING(pev->classname), "terrorist"))
	{
		m_flNextPain = gpGlobals->time + 1.2f;
		Soldier_VoiceN(this, "pain", 4);
	}

	if (pAttacker && pAttacker->IsPlayer())
		m_bHurtByPlayer = 1;

	if (m_trigCond == 2)
		FireTrigger(pAttacker);
	else if (m_trigCond == 3 && pev->health <= m_flMaxHealth * 0.5f)
		FireTrigger(pAttacker);

	if (pev->health <= 0.0f)
	{
		Killed(pevAttacker, GIB_NORMAL);
		return TRUE;
	}

	return TRUE;
}

void CCZSoldier::Killed(entvars_t *pevAttacker, int iGib)
{
	if (m_bDead)
		return;

	m_bDead = true;
	m_bScripted = false;
	m_bFollow = false;
	StopMove();
	m_react = 0;
	pev->deadflag = DEAD_DEAD;
	pev->takedamage = DAMAGE_NO;
	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NONE;
	pev->velocity = g_vecZero;
	pev->effects &= ~EF_NODRAW;
	pev->health = 0;
	m_hPlat = nullptr;

	CBaseEntity *pAttacker = CBaseEntity::Instance(pevAttacker);

	if (Q_strstr(STRING(pev->classname), "terrorist"))
		Soldier_VoiceN(this, "death", 4);

	// death animation: the map's "deathanim", else by the side the shot came from
	int seq = -1;
	if (!FStringNull(m_deathAnim))
		seq = LookupSequence(STRING(m_deathAnim));
	if (seq < 0)
	{
		int act = ACT_DIESIMPLE;
		if (pAttacker)
		{
			UTIL_MakeVectors(pev->angles);
			Vector d = pAttacker->pev->origin - pev->origin;
			d.z = 0;
			if (d.Length() > 1.0f)
				act = DotProduct(d.Normalize(), gpGlobals->v_forward) > 0.0f ? ACT_DIEBACKWARD : ACT_DIEFORWARD;
		}
		seq = LookupActivity(act);
		if (seq < 0)
			seq = LookupActivity(ACT_DIESIMPLE);
	}
	if (seq < 0)
		seq = LookupSequence("die-simple");
	if (seq < 0)
		seq = 0;

	pev->sequence = seq;
	pev->frame = 0;
	pev->framerate = 1.0f;
	ResetSequenceInfo();
	m_fSequenceLoops = FALSE;

	EMIT_SOUND_DYN2(edict(), CHAN_VOICE, "common/null.wav", 0, ATTN_NORM, SND_STOP, PITCH_NORM);

	// map events: TriggerCondition 4 = death, "deathtrigger"
	if (m_trigCond == 4)
		FireTrigger(pAttacker);
	if (!FStringNull(m_deathTrig) && STRING(m_deathTrig)[0])
		FireTargets(STRING(m_deathTrig), pAttacker ? pAttacker : this, this, USE_TOGGLE, 0);

	if (pev->spawnflags & 512) // Fade Corpse
		m_flFadeStart = gpGlobals->time + 10.0f;

	SetThink(&CCZSoldier::AnimThink);
	pev->nextthink = gpGlobals->time + 0.02f;
}

void CCZSoldier::DeadThink()
{
	StudioFrameAdvance();

	if (m_flFadeStart > 0 && gpGlobals->time >= m_flFadeStart)
	{
		float t = (gpGlobals->time - m_flFadeStart) / 3.0f;
		if (t >= 1.0f)
		{
			UTIL_Remove(this);
			return;
		}
		pev->rendermode = kRenderTransTexture;
		pev->renderamt = 255.0f * (1.0f - t);
	}

	pev->nextthink = gpGlobals->time + 0.05f;
}

// called from CGrenade: type 1 = flashbang exploded, 2 = smoke grenade exploded
void Cz_GrenadeEvent(const Vector &origin, int type)
{
	static bool reg = false;
	if (!reg)
	{
		reg = true;
		CVAR_REGISTER(&cz_grenade_react);
	}
	float radius = cz_grenade_react.value > 0 ? cz_grenade_react.value : 140.0f;
	CBaseEntity *pEnt = nullptr;
	while ((pEnt = UTIL_FindEntityInSphere(pEnt, origin, radius)) != nullptr)
	{
		if (FNullEnt(pEnt->edict()) || !pEnt->pev->classname)
			continue;

		if (Q_strncmp(STRING(pEnt->pev->classname), "monster_ct_swat_", 16) && Q_strncmp(STRING(pEnt->pev->classname), "monster_terrorist_", 18))
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
	virtual int Save(CSave &save);
	virtual int Restore(CRestore &restore);
	static TYPEDESCRIPTION m_SaveData[];
	virtual void Precache();
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

TYPEDESCRIPTION CCZMonsterMaker::m_SaveData[] =
{
	DEFINE_FIELD(CCZMonsterMaker, m_iszMonsterType, FIELD_STRING),
	DEFINE_FIELD(CCZMonsterMaker, m_cNumMonsters, FIELD_INTEGER),
	DEFINE_FIELD(CCZMonsterMaker, m_cMonstersMade, FIELD_INTEGER),
	DEFINE_FIELD(CCZMonsterMaker, m_flSpawnDelay, FIELD_FLOAT),
};
IMPLEMENT_SAVERESTORE(CCZMonsterMaker, CBaseDelay)

LINK_ENTITY_TO_CLASS(monstermaker, CCZMonsterMaker, CCSMonsterMaker)

void CCZMonsterMaker::Precache()
{
	if (!FStringNull(m_iszMonsterType))
		UTIL_PrecacheOther(STRING(m_iszMonsterType));
}

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

	// models can only be precached while the map loads: do it now for the monster we will create later
	if (!FStringNull(m_iszMonsterType))
		UTIL_PrecacheOther(STRING(m_iszMonsterType));

	// the maker waits for its trigger (train02: cz_train02.seq Round4_Start fires spawn_CT)
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

	g_czMakerSpawn = true;
	CBaseEntity *pNew = CBaseEntity::Create(STRING(m_iszMonsterType), pev->origin, pev->angles, edict());
	g_czMakerSpawn = false;
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
