#include "precompiled.h"

// CZ:DS mission tools (camera, blowtorch, fiber optic camera, bomb defuse ...) and related entities:
//   weapon_camera / weapon_blowtorch / weapon_fiberopticcamera  - tool pickups
//   trigger_usetool   - zone where a tool can be used (hold +use)
//   trigger_stealth   - stealth zone (shows the stealth icon)
//   info_camera       - fiber optic camera view point (viewed through when triggered)
//   env_missionfailure - "mission failed" fade, music, map reload
// The HUD icons are sent with the "HudIcons" user message (3 shorts: zone icon mask, owned mask, owned mask)
// exactly like the retail game; bit values were taken from the original demos.

enum
{
	CZ_ICON_STEALTH = 1,
	CZ_ICON_CAMERA  = 4,
	CZ_ICON_FIBER   = 8,
	CZ_ICON_ZIPLINE = 16,
	CZ_ICON_RCBOMB  = 32,
	CZ_ICON_TORCH   = 64,
	CZ_ICON_DEFUSE  = 512,
};

static int g_czTools = 0;       // owned tools (CZ_ICON_* bits)
static int g_czZoneCnt[16];     // how many zones of every icon the player is inside
static int g_czLastA = -1, g_czLastC = -1;
static char g_czLastMap[64];

static void Cz_ToolsNewMap()
{
	if (Q_strcmp(g_czLastMap, STRING(gpGlobals->mapname)))
	{
		Q_strncpy(g_czLastMap, STRING(gpGlobals->mapname), sizeof(g_czLastMap) - 1);
		g_czLastMap[sizeof(g_czLastMap) - 1] = '\0';
		g_czTools = 0;
		Q_memset(g_czZoneCnt, 0, sizeof(g_czZoneCnt));
		g_czLastA = g_czLastC = -1;
	}
}

static CBasePlayer *Cz_Player()
{
	CBaseEntity *pl = UTIL_PlayerByIndex(1);
	if (pl && pl->IsPlayer())
		return (CBasePlayer *)pl;
	return nullptr;
}

static void Cz_SendIcons(bool force = false)
{
	CBasePlayer *pl = Cz_Player();
	if (!pl)
		return;

	int a = 0;
	for (int i = 0; i < 16; i++)
		if (g_czZoneCnt[i] > 0)
			a |= (1 << i);

	int c = a & g_czTools;
	if (!force && a == g_czLastA && c == g_czLastC)
		return;

	g_czLastA = a;
	g_czLastC = c;

	MESSAGE_BEGIN(MSG_ONE, gmsgHudIcons, nullptr, pl->pev);
		WRITE_SHORT(a);
		WRITE_SHORT(c);
		WRITE_SHORT(c);
	MESSAGE_END();
}

static void Cz_ZoneEnter(int bits)
{
	for (int i = 0; i < 16; i++)
		if (bits & (1 << i))
			g_czZoneCnt[i]++;
	Cz_SendIcons();
}

static void Cz_ZoneLeave(int bits)
{
	for (int i = 0; i < 16; i++)
		if ((bits & (1 << i)) && g_czZoneCnt[i] > 0)
			g_czZoneCnt[i]--;
	Cz_SendIcons();
}

static bool Cz_Overlaps(CBaseEntity *a, CBaseEntity *b)
{
	return !(a->pev->absmin.x > b->pev->absmax.x || a->pev->absmax.x < b->pev->absmin.x
		|| a->pev->absmin.y > b->pev->absmax.y || a->pev->absmax.y < b->pev->absmin.y
		|| a->pev->absmin.z > b->pev->absmax.z || a->pev->absmax.z < b->pev->absmin.z);
}

// ---------------------------------------------------------------------------
// tool pickups
// ---------------------------------------------------------------------------
class CCZToolPickup: public CBaseAnimating
{
public:
	virtual void Spawn();
	virtual void Precache();
	virtual int ObjectCaps() { return (CBaseAnimating::ObjectCaps() & ~FCAP_ACROSS_TRANSITION); }
	void EXPORT PickupTouch(CBaseEntity *pOther);

	int m_bit;
	const char *m_model;
};

LINK_ENTITY_TO_CLASS(weapon_camera, CCZToolPickup, CCSCzToolPickup)
LINK_ENTITY_TO_CLASS(weapon_blowtorch, CCZToolPickup, CCSCzToolPickup)
LINK_ENTITY_TO_CLASS(weapon_fiberopticcamera, CCZToolPickup, CCSCzToolPickup)

void CCZToolPickup::Precache()
{
	PRECACHE_MODEL("models/w_camera.mdl");
	PRECACHE_MODEL("models/w_btorch.mdl");
	PRECACHE_MODEL("models/w_focamera.mdl");
	PRECACHE_SOUND("items/gunpickup2.wav");
}

