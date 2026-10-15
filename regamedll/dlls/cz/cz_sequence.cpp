#include "cz_sequence.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

namespace czseq
{

std::string ToLower(const std::string &s)
{
	std::string r = s;
	for (size_t i = 0; i < r.size(); i++)
		r[i] = (char)tolower((unsigned char)r[i]);
	return r;
}

static std::string Trim(const std::string &s)
{
	size_t b = 0, e = s.size();
	while (b < e && isspace((unsigned char)s[b])) b++;
	while (e > b && isspace((unsigned char)s[e - 1])) e--;
	return s.substr(b, e - b);
}

static std::string Unquote(const std::string &s)
{
	std::string t = Trim(s);
	if (t.size() >= 2 && t[0] == '"' && t[t.size() - 1] == '"')
		return t.substr(1, t.size() - 2);
	return t;
}

// Remove "//" comments that are outside of quotes.
static std::string StripComment(const std::string &line, bool &inQuote)
{
	std::string out;
	for (size_t i = 0; i < line.size(); i++)
	{
		char c = line[i];
		if (c == '"')
			inQuote = !inQuote;
		if (!inQuote && c == '/' && i + 1 < line.size() && line[i + 1] == '/')
			break;
		out += c;
	}
	return out;
}

static std::string StripTrailingDigits(const std::string &s)
{
	size_t e = s.size();
	while (e > 0 && isdigit((unsigned char)s[e - 1])) e--;
	return s.substr(0, e);
}

// split "k = v, k2 = v2" (commas inside quotes are kept)
static void SplitPairs(const std::string &s, std::vector<std::pair<std::string, std::string>> &out)
{
	std::vector<std::string> parts;
	std::string cur;
	bool q = false;
	for (size_t i = 0; i < s.size(); i++)
	{
		char c = s[i];
		if (c == '"') q = !q;
		if (c == ',' && !q)
		{
			parts.push_back(cur);
			cur.clear();
		}
		else
			cur += c;
	}
	parts.push_back(cur);

	for (size_t i = 0; i < parts.size(); i++)
	{
		std::string p = parts[i];
		size_t eq = p.find('=');
		std::string k, v;
		if (eq == std::string::npos)
		{
			k = Trim(p);
		}
		else
		{
			k = Trim(p.substr(0, eq));
			v = Unquote(p.substr(eq + 1));
		}
		if (!k.empty())
			out.push_back(std::make_pair(k, v));
	}
}

void Library::Clear()
{
	m_raw.clear();
	m_built.clear();
	m_groups.clear();
	m_files.clear();
}

bool Library::IsFileLoaded(const std::string &fileName) const
{
	std::string f = ToLower(fileName);
	for (size_t i = 0; i < m_files.size(); i++)
		if (m_files[i] == f)
			return true;
	return false;
}

int Library::ParseText(const char *text, size_t length, const std::string &fileName)
{
	std::string file = ToLower(fileName);
	if (!IsFileLoaded(file))
		m_files.push_back(file);

	enum { TOP, WANT_BRACE_ENTRY, WANT_BRACE_SENT, IN_ENTRY, IN_SENT } state = TOP;
	int count = 0;

	RawEntry cur;
	bool inQuote = false;
	std::string pending; // line being assembled (for quotes spanning lines)

	size_t pos = 0;
	while (pos <= length)
	{
		size_t eol = pos;
		while (eol < length && text[eol] != '\n') eol++;
		std::string rawLine(text + pos, eol - pos);
		pos = eol + 1;

		if (!rawLine.empty() && rawLine[rawLine.size() - 1] == '\r')
			rawLine.erase(rawLine.size() - 1);

		bool wasInQuote = inQuote;
		std::string line = StripComment(rawLine, inQuote);

		if (wasInQuote || !pending.empty())
		{
			pending += "\n" + line;
			if (inQuote)
				continue; // still inside a multi-line string
			line = pending;
			pending.clear();
		}
		else if (inQuote)
		{
			pending = line;
			continue;
		}

		line = Trim(line);
		if (line.empty())
			continue;

		// Handle "{" or "}" possibly followed by more text.
		if (state == WANT_BRACE_ENTRY || state == WANT_BRACE_SENT)
		{
			if (line[0] == '{')
			{
				state = (state == WANT_BRACE_ENTRY) ? IN_ENTRY : IN_SENT;
				line = Trim(line.substr(1));
				if (line.empty())
					continue;
			}
			else
			{
				// malformed, drop back
				state = TOP;
			}
		}

		if (state == TOP)
		{
			if (line[0] == '%')
			{
				cur = RawEntry();
				cur.fileName = file;
				size_t e = 1;
				while (e < line.size() && !isspace((unsigned char)line[e]) && line[e] != '{') e++;
				cur.name = line.substr(1, e - 1);
				std::string rest = Trim(line.substr(e));
				state = WANT_BRACE_ENTRY;
				if (!rest.empty() && rest[0] == '{')
				{
					state = IN_ENTRY;
				}
			}
			else if (line[0] == '!')
			{
				state = WANT_BRACE_SENT; // block name is ignored, only "!sentences" exists
				if (line.find('{') != std::string::npos)
					state = IN_SENT;
			}
			continue;
		}

		if (line[0] == '}')
		{
			if (state == IN_ENTRY)
			{
				m_raw.push_back(cur);
				count++;
			}
			state = TOP;
			continue;
		}

		if (state == IN_SENT)
		{
			size_t sp = 0;
			while (sp < line.size() && !isspace((unsigned char)line[sp])) sp++;
			std::string name = line.substr(0, sp);
			std::string path = Trim(line.substr(sp));
			if (!name.empty() && !path.empty())
				m_groups[ToLower(StripTrailingDigits(name))].push_back(path);
			continue;
		}

		// IN_ENTRY
		Op op;
		op.type = line[0];
		if (op.type == '@')
		{
			op.key = Trim(line.substr(1));
			cur.ops.push_back(op);
		}
		else if (op.type == '$' || op.type == '#')
		{
			std::string body = line.substr(1);
			std::vector<std::pair<std::string, std::string>> pairs;
			SplitPairs(body, pairs);
			if (pairs.empty())
				continue;
			op.key = ToLower(pairs[0].first);
			for (size_t i = 0; i < pairs.size(); i++)
			{
				Pair p;
				p.key = ToLower(pairs[i].first);
				p.value = pairs[i].second;
				op.args.push_back(p);
			}
			cur.ops.push_back(op);
		}
	}

	m_built.clear();
	return count;
}

static void ParseFloats(const std::string &v, float *out, int n)
{
	const char *p = v.c_str();
	for (int i = 0; i < n; i++)
	{
		char *end;
		double d = strtod(p, &end);
		if (end == p)
			break;
		out[i] = (float)d;
		p = end;
	}
}

static void ParseColor(const std::string &v, unsigned char *out)
{
	float f[4] = { (float)out[0], (float)out[1], (float)out[2], (float)out[3] };
	ParseFloats(v, f, 4);
	for (int i = 0; i < 4; i++)
		out[i] = (unsigned char)(f[i] < 0 ? 0 : (f[i] > 255 ? 255 : f[i]));
}

void Library::ApplyModifier(Modifiers &m, const std::string &key, const std::string &value)
{
	if (key == "speaker") m.speaker = value;
	else if (key == "listener") m.listener = value;
	else if (key == "position")
	{
		float f[2] = { m.x, m.y };
		ParseFloats(value, f, 2);
		m.x = f[0]; m.y = f[1];
	}
	else if (key == "color") ParseColor(value, m.c1);
	else if (key == "color2") ParseColor(value, m.c2);
	else if (key == "effect") m.effect = atoi(value.c_str());
	else if (key == "fadein") m.fadein = (float)atof(value.c_str());
	else if (key == "fadeout") m.fadeout = (float)atof(value.c_str());
	else if (key == "holdtime") m.holdtime = (float)atof(value.c_str());
	else if (key == "fxtime") m.fxtime = (float)atof(value.c_str());
	else if (key == "channel") m.channel = atoi(value.c_str());
}

void Library::Emit(const RawEntry &raw, Modifiers &state, std::vector<Command> &out, int depth)
{
	for (size_t i = 0; i < raw.ops.size(); i++)
	{
		const Op &op = raw.ops[i];

		if (op.type == '@')
		{
			if (depth >= 8)
				continue;
			std::string want = ToLower(op.key);
			for (size_t j = 0; j < m_raw.size(); j++)
			{
				// prefer same file, fall back to any file
				if (ToLower(m_raw[j].name) == want && m_raw[j].fileName == raw.fileName)
				{
					Emit(m_raw[j], state, out, depth + 1);
					want.clear();
					break;
				}
			}
			if (!want.empty())
			{
				for (size_t j = 0; j < m_raw.size(); j++)
				{
					if (ToLower(m_raw[j].name) == want)
					{
						Emit(m_raw[j], state, out, depth + 1);
						break;
					}
				}
			}
			continue;
		}

		if (op.type == '$')
		{
			for (size_t a = 0; a < op.args.size(); a++)
				ApplyModifier(state, op.args[a].key, op.args[a].value);
			continue;
		}

		// '#' command
		Command c;
		c.mod = state;
		// inline modifiers apply only to this command
		for (size_t a = 1; a < op.args.size(); a++)
			ApplyModifier(c.mod, op.args[a].key, op.args[a].value);

		const std::string &val = op.args.empty() ? std::string() : op.args[0].value;

		if (op.key == "pause") { c.kind = CMD_PAUSE; c.delay = (float)atof(val.c_str()); }
		else if (op.key == "firetargets") { c.kind = CMD_FIRE; c.value = val; }
		else if (op.key == "killtargets") { c.kind = CMD_KILL; c.value = val; }
		else if (op.key == "text") { c.kind = CMD_TEXT; c.value = val; }
		else if (op.key == "sound") { c.kind = CMD_SOUND; c.value = val; }
		else if (op.key == "repeat") { c.kind = CMD_REPEAT; c.repeat = atoi(val.c_str()); }
		else if (op.key == "holdtime") { state.holdtime = (float)atof(val.c_str()); continue; }
		else continue;

		out.push_back(c);
	}
}

const Entry *Library::Find(const std::string &fileName, const std::string &entryName)
{
	std::string f = ToLower(fileName);
	std::string n = ToLower(entryName);
	std::string key = f + "|" + n;

	std::map<std::string, Entry>::iterator it = m_built.find(key);
	if (it != m_built.end())
		return &it->second;

	const RawEntry *found = nullptr;
	for (size_t i = 0; i < m_raw.size(); i++)
	{
		if (ToLower(m_raw[i].name) != n)
			continue;
		if (!f.empty() && m_raw[i].fileName != f)
		{
			if (!found) found = nullptr; // keep looking for exact file
			continue;
		}
		found = &m_raw[i];
		break;
	}
	if (!found && !f.empty())
	{
		// fall back to any file
		for (size_t i = 0; i < m_raw.size(); i++)
			if (ToLower(m_raw[i].name) == n) { found = &m_raw[i]; break; }
	}
	if (!found)
		return nullptr;

	Entry e;
	e.fileName = found->fileName;
	e.name = found->name;
	Modifiers state;
	Emit(*found, state, e.commands, 0);
	return &(m_built[key] = e);
}

const std::vector<std::string> *Library::GetGroup(const std::string &groupName) const
{
	std::map<std::string, std::vector<std::string>>::const_iterator it = m_groups.find(ToLower(groupName));
	if (it == m_groups.end() || it->second.empty())
		return nullptr;
	return &it->second;
}

const std::string *Library::PickSentence(const std::string &groupName, int *picked) const
{
	const std::vector<std::string> *g = GetGroup(groupName);
	if (!g)
		return nullptr;
	int idx = (int)(rand() % g->size());
	if (picked)
		*picked = idx;
	return &(*g)[idx];
}

} // namespace czseq
