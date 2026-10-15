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

	int m_head;
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
	{ "pistol",       5, "pistol_combatidlle" },
	{ "smg",          7, "combatidle" },
	{ "mp5",          0, "combatidle" },
	{ "shotgun",      1, "combatidle" },
	{ "assaultrifle", 4, "combatidle" },
	{ "sniperrifle",  3, "sniper_stand" },
	{ "machinegun",   6, "combatidle" },
	{ "grenader",     0, "combatidle" },
	{ "law",          2, "LAW_fire_idle" },
	{ "melee",        8, "machete_combatidle" },
	{ "kamakazi",     8, "machete_combatidle" },
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
}

void CCZSoldier::Spawn()
{
	Precache();

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

void CCZSoldier::AnimThink()
{
	StudioFrameAdvance();

	if (m_fSequenceFinished && !m_fSequenceLoops)
	{
		pev->frame = 0;
		ResetSequenceInfo();
	}

	pev->nextthink = gpGlobals->time + 0.1f;
}