void CCZToolPickup::Spawn()
{
	Cz_ToolsNewMap();
	Precache();

	const char *cls = STRING(pev->classname);
	if (!Q_strcmp(cls, "weapon_blowtorch"))
	{
		m_bit = CZ_ICON_TORCH;
		m_model = "models/w_btorch.mdl";
	}
	else if (!Q_strcmp(cls, "weapon_fiberopticcamera"))
	{
		m_bit = CZ_ICON_FIBER;
		m_model = "models/w_focamera.mdl";
	}
	else
	{
		m_bit = CZ_ICON_CAMERA;
		m_model = "models/w_camera.mdl";
	}

	SET_MODEL(ENT(pev), m_model);
	pev->movetype = MOVETYPE_TOSS;
	pev->solid = SOLID_TRIGGER;
	UTIL_SetSize(pev, Vector(-16, -16, 0), Vector(16, 16, 16));
	UTIL_SetOrigin(pev, pev->origin);
	DROP_TO_FLOOR(ENT(pev));

	SetTouch(&CCZToolPickup::PickupTouch);
}

void CCZToolPickup::PickupTouch(CBaseEntity *pOther)
{
	if (!pOther || !pOther->IsPlayer() || !pOther->IsAlive())
		return;

	g_czTools |= m_bit;
	EMIT_SOUND(ENT(pOther->pev), CHAN_ITEM, "items/gunpickup2.wav", VOL_NORM, ATTN_NORM);
	ALERT(at_aiconsole, "tool picked up: %s (mask %d)\n", STRING(pev->classname), g_czTools);
	Cz_SendIcons(true);

	SetTouch(nullptr);
	UTIL_Remove(this);
}

// ---------------------------------------------------------------------------
// info_camera: fiber optic camera viewpoint
// ---------------------------------------------------------------------------
class CCZInfoCamera: public CBaseAnimating
{
public:
	virtual void Spawn();
	virtual int ObjectCaps() { return (CBaseAnimating::ObjectCaps() & ~FCAP_ACROSS_TRANSITION); }
	virtual void Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value);
	void EXPORT EndView();

	EHANDLE m_hViewer;
};

LINK_ENTITY_TO_CLASS(info_camera, CCZInfoCamera, CCSCzInfoCamera)

void CCZInfoCamera::Spawn()
{
	PRECACHE_MODEL("models/rpgrocket.mdl");
	SET_MODEL(ENT(pev), "models/rpgrocket.mdl");
	pev->movetype = MOVETYPE_NONE;
	pev->solid = SOLID_NOT;
	pev->rendermode = kRenderTransTexture;
	pev->renderamt = 0;
	UTIL_SetOrigin(pev, pev->origin);
}

void CCZInfoCamera::Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value)
{
	CBasePlayer *pl = Cz_Player();
	if (!pl || !pl->IsAlive())
		return;

	if (m_hViewer)
		return;

	// look at the target entity
	CBaseEntity *pTarget = nullptr;
	if (!FStringNull(pev->target))
		pTarget = UTIL_FindEntityByTargetname(nullptr, STRING(pev->target));

	if (pTarget)
	{
		Vector dir = pTarget->pev->origin - pev->origin;
		pev->angles = UTIL_VecToAngles(dir);
		pev->angles.x = -pev->angles.x;
	}

	m_hViewer = pl;
	SET_VIEW(pl->edict(), edict());
	pl->EnableControl(FALSE);

	ALERT(at_aiconsole, "info_camera '%s': view on\n", STRING(pev->targetname));

	SetThink(&CCZInfoCamera::EndView);
	pev->nextthink = gpGlobals->time + 4.0f;
}

void CCZInfoCamera::EndView()
{
	CBasePlayer *pl = Cz_Player();
	if (pl)
	{
		SET_VIEW(pl->edict(), pl->edict());
		pl->EnableControl(TRUE);
	}

	m_hViewer = nullptr;
	SetThink(nullptr);
}

// ---------------------------------------------------------------------------
// trigger_usetool / trigger_stealth
// ---------------------------------------------------------------------------
class CCZUseTool: public CBaseTrigger
{
public:
	virtual void KeyValue(KeyValueData *pkvd);
	virtual void Spawn();
	void EXPORT ToolThink();
	void Complete(CBasePlayer *pl);

	string_t m_toolName;
	string_t m_toolTarget;
	string_t m_rcBombTarget;
	int m_toolSet;
	float m_defuseTime;
	int m_need;       // tool bit the player must own (0 = none)
	int m_iconBits;   // icons shown while inside
	float m_holdTime; // how long +use has to be held
	float m_holdStart;
	float m_nextUse;
	bool m_inside;
	bool m_done;
	bool m_stealth;
	int m_showIcon;
};

