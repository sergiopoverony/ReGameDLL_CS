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
	CZ_ICON_RADIO   = 16,
	CZ_ICON_RCBOMB  = 32,
	CZ_ICON_TORCH   = 64,
	CZ_ICON_DEFUSE  = 512,
};

static int g_czTools = 0;       // owned tools (CZ_ICON_* bits)
static int g_czZoneCnt[16];     // how many zones of every icon the player is inside
static int g_czLastA = -1, g_czLastC = -1;
static bool g_czRadioDone = false; // radio already used: its HUD icon goes away
static char g_czLastMap[64];

static int  g_rcCharges = 5;     // radio controlled mines left
static int  g_rcHold = 0;        // 0 = mine in hands, 1 = detonator
static bool g_fiberOn = false;   // looking through a fiber optic camera
static float g_fiberExitT = -10;
struct czq_t { int anim; float t; };
static czq_t g_tQ[2];            // delayed viewmodel animations
static float g_czToolSent = -1;
static bool g_czSaveFix = false;
static float g_rcBusy = 0;       // switching mine/detonator: buttons ignored until then

static void Cz_ToolsNewMap()
{
	if (Q_strcmp(g_czLastMap, STRING(gpGlobals->mapname)))
	{
		Q_strncpy(g_czLastMap, STRING(gpGlobals->mapname), sizeof(g_czLastMap) - 1);
		g_czLastMap[sizeof(g_czLastMap) - 1] = '\0';
		g_czTools = 0;
		g_czRadioDone = false;
		g_rcCharges = 5;
		g_rcHold = 0;
		g_fiberOn = false;
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

static void Cz_EnsureStateFwd();

static void Cz_WeapAnim(CBasePlayer *pl, int anim)
{
	if (!pl)
		return;
	pl->pev->weaponanim = anim;
	// like the original: an explicit weapon animation message
	MESSAGE_BEGIN(MSG_ONE, SVC_WEAPONANIM, nullptr, pl->pev);
		WRITE_BYTE(anim);
		WRITE_BYTE(0);
	MESSAGE_END();
	ALERT(at_aiconsole, "tool anim %d\n", anim);
}

static void Cz_QueueAnim(int slot, int anim, float delay)
{
	g_tQ[slot].anim = anim;
	g_tQ[slot].t = gpGlobals->time + delay;
}

static void Cz_RunQueue(CBasePlayer *pl)
{
	for (int i = 0; i < 2; i++)
	{
		if (g_tQ[i].t && gpGlobals->time >= g_tQ[i].t)
		{
			g_tQ[i].t = 0;
			if (pl)
				Cz_WeapAnim((CBasePlayer *)pl, g_tQ[i].anim);
		}
	}
}

// "CzTool": BYTE current tool (0 none, 1 camera, 2 fiber, 3 torch, 4 mines), BYTE mines left, BYTE 1 = detonator in hands
static void Cz_SendTool()
{
	CBasePlayer *pl = Cz_Player();
	if (!pl)
		return;
	int tool = 0;
	if (g_czTools & CZ_ICON_RADIO) tool = 5;
	else if (g_czTools & CZ_ICON_RCBOMB) tool = 4;
	else if (g_czTools & CZ_ICON_TORCH) tool = 3;
	else if (g_czTools & CZ_ICON_FIBER) tool = 2;
	else if (g_czTools & CZ_ICON_CAMERA) tool = 1;
	MESSAGE_BEGIN(MSG_ONE, gmsgCzTool, nullptr, pl->pev);
		WRITE_BYTE(tool);
		WRITE_BYTE(g_rcCharges);
		WRITE_BYTE(g_rcHold);
	MESSAGE_END();
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

	// like the original: (icons shown, icons active = player is in the zone, tools owned);
	// an owned tool icon is always shown, red outside its zone and green inside
	int c = g_czTools;
	if (g_czRadioDone)
		c &= ~CZ_ICON_RADIO;
	// a tool icon (even its zone) is not shown until the tool is taken
	const int toolMask = CZ_ICON_CAMERA | CZ_ICON_FIBER | CZ_ICON_RCBOMB | CZ_ICON_TORCH | CZ_ICON_RADIO;
	a &= ~toolMask | c;
	int shown = a | (c & (CZ_ICON_CAMERA | CZ_ICON_FIBER | CZ_ICON_RCBOMB | CZ_ICON_TORCH | CZ_ICON_RADIO));
	if (!force && a == g_czLastA && c == g_czLastC)
		return;

	g_czLastA = a;
	g_czLastC = c;
	Cz_EnsureStateFwd();

	MESSAGE_BEGIN(MSG_ONE, gmsgHudIcons, nullptr, pl->pev);
		WRITE_SHORT(shown);
		WRITE_SHORT(a);
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
static cvar_t cz_torch_right = { "cz_torch_right", "11", 0, 11.0f, nullptr };
static cvar_t cz_torch_up    = { "cz_torch_up",    "5", 0, 5.0f, nullptr };
static cvar_t cz_torch_scale = { "cz_torch_scale", "0.25", 0, 0.25f, nullptr };
static bool g_czTorchCvars = false;

// the padlock the player is aiming at (item_generic with a padlock model), or null
static CBaseEntity *Cz_AimedLock(CBasePlayer *pl, float maxDist = 64.0f)
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
	virtual int Save(CSave &save);
	virtual int Restore(CRestore &restore);
	static TYPEDESCRIPTION m_SaveData[];
	virtual void Spawn();
	virtual int ObjectCaps() { return (CBaseEntity::ObjectCaps() & ~FCAP_ACROSS_TRANSITION); }
	void EXPORT FxThink();

	float m_nextLight;
	float m_nextSound;
	bool m_wasAttack;
	EHANDLE m_hFlame;
	EHANDLE m_hSndLock;
};

TYPEDESCRIPTION CCZTorchFx::m_SaveData[] =
{
	DEFINE_FIELD(CCZTorchFx, m_nextLight, FIELD_TIME),
	DEFINE_FIELD(CCZTorchFx, m_nextSound, FIELD_TIME),
	DEFINE_FIELD(CCZTorchFx, m_wasAttack, FIELD_CHARACTER),
	DEFINE_FIELD(CCZTorchFx, m_hFlame, FIELD_EHANDLE),
	DEFINE_FIELD(CCZTorchFx, m_hSndLock, FIELD_EHANDLE),
};
IMPLEMENT_SAVERESTORE(CCZTorchFx, CBaseEntity)

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

	// the flame itself is drawn by the client (CZ_TorchFlame) so it does not lag behind the view
	if (false && !flame)
	{
		flame = CSprite::SpriteCreate("sprites/glow01.spr", tip, FALSE);
		if (flame)
		{
			flame->SetTransparency(kRenderTransAdd, 255, 150, 50, 220, kRenderFxNone);
			m_hFlame = flame;
		}
	}

	if (false && flame)
	{
		flame->pev->effects &= ~EF_NODRAW;
		flame->pev->scale = cz_torch_scale.value * (attack ? 1.5f : 1.0f);
		UTIL_SetOrigin(flame->pev, tip);
	}

	if (!attack)
	{
		if (m_wasAttack)
		{
			CBaseEntity *ls = m_hSndLock;
			if (ls)
				STOP_SOUND(ls->edict(), CHAN_WEAPON, "weapons/blowtorch-1.wav");
		}
		m_wasAttack = false;
		return;
	}

	if (!m_wasAttack)
	{
		m_wasAttack = true;
		Cz_WeapAnim((CBasePlayer *)pl, 1);
		CBaseEntity *l0 = Cz_AimedLock(pl);
		if (l0)
		{
			EMIT_SOUND(l0->edict(), CHAN_WEAPON, "weapons/blowtorch-1.wav", VOL_NORM, ATTN_NORM);
			m_hSndLock = l0;
		}
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

}

// ---------------------------------------------------------------------------
// radio controlled bomb: planted in a marked zone with the left button, detonated with the next press
// ---------------------------------------------------------------------------
static EHANDLE g_hRcPlanted;
static float g_flRcPlantTime = 0;
// viewmodel sequences of v_rcontrolbomb (taken from the original demo): 0 idle, 2 draw, 6 plant press, 3 bomb placed,
// 1 after plant, 7 detonate press, 5 explosion, 2 draw again
static int   g_rcState = 0;      // 0 none, 1 planting, 2 planted, 3 detonating, 4 finished
static float g_rcDue = 0;
static float g_rcStep2 = 0;
static Vector g_rcPos, g_rcAng;
static void Cz_RcAnim(CBasePlayer *pl, int a) { Cz_WeapAnim(pl, a); }

// ---------------------------------------------------------------------------
// persistent copy of the tool state (the globals above are not part of a save game)
// ---------------------------------------------------------------------------
static bool g_czStatePending = false;

class CCZState: public CBaseEntity
{
public:
	virtual int Save(CSave &save);
	virtual int Restore(CRestore &restore);
	static TYPEDESCRIPTION m_SaveData[];
	virtual void Spawn()
	{
		pev->solid = SOLID_NOT;
		pev->movetype = MOVETYPE_NONE;
		pev->effects = EF_NODRAW;
		SetThink(&CCZState::StateThink);
		pev->nextthink = gpGlobals->time + 0.3f;
	}
	virtual void Precache()
	{
		// called after a save game was loaded: the globals take the saved values over
			g_czTools = m_tools;
			g_rcCharges = m_charges;
			g_rcHold = m_hold;
			g_rcState = m_rcState;
		g_rcDue = m_rcDue;
		g_rcStep2 = m_rcStep2;
		g_rcPos = m_rcPos;
		g_rcAng = m_rcAng;
		g_hRcPlanted = m_hPlanted;
		g_flRcPlantTime = m_plantTime;
		Q_strncpy(g_czLastMap, STRING(gpGlobals->mapname), sizeof(g_czLastMap) - 1);
		g_czStatePending = true;
		m_bLoaded = true;
		pev->nextthink = gpGlobals->time + 0.3f;
	}
	virtual int ObjectCaps() { return (CBaseEntity::ObjectCaps() & ~FCAP_ACROSS_TRANSITION); }
	void EXPORT StateThink()
	{
			pev->nextthink = gpGlobals->time + 0.05f;
			CBasePlayer *pl = Cz_Player();
			Cz_RunQueue(pl);
			if (m_bLoaded)
		{
			if (!pl)
				return;
			m_bLoaded = false;
			g_czStatePending = false;
			g_czLastA = g_czLastC = -1;
			if ((g_czTools & CZ_ICON_RCBOMB) && !UTIL_FindEntityByClassname(nullptr, "cz_rc_ctl"))
				CBaseEntity::Create("cz_rc_ctl", pl->pev->origin, g_vecZero, nullptr);
				Cz_SendIcons(true);
				Cz_SendTool();
				// the view / body state of a game that was saved in the middle of something
				g_fiberOn = false;
				g_czSaveFix = true;
				MESSAGE_BEGIN(MSG_ONE, gmsgCzFiber, nullptr, pl->pev);
					WRITE_BYTE(0);
					WRITE_BYTE(0);
				MESSAGE_END();
				if (pl->pev->movetype == MOVETYPE_NONE && pl->IsAlive())
					pl->pev->movetype = MOVETYPE_WALK;
				SET_VIEW(pl->edict(), pl->edict());
				{
					const char *vm = nullptr;
					if (g_czTools & CZ_ICON_RADIO) vm = "models/v_radio.mdl";
					else if (g_czTools & CZ_ICON_RCBOMB) vm = "models/v_rcontrolbomb.mdl";
					else if (g_czTools & CZ_ICON_TORCH) vm = "models/v_btorch.mdl";
					else if (g_czTools & CZ_ICON_FIBER) vm = "models/v_focamera.mdl";
					else if (g_czTools & CZ_ICON_CAMERA) vm = "models/v_camera.mdl";
					if (vm)
					{
						pl->pev->viewmodel = MAKE_STRING(vm);
						Cz_WeapAnim(pl, ((g_czTools & CZ_ICON_RCBOMB) && g_rcHold) ? 1 : 0);
					}
				}
				return;
			}
			m_charges = g_rcCharges;
			m_hold = g_rcHold;
			m_tools = g_czTools;
		m_rcState = g_rcState;
		m_rcDue = g_rcDue;
		m_rcStep2 = g_rcStep2;
		m_rcPos = g_rcPos;
		m_rcAng = g_rcAng;
		m_hPlanted = g_hRcPlanted;
		m_plantTime = g_flRcPlantTime;
	}

	int m_tools = 0;
	int m_charges = 5;
	int m_hold = 0;
	int m_rcState = 0;
	float m_rcDue = 0, m_rcStep2 = 0, m_plantTime = 0;
	Vector m_rcPos, m_rcAng;
	EHANDLE m_hPlanted;
	bool m_bLoaded = false;
};
TYPEDESCRIPTION CCZState::m_SaveData[] =
{
	DEFINE_FIELD(CCZState, m_tools, FIELD_INTEGER),
	DEFINE_FIELD(CCZState, m_charges, FIELD_INTEGER),
	DEFINE_FIELD(CCZState, m_hold, FIELD_INTEGER),
	DEFINE_FIELD(CCZState, m_rcState, FIELD_INTEGER),
	DEFINE_FIELD(CCZState, m_rcDue, FIELD_TIME),
	DEFINE_FIELD(CCZState, m_rcStep2, FIELD_TIME),
	DEFINE_FIELD(CCZState, m_plantTime, FIELD_TIME),
	DEFINE_FIELD(CCZState, m_rcPos, FIELD_VECTOR),
	DEFINE_FIELD(CCZState, m_rcAng, FIELD_VECTOR),
	DEFINE_FIELD(CCZState, m_hPlanted, FIELD_EHANDLE),
};
IMPLEMENT_SAVERESTORE(CCZState, CBaseEntity)
LINK_ENTITY_TO_CLASS(cz_state, CCZState, CCSCzState)

static void Cz_EnsureState()
{
	if (!UTIL_FindEntityByClassname(nullptr, "cz_state"))
		CBaseEntity::Create("cz_state", g_vecZero, g_vecZero, nullptr);
}
static void Cz_EnsureStateFwd() { Cz_EnsureState(); }

class CCZRcPlanted: public CBaseAnimating
{
public:
	virtual void Precache() { PRECACHE_MODEL("models/w_rcontrolbomb_bomb.mdl"); }
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
	virtual int Save(CSave &save);
	virtual int Restore(CRestore &restore);
	static TYPEDESCRIPTION m_SaveData[];
	virtual void Spawn()
	{
		pev->solid = SOLID_NOT;
		pev->movetype = MOVETYPE_NONE;
		pev->effects = EF_NODRAW;
		m_prev = true;
		m_prev2 = true;
		m_tEnd = 0;
		SetThink(&CCZRcCtl::CtlThink);
		pev->nextthink = gpGlobals->time + 0.05f;
	}
	virtual int ObjectCaps() { return (CBaseEntity::ObjectCaps() & ~FCAP_ACROSS_TRANSITION); }
	void EXPORT CtlThink();
	bool m_prev;
	bool m_prev2;
	float m_tEnd;
};

TYPEDESCRIPTION CCZRcCtl::m_SaveData[] =
{
	DEFINE_FIELD(CCZRcCtl, m_prev, FIELD_CHARACTER),
	DEFINE_FIELD(CCZRcCtl, m_prev2, FIELD_CHARACTER),
	DEFINE_FIELD(CCZRcCtl, m_tEnd, FIELD_TIME),
};
IMPLEMENT_SAVERESTORE(CCZRcCtl, CBaseEntity)
LINK_ENTITY_TO_CLASS(cz_rc_ctl, CCZRcCtl, CCSRcCtl)

void CCZRcCtl::CtlThink()
{
	pev->nextthink = gpGlobals->time + 0.02f;

	if (g_czStatePending)
		return;
	CBasePlayer *pl = Cz_Player();
	if (!pl || !(g_czTools & CZ_ICON_RCBOMB))
	{
		UTIL_Remove(this);
		return;
	}

	bool atk = (pl->pev->button & IN_ATTACK) != 0;
	bool edge = atk && !m_prev;
	m_prev = atk;
	bool atk2 = (pl->pev->button & IN_ATTACK2) != 0;
	bool edge2 = atk2 && !m_prev2;
	m_prev2 = atk2;
	float now = gpGlobals->time;

	if (g_rcState == 1)
	{
		// bomb appears 1.07 s after the press
		if (now >= g_rcDue)
		{
			CBaseEntity *old = g_hRcPlanted;
			if (old)
				UTIL_Remove(old);
			g_hRcPlanted = CBaseEntity::Create("cz_rc_planted", g_rcPos, g_rcAng, nullptr);
			g_flRcPlantTime = now;
			if (g_rcCharges > 0)
				g_rcCharges--;
			g_rcHold = 1; // the detonator comes out
			Cz_RcAnim(pl, 3);
			Cz_QueueAnim(0, 1, 1.33f);
			g_rcBusy = now + 1.33f;
			g_rcState = 2;
			Cz_SendTool();
			ALERT(at_aiconsole, "rc bomb planted at %.0f %.0f %.0f (left %d)\n", g_rcPos.x, g_rcPos.y, g_rcPos.z, g_rcCharges);
		}
		return;
	}

	if (g_rcState == 3)
	{
		if (now < g_rcDue)
			return;

		CBaseEntity *bomb = g_hRcPlanted;
		Vector pos = bomb ? bomb->pev->origin : pl->pev->origin;
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
		UTIL_ScreenShake(pos, 8.0f, 40.0f, 2.0f, 700.0f);
		if (bomb)
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

		if (bomb)
			UTIL_Remove(bomb);
		g_hRcPlanted = nullptr;
		g_rcState = 0;
		// the next mine comes out (if any); the tool stays until the player leaves through the door
		g_rcHold = (g_rcCharges > 0) ? 0 : 1;
		Cz_QueueAnim(0, 2, 1.1f);
		Cz_QueueAnim(1, 0, 2.1f);
		Cz_SendTool();
		return;
	}

	// idle (nothing planted / planted): buttons
	if (g_rcState == 0 || g_rcState == 2)
	{
		if (now < g_rcBusy)
			return;
		if (edge2)
		{
			if (g_rcHold == 0)
			{
				// mine -> detonator: put the mine away (4), take the detonator (3), idle (1)
				g_rcHold = 1;
				Cz_RcAnim(pl, 4);
				Cz_QueueAnim(0, 3, 0.7f);
				Cz_QueueAnim(1, 1, 1.7f);
				g_rcBusy = now + 1.7f;
				Cz_SendTool();
			}
			else if (g_rcCharges > 0)
			{
				// detonator -> mine: put the detonator away (5), take a mine (2), idle (0)
				g_rcHold = 0;
				Cz_RcAnim(pl, 5);
				Cz_QueueAnim(0, 2, 0.7f);
				Cz_QueueAnim(1, 0, 1.7f);
				g_rcBusy = now + 1.7f;
				Cz_SendTool();
			}
		}
		else if (edge && g_rcHold == 1 && g_rcState == 2 && g_hRcPlanted && now >= g_flRcPlantTime + 0.5f)
		{
			Cz_RcAnim(pl, 7);
			g_rcDue = now + 1.63f;
			Cz_QueueAnim(0, 5, 1.5f);
			g_rcState = 3;
		}
	}
}

// the bomb tool goes into the hands; everything else is taken away
bool Cz_GiveTool(CBasePlayer *pl, const char *name)
{
	if (name && !Q_strcmp(name, "weapon_radio"))
	{
		Cz_ToolsNewMap();
		pl->RemoveAllItems(FALSE);
		g_czTools = CZ_ICON_RADIO;
		g_czRadioDone = false;
		g_rcState = 0;
		g_rcHold = 0;
		Q_memset(g_tQ, 0, sizeof(g_tQ));
		int len = 0;
		byte *f = LOAD_FILE_FOR_ME("models/v_radio.mdl", &len);
		if (f)
		{
			FREE_FILE(f);
			pl->pev->viewmodel = MAKE_STRING("models/v_radio.mdl");
			Cz_WeapAnim(pl, 1); // draw
			Cz_QueueAnim(0, 0, 1.0f);
		}
		else
			ALERT(at_console, "tool: models/v_radio.mdl not found\n");
		EMIT_SOUND(ENT(pl->pev), CHAN_ITEM, "items/gunpickup2.wav", VOL_NORM, ATTN_NORM);
		ALERT(at_aiconsole, "tool given: weapon_radio\n");
		Cz_SendIcons(true);
		Cz_SendTool();
		return true;
	}

	if (!name || Q_strcmp(name, "weapon_radiocontrolledbomb"))
		return false;

	Cz_ToolsNewMap();
	pl->RemoveAllItems(FALSE);
	g_czTools = CZ_ICON_RCBOMB;
	g_hRcPlanted = nullptr;
	g_rcState = 0;
	g_rcCharges = 5;
	g_rcHold = 0;
	Q_memset(g_tQ, 0, sizeof(g_tQ));

	{
		int len = 0;
		byte *f = LOAD_FILE_FOR_ME("models/v_rcontrolbomb.mdl", &len);
		if (f)
		{
			FREE_FILE(f);
			pl->pev->viewmodel = MAKE_STRING("models/v_rcontrolbomb.mdl");
			Cz_WeapAnim((CBasePlayer *)pl, 2);
		}
		else
			ALERT(at_console, "tool: models/v_rcontrolbomb.mdl not found\n");
	}

	if (!UTIL_FindEntityByClassname(nullptr, "cz_rc_ctl"))
		CBaseEntity::Create("cz_rc_ctl", pl->pev->origin, g_vecZero, nullptr);

	EMIT_SOUND(ENT(pl->pev), CHAN_ITEM, "items/gunpickup2.wav", VOL_NORM, ATTN_NORM);
	ALERT(at_aiconsole, "tool given: weapon_radiocontrolledbomb\n");
	Cz_SendIcons(true);
	Cz_SendTool();
	return true;
}

// called from ServerActivate: a level without a saved cz_state (new map, or an older save) starts without tools
void Cz_ServerActivate()
{
	if (UTIL_FindEntityByClassname(nullptr, "cz_state"))
		return;
	g_czTools = 0;
	g_rcState = 0;
	g_rcCharges = 5;
	g_rcHold = 0;
	g_rcBusy = 0;
	g_fiberOn = false;
	g_czStatePending = false;
	g_hRcPlanted = nullptr;
	Q_memset(g_czZoneCnt, 0, sizeof(g_czZoneCnt));
	Q_memset(g_tQ, 0, sizeof(g_tQ));
	g_czLastA = g_czLastC = -1;
	Q_strncpy(g_czLastMap, STRING(gpGlobals->mapname), sizeof(g_czLastMap) - 1);
	g_czLastMap[sizeof(g_czLastMap) - 1] = '\0';
}

// player_weaponstrip ("take_gadgets") takes the mission tools away as well
void Cz_StripTools()
{
	CBasePlayer *pl = Cz_Player();
	if (!g_czTools && !g_fiberOn)
		return;
	ALERT(at_aiconsole, "tools taken away (mask %d)\n", g_czTools);
	g_czTools = 0;
	g_rcState = 0;
	g_rcCharges = 5;
	g_rcHold = 0;
	// forget the tool zones the player was standing in (they may have been removed meanwhile); the rescue zone (bit 10) stays
	for (int i = 0; i < 16; i++)
		if (i != 10)
			g_czZoneCnt[i] = 0;
	Q_memset(g_tQ, 0, sizeof(g_tQ));
	if (pl)
		pl->pev->viewmodel = 0;
	Cz_SendIcons(true);
	Cz_SendTool();
}

// ---------------------------------------------------------------------------
// tool pickups
// ---------------------------------------------------------------------------
class CCZToolPickup: public CBaseAnimating
{
public:
	virtual int Save(CSave &save);
	virtual int Restore(CRestore &restore);
	static TYPEDESCRIPTION m_SaveData[];
	virtual void Spawn();
	virtual void Precache();
	virtual int ObjectCaps() { return ((CBaseAnimating::ObjectCaps() & ~FCAP_ACROSS_TRANSITION) | FCAP_MUST_SPAWN); }
	void EXPORT PickupTouch(CBaseEntity *pOther);
	void EXPORT PickupThink();
	void Pickup(CBaseEntity *pOther);

	int m_bit;
	const char *m_model;
	const char *m_vmodel;
};

TYPEDESCRIPTION CCZToolPickup::m_SaveData[] =
{
	DEFINE_FIELD(CCZToolPickup, m_bit, FIELD_INTEGER),
};
IMPLEMENT_SAVERESTORE(CCZToolPickup, CBaseAnimating)

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
	PRECACHE_SOUND("weapons/blowtorch-1.wav");
	PRECACHE_SOUND("weapons/dryfire1.wav");
	PRECACHE_SOUND("items/flashlight1.wav");
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
			Cz_WeapAnim((CBasePlayer *)pOther, 0);
		}
	}
	EMIT_SOUND(ENT(pOther->pev), CHAN_ITEM, "items/gunpickup2.wav", VOL_NORM, ATTN_NORM);
	ALERT(at_aiconsole, "tool picked up: %s (mask %d)\n", STRING(pev->classname), g_czTools);
	Cz_SendIcons(true);
	Cz_SendTool();
	// pickup / draw animations of the original: camera 2, torch 2, fiber optic camera 1
	Cz_WeapAnim((CBasePlayer *)pOther, (m_bit == CZ_ICON_FIBER) ? 1 : 2);
	Cz_QueueAnim(0, 0, 1.0f);

	SetTouch(nullptr);
	UTIL_Remove(this);
}

