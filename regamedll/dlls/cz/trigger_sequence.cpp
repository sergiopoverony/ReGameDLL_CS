#include "precompiled.h"
#include "cz_sequence.h"

// CZ:DS trigger_sequence.
// The original engine parses sequences/<map>.seq itself (pfnSequenceGet).
// Xash3D only has a stub for that, so the parser lives in the game library.

static czseq::Library g_seqLib;
static std::string g_seqLoadedMap;

static void Seq_LoadFile(const char *name)
{
	if (!name || !name[0])
		return;

	std::string base = name;
	if (base.size() > 4 && czseq::ToLower(base.substr(base.size() - 4)) == ".seq")
		base.erase(base.size() - 4);

	if (g_seqLib.IsFileLoaded(base))
		return;

	// shared styles (TitleStyle, RadioStyle, ...) live in sequences/global.seq
	if (czseq::ToLower(base) != "global")
		Seq_LoadFile("global");

	char path[260];
	Q_snprintf(path, sizeof(path), "sequences/%s.seq", base.c_str());

	int length = 0;
	byte *data = LOAD_FILE_FOR_ME(path, &length);
	if (!data)
	{
		ALERT(at_console, "trigger_sequence: can't open %s\n", path);
		return;
	}

	int count = g_seqLib.ParseText((const char *)data, (size_t)length, base);
	FREE_FILE(data);

	ALERT(at_aiconsole, "trigger_sequence: %s: %d entries\n", path, count);
}

static void Seq_EnsureMapLoaded()
{
	const char *map = STRING(gpGlobals->mapname);
	if (g_seqLoadedMap != map)
	{
		g_seqLib.Clear();
		g_seqLoadedMap = map;
	}
	Seq_LoadFile(map);
}

// "#KEY" strings come from resource/<game>_english.txt ("lang" { "Tokens" { "KEY" "text" } })
static std::map<std::string, std::string> g_locTable;
static bool g_locLoaded = false;

static void Loc_Load()
{
	g_locLoaded = true;

	const char *files[] = { "resource/czeror_english.txt", "resource/cstrike_english.txt" };
	for (const char *path : files)
	{
		int length = 0;
		byte *data = LOAD_FILE_FOR_ME((char *)path, &length);
		if (!data)
			continue;

		const char *p = (const char *)data;
		const char *end = p + length;
		std::vector<std::string> tokens;

		// tokenizer: "quoted strings" (may span lines), { }, // comments
		while (p < end)
		{
			if (*p == '/' && p + 1 < end && p[1] == '/')
			{
				while (p < end && *p != '\n') p++;
			}
			else if (*p == '"')
			{
				p++;
				std::string t;
				while (p < end && *p != '"')
				{
					if (*p != '\r') t += *p;
					p++;
				}
				p++;
				tokens.push_back(t);
			}
			else if (*p == '{' || *p == '}')
			{
				tokens.push_back(std::string(1, *p));
				p++;
			}
			else
				p++;
		}
		FREE_FILE(data);

		bool inTokens = false;
		for (size_t i = 0; i + 1 < tokens.size(); )
		{
			if (!inTokens)
			{
				if (czseq::ToLower(tokens[i]) == "tokens")
					inTokens = true;
				i++;
				continue;
			}

			if (tokens[i] == "{") { i++; continue; }
			if (tokens[i] == "}") break;

			g_locTable[czseq::ToLower(tokens[i])] = tokens[i + 1];
			i += 2;
		}
	}

	ALERT(at_aiconsole, "trigger_sequence: %d localized strings\n", (int)g_locTable.size());
}

static const char *Seq_Localize(const char *text)
{
	if (!text || text[0] != '#')
		return text;

	if (!g_locLoaded)
		Loc_Load();

	auto it = g_locTable.find(czseq::ToLower(text + 1));
	if (it != g_locTable.end())
		return it->second.c_str();

	return text;
}

const int MAX_SEQ_NAME = 256;

class CTriggerSequence: public CBaseDelay
{
public:
	virtual void Spawn();
	virtual void Precache();
	virtual void Restart();
	virtual void KeyValue(KeyValueData *pkvd);
	virtual int Save(CSave &save);
	virtual int Restore(CRestore &restore);
	virtual int ObjectCaps() { return (CBaseDelay::ObjectCaps() & ~FCAP_ACROSS_TRANSITION); }
	virtual void Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value);
	virtual void Think();

public:
	static TYPEDESCRIPTION m_SaveData[];

	void Resolve();
	void StopSequence();
	void ExecuteCommand(const czseq::Command &cmd);
	void ExecuteText(const czseq::Command &cmd);
	void ExecuteSound(const czseq::Command &cmd);

	char m_entryName[MAX_SEQ_NAME];
	char m_fileName[64];

	float m_startTime;
	int m_nextIndex;     // -1 = not running
	int m_useType;
	float m_value;
	EHANDLE m_activator;
	int m_isDisabled;

	const czseq::Entry *m_entry; // not saved, resolved again by Resolve()
};

