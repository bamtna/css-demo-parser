#pragma once
#include "cssdemo/Protocol/Types.h"
#include "cssdemo/Util/BitReader.h"
#include "cssdemo/Util/BitWriter.h"
#include <memory>
#include <ostream>
#include <span>
#include <string>
#include <vector>

namespace cssdemo {

// layouts follow ReadFromBuffer / WriteToBuffer in common/netmessages.cpp
struct NetMessage {
	enum class Type {
		net_nop = 0,                 // nop command used for padding
		net_disconnect = 1,          // disconnect, last message in connection
		net_file = 2,                // file transmission message request/deny
		net_tick = 3,                // send last world tick
		net_string_cmd = 4,          // a string command
		net_set_con_var = 5,         // sends one/multiple convar settings
		net_signon_state = 6,        // signals current signon state
		svc_print = 7,               // print text to console
		svc_server_info = 8,         // first message from server about game, map etc
		svc_send_table = 9,          // sends a sendtable description for a game class
		svc_class_info = 10,         // info about classes (first byte is a classinfo_ define).
		svc_set_pause = 11,          // tells client if server paused or unpaused
		svc_create_string_table = 12,// inits shared string tables
		svc_update_string_table = 13,// updates a string table
		svc_voice_init = 14,         // inits used voice codecs & quality
		svc_voice_data = 15,         // voicestream data from the server
		svc_sounds = 17,             // starts playing sound
		svc_set_view = 18,           // sets entity as point of view
		svc_fix_angle = 19,          // sets/corrects players viewangle
		svc_crosshair_angle = 20,    // adjusts crosshair in auto aim mode to lock on target
		svc_bsp_decal = 21,          // add a static decal to the world bsp
		svc_user_message = 23,       // a game specific message
		svc_entity_message = 24,     // a message for an entity
		svc_game_event = 25,         // global game event fired
		svc_packet_entities = 26,    // non-delta compressed entities
		svc_temp_entities = 27,      // non-reliable event object
		svc_prefetch = 28,           // only sound indices for now
		svc_menu = 29,               // display a menu from a plugin
		svc_game_event_list = 30,    // list of known games events and fields
		svc_get_cvar_value = 31,     // server wants to know the value of a cvar on the client
		svc_cmd_key_values = 32,     // server submits keyvalues command for the client
		svc_set_pause_timed = 33,    // timed pause - to avoid breaking demos
	};

	explicit NetMessage(Type message_type) : type(message_type) {}
	virtual ~NetMessage() = default;

	virtual void parse(BitReader& reader, const ProtocolVersion& protocol) = 0;
	// body only, write_net_message adds the type
	virtual void write(BitWriter& writer, const ProtocolVersion& protocol) const = 0;
	virtual void print(std::ostream& os) const = 0;

