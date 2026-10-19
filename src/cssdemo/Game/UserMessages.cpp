#include "UserMessages.h"
#include "cssdemo/Net/NetMessages.h"
#include "cssdemo/Protocol/Constants.h"
#include "cssdemo/Util/BitReader.h"
#include "cssdemo/Util/BitWriter.h"
#include <array>
#include <functional>
#include <stdexcept>
#include <type_traits>

namespace cssdemo {

namespace {

// registration order in cs_usermessages.cpp
constexpr std::array<const char*, 47> USER_MESSAGE_NAMES = {
	"Geiger", "Train", "HudText", "SayText", "SayText2", "TextMsg", "HudMsg", "ResetHUD",
	"GameTitle", "ItemPickup", "ShowMenu", "Shake", "Fade", "VGUIMenu", "Rumble", "CloseCaption",
	"SendAudio", "RawAudio", "VoiceMask", "RequestState", "BarTime", "Damage", "RadioText",
	"HintText", "KeyHintText", "ReloadEffect", "PlayerAnimEvent", "AmmoDenied", "UpdateRadar",
	"KillCam", "MarkAchievement", "CallVoteFailed", "VoteStart", "VotePass", "VoteFailed",
	"VoteSetup", "SPHapWeapEvent", "HapDmg", "HapPunch", "HapSetDrag", "HapSetConst",
	"HapMeleeContact", "PlayerStatsUpdate_DEPRECATED", "AchievementEvent", "MatchEndConditions",
	"MatchStatsUpdate", "PlayerStatsUpdate",
};

std::string clean(const std::string& text)
{
	std::string result;
	for (char c : text) {
		if (static_cast<unsigned char>(c) >= 0x20) result += c;
	}
	return result;
}

// reads into message, or writes source's fields in the same order
class Fields {
	BitReader* reader{};
	BitWriter* writer{};
	UserMessage& message;
	const UserMessage* source{};
	size_t next = 0;

public:
	Fields(BitReader& r, UserMessage& m) : reader(&r), message(m) {}
	Fields(BitWriter& w, const UserMessage& from, UserMessage& scratch) : writer(&w), message(scratch), source(&from) {}

	bool more() const { return reader ? reader->bits_left() >= 8 : next < source->fields.size(); }

