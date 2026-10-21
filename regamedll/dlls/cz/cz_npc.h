#pragma once

// shared declaration of the CZ:DS actor base (monster_npc, hostages, soldiers)

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
	bool PlaySeq(const char *name);
	void ApplySitOffset();
	void StartMove(const Vector &target, float yaw, bool run);
	void StopMove();
	bool MoveStep(float stopDist, bool checkWalls);
	void BuildPath(const Vector &goal);
	void StartFollow(CBaseEntity *leader);
	void StopFollow();
	void FollowStep();

	string_t m_idleAnim;
	string_t m_useTarget;
	string_t m_deathAnim;
	bool m_bDead = false;
	bool m_bDeadSwapped = false;
	int m_head;
	int m_spawned;
	bool m_bScripted; // driven by scripted_sequence
	bool m_bMoving;   // walking to m_vMoveTarget (scripted)
	bool m_bArrived;
	bool m_bFollow;   // follows the player after +use
	bool m_bRun;
	bool m_bSitting;
	float m_flSitApplied;
	float m_flMoveYaw;
	float m_flBiasD0 = 0;
	float m_flBiasCur = 0;
	int m_nWalks = 0;
	bool m_bGhost = false; // red-shirt worker: no collision of his own while walking
	float m_flYawBias = 0; // original quirk: a scripted walk starts ~35 deg to the left of the goal and straightens out
	float m_flLastMove;
	float m_flStuckSince;
	float m_flProgDist = 0, m_flProgTime = 0;
	float m_flFaceUntil = 0, m_flBaseYaw = 0;
	bool m_bBaseYawSet = false;
	bool m_bReplanned = false;
	bool m_bHoldEnd = false; // non-looping idle (crouch ...) stays on its last frame
	bool m_bNoSit = false;   // soldiers never get the chair offset
	// path_corner patrol (monster "target" -> path_corner chain)
	bool m_bPatrolInit = false, m_bPatrolOn = false, m_bPatrolHeading = false, m_bNoPatrol = false;
	EHANDLE m_hPatrol;
	float m_flPatrolNext = 0;
	float m_flGhostUntil = 0; // collision-free steps for a while after the hull got stuck
	bool m_bAllowGhost = false; // soldiers: fall back to collision-free steps when boxed in
	void PatrolStep();
	Vector m_path[16]; // waypoints (info_node path); the last one is the goal
	int m_nPath;
	int m_iPath;
	float m_flNextPath;
	Vector m_vMoveTarget;
	EHANDLE m_hLeader;
};