LINK_ENTITY_TO_CLASS(trigger_usetool, CCZUseTool, CCSTriggerUseTool)
LINK_ENTITY_TO_CLASS(trigger_stealth, CCZUseTool, CCSTriggerStealth)

void CCZUseTool::KeyValue(KeyValueData *pkvd)
{
	if (FStrEq(pkvd->szKeyName, "toolname"))
	{
		m_toolName = ALLOC_STRING(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "tooltarget"))
	{
		m_toolTarget = ALLOC_STRING(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "rcbombtarget"))
	{
		m_rcBombTarget = ALLOC_STRING(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "toolset"))
	{
		m_toolSet = Q_atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "bombdefusetime"))
	{
		m_defuseTime = Q_atof(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "showicon"))
	{
		m_showIcon = Q_atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "cover") || FStrEq(pkvd->szKeyName, "ct1") || FStrEq(pkvd->szKeyName, "ct2")
		|| FStrEq(pkvd->szKeyName, "elev_doors") || FStrEq(pkvd->szKeyName, "elesnd"))
	{
		pkvd->fHandled = TRUE;
	}
	else
		CBaseTrigger::KeyValue(pkvd);
}

void CCZUseTool::Spawn()
{
	Cz_ToolsNewMap();

	m_stealth = !Q_strcmp(STRING(pev->classname), "trigger_stealth");
	m_need = 0;
	m_iconBits = 0;
	m_holdTime = 0;

	if (m_stealth)
	{
		if (m_showIcon != 0 || FStringNull(m_toolName))
			m_iconBits = CZ_ICON_STEALTH;
	}
	else
	{
		const char *tn = STRING(m_toolName);
		if (!Q_strcmp(tn, "weapon_camera"))
			m_need = CZ_ICON_CAMERA;
		else if (!Q_strcmp(tn, "weapon_blowtorch"))
		{
			m_need = CZ_ICON_TORCH;
			m_holdTime = 3.0f;
		}
		else if (!Q_strcmp(tn, "weapon_fiberopticcamera"))
			m_need = CZ_ICON_FIBER;
		else if (!Q_strcmp(tn, "bomb_defuse"))
		{
			m_need = 0;
			m_iconBits = CZ_ICON_DEFUSE;
			m_holdTime = m_defuseTime > 0 ? m_defuseTime : 5.0f;
		}

		m_iconBits |= m_need | (m_toolSet & 0xFFFF);
	}

	InitTrigger();
	SetThink(&CCZUseTool::ToolThink);
	pev->nextthink = gpGlobals->time + 0.2f;
}

void CCZUseTool::Complete(CBasePlayer *pl)
{
	ALERT(at_aiconsole, "trigger_usetool '%s' (%s) used\n", STRING(pev->targetname), STRING(m_toolName));

	if (m_defuseTime > 0 && !FStringNull(m_toolTarget))
	{
		// remove the bomb that was defused
		CBaseEntity *pBomb = nullptr;
		while ((pBomb = UTIL_FindEntityByTargetname(pBomb, STRING(m_toolTarget))))
		{
			if (pBomb != this)
				UTIL_Remove(pBomb);
		}
	}

	if (!FStringNull(m_rcBombTarget))
	{
		CBaseEntity *pBomb = nullptr;
		while ((pBomb = UTIL_FindEntityByTargetname(pBomb, STRING(m_rcBombTarget))))
			UTIL_Remove(pBomb);
	}

	SUB_UseTargets(pl, USE_TOGGLE, 0);

	if (pev->spawnflags & 1)
	{
		m_done = true;
		if (m_inside)
		{
			Cz_ZoneLeave(m_iconBits);
			m_inside = false;
		}
	}
}

