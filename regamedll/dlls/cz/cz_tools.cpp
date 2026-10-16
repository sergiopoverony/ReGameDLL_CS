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

// rescue zone indicator (bit 1024) - sent the same way as the original game does (HudIcons)
void Cz_RescueIcon(bool on)
{
	Cz_ToolsNewMap();
	g_czZoneCnt[10] = on ? 1 : 0;
	Cz_SendIcons(true);
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
// blowtorch effects: a flame at the tip while held, sparks + light while "firing" on a close target
// ---------------------------------------------------------------------------
static cvar_t cz_torch_fwd   = { "cz_torch_fwd",   "24", 0, 24.0f, nullptr };
static cvar_t cz_torch_right = { "cz_torch_right", "7",  0, 7.0f,  nullptr };
static cvar_t cz_torch_up    = { "cz_torch_up",    "3", 0, 3.0f, nullptr };
static cvar_t cz_torch_scale = { "cz_torch_scale", "0.25", 0, 0.25f, nullptr };
static bool g_czTorchCvars = false;

// the padlock the player is aiming at (item_generic with a padlock model), or null
static CBaseEntity *Cz_AimedLock(CBasePlayer *pl, float maxDist = 90.0f)
{
	UTIL_MakeVectors(pl->pev->v_angle);
	Vector eye = pl->pev->origin + pl->pev->view_ofs;
	CBaseEntity *best = nullptr;
	float bestDot = 0.93f;

	CBaseEntity *e = nullptr;
	while ((e = UTIL_FindEntityByClassname(e, "item_generic")) != nullptr)
	{
		if (!e->pev->model || !Q_strstr(STRING(e->pev->model), "padlock"))
			continue;

		Vector to = e->pev->origin - eye;
		float dist = to.Length();
		if (dist > maxDist || dist < 1.0f)
			continue;

		float dot = DotProduct(gpGlobals->v_forward, to * (1.0f / dist));
		if (dot > bestDot)
		{
			bestDot = dot;
			best = e;
		}
	}

	return best;
}

static bool Cz_MapHasLocks()
{
	CBaseEntity *e = nullptr;
	while ((e = UTIL_FindEntityByClassname(e, "item_generic")) != nullptr)
		if (e->pev->model && Q_strstr(STRING(e->pev->model), "padlock"))
			return true;
	return false;
}

class CCZTorchFx: public CBaseEntity
{
public:
	virtual void Spawn();
	virtual int ObjectCaps() { return (CBaseEntity::ObjectCaps() & ~FCAP_ACROSS_TRANSITION); }
	void EXPORT FxThink();

	float m_nextLight;
	float m_nextSound;
	bool m_wasAttack;
	EHANDLE m_hFlame;
};

LINK_ENTITY_TO_CLASS(cz_torch_fx, CCZTorchFx, CCSTorchFx)

void CCZTorchFx::Spawn()
{
	if (!g_czTorchCvars)
	{
		g_czTorchCvars = true;
		CVAR_REGISTER(&cz_torch_fwd);
		CVAR_REGISTER(&cz_torch_right);
		CVAR_REGISTER(&cz_torch_up);
		CVAR_REGISTER(&cz_torch_scale);
	}

	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NONE;
	pev->effects = EF_NODRAW;
	m_nextLight = m_nextSound = 0;
	m_wasAttack = false;
	m_hFlame = nullptr;
	SetThink(&CCZTorchFx::FxThink);
	pev->nextthink = gpGlobals->time + 0.05f;
}

void CCZTorchFx::FxThink()
{
	pev->nextthink = gpGlobals->time + 0.02f;

	CBasePlayer *pl = Cz_Player();
	CSprite *flame = (CSprite *)(CBaseEntity *)m_hFlame;

	bool holding = pl && pl->IsAlive() && (g_czTools & CZ_ICON_TORCH) && !pl->m_pActiveItem;
	if (!holding)
	{
		if (flame)
			flame->pev->effects |= EF_NODRAW;
		return;
	}

	if (FStringNull(pl->pev->viewmodel))
		pl->pev->viewmodel = MAKE_STRING("models/v_btorch.mdl");

	UTIL_MakeVectors(pl->pev->v_angle);
	Vector eye = pl->pev->origin + pl->pev->view_ofs;
	bool attack = (pl->pev->button & IN_ATTACK) != 0;

	// visible flame at the tip of the torch (tweak with cz_torch_fwd / right / up / scale)
	Vector tip = eye + gpGlobals->v_forward * cz_torch_fwd.value + gpGlobals->v_right * cz_torch_right.value + gpGlobals->v_up * cz_torch_up.value;

	if (!flame)
	{
		flame = CSprite::SpriteCreate("sprites/glow01.spr", tip, FALSE);
		if (flame)
		{
			flame->SetTransparency(kRenderTransAdd, 255, 150, 50, 220, kRenderFxNone);
			m_hFlame = flame;
		}
	}

	if (flame)
	{
		flame->pev->effects &= ~EF_NODRAW;
		flame->pev->scale = cz_torch_scale.value * (attack ? 1.5f : 1.0f);
		UTIL_SetOrigin(flame->pev, tip);
	}

	if (!attack)
	{
		m_wasAttack = false;
		return;
	}

	if (!m_wasAttack)
	{
		m_wasAttack = true;
		pl->pev->weaponanim = 1;
	}

	// sparks and light come from the lock, and only when the torch touches it
	CBaseEntity *lock = Cz_AimedLock(pl);
	if (!lock)
		return;

	Vector hit = lock->pev->origin + (eye - lock->pev->origin).Normalize() * 3.0f;

	if (gpGlobals->time >= m_nextLight)
	{
		m_nextLight = gpGlobals->time + 0.1f;

		MESSAGE_BEGIN(MSG_PVS, SVC_TEMPENTITY, hit);
			WRITE_BYTE(TE_SPARKS);
			WRITE_COORD(hit.x);
			WRITE_COORD(hit.y);
			WRITE_COORD(hit.z);
		MESSAGE_END();

		MESSAGE_BEGIN(MSG_PVS, SVC_TEMPENTITY, hit);
			WRITE_BYTE(TE_DLIGHT);
			WRITE_COORD(hit.x);
			WRITE_COORD(hit.y);
			WRITE_COORD(hit.z);
			WRITE_BYTE(14);
			WRITE_BYTE(255);
			WRITE_BYTE(200);
			WRITE_BYTE(110);
			WRITE_BYTE(2);
			WRITE_BYTE(0);
		MESSAGE_END();
	}

	if (gpGlobals->time >= m_nextSound)
	{
		m_nextSound = gpGlobals->time + 0.35f;
		EMIT_SOUND(pl->edict(), CHAN_WEAPON, (RANDOM_LONG(0, 1) ? "buttons/spark5.wav" : "buttons/spark6.wav"), 0.6f, ATTN_NORM);
	}
}

// ---------------------------------------------------------------------------
// radio controlled bomb: planted in a marked zone with the left button, detonated with the next press
// ---------------------------------------------------------------------------
static EHANDLE g_hRcPlanted;
static float g_flRcPlantTime = 0;

class CCZRcPlanted: public CBaseAnimating
{
public:
	virtual void Spawn()
	{
		SET_MODEL(ENT(pev), "models/w_rcontrolbomb_bomb.mdl");
		pev->movetype = MOVETYPE_NONE;
		pev->solid = SOLID_NOT;
		UTIL_SetOrigin(pev, pev->origin);
	}
	virtual int ObjectCaps() { return (CBaseAnimating::ObjectCaps() & ~FCAP_ACROSS_TRANSITION); }
};
LINK_ENTITY_TO_CLASS(cz_rc_planted, CCZRcPlanted, CCSRcPlanted)

class CCZRcCtl: public CBaseEntity
{
public:
	virtual void Spawn()
	{
		pev->solid = SOLID_NOT;
		pev->movetype = MOVETYPE_NONE;
		pev->effects = EF_NODRAW;
		m_prev = true;
		SetThink(&CCZRcCtl::CtlThink);
		pev->nextthink = gpGlobals->time + 0.05f;
	}
	virtual int ObjectCaps() { return (CBaseEntity::ObjectCaps() & ~FCAP_ACROSS_TRANSITION); }
	void EXPORT CtlThink();
	bool m_prev;
};
LINK_ENTITY_TO_CLASS(cz_rc_ctl, CCZRcCtl, CCSRcCtl)

void CCZRcCtl::CtlThink()
{
	pev->nextthink = gpGlobals->time + 0.02f;

	CBasePlayer *pl = Cz_Player();
	if (!pl || !(g_czTools & CZ_ICON_RCBOMB))
	{
		UTIL_Remove(this);
		return;
	}

	bool atk = (pl->pev->button & IN_ATTACK) != 0;
	bool edge = atk && !m_prev;
	m_prev = atk;

	CBaseEntity *bomb = g_hRcPlanted;
	if (!edge || !bomb || gpGlobals->time < g_flRcPlantTime + 0.5f)
		return;

	// boom
	Vector pos = bomb->pev->origin;
	MESSAGE_BEGIN(MSG_PVS, SVC_TEMPENTITY, pos);
		WRITE_BYTE(TE_EXPLOSION);
		WRITE_COORD(pos.x);
		WRITE_COORD(pos.y);
		WRITE_COORD(pos.z);
		WRITE_SHORT(g_sModelIndexFireball);
		WRITE_BYTE(25);
		WRITE_BYTE(15);
		WRITE_BYTE(0);
	MESSAGE_END();
	EMIT_SOUND(bomb->edict(), CHAN_WEAPON, "weapons/c4_explode1.wav", VOL_NORM, ATTN_NORM);
	ALERT(at_aiconsole, "rc bomb detonated at %.0f %.0f %.0f\n", pos.x, pos.y, pos.z);

	CBaseEntity *brk = nullptr;
	while ((brk = UTIL_FindEntityByClassname(brk, "func_breakable")))
	{
		if (brk->pev->iuser4 == 2 && brk->pev->takedamage != DAMAGE_NO)
		{
			ALERT(at_aiconsole, "rc bomb: breaking '%s'\n", STRING(brk->pev->targetname));
			brk->Use(pl, pl, USE_TOGGLE, 0);
		}
	}

	UTIL_Remove(bomb);
	g_hRcPlanted = nullptr;
	g_czTools &= ~CZ_ICON_RCBOMB;
	pl->pev->viewmodel = 0;
	Cz_SendIcons(true);
}

// the bomb tool goes into the hands; everything else is taken away
bool Cz_GiveTool(CBasePlayer *pl, const char *name)
{
	if (!name || Q_strcmp(name, "weapon_radiocontrolledbomb"))
		return false;

	Cz_ToolsNewMap();
	pl->RemoveAllItems(FALSE);
	g_czTools = CZ_ICON_RCBOMB;
	g_hRcPlanted = nullptr;

	{
		int len = 0;
		byte *f = LOAD_FILE_FOR_ME("models/v_rcontrolbomb.mdl", &len);
		if (f)
		{
			FREE_FILE(f);
			pl->pev->viewmodel = MAKE_STRING("models/v_rcontrolbomb.mdl");
			pl->pev->weaponanim = 0;
		}
		else
			ALERT(at_console, "tool: models/v_rcontrolbomb.mdl not found\n");
	}

	if (!UTIL_FindEntityByClassname(nullptr, "cz_rc_ctl"))
		CBaseEntity::Create("cz_rc_ctl", pl->pev->origin, g_vecZero, nullptr);

	EMIT_SOUND(ENT(pl->pev), CHAN_ITEM, "items/gunpickup2.wav", VOL_NORM, ATTN_NORM);
	ALERT(at_aiconsole, "tool given: weapon_radiocontrolledbomb\n");
	Cz_SendIcons(true);
	return true;
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
	void EXPORT PickupThink();
	void Pickup(CBaseEntity *pOther);

	int m_bit;
	const char *m_model;
	const char *m_vmodel;
};

LINK_ENTITY_TO_CLASS(weapon_camera, CCZToolPickup, CCSCzToolPickup)
LINK_ENTITY_TO_CLASS(weapon_blowtorch, CCZToolPickup, CCSCzToolPickup)
LINK_ENTITY_TO_CLASS(weapon_fiberopticcamera, CCZToolPickup, CCSCzToolPickup)

void CCZToolPickup::Precache()
{
	PRECACHE_MODEL("models/w_camera.mdl");
	PRECACHE_MODEL("models/w_btorch.mdl");
	PRECACHE_MODEL("models/w_focamera.mdl");
	{
		static const char *vm[] = { "models/v_camera.mdl", "models/v_btorch.mdl", "models/v_focamera.mdl" };
		for (const char *m : vm)
		{
			int len = 0;
			byte *f = LOAD_FILE_FOR_ME(m, &len);
			if (f)
			{
				FREE_FILE(f);
				PRECACHE_MODEL(m);
			}
			else
				ALERT(at_console, "tool: %s not found\n", m);
		}
	}
	PRECACHE_SOUND("items/gunpickup2.wav");
	PRECACHE_MODEL("sprites/glow01.spr");
	PRECACHE_SOUND("buttons/spark5.wav");
	PRECACHE_SOUND("buttons/spark6.wav");
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
		m_vmodel = "models/v_btorch.mdl";
	}
	else if (!Q_strcmp(cls, "weapon_fiberopticcamera"))
	{
		m_bit = CZ_ICON_FIBER;
		m_model = "models/w_focamera.mdl";
		m_vmodel = "models/v_focamera.mdl";
	}
	else
	{
		m_bit = CZ_ICON_CAMERA;
		m_model = "models/w_camera.mdl";
		m_vmodel = "models/v_camera.mdl";
	}

	SET_MODEL(ENT(pev), m_model);
	pev->movetype = MOVETYPE_TOSS;
	pev->solid = SOLID_TRIGGER;
	UTIL_SetSize(pev, Vector(-16, -16, 0), Vector(16, 16, 16));
	UTIL_SetOrigin(pev, pev->origin);
	DROP_TO_FLOOR(ENT(pev));

	SetTouch(&CCZToolPickup::PickupTouch);

	// backup for a touch that never fires: pick up when the player stands next to the tool
	SetThink(&CCZToolPickup::PickupThink);
	pev->nextthink = gpGlobals->time + 0.5f;

	ALERT(at_console, "tool pickup %s at %.0f %.0f %.0f\n", cls, pev->origin.x, pev->origin.y, pev->origin.z);
}