#define SF_SEQUENCE_FIRE_ONCE BIT(0)

TYPEDESCRIPTION CTriggerSequence::m_SaveData[] =
{
	DEFINE_ARRAY(CTriggerSequence, m_entryName, FIELD_CHARACTER, MAX_SEQ_NAME),
	DEFINE_ARRAY(CTriggerSequence, m_fileName, FIELD_CHARACTER, 64),
	DEFINE_FIELD(CTriggerSequence, m_startTime, FIELD_TIME),
	DEFINE_FIELD(CTriggerSequence, m_nextIndex, FIELD_INTEGER),
	DEFINE_FIELD(CTriggerSequence, m_useType, FIELD_INTEGER),
	DEFINE_FIELD(CTriggerSequence, m_value, FIELD_FLOAT),
	DEFINE_FIELD(CTriggerSequence, m_activator, FIELD_EHANDLE),
	DEFINE_FIELD(CTriggerSequence, m_isDisabled, FIELD_INTEGER),
};

LINK_ENTITY_TO_CLASS(trigger_sequence, CTriggerSequence, CCSTriggerSequence)
IMPLEMENT_SAVERESTORE(CTriggerSequence, CBaseDelay)

void CTriggerSequence::KeyValue(KeyValueData *pkvd)
{
	// some maps have a trailing space in the key name ("sequence_id ")
	char key[64];
	Q_strncpy(key, pkvd->szKeyName, sizeof(key) - 1);
	key[sizeof(key) - 1] = '\0';
	for (int i = Q_strlen(key) - 1; i >= 0 && key[i] == ' '; i--)
		key[i] = '\0';

	if (FStrEq(key, "sequence_id"))
	{
		Q_strncpy(m_entryName, pkvd->szValue, sizeof(m_entryName) - 1);
		m_entryName[sizeof(m_entryName) - 1] = '\0';
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(key, "sequence_file"))
	{
		Q_strncpy(m_fileName, pkvd->szValue, sizeof(m_fileName) - 1);
		m_fileName[sizeof(m_fileName) - 1] = '\0';
		pkvd->fHandled = TRUE;
	}
	else
	{
		CBaseDelay::KeyValue(pkvd);
	}
}

void CTriggerSequence::Resolve()
{
	Seq_EnsureMapLoaded();
	Seq_LoadFile(m_fileName);

	m_entry = g_seqLib.Find(m_fileName, m_entryName);
}

void CTriggerSequence::Precache()
{
	if (!m_entry)
		Resolve(); // after a load m_entry is empty (not saved)

	if (!m_entry)
		return;

	for (size_t i = 0; i < m_entry->commands.size(); i++)
	{
		const czseq::Command &c = m_entry->commands[i];
		if (c.kind != czseq::CMD_SOUND)
			continue;

		const std::vector<std::string> *group = g_seqLib.GetGroup(c.value);
		if (group)
		{
			for (size_t j = 0; j < group->size(); j++)
			{
				std::string wav = (*group)[j] + ".wav";
				PRECACHE_SOUND((char *)wav.c_str());
			}
		}
		else
		{
			std::string wav = c.value + ".wav";
			PRECACHE_SOUND((char *)wav.c_str());
		}
	}
}

void CTriggerSequence::Spawn()
{
	m_nextIndex = -1;
	m_isDisabled = 0;
	Resolve();

	if (!m_entry)
		ALERT(at_console, "trigger_sequence: unknown sequence \"%s\" (file \"%s\")\n", m_entryName, m_fileName);

	Precache();
}

void CTriggerSequence::Restart()
{
	Resolve();
}

void CTriggerSequence::StopSequence()
{
	m_nextIndex = -1;
	m_activator = nullptr;
	m_useType = USE_OFF;
	m_value = 0;
	m_startTime = 0;

	if (pev)
		pev->nextthink = 0;
}

void CTriggerSequence::Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value)
{
	if (!m_entry)
		Resolve();

	if (!m_entry)
	{
		ALERT(at_console, "trigger_sequence: unknown sequence \"%s\"\n", m_entryName);
		return;
	}

	ALERT(at_console, "trigger_sequence '%s': start \"%s\" (%d commands)%s\n", STRING(pev->targetname), m_entryName,
		(int)m_entry->commands.size(), m_isDisabled ? " - DISABLED" : "");

	if (m_isDisabled)
		return;

	if (pev->spawnflags & SF_SEQUENCE_FIRE_ONCE)
		m_isDisabled = 1;

	StopSequence();

	if (m_entry->commands.empty())
	{
		ALERT(at_console, "trigger_sequence: sequence \"%s\" is empty\n", m_entryName);
		return;
	}

	m_value = value;
	m_useType = useType;
	m_activator = pActivator;
	m_startTime = gpGlobals->time;
	m_nextIndex = 0;

	Think();
}