	const Type type;
};

std::unique_ptr<NetMessage> create_net_message(NetMessage::Type type);

struct ParsedPacket {
	std::vector<std::unique_ptr<NetMessage>> messages;
	std::string error; // set if parsing stopped early
	BitBuffer padding; // bits after the last message, not always zero
};

ParsedPacket parse_packet(std::span<const std::byte> payload, const ProtocolVersion& protocol);

void write_net_message(BitWriter& writer, const NetMessage& message, const ProtocolVersion& protocol);

// CDemoRecorder::WriteMessages, pads with a nop like the engine when the last byte has room
std::vector<std::byte> write_packet(std::span<const NetMessage* const> messages, const ProtocolVersion& protocol);
std::vector<std::byte> write_packet(const std::vector<std::unique_ptr<NetMessage>>& messages, const ProtocolVersion& protocol);
// exact inverse of parse_packet
std::vector<std::byte> write_packet(const ParsedPacket& packet, const ProtocolVersion& protocol);

template <NetMessage::Type T>
struct NetMessageOf : NetMessage {
	static constexpr Type message_type = T;
	NetMessageOf() : NetMessage(T) {}
};

#define NET_MESSAGE_METHODS \
	void parse(BitReader& reader, const ProtocolVersion& protocol) override; \
	void write(BitWriter& writer, const ProtocolVersion& protocol) const override; \
	void print(std::ostream& os) const override;

struct NetNop : NetMessageOf<NetMessage::Type::net_nop> {
	NET_MESSAGE_METHODS
};

struct NetDisconnect : NetMessageOf<NetMessage::Type::net_disconnect> {
	NET_MESSAGE_METHODS
	std::string reason;
};

struct NetFile : NetMessageOf<NetMessage::Type::net_file> {
	NET_MESSAGE_METHODS
	uint32_t transfer_id{};
	std::string file_name;
	bool file_requested{};
};

struct NetTick : NetMessageOf<NetMessage::Type::net_tick> {
	NET_MESSAGE_METHODS
	int tick{};
	float host_frame_time{};
	float host_frame_time_std_deviation{};
};

struct NetStringCmd : NetMessageOf<NetMessage::Type::net_string_cmd> {
	NET_MESSAGE_METHODS
	std::string command;
};

struct NetSetConVar : NetMessageOf<NetMessage::Type::net_set_con_var> {
	NET_MESSAGE_METHODS
	struct ConVar {
		std::string name;
		std::string value;
	};
	std::vector<ConVar> convars;
};

struct NetSignonState : NetMessageOf<NetMessage::Type::net_signon_state> {
	NET_MESSAGE_METHODS
	int signon_state{};
	int spawn_count{};
};

struct SvcPrint : NetMessageOf<NetMessage::Type::svc_print> {
	NET_MESSAGE_METHODS
	std::string text;
};

struct SvcServerInfo : NetMessageOf<NetMessage::Type::svc_server_info> {
	NET_MESSAGE_METHODS
	int protocol{};
	int server_count{};
	bool is_hltv{};
	bool is_dedicated{};
	uint32_t client_crc{ 0xFFFFFFFF }; // legacy
	int max_classes{};
	std::vector<std::byte> map_md5; // protocol > 17
	int map_crc{};                  // protocol <= 17
	int player_slot{};
	int max_clients{};
	float tick_interval{};
	char os{}; // 'l' = linux, 'w' = win32
	std::string game_dir;
	std::string map_name;
	std::string sky_name;
	std::string host_name;
	bool is_replay{};
};

struct SvcSendTable : NetMessageOf<NetMessage::Type::svc_send_table> {
	NET_MESSAGE_METHODS
	bool needs_decoder{};
	BitBuffer data;
};

struct SvcClassInfo : NetMessageOf<NetMessage::Type::svc_class_info> {
	NET_MESSAGE_METHODS
	struct ServerClassInfo {
		int class_id{};
		std::string class_name;
		std::string data_table_name;
	};
	int num_server_classes{};
	bool create_on_client{}; // true in demos
	std::vector<ServerClassInfo> classes;
};

struct SvcSetPause : NetMessageOf<NetMessage::Type::svc_set_pause> {
	NET_MESSAGE_METHODS
	bool paused{};
};

struct SvcCreateStringTable : NetMessageOf<NetMessage::Type::svc_create_string_table> {
	NET_MESSAGE_METHODS
	std::string table_name;
	bool is_filenames{};
	int max_entries{};
	int num_entries{};
	bool user_data_fixed_size{};
	int user_data_size{};
	int user_data_size_bits{};
	bool data_compressed{};
	BitBuffer data;
};

struct SvcUpdateStringTable : NetMessageOf<NetMessage::Type::svc_update_string_table> {
	NET_MESSAGE_METHODS
	int table_id{};
	int num_changed_entries{};
	BitBuffer data;
};

struct SvcVoiceInit : NetMessageOf<NetMessage::Type::svc_voice_init> {
	NET_MESSAGE_METHODS
	std::string codec;
	int legacy_quality{};
	int sample_rate{};
};

struct SvcVoiceData : NetMessageOf<NetMessage::Type::svc_voice_data> {
	NET_MESSAGE_METHODS
	int from_client{};
	bool proximity{};
	BitBuffer data;
};

struct SvcSounds : NetMessageOf<NetMessage::Type::svc_sounds> {
	NET_MESSAGE_METHODS
	bool reliable_sound{};
	int num_sounds{};
	BitBuffer data;
};

struct SvcSetView : NetMessageOf<NetMessage::Type::svc_set_view> {
	NET_MESSAGE_METHODS
	int entity_index{};
};

struct SvcFixAngle : NetMessageOf<NetMessage::Type::svc_fix_angle> {
	NET_MESSAGE_METHODS
	bool relative{};
	QAngle angle{};
};

struct SvcCrosshairAngle : NetMessageOf<NetMessage::Type::svc_crosshair_angle> {
	NET_MESSAGE_METHODS
	QAngle angle{};
};

struct SvcBSPDecal : NetMessageOf<NetMessage::Type::svc_bsp_decal> {
	NET_MESSAGE_METHODS
	Vector pos{};
	int decal_texture_index{};
	int entity_index{};
	int model_index{};
	bool low_priority{};
};

struct SvcUserMessage : NetMessageOf<NetMessage::Type::svc_user_message> {
	NET_MESSAGE_METHODS
	int msg_type{};
	BitBuffer data;
};

struct SvcEntityMessage : NetMessageOf<NetMessage::Type::svc_entity_message> {
	NET_MESSAGE_METHODS
	int entity_index{};
	int class_id{};
	BitBuffer data;
};

struct SvcGameEvent : NetMessageOf<NetMessage::Type::svc_game_event> {
	NET_MESSAGE_METHODS
	BitBuffer data;
};

struct SvcPacketEntities : NetMessageOf<NetMessage::Type::svc_packet_entities> {
	NET_MESSAGE_METHODS
	int max_entries{};
	bool is_delta{};
	int delta_from{ -1 };
	int baseline{};
	int updated_entries{};
	bool update_baseline{};
	BitBuffer data;
};

struct SvcTempEntities : NetMessageOf<NetMessage::Type::svc_temp_entities> {
	NET_MESSAGE_METHODS
	int num_entries{};
	BitBuffer data;
};

struct SvcPrefetch : NetMessageOf<NetMessage::Type::svc_prefetch> {
	NET_MESSAGE_METHODS
	int sound_index{};
};

struct SvcMenu : NetMessageOf<NetMessage::Type::svc_menu> {
	NET_MESSAGE_METHODS
	int menu_type{};
	std::vector<std::byte> key_values; // binary KeyValues
};

struct SvcGameEventList : NetMessageOf<NetMessage::Type::svc_game_event_list> {
	NET_MESSAGE_METHODS
	int num_events{};
	BitBuffer data;
};

struct SvcGetCvarValue : NetMessageOf<NetMessage::Type::svc_get_cvar_value> {
	NET_MESSAGE_METHODS
	int cookie{};
	std::string cvar_name;
};

struct SvcCmdKeyValues : NetMessageOf<NetMessage::Type::svc_cmd_key_values> {
	NET_MESSAGE_METHODS
	std::vector<std::byte> key_values; // binary KeyValues
};

struct SvcSetPauseTimed : NetMessageOf<NetMessage::Type::svc_set_pause_timed> {
	NET_MESSAGE_METHODS
	bool paused{};
	float expire_time{};
};

#undef NET_MESSAGE_METHODS

}
