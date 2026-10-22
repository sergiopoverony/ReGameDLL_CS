#include "precompiled.h"

// Small CZ:DS trigger entities used by the mission scripts.
// Behaviour reconstructed from the exported symbols of cz.so and from
// how the maps use them (see cz_*_entities.txt of the retail maps).

// ---------------------------------------------------------------------------
// trigger_changekeyvalue
// Changes up to 16 key values of every entity named by "target".
// "changetarget" additionally changes the entity's own target.
// ---------------------------------------------------------------------------
const int MAX_CHANGE_KEYVALUES = 16;

class CTriggerChangeKeyValue: public CBaseDelay
{
public:
	virtual int ObjectCaps() { return 0; }
	virtual void KeyValue(KeyValueData *pkvd);
	virtual void Spawn() {}
	virtual int Save(CSave &save);
	virtual int Restore(CRestore &restore);
	virtual void Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value);

public:
	static TYPEDESCRIPTION m_SaveData[];

	string_t m_changeTargetName;
	int m_cTargets;
	string_t m_iKey[MAX_CHANGE_KEYVALUES];
	string_t m_iValue[MAX_CHANGE_KEYVALUES];
};

TYPEDESCRIPTION CTriggerChangeKeyValue::m_SaveData[] =
{
	DEFINE_FIELD(CTriggerChangeKeyValue, m_cTargets, FIELD_INTEGER),
	DEFINE_ARRAY(CTriggerChangeKeyValue, m_iKey, FIELD_STRING, MAX_CHANGE_KEYVALUES),
	DEFINE_ARRAY(CTriggerChangeKeyValue, m_iValue, FIELD_STRING, MAX_CHANGE_KEYVALUES),
	DEFINE_FIELD(CTriggerChangeKeyValue, m_changeTargetName, FIELD_STRING),
};

LINK_ENTITY_TO_CLASS(trigger_changekeyvalue, CTriggerChangeKeyValue, CCSTriggerChangeKeyValue)
IMPLEMENT_SAVERESTORE(CTriggerChangeKeyValue, CBaseDelay)