void CCZUseTool::ToolThink()
{
	pev->nextthink = gpGlobals->time + 0.05f;

	CBasePlayer *pl = Cz_Player();
	bool inside = pl && pl->IsAlive() && !m_done && Cz_Overlaps(this, pl);

	if (inside != m_inside)
	{
		m_inside = inside;
		if (m_iconBits)
		{
			if (inside)
				Cz_ZoneEnter(m_iconBits);
			else
				Cz_ZoneLeave(m_iconBits);
		}
		m_holdStart = 0;
	}

	if (m_stealth || !inside || (FStringNull(pev->target) && FStringNull(m_toolTarget) && FStringNull(m_rcBombTarget)))
		return;

	if (m_need && !(g_czTools & m_need))
		return;

	if (gpGlobals->time < m_nextUse)
		return;

	bool use = (pl->pev->button & IN_USE) != 0;
	if (!use)
	{
		if (m_holdStart != 0)
		{
			MESSAGE_BEGIN(MSG_ONE, gmsgBarTime, nullptr, pl->pev);
				WRITE_SHORT(0);
			MESSAGE_END();
		}
		m_holdStart = 0;
		return;
	}

	if (m_holdStart == 0)
	{
		m_holdStart = gpGlobals->time;
		if (m_holdTime > 0)
		{
			MESSAGE_BEGIN(MSG_ONE, gmsgBarTime, nullptr, pl->pev);
				WRITE_SHORT((int)(m_holdTime + 0.5f));
			MESSAGE_END();
		}
	}

	if (gpGlobals->time - m_holdStart >= m_holdTime)
	{
		m_holdStart = 0;
		m_nextUse = gpGlobals->time + 1.5f;
		if (m_holdTime > 0)
		{
			MESSAGE_BEGIN(MSG_ONE, gmsgBarTime, nullptr, pl->pev);
				WRITE_SHORT(0);
			MESSAGE_END();
		}
		Complete(pl);
	}
}

// ---------------------------------------------------------------------------
// env_missionfailure
// ---------------------------------------------------------------------------
class CCZMissionFailure: public CBaseDelay
{
public:
	virtual int ObjectCaps() { return 0; }
	virtual void KeyValue(KeyValueData *pkvd);
	virtual void Spawn() {}
	virtual void Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value);
	void EXPORT Restart();

	float m_holdTime;
	float m_duration;
	string_t m_music;
	int m_fr, m_fg, m_fb, m_fa = 255;
	string_t m_nextMap;
	void EXPORT ChangeMap();
};

LINK_ENTITY_TO_CLASS(env_missionfailure, CCZMissionFailure, CCSMissionFailure)
LINK_ENTITY_TO_CLASS(trigger_endmission, CCZMissionFailure, CCSMissionFailure)

void CCZMissionFailure::KeyValue(KeyValueData *pkvd)
{
	if (FStrEq(pkvd->szKeyName, "holdtime"))
	{
		m_holdTime = Q_atof(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "duration"))
	{
		m_duration = Q_atof(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "music"))
	{
		m_music = ALLOC_STRING(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "fade_red")) { m_fr = Q_atoi(pkvd->szValue); pkvd->fHandled = TRUE; }
	else if (FStrEq(pkvd->szKeyName, "fade_green")) { m_fg = Q_atoi(pkvd->szValue); pkvd->fHandled = TRUE; }
	else if (FStrEq(pkvd->szKeyName, "fade_blue")) { m_fb = Q_atoi(pkvd->szValue); pkvd->fHandled = TRUE; }
	else if (FStrEq(pkvd->szKeyName, "fade_alpha")) { m_fa = Q_atoi(pkvd->szValue); pkvd->fHandled = TRUE; }
	else if (FStrEq(pkvd->szKeyName, "nextmap")) { m_nextMap = ALLOC_STRING(pkvd->szValue); pkvd->fHandled = TRUE; }
	else if (FStrEq(pkvd->szKeyName, "message") || FStrEq(pkvd->szKeyName, "loadtime") || FStrEq(pkvd->szKeyName, "messagevolume"))
		pkvd->fHandled = TRUE;
	else
		CBaseDelay::KeyValue(pkvd);
}

void CCZMissionFailure::Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value)
{
	CBasePlayer *pl = Cz_Player();
	if (!pl)
		return;

	ALERT(at_aiconsole, "env_missionfailure '%s'\n", STRING(pev->targetname));

	float dur = m_duration > 0 ? m_duration : 2.0f;
	float hold = m_holdTime > 0 ? m_holdTime : 2.0f;

	UTIL_ScreenFade(pl, Vector(m_fr, m_fg, m_fb), dur, hold + 2.0f, m_fa, FFADE_OUT | FFADE_STAYOUT);

	if (!FStringNull(m_music))
		CLIENT_COMMAND(pl->edict(), "mp3 play %s\n", STRING(m_music));

	pl->EnableControl(FALSE);

	if (!Q_strcmp(STRING(pev->classname), "trigger_endmission") && !FStringNull(m_nextMap))
	{
		SetThink(&CCZMissionFailure::ChangeMap);
		pev->nextthink = gpGlobals->time + dur + hold + 0.5f;
		return;
	}

	SetThink(&CCZMissionFailure::Restart);
	pev->nextthink = gpGlobals->time + dur + hold + 3.0f;
}

void CCZMissionFailure::ChangeMap()
{
	CHANGE_LEVEL(STRING(m_nextMap), nullptr);
}

void CCZMissionFailure::Restart()
{
	char cmd[96];
	Q_snprintf(cmd, sizeof(cmd), "map %s\n", STRING(gpGlobals->mapname));
	SERVER_COMMAND(cmd);
}