// ---------------------------------------------------------------------------
// info_camera: fiber optic camera viewpoint
// ---------------------------------------------------------------------------
class CCZInfoCamera: public CBaseAnimating
{
public:
	virtual int Save(CSave &save);
	virtual int Restore(CRestore &restore);
	static TYPEDESCRIPTION m_SaveData[];
	virtual void Precache() { PRECACHE_MODEL("models/rpgrocket.mdl"); }
	virtual void Spawn();
	virtual int ObjectCaps() { return (CBaseAnimating::ObjectCaps() & ~FCAP_ACROSS_TRANSITION); }
	virtual void Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value);
	void EXPORT EndView();

	EHANDLE m_hViewer;
	int m_zoom = 0;
	Vector m_viewAng;
	Vector m_baseAng;
	int m_moveType = 0;
	float m_tStart = 0;
	bool m_prev1 = true, m_prev2 = true;
};

TYPEDESCRIPTION CCZInfoCamera::m_SaveData[] =
{
	DEFINE_FIELD(CCZInfoCamera, m_hViewer, FIELD_EHANDLE),
	DEFINE_FIELD(CCZInfoCamera, m_zoom, FIELD_INTEGER),
	DEFINE_FIELD(CCZInfoCamera, m_viewAng, FIELD_VECTOR),
	DEFINE_FIELD(CCZInfoCamera, m_baseAng, FIELD_VECTOR),
	DEFINE_FIELD(CCZInfoCamera, m_moveType, FIELD_INTEGER),
	DEFINE_FIELD(CCZInfoCamera, m_tStart, FIELD_TIME),
};
IMPLEMENT_SAVERESTORE(CCZInfoCamera, CBaseAnimating)

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