void CTriggerChangeKeyValue::KeyValue(KeyValueData *pkvd)
{
	if (FStrEq("origin", pkvd->szKeyName) || FStrEq("target", pkvd->szKeyName) ||
		FStrEq("targetname", pkvd->szKeyName) || FStrEq("classname", pkvd->szKeyName) ||
		FStrEq("delay", pkvd->szKeyName))
	{
		CBaseDelay::KeyValue(pkvd);
	}
	else if (FStrEq("changetarget", pkvd->szKeyName))
	{
		m_changeTargetName = ALLOC_STRING(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (m_cTargets < MAX_CHANGE_KEYVALUES)
	{
		char temp[256];

		UTIL_StripToken(pkvd->szKeyName, temp);
		m_iKey[m_cTargets] = ALLOC_STRING(temp);

		UTIL_StripToken(pkvd->szValue, temp);
		m_iValue[m_cTargets] = ALLOC_STRING(temp);

		m_cTargets++;
		pkvd->fHandled = TRUE;
	}
}

void CTriggerChangeKeyValue::Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value)
{
	CBaseEntity *pTarget = nullptr;

	while ((pTarget = UTIL_FindEntityByString(pTarget, "targetname", STRING(pev->target))) != nullptr)
	{
		for (int i = 0; i < m_cTargets; i++)
		{
			char key[128];
			char val[128];

			Q_strncpy(key, STRING(m_iKey[i]), sizeof(key) - 1);
			key[sizeof(key) - 1] = '\0';
			Q_strncpy(val, STRING(m_iValue[i]), sizeof(val) - 1);
			val[sizeof(val) - 1] = '\0';

			KeyValueData kvd;
			Q_memset(&kvd, 0, sizeof(kvd));
			kvd.szKeyName = key;
			kvd.szValue = val;
			kvd.szClassName = (char *)STRING(pTarget->pev->classname);
			kvd.fHandled = FALSE;

			DispatchKeyValue(pTarget->edict(), &kvd);
		}

		if (!FStringNull(m_changeTargetName))
			pTarget->pev->target = m_changeTargetName;
	}
}

// ---------------------------------------------------------------------------
// trigger_hud  (display 0 = hide HUD, 1 = show HUD)
// ---------------------------------------------------------------------------
class CTriggerHud: public CBaseDelay
{
public:
	virtual int ObjectCaps() { return 0; }
	virtual void KeyValue(KeyValueData *pkvd);
	virtual void Spawn();
	virtual int Save(CSave &save);
	virtual int Restore(CRestore &restore);
	void EXPORT TriggerHudUse(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value);

public:
	static TYPEDESCRIPTION m_SaveData[];

	int m_display;
};

TYPEDESCRIPTION CTriggerHud::m_SaveData[] =
{
	DEFINE_FIELD(CTriggerHud, m_display, FIELD_INTEGER),
};

LINK_ENTITY_TO_CLASS(trigger_hud, CTriggerHud, CCSTriggerHud)
IMPLEMENT_SAVERESTORE(CTriggerHud, CBaseDelay)

void CTriggerHud::KeyValue(KeyValueData *pkvd)
{
	if (FStrEq(pkvd->szKeyName, "display"))
	{
		m_display = Q_atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else
		CBaseDelay::KeyValue(pkvd);
}

void CTriggerHud::Spawn()
{
	SetUse(&CTriggerHud::TriggerHudUse);
}

void CTriggerHud::TriggerHudUse(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value)
{
	// HIDEHUD_ALL is not used: the client skips ALL hud drawing with it, including the text messages
	const int hideBits = HIDEHUD_WEAPONS | HIDEHUD_FLASHLIGHT | HIDEHUD_HEALTH | HIDEHUD_TIMER | HIDEHUD_MONEY | HIDEHUD_CROSSHAIR;

	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex(i);
		if (!pPlayer || pPlayer->has_disconnected)
			continue;

		if (m_display)
		{
			// CZ training shows only health/armor (+ammo with a weapon): money and timer stay hidden
			pPlayer->m_iHideHUD &= ~hideBits;
			pPlayer->m_iHideHUD |= (HIDEHUD_MONEY | HIDEHUD_TIMER);
		}
		else
			pPlayer->m_iHideHUD |= hideBits;

		// the client draws health/armor only when the suit bit is set
		if (m_display)
			pPlayer->pev->weapons |= (1 << WEAPON_SUIT);

		pPlayer->m_iClientHideHUD = -1; // force the HideWeapon message to be resent
		ALERT(at_console, "trigger_hud '%s': display %d -> hide bits %d, weapons 0x%x, health %d\n", STRING(pev->targetname), m_display, pPlayer->m_iHideHUD, pPlayer->pev->weapons, (int)pPlayer->pev->health);
	}
}

// ---------------------------------------------------------------------------
// trigger_freezeplayer
// USE_ON freezes, USE_OFF unfreezes, USE_TOGGLE flips.
// ---------------------------------------------------------------------------
class CTriggerFreezePlayer: public CBaseDelay
{
public:
	virtual int Save(CSave &save);
	virtual int Restore(CRestore &restore);
	static TYPEDESCRIPTION m_SaveData[];
	virtual int ObjectCaps() { return 0; }
	virtual void Spawn();
	void EXPORT FreezeUse(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value);

public:
	BOOL m_frozen;
};

TYPEDESCRIPTION CTriggerFreezePlayer::m_SaveData[] =
{
	DEFINE_FIELD(CTriggerFreezePlayer, m_frozen, FIELD_BOOLEAN),
};
IMPLEMENT_SAVERESTORE(CTriggerFreezePlayer, CBaseDelay)

LINK_ENTITY_TO_CLASS(trigger_freezeplayer, CTriggerFreezePlayer, CCSTriggerFreezePlayer)

void CTriggerFreezePlayer::Spawn()
{
	SetUse(&CTriggerFreezePlayer::FreezeUse);
}

void CTriggerFreezePlayer::FreezeUse(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value)
{
	BOOL freeze;

	if (useType == USE_ON)
		freeze = TRUE;
	else if (useType == USE_OFF)
		freeze = FALSE;
	else
		freeze = !m_frozen;

	m_frozen = freeze;

	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex(i);
		if (!pPlayer || pPlayer->has_disconnected)
			continue;

		pPlayer->EnableControl(!freeze);
	}
}