void CTriggerSequence::Think()
{
	if (!m_entry && m_nextIndex >= 0)
		Resolve(); // m_entry is not saved: re-resolve after a load

	if (!m_entry || m_nextIndex < 0)
	{
		StopSequence();
		return;
	}

	float timeNext = gpGlobals->time;

	while (m_nextIndex >= 0 && m_nextIndex < (int)m_entry->commands.size())
	{
		const czseq::Command &cmd = m_entry->commands[m_nextIndex];
		timeNext += cmd.delay;

		ExecuteCommand(cmd);

		if (gpGlobals->time < timeNext)
			break;
	}

	if (m_nextIndex >= 0 && m_nextIndex < (int)m_entry->commands.size() && pev)
		pev->nextthink = timeNext;
	else
		StopSequence();
}

void CTriggerSequence::ExecuteCommand(const czseq::Command &cmd)
{
	CBaseEntity *pActivator = m_activator;

	switch (cmd.kind)
	{
	case czseq::CMD_FIRE:
		FireTargets(cmd.value.c_str(), pActivator, this, (USE_TYPE)m_useType, m_value);
		break;
	case czseq::CMD_KILL:
	{
		CBaseEntity *pEntity = nullptr;
		while ((pEntity = UTIL_FindEntityByTargetname(pEntity, cmd.value.c_str())) != nullptr)
			UTIL_Remove(pEntity);
		break;
	}
	case czseq::CMD_TEXT:
		ExecuteText(cmd);
		break;
	case czseq::CMD_SOUND:
		ExecuteSound(cmd);
		break;
	default:
		break;
	}

	if (cmd.kind == czseq::CMD_REPEAT && cmd.repeat != 0)
		m_nextIndex = 0;
	else
		m_nextIndex++;
}

void CTriggerSequence::ExecuteText(const czseq::Command &cmd)
{
	const char *msg = Seq_Localize(cmd.value.c_str());
	if (!msg || !msg[0])
		return;

	hudtextparms_t p;
	Q_memset(&p, 0, sizeof(p));
	p.x = cmd.mod.x;
	p.y = cmd.mod.y;
	p.effect = cmd.mod.effect;
	p.r1 = cmd.mod.c1[0]; p.g1 = cmd.mod.c1[1]; p.b1 = cmd.mod.c1[2]; p.a1 = cmd.mod.c1[3];
	p.r2 = cmd.mod.c2[0]; p.g2 = cmd.mod.c2[1]; p.b2 = cmd.mod.c2[2]; p.a2 = cmd.mod.c2[3];
	p.fadeinTime = cmd.mod.fadein;
	p.fadeoutTime = cmd.mod.fadeout;
	p.holdTime = cmd.mod.holdtime;
	p.fxTime = cmd.mod.fxtime;
	p.channel = cmd.mod.channel;

	if (p.holdTime < 0)
		p.holdTime = Q_strlen(msg) * 0.05f + 1.0f;

	UTIL_HudMessageAll(p, msg);
}

void Cz_NpcSpeak(CBaseEntity *pSpeaker, float seconds);

void CTriggerSequence::ExecuteSound(const czseq::Command &cmd)
{
	std::string path;
	const std::string *picked = g_seqLib.PickSentence(cmd.value);
	path = (picked ? *picked : cmd.value) + ".wav";

	CBaseEntity *pSpeaker = nullptr;
	if (!cmd.mod.speaker.empty() && czseq::ToLower(cmd.mod.speaker) != "none")
		pSpeaker = UTIL_FindEntityByTargetname(nullptr, cmd.mod.speaker.c_str());

	if (pSpeaker)
	{
		Cz_NpcSpeak(pSpeaker, 4.0f);
		CBaseEntity *pl = UTIL_PlayerByIndex(1);
		ALERT(at_console, "sequence sound '%s' from '%s' dist %.0f\n", path.c_str(), STRING(pSpeaker->pev->targetname),
			pl ? (pl->pev->origin - pSpeaker->pev->origin).Length() : -1.0f);
		// background chatter must stay audible through doors/walls: wide attenuation
		// explicit origin: the speaker may be outside the player's PVS (behind a door) and the client would not know where it is
		EMIT_AMBIENT_SOUND(ENT(pSpeaker->pev), pSpeaker->pev->origin + Vector(0, 0, 48), path.c_str(), VOL_NORM, 0.3f, 0, PITCH_NORM);
	}
	else
	{
		ALERT(at_console, "sequence sound '%s': no speaker '%s', played globally\n", path.c_str(), cmd.mod.speaker.c_str());
		EMIT_SOUND_DYN2(ENT(pev), CHAN_VOICE, path.c_str(), VOL_NORM, ATTN_NONE, 0, PITCH_NORM);
	}
}