static void Cz_SetFov(CBasePlayer *pl, int fov)
{
	pl->m_iFOV = fov;
	pl->pev->fov = (float)fov;
	MESSAGE_BEGIN(MSG_ONE, gmsgSetFOV, nullptr, pl->pev);
		WRITE_BYTE(fov);
	MESSAGE_END();
}

void CCZInfoCamera::Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value)
{
	CBasePlayer *pl = Cz_Player();
	if (!pl || !pl->IsAlive() || g_fiberOn || !(g_czTools & CZ_ICON_FIBER))
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

	m_baseAng = pev->angles;
	m_hViewer = pl;
	g_fiberOn = true;
	m_zoom = 0;
	m_tStart = gpGlobals->time;
	m_viewAng = pl->pev->v_angle;
	SET_VIEW(pl->edict(), edict());
	m_moveType = pl->pev->movetype;
	pl->pev->movetype = MOVETYPE_NONE;
	pl->pev->velocity = g_vecZero;
	Cz_SetFov(pl, 120);

	MESSAGE_BEGIN(MSG_ONE, gmsgCzFiber, nullptr, pl->pev);
		WRITE_BYTE(1);
		WRITE_BYTE(0);
	MESSAGE_END();

	Cz_WeapAnim(pl, 2);
	Cz_QueueAnim(0, 3, 0.6f);
	EMIT_SOUND(ENT(pl->pev), CHAN_ITEM, "items/flashlight1.wav", 0.8f, ATTN_NORM);

	ALERT(at_aiconsole, "info_camera '%s': view on\n", STRING(pev->targetname));

	SetThink(&CCZInfoCamera::EndView);
	pev->nextthink = gpGlobals->time + 0.02f;
}