// ---------------------------------------------------------------------------
// trigger_killtarget: removes every entity named by "target".
// ---------------------------------------------------------------------------
bool Cz_KillGeneric(CBaseEntity *pEnt);

class CTriggerKillTarget: public CBaseDelay
{
public:
	virtual int ObjectCaps() { return 0; }
	virtual void Spawn();
	void EXPORT KillEmAll(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value);
};

LINK_ENTITY_TO_CLASS(trigger_killtarget, CTriggerKillTarget, CCSTriggerKillTarget)

void CTriggerKillTarget::Spawn()
{
	SetUse(&CTriggerKillTarget::KillEmAll);
}

void CTriggerKillTarget::KillEmAll(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value)
{
	if (FStringNull(pev->target))
		return;

	CBaseEntity *pTarget = nullptr;
	int nFound = 0;
	while ((pTarget = UTIL_FindEntityByTargetname(pTarget, STRING(pev->target))) != nullptr)
	{
		nFound++;
		if (!(pev->spawnflags & 1) && Cz_KillGeneric(pTarget))
			continue;
		UTIL_Remove(pTarget);
	}

	ALERT(at_console, "trigger_killtarget '%s': target '%s' found %d\n", STRING(pev->targetname), STRING(pev->target), nFound);

	if (pev->spawnflags & 2)
		UTIL_Remove(this);
}

// ---------------------------------------------------------------------------
// trigger_objective
// Server side only records the objective; the on-screen list is a client job.
// state: 0 = new, 1 = completed, 2 = failed (guess from map usage).
// ---------------------------------------------------------------------------
class CTriggerObjective: public CBaseDelay
{
public:
	virtual int ObjectCaps() { return 0; }
	virtual void KeyValue(KeyValueData *pkvd);
	virtual void Spawn();
	virtual int Save(CSave &save);
	virtual int Restore(CRestore &restore);
	void EXPORT ObjectiveUse(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value);

public:
	static TYPEDESCRIPTION m_SaveData[];

	string_t m_objective;
	int m_state;
	int m_noTransition;
};

TYPEDESCRIPTION CTriggerObjective::m_SaveData[] =
{
	DEFINE_FIELD(CTriggerObjective, m_objective, FIELD_STRING),
	DEFINE_FIELD(CTriggerObjective, m_state, FIELD_INTEGER),
	DEFINE_FIELD(CTriggerObjective, m_noTransition, FIELD_INTEGER),
};

LINK_ENTITY_TO_CLASS(trigger_objective, CTriggerObjective, CCSTriggerObjective)
IMPLEMENT_SAVERESTORE(CTriggerObjective, CBaseDelay)

void CTriggerObjective::KeyValue(KeyValueData *pkvd)
{
	if (FStrEq(pkvd->szKeyName, "objective"))
	{
		m_objective = ALLOC_STRING(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "state"))
	{
		m_state = Q_atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "notransition") || FStrEq(pkvd->szKeyName, "notranisition"))
	{
		m_noTransition = Q_atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else
		CBaseDelay::KeyValue(pkvd);
}

void CTriggerObjective::Spawn()
{
	SetUse(&CTriggerObjective::ObjectiveUse);
}

void CTriggerObjective::ObjectiveUse(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value)
{
	// TODO: send the objective list to the client (needs a user message + HUD).
	ALERT(at_aiconsole, "objective %s state %d\n", STRING(m_objective), m_state);
	SUB_UseTargets(this, USE_TOGGLE, 0);
}