	int byte(const char* name) {
		return field<int>(name, [&] { return static_cast<int>(reader->read_uint8()); },
			[&](int v) { writer->write_uint8(static_cast<uint8_t>(v)); });
	}
	int character(const char* name) {
		return field<int>(name, [&] { return static_cast<int>(reader->read_int8()); },
			[&](int v) { writer->write_int8(static_cast<int8_t>(v)); });
	}
	int word(const char* name) {
		return field<int>(name, [&] { return static_cast<int>(reader->read_uint16()); },
			[&](int v) { writer->write_uint16(static_cast<uint16_t>(v)); });
	}
	int short_(const char* name) {
		return field<int>(name, [&] { return static_cast<int>(reader->read_int16()); },
			[&](int v) { writer->write_int16(static_cast<int16_t>(v)); });
	}
	int long_(const char* name) {
		return field<int>(name, [&] { return static_cast<int>(reader->read_int32()); },
			[&](int v) { writer->write_int32(v); });
	}
	int sbits(const char* name, int bits) {
		return field<int>(name, [&] { return reader->read_signed_bits(bits); },
			[&](int v) { writer->write_signed_bits(v, bits); });
	}
	float float_(const char* name) {
		return field<float>(name, [&] { return reader->read_float32(); },
			[&](float v) { writer->write_float32(v); });
	}
	Vector vec3_coord(const char* name) {
		return field<Vector>(name, [&] { return reader->read_bit_vec3_coord(); },
			[&](const Vector& v) { writer->write_bit_vec3_coord(v); });
	}
	// fields keep the raw string, the returned one is for display
	std::string string(const char* name) {
		return clean(field<std::string>(name, [&] { return reader->read_string(2048); },
			[&](const std::string& v) { writer->write_string(v); }));
	}

private:
	template <typename T, typename Read, typename Write>
	T field(const char* name, Read read, Write write) {
		if (reader) {
			T value = read();
			message.fields.emplace_back(name, value);
			return value;
		}
		if (next >= source->fields.size()) {
			throw std::runtime_error(std::string("missing field ") + name);
		}
		const auto& value = source->fields[next++].second;
		T result{};
		if (auto v = std::get_if<T>(&value)) {
			result = *v;
		}
		else if constexpr (std::is_same_v<T, int>) {
			if (auto f = std::get_if<float>(&value)) result = static_cast<int>(*f);
			else throw std::runtime_error(std::string("field ") + name + " has the wrong type");
		}
		else if constexpr (std::is_same_v<T, float>) {
			if (auto i = std::get_if<int>(&value)) result = static_cast<float>(*i);
			else throw std::runtime_error(std::string("field ") + name + " has the wrong type");
		}
		else {
			throw std::runtime_error(std::string("field ") + name + " has the wrong type");
		}
		write(result);
		return result;
	}
};

using Decoder = std::function<void(Fields&, UserMessage&)>;

void read_params(Fields& f, UserMessage& m, int count)
{
	static const char* names[] = { "param1", "param2", "param3", "param4" };
	for (int i = 0; i < count && f.more(); i++) {
		std::string param = f.string(names[i]);
		if (!param.empty()) m.text += " \"" + param + "\"";
	}
}

const std::array<Decoder, USER_MESSAGE_NAMES.size()>& decoders()
{
	static const std::array<Decoder, USER_MESSAGE_NAMES.size()> table = {
		/* Geiger */ [](Fields& f, UserMessage&) { f.byte("range"); },
		/* Train */ [](Fields& f, UserMessage&) { f.byte("position"); },
		/* HudText */ [](Fields& f, UserMessage& m) { m.text = f.string("text"); },
		/* SayText */ [](Fields& f, UserMessage& m) {
			int client = f.byte("client");
			std::string text = f.string("text");
			f.byte("chat");
			m.text = "client " + std::to_string(client) + ": " + text;
		},
		/* SayText2 */ [](Fields& f, UserMessage& m) {
			int client = f.byte("client");
			f.byte("chat");
			std::string format = f.string("format");
			std::string params[4];
			static const char* names[] = { "player", "message", "location", "unused" };
			for (int i = 0; i < 4 && f.more(); i++) params[i] = f.string(names[i]);
			// Cstrike_Chat_* formats take name and message, anything else is plain server text
			if (format.rfind("Cstrike_Chat", 0) == 0 || format.rfind("#Cstrike_Chat", 0) == 0) {
				m.text = "[" + format + "] " + params[0] + ": " + params[1];
			}
			else {
				m.text = "client " + std::to_string(client) + ": " + format;
				for (const auto& p : params) if (!p.empty()) m.text += " \"" + p + "\"";
			}
		},
		/* TextMsg */ [](Fields& f, UserMessage& m) {
			int dest = f.byte("destination");
			m.text = "dest " + std::to_string(dest) + ": " + f.string("message");
			read_params(f, m, 4);
		},
		/* HudMsg */ [](Fields& f, UserMessage& m) {
			f.byte("channel"); f.float_("x"); f.float_("y");
			f.byte("r1"); f.byte("g1"); f.byte("b1"); f.byte("a1");
			f.byte("r2"); f.byte("g2"); f.byte("b2"); f.byte("a2");
			f.byte("effect"); f.float_("fade_in"); f.float_("fade_out"); f.float_("hold"); f.float_("fx_time");
			m.text = f.string("message");
		},
		/* ResetHUD */ [](Fields& f, UserMessage&) { f.byte("reset"); },
		/* GameTitle */ [](Fields&, UserMessage&) {},
		/* ItemPickup */ [](Fields& f, UserMessage&) { f.string("item"); },
		/* ShowMenu */ [](Fields& f, UserMessage& m) {
			f.word("valid_slots"); f.character("display_time"); f.byte("need_more");
			m.text = f.string("text");
		},
		/* Shake */ [](Fields& f, UserMessage&) {
			f.byte("command"); f.float_("amplitude"); f.float_("frequency"); f.float_("duration");
		},
		/* Fade */ [](Fields& f, UserMessage&) {
			f.short_("duration"); f.short_("hold_time"); f.short_("flags");
			f.byte("r"); f.byte("g"); f.byte("b"); f.byte("a");
		},
		/* VGUIMenu */ [](Fields& f, UserMessage& m) {
			m.text = f.string("name");
			f.byte("show");
			int count = f.byte("count");
			for (int i = 0; i < count; i++) {
				f.string("key");
				f.string("value");
			}
		},
		/* Rumble */ [](Fields& f, UserMessage&) { f.byte("waveform"); f.byte("data"); f.byte("flags"); },
		/* CloseCaption */ [](Fields& f, UserMessage& m) {
			m.text = f.string("token"); f.short_("duration_tenths"); f.byte("flags");
		},
		/* SendAudio */ [](Fields& f, UserMessage& m) { m.text = f.string("sound"); },
		/* RawAudio */ [](Fields& f, UserMessage& m) {
			f.byte("pitch"); f.byte("player"); f.float_("feedback_duration");
			m.text = f.string("sound");
		},
		/* VoiceMask */ [](Fields& f, UserMessage&) {
			// VOICE_MAX_PLAYERS_DW mask pairs then the mod enable flag
			for (int i = 0; i < 2; i++) { f.long_("game_rules_mask"); f.long_("ban_mask"); }
			f.byte("player_mod_enable");
		},
		/* RequestState */ [](Fields&, UserMessage&) {},
		/* BarTime */ nullptr,
		/* Damage */ [](Fields& f, UserMessage&) {
			f.byte("damage"); f.vec3_coord("from"); f.long_("hitbox"); f.vec3_coord("hit_location");
		},
		/* RadioText */ [](Fields& f, UserMessage& m) {
			int dest = f.byte("destination");
			int client = f.byte("client");
			m.text = "dest " + std::to_string(dest) + " client " + std::to_string(client) + ": " + f.string("message");
			read_params(f, m, 4);
		},
		/* HintText */ [](Fields& f, UserMessage& m) { m.text = f.string("text"); },
		/* KeyHintText */ [](Fields& f, UserMessage& m) {
			int count = f.byte("count");
			for (int i = 0; i < count; i++) {
				if (i) m.text += " | ";
				m.text += f.string("text");
			}
		},
		/* ReloadEffect */ [](Fields& f, UserMessage&) { f.short_("entity"); },
		/* PlayerAnimEvent */ nullptr,
		/* AmmoDenied */ [](Fields& f, UserMessage&) { f.short_("ammo_type"); },
		/* UpdateRadar */ [](Fields& f, UserMessage&) {
			// entity, x/4, y/4, z/4, yaw until entity 0
			while (f.byte("entity") > 0) {
				f.sbits("x", COORD_INTEGER_BITS - 1);
				f.sbits("y", COORD_INTEGER_BITS - 1);
				f.sbits("z", COORD_INTEGER_BITS - 1);
				f.sbits("yaw", 9);
			}
		},
		/* KillCam */ [](Fields& f, UserMessage&) { f.byte("mode"); f.byte("target1"); f.byte("target2"); },
		/* MarkAchievement */ nullptr,
		/* CallVoteFailed */ [](Fields& f, UserMessage&) { f.byte("reason"); f.short_("time"); },
		/* VoteStart */ [](Fields& f, UserMessage& m) {
			f.byte("team"); f.byte("caller");
			m.text = f.string("issue");
			std::string param = f.string("param1");
			if (!param.empty()) m.text += " \"" + param + "\"";
			f.byte("yes_no_vote");
			if (f.more()) f.byte("target");
		},
		/* VotePass */ [](Fields& f, UserMessage& m) {
			f.byte("team");
			m.text = f.string("result");
			std::string param = f.string("param1");
			if (!param.empty()) m.text += " \"" + param + "\"";
		},
		/* VoteFailed */ [](Fields& f, UserMessage&) { f.byte("team"); f.byte("reason"); },
		/* VoteSetup */ [](Fields& f, UserMessage&) {
			int count = f.byte("count");
			for (int i = 0; i < count; i++) { f.string("issue"); f.string("issue_string"); f.byte("active"); }
		},
		/* SPHapWeapEvent */ nullptr,
		/* HapDmg */ nullptr,
		/* HapPunch */ nullptr,
		/* HapSetDrag */ nullptr,
		/* HapSetConst */ nullptr,
		/* HapMeleeContact */ nullptr,
		/* PlayerStatsUpdate_DEPRECATED */ nullptr,
		/* AchievementEvent */ nullptr,
		/* MatchEndConditions */ [](Fields& f, UserMessage&) {
			f.long_("frag_limit"); f.long_("max_rounds"); f.long_("win_rounds"); f.long_("time_limit");
		},
		/* MatchStatsUpdate */ nullptr,
		/* PlayerStatsUpdate */ [](Fields& f, UserMessage&) {
			f.byte("version");
			int count = f.byte("count");
			for (int i = 0; i < count; i++) { f.byte("stat"); f.short_("delta"); }
			f.long_("crc");
		},
	};
	return table;
}

} // namespace

std::string user_message_name(int type)
{
	if (type >= 0 && type < static_cast<int>(USER_MESSAGE_NAMES.size())) {
		return USER_MESSAGE_NAMES[type];
	}
	return "UserMessage" + std::to_string(type);
}

int user_message_type(const std::string& name)
{
	for (size_t i = 0; i < USER_MESSAGE_NAMES.size(); i++) {
		if (name == USER_MESSAGE_NAMES[i]) return static_cast<int>(i);
	}
	return -1;
}

SvcUserMessage encode_user_message(const UserMessage& message)
{
	int type = message.name.empty() ? message.type : user_message_type(message.name);
	if (type < 0 || type >= static_cast<int>(decoders().size()) || !decoders()[type]) {
		throw std::runtime_error("Can't encode user message " + message.name);
	}
	BitWriter writer;
	UserMessage scratch;
	Fields fields(writer, message, scratch);
	decoders()[type](fields, scratch);
	if (fields.more()) {
		throw std::runtime_error("User message " + message.name + " has more fields than its layout");
	}
	SvcUserMessage result;
	result.msg_type = type;
	result.data = writer.take_buffer();
	return result;
}

UserMessage decode_user_message(const SvcUserMessage& message)
{
	UserMessage result;
	result.type = message.msg_type;
	result.name = user_message_name(message.msg_type);

	if (message.msg_type < 0 || message.msg_type >= static_cast<int>(decoders().size())) {
		return result;
	}
	const Decoder& decoder = decoders()[message.msg_type];
	if (!decoder) {
		return result;
	}

	BitReader reader = message.data.reader();
	Fields fields(reader, result);
	try {
		decoder(fields, result);
		result.decoded = true;
		if (reader.bits_left() >= 8) {
			result.error = std::to_string(reader.bits_left()) + " bits left unread";
		}
	}
	catch (const std::exception& e) {
		result.error = e.what();
	}
	return result;
}

std::ostream& operator<<(std::ostream& os, const UserMessage& message)
{
	os << message.name;
	if (!message.text.empty()) {
		os << ": " << message.text;
	}
	else if (!message.fields.empty()) {
		for (const auto& [name, value] : message.fields) {
			os << " " << name << "=";
			std::visit([&](const auto& v) {
				using T = std::decay_t<decltype(v)>;
				if constexpr (std::is_same_v<T, std::string>) os << '"' << v << '"';
				else if constexpr (std::is_same_v<T, Vector>) os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
				else os << v;
			}, value);
		}
	}
	if (!message.error.empty()) {
		os << " [decode error: " << message.error << "]";
	}
	return os;
}

}