// while viewing: the view turns within limits, right button = zoom, left button = leave
void CCZInfoCamera::EndView()
{
	CBasePlayer *pl = Cz_Player();
	if (!pl || !pl->IsAlive())
	{
		g_fiberOn = false;
		m_hViewer = nullptr;
		SetThink(nullptr);
		return;
	}

	pev->nextthink = gpGlobals->time + 0.02f;

	bool a1 = (pl->pev->button & IN_ATTACK) != 0;
	bool a2 = (pl->pev->button & IN_ATTACK2) != 0;
	pl->pev->velocity = g_vecZero;

	// the view can be turned within limited angles around the direction to the target
	{
		static bool reg = false;
		static cvar_t cvYaw = { "cz_fiber_yaw", "35", 0, 35.0f, nullptr };
		static cvar_t cvPitch = { "cz_fiber_pitch", "25", 0, 25.0f, nullptr };
		if (!reg)
		{
			reg = true;
			CVAR_REGISTER(&cvYaw);
			CVAR_REGISTER(&cvPitch);
		}
		float ly = cvYaw.value > 0 ? cvYaw.value : 35.0f;
		float lp = cvPitch.value > 0 ? cvPitch.value : 25.0f;
		float dy = UTIL_AngleDiff(pl->pev->v_angle.y, m_viewAng.y);
		float dp = UTIL_AngleDiff(pl->pev->v_angle.x, m_viewAng.x);
		float cy = dy < -ly ? -ly : (dy > ly ? ly : dy);
		float cp = dp < -lp ? -lp : (dp > lp ? lp : dp);
		pev->angles.y = UTIL_AngleMod(m_baseAng.y + cy);
		pev->angles.x = m_baseAng.x + cp;
		pev->angles.z = 0;
		if (cy != dy || cp != dp)
		{
			pl->pev->v_angle.y = UTIL_AngleMod(m_viewAng.y + cy);
			pl->pev->v_angle.x = m_viewAng.x + cp;
			pl->pev->angles = pl->pev->v_angle;
			pl->pev->fixangle = 1;
		}
	}

	// ignore the press that started the view
	if (gpGlobals->time < m_tStart + 0.4f)
	{
		m_prev1 = a1;
		m_prev2 = a2;
		return;
	}

	if (a2 && !m_prev2)
	{
		m_zoom = !m_zoom;
		Cz_SetFov(pl, m_zoom ? 60 : 120);
		EMIT_SOUND(ENT(pl->pev), CHAN_ITEM, "items/flashlight1.wav", 0.8f, ATTN_NORM);
		MESSAGE_BEGIN(MSG_ONE, gmsgCzFiber, nullptr, pl->pev);
			WRITE_BYTE(1);
			WRITE_BYTE(m_zoom);
		MESSAGE_END();
	}

	bool leave = (a1 && !m_prev1);
	m_prev1 = a1;
	m_prev2 = a2;

	if (!leave)
		return;

	SET_VIEW(pl->edict(), pl->edict());
	pl->pev->v_angle = m_viewAng;
	pl->pev->angles = m_viewAng;
	pl->pev->fixangle = 1;
	pl->pev->movetype = m_moveType ? m_moveType : MOVETYPE_WALK;
	Cz_SetFov(pl, 90);
	MESSAGE_BEGIN(MSG_ONE, gmsgCzFiber, nullptr, pl->pev);
		WRITE_BYTE(0);
		WRITE_BYTE(0);
	MESSAGE_END();
	Cz_WeapAnim(pl, 1);
	Cz_QueueAnim(0, 0, 0.6f);
	EMIT_SOUND(ENT(pl->pev), CHAN_ITEM, "items/flashlight1.wav", 0.8f, ATTN_NORM);

	g_fiberOn = false;
	g_fiberExitT = gpGlobals->time;
	m_hViewer = nullptr;
	SetThink(nullptr);
}