// ---------------------------------------------------------------------------
// env_liveammoshooter
// On Use throws an active explosive (hegrenade / smokegrenade / flashgrenade /
// lawrocket) from its origin towards the entity named by "target".
// ---------------------------------------------------------------------------
class CEnvLiveAmmoShooter: public CBaseDelay
{
public:
	virtual int ObjectCaps() { return 0; }
	virtual void KeyValue(KeyValueData *pkvd);
	virtual void Spawn() { Precache(); }
	virtual void Precache();
	virtual int Save(CSave &save);
	virtual int Restore(CRestore &restore);
	virtual void Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value);

public:
	static TYPEDESCRIPTION m_SaveData[];
	string_t m_iszType;
	int m_usEvent;
};

TYPEDESCRIPTION CEnvLiveAmmoShooter::m_SaveData[] =
{
	DEFINE_FIELD(CEnvLiveAmmoShooter, m_iszType, FIELD_STRING),
	DEFINE_FIELD(CEnvLiveAmmoShooter, m_usEvent, FIELD_INTEGER),
};

LINK_ENTITY_TO_CLASS(env_liveammoshooter, CEnvLiveAmmoShooter, CCSEnvLiveAmmoShooter)
IMPLEMENT_SAVERESTORE(CEnvLiveAmmoShooter, CBaseDelay)

void CEnvLiveAmmoShooter::KeyValue(KeyValueData *pkvd)
{
	if (FStrEq("type", pkvd->szKeyName))
	{
		m_iszType = ALLOC_STRING(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else
		CBaseDelay::KeyValue(pkvd);
}

void CEnvLiveAmmoShooter::Precache()
{
	PRECACHE_MODEL("models/w_hegrenade.mdl");
	PRECACHE_MODEL("models/w_smokegrenade.mdl");
	PRECACHE_MODEL("models/w_flashbang.mdl");
	// the grenade code plays its smoke / explosion visuals through a client event
	if (FStrEq(STRING(m_iszType), "smokegrenade"))
		m_usEvent = PRECACHE_EVENT(1, "events/createsmoke.sc");
	else if (FStrEq(STRING(m_iszType), "hegrenade"))
		m_usEvent = PRECACHE_EVENT(1, "events/createexplo.sc");
	if (FStrEq(STRING(m_iszType), "lawrocket"))
		UTIL_PrecacheOther("cz_law_rocket");
}

void CEnvLiveAmmoShooter::Use(CBaseEntity *, CBaseEntity *, USE_TYPE, float)
{
	CBaseEntity *pTarget = UTIL_FindEntityByTargetname(nullptr, STRING(pev->target));
	if (!pTarget)
		return;

	Vector src = pev->origin;
	Vector delta = pTarget->pev->origin - src;
	const char *type = STRING(m_iszType);

	if (FStrEq(type, "lawrocket"))
	{
		CBaseEntity *pRocket = CBaseEntity::Create("cz_law_rocket", src, UTIL_VecToAngles(delta), nullptr);
		if (pRocket)
			pRocket->pev->velocity = delta.Normalize() * 1500.0f;
		return;
	}

	// lob the grenade: flight time T, velocity = delta / T + 0.5 * g * T
	float T = delta.Length() / 600.0f;
	if (T < 0.5f) T = 0.5f;
	if (T > 2.5f) T = 2.5f;
	float g = CVAR_GET_FLOAT("sv_gravity") * 0.55f;
	Vector vel = delta * (1.0f / T);
	vel.z += 0.5f * g * T;

	ALERT(at_console, "env_liveammoshooter '%s': %s -> '%s'\n", STRING(pev->targetname), type, STRING(pev->target));

	if (FStrEq(type, "smokegrenade"))
		CGrenade::ShootSmokeGrenade(nullptr, src, vel, T + 0.2f, (unsigned short)m_usEvent);
	else if (FStrEq(type, "flashgrenade"))
		CGrenade::ShootTimed(nullptr, src, vel, T + 0.2f);
	else
		CGrenade::ShootTimed2(nullptr, src, vel, T + 0.2f, 0, (unsigned short)m_usEvent);
}
