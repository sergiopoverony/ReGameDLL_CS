#include "precompiled.h"

// CZ:DS: M72 LAW rocket launcher (weapon_laws). Single shot, the tube is discarded after firing.
// Animations of models/v_law.mdl: 0 idle, 1 draw, 2 shoot, 3 discard.
// The weapon id 31 is free in the CS weapon table (the original CZ uses its own numbering).

#define CZ_WEAPON_LAW 31

class CCzLawRocket: public CBaseEntity
{
public:
	virtual void Spawn();
	virtual void Precache();
	virtual int ObjectCaps() { return CBaseEntity::ObjectCaps() & ~FCAP_ACROSS_TRANSITION; }

	void EXPORT RocketTouch(CBaseEntity *pOther);
	void EXPORT RocketThink();
};

LINK_ENTITY_TO_CLASS(cz_law_rocket, CCzLawRocket, CCSLawRocket)

void CCzLawRocket::Precache()
{
	PRECACHE_MODEL("models/lawrocket.mdl");
	PRECACHE_SOUND("weapons/rocket1.wav");
}

void CCzLawRocket::Spawn()
{
	Precache();

	pev->movetype = MOVETYPE_FLY;
	pev->solid = SOLID_BBOX;
	pev->classname = MAKE_STRING("cz_law_rocket");

	SET_MODEL(ENT(pev), "models/lawrocket.mdl");
	UTIL_SetSize(pev, g_vecZero, g_vecZero);
	UTIL_SetOrigin(pev, pev->origin);

	SetTouch(&CCzLawRocket::RocketTouch);
	SetThink(&CCzLawRocket::RocketThink);
	pev->nextthink = gpGlobals->time + 0.1f;

	pev->dmg = 150.0f;
	EMIT_SOUND(ENT(pev), CHAN_VOICE, "weapons/rocket1.wav", 0.7f, ATTN_NORM);
}

void CCzLawRocket::RocketThink()
{
	// keep the model pointing along the flight path
	if (pev->velocity.Length() > 1.0f)
		pev->angles = UTIL_VecToAngles(pev->velocity);

	// smoke trail
	MESSAGE_BEGIN(MSG_PVS, SVC_TEMPENTITY, pev->origin);
		WRITE_BYTE(TE_SMOKE);
		WRITE_COORD(pev->origin.x);
		WRITE_COORD(pev->origin.y);
		WRITE_COORD(pev->origin.z);
		WRITE_SHORT(g_sModelIndexSmoke);
		WRITE_BYTE(6);
		WRITE_BYTE(20);
	MESSAGE_END();

	pev->nextthink = gpGlobals->time + 0.05f;
}

void CCzLawRocket::RocketTouch(CBaseEntity *pOther)
{
	STOP_SOUND(edict(), CHAN_VOICE, "weapons/rocket1.wav");

	SetTouch(nullptr);
	SetThink(nullptr);

	Vector origin = pev->origin;
	Vector angles = g_vecZero;
	edict_t *owner = pev->owner;

	// step back from the wall so the explosion is not buried in it
	origin = origin - pev->velocity.Normalize() * 8.0f;

	ExplosionCreate(origin, angles, owner, (int)pev->dmg, TRUE);
	UTIL_Remove(this);
}

class CLaws: public CBasePlayerWeapon
{
public:
	virtual void Spawn();
	virtual void Precache();
	virtual int GetItemInfo(ItemInfo *p);
	virtual BOOL Deploy();
	virtual int iItemSlot() { return C4_SLOT; }
	virtual float GetMaxSpeed() { return 240.0f; }
	virtual void PrimaryAttack();
	virtual void WeaponIdle();
	virtual BOOL UseDecrement() { return FALSE; }
	virtual int Save(CSave &save);
	virtual int Restore(CRestore &restore);
	static TYPEDESCRIPTION m_SaveData[];

	float m_flDiscardTime;
	float m_flRemoveTime;
};

LINK_ENTITY_TO_CLASS(weapon_laws, CLaws, CCSLaws)

TYPEDESCRIPTION CLaws::m_SaveData[] =
{
	DEFINE_FIELD(CLaws, m_flDiscardTime, FIELD_TIME),
	DEFINE_FIELD(CLaws, m_flRemoveTime, FIELD_TIME),
};