// ---------------------------------------------------------------------------
// trigger_usetool / trigger_stealth
// ---------------------------------------------------------------------------
class CCZUseTool: public CBaseTrigger
{
public:
	virtual int Save(CSave &save);
	virtual int Restore(CRestore &restore);
	static TYPEDESCRIPTION m_SaveData[];
	virtual void Precache();
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
	float m_acc = 0;
	float m_nextUse;
	bool m_inside;
	bool m_done;
	bool m_stealth;
	bool m_rcMode;
	bool m_prevAtk;
	int m_showIcon;
};

TYPEDESCRIPTION CCZUseTool::m_SaveData[] =
{
	DEFINE_FIELD(CCZUseTool, m_toolName, FIELD_STRING),
	DEFINE_FIELD(CCZUseTool, m_toolTarget, FIELD_STRING),
	DEFINE_FIELD(CCZUseTool, m_rcBombTarget, FIELD_STRING),
	DEFINE_FIELD(CCZUseTool, m_toolSet, FIELD_INTEGER),
	DEFINE_FIELD(CCZUseTool, m_defuseTime, FIELD_FLOAT),
	DEFINE_FIELD(CCZUseTool, m_need, FIELD_INTEGER),
	DEFINE_FIELD(CCZUseTool, m_iconBits, FIELD_INTEGER),
	DEFINE_FIELD(CCZUseTool, m_holdTime, FIELD_FLOAT),
	DEFINE_FIELD(CCZUseTool, m_holdStart, FIELD_TIME),
	DEFINE_FIELD(CCZUseTool, m_acc, FIELD_FLOAT),
	DEFINE_FIELD(CCZUseTool, m_nextUse, FIELD_TIME),
	DEFINE_FIELD(CCZUseTool, m_inside, FIELD_CHARACTER),
	DEFINE_FIELD(CCZUseTool, m_done, FIELD_CHARACTER),
	DEFINE_FIELD(CCZUseTool, m_stealth, FIELD_CHARACTER),
	DEFINE_FIELD(CCZUseTool, m_rcMode, FIELD_CHARACTER),
	DEFINE_FIELD(CCZUseTool, m_prevAtk, FIELD_CHARACTER),
	DEFINE_FIELD(CCZUseTool, m_showIcon, FIELD_INTEGER),
};
IMPLEMENT_SAVERESTORE(CCZUseTool, CBaseTrigger)

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

