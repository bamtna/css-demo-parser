#include "NetMessages.h"
#include "cssdemo/Protocol/Constants.h"
#include <stdexcept>

namespace cssdemo {

namespace {

template <typename T>
std::unique_ptr<NetMessage> make() { return std::make_unique<T>(); }

void print_bits(std::ostream& os, const BitBuffer& data)
{
	os << ", length=" << data.num_bits << " bits";
}

} // namespace

std::unique_ptr<NetMessage> create_net_message(NetMessage::Type type)
{
	using T = NetMessage::Type;
	switch (type) {
	case T::net_nop: return make<NetNop>();
	case T::net_disconnect: return make<NetDisconnect>();
	case T::net_file: return make<NetFile>();
	case T::net_tick: return make<NetTick>();
	case T::net_string_cmd: return make<NetStringCmd>();
	case T::net_set_con_var: return make<NetSetConVar>();
	case T::net_signon_state: return make<NetSignonState>();
	case T::svc_print: return make<SvcPrint>();
	case T::svc_server_info: return make<SvcServerInfo>();
	case T::svc_send_table: return make<SvcSendTable>();
	case T::svc_class_info: return make<SvcClassInfo>();
	case T::svc_set_pause: return make<SvcSetPause>();
	case T::svc_create_string_table: return make<SvcCreateStringTable>();
	case T::svc_update_string_table: return make<SvcUpdateStringTable>();
	case T::svc_voice_init: return make<SvcVoiceInit>();
	case T::svc_voice_data: return make<SvcVoiceData>();
	case T::svc_sounds: return make<SvcSounds>();
	case T::svc_set_view: return make<SvcSetView>();
	case T::svc_fix_angle: return make<SvcFixAngle>();
	case T::svc_crosshair_angle: return make<SvcCrosshairAngle>();
	case T::svc_bsp_decal: return make<SvcBSPDecal>();
	case T::svc_user_message: return make<SvcUserMessage>();
	case T::svc_entity_message: return make<SvcEntityMessage>();
	case T::svc_game_event: return make<SvcGameEvent>();
	case T::svc_packet_entities: return make<SvcPacketEntities>();
	case T::svc_temp_entities: return make<SvcTempEntities>();
	case T::svc_prefetch: return make<SvcPrefetch>();
	case T::svc_menu: return make<SvcMenu>();
	case T::svc_game_event_list: return make<SvcGameEventList>();
	case T::svc_get_cvar_value: return make<SvcGetCvarValue>();
	case T::svc_cmd_key_values: return make<SvcCmdKeyValues>();
	case T::svc_set_pause_timed: return make<SvcSetPauseTimed>();
	}
	throw std::runtime_error("Unknown net message type " + std::to_string(static_cast<int>(type)));
}

ParsedPacket parse_packet(std::span<const std::byte> payload, const ProtocolVersion& protocol)
{
	ParsedPacket packet;
	BitReader reader(payload);

	// Anything shorter than a message type is padding.
	while (reader.bits_left() > NETMSG_TYPE_BITS) {
		auto type = static_cast<NetMessage::Type>(reader.read_bits(NETMSG_TYPE_BITS));
		try {
			auto message = create_net_message(type);
			message->parse(reader, protocol);
			packet.messages.push_back(std::move(message));
		}
		catch (const std::exception& e) {
			packet.error = e.what();
			break;
		}
	}
	return packet;
}

void NetNop::parse(BitReader&, const ProtocolVersion&) {}

void NetNop::print(std::ostream& os) const
{
	os << "NetNop";
}

void NetDisconnect::parse(BitReader& reader, const ProtocolVersion&)
{
	reason = reader.read_string(1024);
}

void NetDisconnect::print(std::ostream& os) const
{
	os << "NetDisconnect: " << reason;
}

void NetFile::parse(BitReader& reader, const ProtocolVersion&)
{
	transfer_id = reader.read_uint32();
	file_name = reader.read_string(1024);
	file_requested = reader.read_bool();
}

void NetFile::print(std::ostream& os) const
{
	os << "NetFile: transfer_id=" << transfer_id << ", file_name=" << file_name
		<< ", file_requested=" << file_requested;
}

void NetTick::parse(BitReader& reader, const ProtocolVersion&)
{
	tick = reader.read_int32();
	host_frame_time = reader.read_bits(16) / NET_TICK_SCALEUP;
	host_frame_time_std_deviation = reader.read_bits(16) / NET_TICK_SCALEUP;
}

void NetTick::print(std::ostream& os) const
{
	os << "NetTick: tick=" << tick << ", host_frame_time=" << host_frame_time
		<< ", host_frame_time_std_deviation=" << host_frame_time_std_deviation;
}

void NetStringCmd::parse(BitReader& reader, const ProtocolVersion&)
{
	command = reader.read_string(1024);
}

void NetStringCmd::print(std::ostream& os) const
{
	os << "NetStringCmd: " << command;
}

void NetSetConVar::parse(BitReader& reader, const ProtocolVersion&)
{
	int count = reader.read_uint8();
	for (int i = 0; i < count; i++) {
		ConVar convar;
		convar.name = reader.read_string(260);
		convar.value = reader.read_string(260);
		convars.push_back(std::move(convar));
	}
}

void NetSetConVar::print(std::ostream& os) const
{
	os << "NetSetConVar: " << convars.size() << " convars";
	for (const auto& convar : convars) {
		os << "\n        " << convar.name << " = " << convar.value;
	}
}

void NetSignonState::parse(BitReader& reader, const ProtocolVersion&)
{
	signon_state = reader.read_uint8();
	spawn_count = reader.read_int32();
}

void NetSignonState::print(std::ostream& os) const
{
	static const char* names[] = { "none", "challenge", "connected", "new", "prespawn", "spawn", "full", "changelevel" };
	os << "NetSignonState: signon_state=" << signon_state;
	if (signon_state >= 0 && signon_state < 8) os << " (" << names[signon_state] << ")";
	os << ", spawn_count=" << spawn_count;
}

void SvcPrint::parse(BitReader& reader, const ProtocolVersion&)
{
	text = reader.read_string(2048);
}

void SvcPrint::print(std::ostream& os) const
{
	os << "SvcPrint:";
	size_t start = 0;
	while (start < text.size()) {
		size_t end = text.find('\n', start);
		if (end == std::string::npos) end = text.size();
		if (end > start) {
			os << (start == 0 ? " " : "\n        ") << text.substr(start, end - start);
		}
		start = end + 1;
	}
}

void SvcServerInfo::parse(BitReader& reader, const ProtocolVersion& version)
{
	protocol = reader.read_int16();
	server_count = reader.read_int32();
	is_hltv = reader.read_bool();
	is_dedicated = reader.read_bool();
	reader.read_int32(); // legacy client CRC
	max_classes = reader.read_uint16();
	if (protocol > PROTOCOL_VERSION_17) {
		map_md5 = reader.read_bytes(16);
	}
	else {
		map_crc = reader.read_int32();
	}
	player_slot = reader.read_uint8();
	max_clients = reader.read_uint8();
	tick_interval = reader.read_float32();
	os = static_cast<char>(reader.read_uint8());
	game_dir = reader.read_string(MAX_OSPATH);
	map_name = reader.read_string(MAX_OSPATH);
	sky_name = reader.read_string(MAX_OSPATH);
	host_name = reader.read_string(MAX_OSPATH);
	if (version.network >= PROTOCOL_VERSION_REPLAY) {
		is_replay = reader.read_bool();
	}
}

void SvcServerInfo::print(std::ostream& out) const
{
	out << "SvcServerInfo: protocol=" << protocol
		<< ", server_count=" << server_count
		<< ", is_hltv=" << is_hltv
		<< ", is_dedicated=" << is_dedicated
		<< ", max_classes=" << max_classes
		<< ", player_slot=" << player_slot
		<< ", max_clients=" << max_clients
		<< ", tick_interval=" << tick_interval
		<< ", os=" << os
		<< ", game_dir=" << game_dir
		<< ", map_name=" << map_name
		<< ", sky_name=" << sky_name
		<< ", host_name=" << host_name
		<< ", is_replay=" << is_replay;
}

void SvcSendTable::parse(BitReader& reader, const ProtocolVersion&)
{
	needs_decoder = reader.read_bool();
	int length = reader.read_int16();
	data = reader.read_buffer(length);
}

void SvcSendTable::print(std::ostream& os) const
{
	os << "SvcSendTable: needs_decoder=" << needs_decoder;
	print_bits(os, data);
}

void SvcClassInfo::parse(BitReader& reader, const ProtocolVersion&)
{
	num_server_classes = reader.read_int16();
	int server_class_bits = Q_log2(num_server_classes) + 1;
	create_on_client = reader.read_bool();
	if (create_on_client) {
		return;
	}
	for (int i = 0; i < num_server_classes; i++) {
		ServerClassInfo info;
		info.class_id = reader.read_bits(server_class_bits);
		info.class_name = reader.read_string(64);
		info.data_table_name = reader.read_string(64);
		classes.push_back(std::move(info));
	}
}

void SvcClassInfo::print(std::ostream& os) const
{
	os << "SvcClassInfo: num_server_classes=" << num_server_classes << ", create_on_client=" << create_on_client;
	for (const auto& info : classes) {
		os << "\n        [" << info.class_id << "] " << info.class_name << " (" << info.data_table_name << ")";
	}
}

void SvcSetPause::parse(BitReader& reader, const ProtocolVersion&)
{
	paused = reader.read_bool();
}

void SvcSetPause::print(std::ostream& os) const
{
	os << "SvcSetPause: paused=" << paused;
}

void SvcCreateStringTable::parse(BitReader& reader, const ProtocolVersion& protocol)
{
	if (reader.peek_bits(8) == ':') {
		is_filenames = true;
		reader.read_uint8();
	}
	table_name = reader.read_string(256);
	max_entries = reader.read_uint16();
	num_entries = reader.read_bits(Q_log2(max_entries) + 1);
	size_t length = protocol.network > PROTOCOL_VERSION_23
		? reader.read_var_int32()
		: reader.read_bits(NET_MAX_PAYLOAD_BITS_V23 + 3);
	user_data_fixed_size = reader.read_bool();
	if (user_data_fixed_size) {
		user_data_size = reader.read_bits(12);
		user_data_size_bits = reader.read_bits(4);
	}
	data_compressed = protocol.network > PROTOCOL_VERSION_14 ? reader.read_bool() : false;
	data = reader.read_buffer(length);
}

void SvcCreateStringTable::print(std::ostream& os) const
{
	os << "SvcCreateStringTable: table_name=" << table_name
		<< ", max_entries=" << max_entries
		<< ", num_entries=" << num_entries
		<< ", user_data_fixed_size=" << user_data_fixed_size
		<< ", user_data_size=" << user_data_size
		<< ", user_data_size_bits=" << user_data_size_bits
		<< ", data_compressed=" << data_compressed;
	print_bits(os, data);
}

void SvcUpdateStringTable::parse(BitReader& reader, const ProtocolVersion&)
{
	table_id = reader.read_bits(Q_log2(MAX_TABLES));
	num_changed_entries = reader.read_bool() ? reader.read_uint16() : 1;
	data = reader.read_buffer(reader.read_bits(20));
}

void SvcUpdateStringTable::print(std::ostream& os) const
{
	os << "SvcUpdateStringTable: table_id=" << table_id
		<< ", num_changed_entries=" << num_changed_entries;
	print_bits(os, data);
}

void SvcVoiceInit::parse(BitReader& reader, const ProtocolVersion&)
{
	codec = reader.read_string(128);
	legacy_quality = reader.read_uint8();
	if (legacy_quality == 255) {
		sample_rate = reader.read_int16();
	}
}

void SvcVoiceInit::print(std::ostream& os) const
{
	os << "SvcVoiceInit: codec=" << codec << ", legacy_quality=" << legacy_quality
		<< ", sample_rate=" << sample_rate;
}

void SvcVoiceData::parse(BitReader& reader, const ProtocolVersion&)
{
	from_client = reader.read_uint8();
	proximity = reader.read_uint8() != 0;
	data = reader.read_buffer(reader.read_uint16());
}

void SvcVoiceData::print(std::ostream& os) const
{
	os << "SvcVoiceData: from_client=" << from_client << ", proximity=" << proximity;
	print_bits(os, data);
}

void SvcSounds::parse(BitReader& reader, const ProtocolVersion&)
{
	reliable_sound = reader.read_bool();
	size_t length;
	if (reliable_sound) {
		num_sounds = 1;
		length = reader.read_bits(8);
	}
	else {
		num_sounds = reader.read_bits(8);
		length = reader.read_bits(16);
	}
	data = reader.read_buffer(length);
}

void SvcSounds::print(std::ostream& os) const
{
	os << "SvcSounds: reliable_sound=" << reliable_sound << ", num_sounds=" << num_sounds;
	print_bits(os, data);
}

void SvcSetView::parse(BitReader& reader, const ProtocolVersion&)
{
	entity_index = reader.read_bits(MAX_EDICT_BITS);
}

void SvcSetView::print(std::ostream& os) const
{
	os << "SvcSetView: entity_index=" << entity_index;
}

void SvcFixAngle::parse(BitReader& reader, const ProtocolVersion&)
{
	relative = reader.read_bool();
	angle.x = reader.read_bit_angle(16);
	angle.y = reader.read_bit_angle(16);
	angle.z = reader.read_bit_angle(16);
}

void SvcFixAngle::print(std::ostream& os) const
{
	os << "SvcFixAngle: relative=" << relative << ", angle=(" << angle.x << ", " << angle.y << ", " << angle.z << ")";
}

void SvcCrosshairAngle::parse(BitReader& reader, const ProtocolVersion&)
{
	angle.x = reader.read_bit_angle(16);
	angle.y = reader.read_bit_angle(16);
	angle.z = reader.read_bit_angle(16);
}

void SvcCrosshairAngle::print(std::ostream& os) const
{
	os << "SvcCrosshairAngle: angle=(" << angle.x << ", " << angle.y << ", " << angle.z << ")";
}

void SvcBSPDecal::parse(BitReader& reader, const ProtocolVersion&)
{
	pos = reader.read_bit_vec3_coord();
	decal_texture_index = reader.read_bits(MAX_DECAL_INDEX_BITS);
	if (reader.read_bool()) {
		entity_index = reader.read_bits(MAX_EDICT_BITS);
		model_index = reader.read_bits(SP_MODEL_INDEX_BITS);
	}
	low_priority = reader.read_bool();
}

void SvcBSPDecal::print(std::ostream& os) const
{
	os << "SvcBSPDecal: pos=(" << pos.x << ", " << pos.y << ", " << pos.z << ")"
		<< ", decal_texture_index=" << decal_texture_index
		<< ", entity_index=" << entity_index
		<< ", model_index=" << model_index
		<< ", low_priority=" << low_priority;
}

void SvcUserMessage::parse(BitReader& reader, const ProtocolVersion&)
{
	msg_type = reader.read_uint8();
	data = reader.read_buffer(reader.read_bits(NETMSG_LENGTH_BITS));
}

void SvcUserMessage::print(std::ostream& os) const
{
	os << "SvcUserMessage: msg_type=" << msg_type;
	print_bits(os, data);
}

void SvcEntityMessage::parse(BitReader& reader, const ProtocolVersion&)
{
	entity_index = reader.read_bits(MAX_EDICT_BITS);
	class_id = reader.read_bits(MAX_SERVER_CLASS_BITS);
	data = reader.read_buffer(reader.read_bits(NETMSG_LENGTH_BITS));
}

void SvcEntityMessage::print(std::ostream& os) const
{
	os << "SvcEntityMessage: entity_index=" << entity_index << ", class_id=" << class_id;
	print_bits(os, data);
}

void SvcGameEvent::parse(BitReader& reader, const ProtocolVersion&)
{
	data = reader.read_buffer(reader.read_bits(NETMSG_LENGTH_BITS));
}

void SvcGameEvent::print(std::ostream& os) const
{
	os << "SvcGameEvent";
	print_bits(os, data);
}

void SvcPacketEntities::parse(BitReader& reader, const ProtocolVersion&)
{
	max_entries = reader.read_bits(MAX_EDICT_BITS);
	is_delta = reader.read_bool();
	delta_from = is_delta ? reader.read_int32() : -1;
	baseline = reader.read_bits(1);
	updated_entries = reader.read_bits(MAX_EDICT_BITS);
	size_t length = reader.read_bits(DELTASIZE_BITS);
	update_baseline = reader.read_bool();
	data = reader.read_buffer(length);
}

void SvcPacketEntities::print(std::ostream& os) const
{
	os << "SvcPacketEntities: max_entries=" << max_entries
		<< ", is_delta=" << is_delta
		<< ", delta_from=" << delta_from
		<< ", baseline=" << baseline
		<< ", updated_entries=" << updated_entries
		<< ", update_baseline=" << update_baseline;
	print_bits(os, data);
}

void SvcTempEntities::parse(BitReader& reader, const ProtocolVersion& protocol)
{
	num_entries = reader.read_bits(EVENT_INDEX_BITS);
	size_t length = protocol.network > PROTOCOL_VERSION_23
		? reader.read_var_int32()
		: reader.read_bits(NET_MAX_PAYLOAD_BITS_V23);
	data = reader.read_buffer(length);
}

void SvcTempEntities::print(std::ostream& os) const
{
	os << "SvcTempEntities: num_entries=" << num_entries;
	print_bits(os, data);
}

void SvcPrefetch::parse(BitReader& reader, const ProtocolVersion& protocol)
{
	sound_index = reader.read_bits(protocol.network > PROTOCOL_VERSION_22 ? MAX_SOUND_INDEX_BITS : 13);
}

void SvcPrefetch::print(std::ostream& os) const
{
	os << "SvcPrefetch: sound_index=" << sound_index;
}

void SvcMenu::parse(BitReader& reader, const ProtocolVersion&)
{
	menu_type = reader.read_int16();
	key_values = reader.read_bytes(reader.read_uint16());
}

void SvcMenu::print(std::ostream& os) const
{
	os << "SvcMenu: menu_type=" << menu_type << ", length=" << key_values.size() << " bytes";
}

void SvcGameEventList::parse(BitReader& reader, const ProtocolVersion&)
{
	num_events = reader.read_bits(MAX_EVENT_BITS);
	data = reader.read_buffer(reader.read_bits(20));
}

void SvcGameEventList::print(std::ostream& os) const
{
	os << "SvcGameEventList: num_events=" << num_events;
	print_bits(os, data);
}

void SvcGetCvarValue::parse(BitReader& reader, const ProtocolVersion&)
{
	cookie = reader.read_int32();
	cvar_name = reader.read_string(256);
}

void SvcGetCvarValue::print(std::ostream& os) const
{
	os << "SvcGetCvarValue: cookie=" << cookie << ", cvar_name=" << cvar_name;
}

void SvcCmdKeyValues::parse(BitReader& reader, const ProtocolVersion&)
{
	int32_t length = reader.read_int32();
	if (length <= 0 || static_cast<size_t>(length) > reader.bits_left() / 8) {
		throw std::runtime_error("SvcCmdKeyValues: bad length " + std::to_string(length));
	}
	key_values = reader.read_bytes(length);
}

void SvcCmdKeyValues::print(std::ostream& os) const
{
	os << "SvcCmdKeyValues: length=" << key_values.size() << " bytes";
}

void SvcSetPauseTimed::parse(BitReader& reader, const ProtocolVersion&)
{
	paused = reader.read_bool();
	expire_time = reader.read_float32();
}

void SvcSetPauseTimed::print(std::ostream& os) const
{
	os << "SvcSetPauseTimed: paused=" << paused << ", expire_time=" << expire_time;
}

}