IMPLEMENT_SAVERESTORE(CLaws, CBasePlayerWeapon)

void CLaws::Spawn()
{
	Precache();

	m_iId = CZ_WEAPON_LAW;
	SET_MODEL(edict(), "models/w_law.mdl");

	m_iDefaultAmmo = 1;
	m_flDiscardTime = 0;
	m_flRemoveTime = 0;

	FallInit();
	CBasePlayerWeapon::Spawn();
}

void CLaws::Precache()
{
	PRECACHE_MODEL("models/v_law.mdl");
	PRECACHE_MODEL("models/w_law.mdl");
	PRECACHE_MODEL("models/p_m249.mdl");
	PRECACHE_SOUND("weapons/rocketfire1.wav");
	PRECACHE_SOUND("weapons/glauncher.wav");

	UTIL_PrecacheOther("cz_law_rocket");
}

int CLaws::GetItemInfo(ItemInfo *p)
{
	p->pszName = STRING(pev->classname);
	p->pszAmmo1 = "LAW";
	p->iMaxAmmo1 = 1;
	p->pszAmmo2 = nullptr;
	p->iMaxAmmo2 = -1;
	p->iMaxClip = WEAPON_NOCLIP;
	p->iSlot = 4;
	p->iPosition = 7;
	p->iId = m_iId = CZ_WEAPON_LAW;
	p->iFlags = ITEM_FLAG_LIMITINWORLD | ITEM_FLAG_EXHAUSTIBLE;
	p->iWeight = 5;
	return 1;
}

BOOL CLaws::Deploy()
{
	m_flDiscardTime = 0;
	m_flRemoveTime = 0;
	return DefaultDeploy("models/v_law.mdl", "models/p_m249.mdl", 1, "m249", UseDecrement() != FALSE);
}

void CLaws::PrimaryAttack()
{
	if (m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0)
	{
		PlayEmptySound();
		m_flNextPrimaryAttack = GetNextAttackDelay(0.5f);
		return;
	}

	m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]--;

	m_pPlayer->SetAnimation(PLAYER_ATTACK1);
	SendWeaponAnim(2, UseDecrement() != FALSE);
	EMIT_SOUND(ENT(m_pPlayer->pev), CHAN_WEAPON, "weapons/rocketfire1.wav", VOL_NORM, ATTN_NORM);

	UTIL_MakeVectors(m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle);
	Vector src = m_pPlayer->GetGunPosition() + gpGlobals->v_forward * 16.0f + gpGlobals->v_right * 6.0f - gpGlobals->v_up * 4.0f;

	CBaseEntity *pRocket = CBaseEntity::Create("cz_law_rocket", src, m_pPlayer->pev->v_angle, m_pPlayer->edict());
	if (pRocket)
	{
		pRocket->pev->velocity = gpGlobals->v_forward * 1500.0f + m_pPlayer->pev->velocity * 0.5f;
		pRocket->pev->angles = UTIL_VecToAngles(pRocket->pev->velocity);
	}

	m_pPlayer->pev->punchangle.x -= 8.0f;
	m_pPlayer->m_iWeaponVolume = LOUD_GUN_VOLUME;
	m_pPlayer->m_iWeaponFlash = BRIGHT_GUN_FLASH;

	m_flNextPrimaryAttack = m_flNextSecondaryAttack = GetNextAttackDelay(3.0f);
	m_flDiscardTime = gpGlobals->time + 1.2f;   // shoot anim is 34 frames at 30 fps
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 10.0f;
}

void CLaws::WeaponIdle()
{
	ResetEmptySound();

	if (m_flDiscardTime && gpGlobals->time >= m_flDiscardTime)
	{
		// throw the empty tube away (51 frames at 30 fps)
		m_flDiscardTime = 0;
		m_flRemoveTime = gpGlobals->time + 1.75f;
		SendWeaponAnim(3, UseDecrement() != FALSE);
		return;
	}

	if (m_flRemoveTime && gpGlobals->time >= m_flRemoveTime)
	{
		m_flRemoveTime = 0;
		RetireWeapon();   // switch to the next best weapon first
		DestroyItem();
		return;
	}

	if (m_flTimeWeaponIdle > UTIL_WeaponTimeBase())
		return;

	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 20.0f;
	if (m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] > 0)
		SendWeaponAnim(0, UseDecrement() != FALSE);
}