void CCZUseTool::Precache()
{
	if (m_rcMode)
	{
		PRECACHE_MODEL("models/w_rcontrolbomb_bomb.mdl");
		PRECACHE_MODEL("models/v_rcontrolbomb.mdl");
		PRECACHE_SOUND("weapons/c4_explode1.wav");
		PRECACHE_SOUND("items/gunpickup2.wav");
	}
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
			m_holdTime = 1.5f;
		}
		else if (!Q_strcmp(tn, "weapon_fiberopticcamera"))
			m_need = CZ_ICON_FIBER;
		else if (!Q_strcmp(tn, "bomb_defuse"))
		{
			m_need = 0;
			m_iconBits = CZ_ICON_DEFUSE;
			m_holdTime = m_defuseTime > 0 ? m_defuseTime : 5.0f;
		}

		if ((m_toolSet & CZ_ICON_RADIO) && !(m_toolSet & CZ_ICON_RCBOMB))
		{
			m_need = CZ_ICON_RADIO;
			// the radio is given at run time by game_player_equip: its view model must be loaded with the map
			PRECACHE_MODEL("models/v_radio.mdl");
			PRECACHE_MODEL("models/w_radio.mdl");
			PRECACHE_SOUND("items/gunpickup2.wav");
			PRECACHE_SOUND("weapons/dryfire1.wav");
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

	if (m_defuseTime > 0)
	{
		// the bomb model stays; only the zone and its HUD icon go away
		m_done = true;
		if (m_inside)
		{
			Cz_ZoneLeave(m_iconBits);
			m_inside = false;
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
		if (g_rcState == 0)
		{
			g_rcPos = pos;
			g_rcAng = ang;
			g_rcDue = gpGlobals->time + 1.07f;
			g_rcState = 1;
			Cz_RcAnim(pl, 6);
		}
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
		{
			STOP_SOUND(lock->edict(), CHAN_WEAPON, "weapons/blowtorch-1.wav");
			UTIL_Remove(lock); // the padlock model itself
		}
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

	if (g_fiberOn || gpGlobals->time < g_fiberExitT + 0.6f)
		return;

	if (gpGlobals->time < m_nextUse)
		return;

	if (m_rcMode)
	{
		// plant with the left button (one press), only with a mine in the hands, inside the marked zone
		if (atkEdge && g_rcHold == 0 && g_rcCharges > 0 && g_rcState == 0 && gpGlobals->time >= g_rcBusy)
		{
			m_nextUse = gpGlobals->time + 1.0f;
			Complete(pl);
		}
		return;
	}

	if (m_need == CZ_ICON_RADIO)
	{
		// use the radio: one press of attack 1 inside the radio zone
		if (!atkEdge)
			return;
		EMIT_SOUND(ENT(pl->pev), CHAN_WEAPON, "weapons/dryfire1.wav", VOL_NORM, ATTN_NORM);
		Cz_WeapAnim((CBasePlayer *)pl, 3); // v_radio: 0 idle, 1 draw, 2 holster, 3 use
		Cz_QueueAnim(0, 0, 6.0f); // back to idle when the transmission ("Roger, on our way!") ends
		ALERT(at_console, "radio: used in zone '%s', firing '%s'\n", STRING(pev->targetname), STRING(pev->target));
		m_nextUse = gpGlobals->time + 2.0f;
		g_czRadioDone = true;
		Complete(pl);
		g_czLastA = g_czLastC = -1;
		Cz_SendIcons(true);
		return;
	}

	if (m_need == CZ_ICON_CAMERA || m_need == CZ_ICON_FIBER)
	{
		if (!atkEdge || (pl->pev->button & IN_ATTACK2))
			return;

		if (m_need == CZ_ICON_CAMERA)
		{
			// the photo must be taken of the marked spot
			CBaseEntity *tt = FStringNull(m_toolTarget) ? nullptr : UTIL_FindEntityByTargetname(nullptr, STRING(m_toolTarget));
			if (tt)
			{
				UTIL_MakeVectors(pl->pev->v_angle);
				Vector to = tt->pev->origin - (pl->pev->origin + pl->pev->view_ofs);
				float dist = to.Length();
				if (dist > 500.0f || DotProduct(gpGlobals->v_forward, to.Normalize()) < 0.8f)
					return;
			}
			EMIT_SOUND(ENT(pl->pev), CHAN_WEAPON, "weapons/dryfire1.wav", VOL_NORM, ATTN_NORM);
			Cz_WeapAnim((CBasePlayer *)pl, 1);
			Cz_QueueAnim(0, 0, 1.8f);
			m_nextUse = gpGlobals->time + 2.0f;
			// the picture is "transmitted" first: the targets (Picture received!) fire after a pause
			float savedDelay = m_flDelay;
			m_flDelay = 2.5f;
			Complete(pl);
			m_flDelay = savedDelay;
			return;
		}
		else
			m_nextUse = gpGlobals->time + 1.0f;

		Complete(pl);
		return;
	}

	if (m_need == CZ_ICON_TORCH)
	{
		// the blowtorch is used like a weapon: a few short presses on the lock (~1.5 s in total), no progress bar
		bool burn = (pl->pev->button & IN_ATTACK) != 0;
		if (burn && Cz_MapHasLocks() && !Cz_AimedLock(pl))
			burn = false;
		static bool s_rooted = false;
		if (!burn)
		{
			if (s_rooted)
			{
				s_rooted = false;
				pl->ResetMaxSpeed();
			}
			return;
		}
		// while the torch works on the lock the player cannot walk away
		s_rooted = true;
		pl->pev->maxspeed = 1.0f;
		pl->pev->velocity.x = pl->pev->velocity.y = 0;
		m_acc += 0.05f;
		if (m_acc >= 1.5f)
		{
			if (s_rooted)
			{
				s_rooted = false;
				pl->ResetMaxSpeed();
			}
			m_acc = 0;
			m_nextUse = gpGlobals->time + 1.5f;
			Complete(pl);
		}
		return;
	}

	// bomb defuse: hold +use
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
	virtual int Save(CSave &save);
	virtual int Restore(CRestore &restore);
	static TYPEDESCRIPTION m_SaveData[];
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

TYPEDESCRIPTION CCZMissionFailure::m_SaveData[] =
{
	DEFINE_FIELD(CCZMissionFailure, m_holdTime, FIELD_FLOAT),
	DEFINE_FIELD(CCZMissionFailure, m_duration, FIELD_FLOAT),
	DEFINE_FIELD(CCZMissionFailure, m_music, FIELD_STRING),
	DEFINE_FIELD(CCZMissionFailure, m_fr, FIELD_INTEGER),
	DEFINE_FIELD(CCZMissionFailure, m_fg, FIELD_INTEGER),
	DEFINE_FIELD(CCZMissionFailure, m_fb, FIELD_INTEGER),
	DEFINE_FIELD(CCZMissionFailure, m_fa, FIELD_INTEGER),
	DEFINE_FIELD(CCZMissionFailure, m_nextMap, FIELD_STRING),
};
IMPLEMENT_SAVERESTORE(CCZMissionFailure, CBaseDelay)

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