void CCZToolPickup::PickupThink()
{
	CBasePlayer *pl = Cz_Player();
	if (pl && pl->IsAlive())
	{
		Vector d = pl->pev->origin - pev->origin;
		if (Vector(d.x, d.y, 0).Length() < 44.0f && fabsf(d.z) < 72.0f)
		{
			Pickup(pl);
			return;
		}
	}

	pev->nextthink = gpGlobals->time + 0.1f;
}

void CCZToolPickup::PickupTouch(CBaseEntity *pOther)
{
	if (!pOther || !pOther->IsPlayer() || !pOther->IsAlive())
		return;

	Pickup(pOther);
}

void CCZToolPickup::Pickup(CBaseEntity *pOther)
{
	g_czTools |= m_bit;

	if (m_bit == CZ_ICON_TORCH && !UTIL_FindEntityByClassname(nullptr, "cz_torch_fx"))
		CBaseEntity::Create("cz_torch_fx", pev->origin, g_vecZero, nullptr);

	// the tool is in the hands now
	{
		int len = 0;
		byte *f = LOAD_FILE_FOR_ME(m_vmodel, &len);
		if (f)
		{
			FREE_FILE(f);
			pOther->pev->viewmodel = MAKE_STRING(m_vmodel);
			pOther->pev->weaponanim = 0;
		}
	}
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
	bool m_rcMode;
	bool m_prevAtk;
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
	m_rcMode = false;

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

		m_rcMode = ((m_toolSet & CZ_ICON_RCBOMB) || !FStringNull(m_rcBombTarget));
		m_prevAtk = true;
		if (m_rcMode)
		{
			m_need = CZ_ICON_RCBOMB;
			m_holdTime = 0;
			PRECACHE_MODEL("models/w_rcontrolbomb_bomb.mdl");
			PRECACHE_MODEL("models/v_rcontrolbomb.mdl");
			PRECACHE_SOUND("weapons/c4_explode1.wav");
			PRECACHE_SOUND("items/gunpickup2.wav");
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

	if (m_rcMode)
	{
		Vector pos = pl->pev->origin;
		Vector ang = g_vecZero;
		CBaseEntity *guide = FStringNull(m_rcBombTarget) ? nullptr : UTIL_FindEntityByTargetname(nullptr, STRING(m_rcBombTarget));
		if (guide)
		{
			pos = guide->pev->origin;
			ang = guide->pev->angles;
		}
		CBaseEntity *old = g_hRcPlanted;
		if (old)
			UTIL_Remove(old);
		g_hRcPlanted = CBaseEntity::Create("cz_rc_planted", pos, ang, nullptr);
		g_flRcPlantTime = gpGlobals->time;
		EMIT_SOUND(ENT(pl->pev), CHAN_WEAPON, "items/gunpickup2.wav", VOL_NORM, ATTN_NORM);
		ALERT(at_aiconsole, "rc bomb planted at %.0f %.0f %.0f\n", pos.x, pos.y, pos.z);
	}

	if (!FStringNull(m_rcBombTarget))
	{
		CBaseEntity *pBomb = nullptr;
		while ((pBomb = UTIL_FindEntityByTargetname(pBomb, STRING(m_rcBombTarget))))
			UTIL_Remove(pBomb);
	}

	if (m_need == CZ_ICON_TORCH)
	{
		// the lock is protected by an invisible func_breakable that only the torch can damage: burn it through
		CBaseEntity *lock = Cz_AimedLock(pl, 130.0f);
		Vector c = lock ? lock->pev->origin : pl->pev->origin;
		CBaseEntity *brk = nullptr;
		while ((brk = UTIL_FindEntityByClassname(brk, "func_breakable")))
		{
			Vector bc = (brk->pev->absmin + brk->pev->absmax) * 0.5f;
			if ((bc - c).Length() < 150.0f && brk->pev->takedamage != DAMAGE_NO)
			{
				ALERT(at_aiconsole, "torch: breaking '%s'\n", STRING(brk->pev->targetname));
				brk->Use(pl, pl, USE_TOGGLE, 0);
			}
		}
		if (lock)
			UTIL_Remove(lock); // the padlock model itself
		m_done = true;
		if (m_inside)
		{
			Cz_ZoneLeave(m_iconBits);
			m_inside = false;
		}
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

	bool atkNow = pl && (pl->pev->button & IN_ATTACK) != 0;
	bool atkEdge = atkNow && !m_prevAtk;
	m_prevAtk = atkNow;

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

	if (m_stealth || !inside || (m_need != CZ_ICON_TORCH && FStringNull(pev->target) && FStringNull(m_toolTarget) && FStringNull(m_rcBombTarget)))
		return;

	if (m_need && !(g_czTools & m_need))
		return;

	if (gpGlobals->time < m_nextUse)
		return;

	if (m_rcMode)
	{
		// plant with the left button (one press), only inside the marked zone
		if (atkEdge)
		{
			m_nextUse = gpGlobals->time + 1.0f;
			Complete(pl);
		}
		return;
	}

	// the blowtorch is used like a weapon: hold fire while touching the lock
	bool use = (pl->pev->button & ((m_need == CZ_ICON_TORCH) ? IN_ATTACK : IN_USE)) != 0;
	if (use && m_need == CZ_ICON_TORCH && Cz_MapHasLocks() && !Cz_AimedLock(pl, 110.0f))
		use = false; // the torch only works on the lock
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
