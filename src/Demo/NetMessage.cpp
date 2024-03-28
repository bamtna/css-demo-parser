#include "NetMessage.h"
#include "Util/BitReader.h"
#include "Util/math.h"
#include <iostream>
#include <iomanip>

void NetNop::parse(BitReader& reader) {
	std::cout << "NetNop" << std::endl;
}

void NetDisconnect::parse(BitReader& reader) {
	text = reader.read_ascii_string(1024);

	std::cout << "NetDisconnect: " << text << std::endl;
}

void NetFile::parse(BitReader& reader)
{
	transfer_id = reader.read_int32();
	file_name = reader.read_ascii_string();
	file_requested = reader.read_bit();

	std::cout << "NetFile: transfer_id=" << transfer_id << ", file_name=" << file_name << ", file_requested=" << file_requested << std::endl;
}

void NetTick::parse(BitReader& reader)
{
	tick = reader.read_int32();
	host_frame_time = reader.read_uint16() / SCALEUP;
	host_frame_time_std_deviation = reader.read_uint16() / SCALEUP;
	std::cout << "NetTick: tick=" << tick << ", host_frame_time=" << host_frame_time << ", host_frame_time_std_deviation=" << host_frame_time_std_deviation << std::endl;
}

void NetTick::print()
{
	std::cout << std::fixed << std::setprecision(4);
	std::cout << "NetTick Information:\n"
		<< std::setw(30) << "Tick: " << tick << "\n"
		<< std::setw(30) << "Host Frame Time: " << host_frame_time << "\n"
		<< std::setw(30) << "Host Frame Time Std Deviation: " << host_frame_time_std_deviation << std::endl;
}

void NetStringCmd::parse(BitReader& reader)
{
	command = reader.read_ascii_string(1024);
	std::cout << "NetStringCmd: " << command << std::endl;
}

void NetSetConVar::parse(BitReader& reader) {
    int length = reader.read_bits(8);
    std::cout << "NetSetConVar: NumConVars=" << length << std::endl;
    for (auto i = 0; i < length; i++) {
        ConVar convar{};
        convar.name = reader.read_ascii_string();
        convar.value = reader.read_ascii_string();
        convars.push_back(convar);
        std::cout << "  ConVar " << i << ": " << convar.name << "=" << convar.value << std::endl;
    }
}

void NetSignonState::parse(BitReader& reader)
{
	signon_state = reader.read_uint8();
	spawn_count = reader.read_uint32();
	std::cout << "NetSignonState: signon_state=" << signon_state << ", spawn_count=" << spawn_count << std::endl;
}

void SvcPrint::parse(BitReader& reader)
{
	text = reader.read_ascii_string();
	std::cout << "SvcPrint: " << text << std::endl;
}

void SvcServerInfo::parse(BitReader& reader)
{
	protocol = reader.read_short(); // 16 seems to be correct, but this is 8 bits on https://dem.nekz.me/classes/netsvc/netsetconvar
	server_count = reader.read_uint32();
	is_hltv = reader.read_bit();
	is_dedicated = reader.read_bit();
	client_crc = reader.read_int32();
	max_classes = reader.read_uint16();

	std::cout << "SvcServerInfo: protocol=" << protocol
		<< ", server_count=" << server_count
		<< ", is_hltv=" << is_hltv
		<< ", is_dedicated=" << is_dedicated
		<< ", client_crc=" << client_crc
		<< ", max_classes=" << max_classes << std::endl;

	if (protocol > 17) {
		reader.read_bytes(16);
	}
	else {
		map_crc = reader.read_int32();
		std::cout << "  Map CRC for protocol <= 17: " << map_crc << std::endl;
	}

	player_slot = static_cast<int>(reader.read_byte());
	max_classes = static_cast<int>(reader.read_byte());
	tick_interval = reader.read_float32();
	os = static_cast<char>(reader.read_byte());
	game_dir = reader.read_ascii_string(260);
	map_name = reader.read_ascii_string(260);
	sky_name = reader.read_ascii_string(260);
	host_name = reader.read_ascii_string(260);
	is_replay = reader.read_bit();

	std::cout << "  player_slot=" << player_slot
		<< ", tick_interval=" << tick_interval
		<< ", os=" << os
		<< ", game_dir=" << game_dir
		<< ", map_name=" << map_name
		<< ", sky_name=" << sky_name
		<< ", host_name=" << host_name
		<< ", is_replay=" << is_replay << std::endl;
}

void SvcSendTable::parse(BitReader& reader)
{
	needs_decoder = reader.read_bit();
	length = reader.read_short();
	data = reader.read_many_bits(length);
	std::cout << "SvcSendTable: needs_decoder=" << needs_decoder
		<< ", length=" << length << " bits" << std::endl;
}

void SvcClassInfo::parse(BitReader& reader) {
	num_server_classes = reader.read_int16();
	create_on_client = reader.read_bit();
	
	std::cout << "SvcClassInfo: num_server_classes=" << num_server_classes << ", create_on_client=" << create_on_client << std::endl;

	if (!create_on_client) {
		int server_class_bits = Q_log2(num_server_classes) + 1;
		for (int i = 0; i < num_server_classes; i++) {
			class_t server_class;
			server_class.classID = reader.read_bits(server_class_bits);
			server_class.class_name = reader.read_ascii_string(256);
			server_class.data_table_name = reader.read_ascii_string(256);
			server_classes.push_back(server_class);
			std::cout << "  ClassID: " << server_class.classID
				<< ", ClassName: " << server_class.class_name
				<< ", DataTableName: " << server_class.data_table_name << std::endl;
		}
	}
}

void SvcSetPause::parse(BitReader& reader) {
	paused = reader.read_bit();
	std::cout << "SvcSetPause: paused=" << paused << std::endl;
}

void SvcCreateStringTable::parse(BitReader& reader)
{
	table_name = reader.read_ascii_string();
	max_entries = reader.read_uint16();
	int encode_bits = Q_log2(max_entries);
	num_entries = reader.read_bits(encode_bits + 1);
	length = reader.read_var_int32(); // maybe just read_bits(20) ?
	user_data_fixed_size = reader.read_bool();
	if (user_data_fixed_size) {
		user_data_size = reader.read_bits(12);
		user_data_size_bits = reader.read_bits(4);
	}
	else {
		user_data_size = 0;
		user_data_size_bits = 0;
	}
	data_compressed = reader.read_bool();
	data = reader.read_many_bits(length);

	std::cout << "SvcCreateStringTable: table_name=" << table_name
		<< ", max_entries=" << max_entries
		<< ", num_entries=" << num_entries
		<< ", length=" << length
		<< ", user_data_fixed_size=" << user_data_fixed_size
		<< ", user_data_size=" << user_data_size
		<< ", user_data_size_bits=" << user_data_size_bits
		<< ", data_compressed=" << data_compressed << std::endl;
}

void SvcUpdateStringTable::parse(BitReader& reader)
{
	constexpr auto MAX_TABLES = 32;
	table_id = reader.read_bits(Q_log2(MAX_TABLES));
	if (reader.read_bit()) {
		num_changed_entries = reader.read_uint16();
	}
	else {
		num_changed_entries = 1;
	}

	length = reader.read_bits(20);
	data = reader.read_many_bits(length);

	std::cout << "SvcUpdateStringTable: table_id=" << table_id
		<< ", num_changed_entries=" << num_changed_entries
		<< ", length=" << length << " bits" << std::endl;
}

void SvcVoiceInit::parse(BitReader& reader)
{
	codec = reader.read_ascii_string();
	legacy_quality = reader.read_uint8();
	if (legacy_quality == 255) {
		sample_rate = reader.read_short();
	}

	std::cout << "SvcVoiceInit: codec=" << codec
		<< ", legacy_quality=" << static_cast<int>(legacy_quality)
		<< ", sample_rate=" << sample_rate << std::endl;
}

void SvcVoiceData::parse(BitReader& reader)
{
	from_client = reader.read_bool();
	proximity = reader.read_bool();
	length = reader.read_uint16();
	data = reader.read_many_bits(length);

	std::cout << "SvcVoiceData: from_client=" << from_client
		<< ", proximity=" << proximity
		<< ", length=" << length << " bits" << std::endl;
}

void SvcSounds::parse(BitReader& reader)
{
	reliable_sound = reader.read_bool();
	if (reliable_sound) {
		num_sounds = 1;
		length = reader.read_bits(8);
	}
	else {
		num_sounds = reader.read_bits(8);
		length = reader.read_bits(16);
	}
	data = reader.read_many_bits(length);

	std::cout << "SvcSounds: reliable_sound=" << reliable_sound
		<< ", num_sounds=" << num_sounds
		<< ", length=" << length << " bits" << std::endl;
}

void SvcSetView::parse(BitReader& reader)
{
	entity_index = reader.read_bits(11);

	std::cout << "SvcSetView: entity_index=" << entity_index << std::endl;
}

void SvcFixAngle::parse(BitReader& reader)
{
	relative = reader.read_bit();
	angle.x = reader.read_bit_angle(16);
	angle.y = reader.read_bit_angle(16);
	angle.z = reader.read_bit_angle(16);

	std::cout << "SvcFixAngle: relative=" << relative
		<< ", angle.x=" << angle.x
		<< ", angle.y=" << angle.y
		<< ", angle.z=" << angle.z << std::endl;
}

void SvcCrosshairAngle::parse(BitReader& reader)
{
	angle.x = reader.read_bit_angle(16);
	angle.y = reader.read_bit_angle(16);
	angle.z = reader.read_bit_angle(16);

	std::cout << "SvcCrosshairAngle: angle.x=" << angle.x
		<< ", angle.y=" << angle.y
		<< ", angle.z=" << angle.z << std::endl;
}

void SvcBSPDecal::parse(BitReader& reader)
{
	pos = reader.read_bit_vec3_coord();
	decal_texture_index = reader.read_bits(9);
	if (reader.read_bool()) {
		entity_index = reader.read_bits(11);
		model_index = reader.read_bits(13);
	}
	else {
		entity_index = 0;
		model_index = 0;
	}
	low_priority = reader.read_bool();

	std::cout << "SvcBSPDecal: pos=(" << pos.x << ", " << pos.y << ", " << pos.z << ")"
		<< ", decal_texture_index=" << decal_texture_index
		<< ", entity_index=" << entity_index
		<< ", model_index=" << model_index
		<< ", low_priority=" << low_priority << std::endl;
}

void SvcUserMessage::parse(BitReader& reader)
{
	msg_type = reader.read_uint8();
	length = reader.read_bits(11);
	data = reader.read_many_bits(length);

	std::cout << "SvcUserMessage: msg_type=" << static_cast<int>(msg_type)
		<< ", length=" << length << " bits" << std::endl;
}

void SvcEntityMessage::parse(BitReader& reader)
{
	entity_index = reader.read_bits(11);
	class_id = reader.read_bits(9);
	length = reader.read_bits(11);
	data = reader.read_many_bits(length);

	std::cout << "SvcEntityMessage: entity_index=" << entity_index
		<< ", class_id=" << class_id
		<< ", length=" << length << " bits" << std::endl;
}

void SvcGameEvent::parse(BitReader& reader)
{
	length = reader.read_bits(11);
	data = reader.read_many_bits(length);

	std::cout << "SvcGameEvent: length=" << length << " bits" << std::endl;
}

void SvcPacketEntities::parse(BitReader& reader)
{
	max_entries = reader.read_bits(11);
	is_delta = reader.read_bit();
	if (is_delta) {
		delta_from = reader.read_int32();
	}
	else {
		delta_from = -1;
	}

	baseline = reader.read_bit();
	updated_entries = reader.read_bits(11);
	length = reader.read_bits(20);
	update_baseline = reader.read_bit();
	data = reader.read_many_bits(length);

	std::cout << "SvcPacketEntities: max_entries=" << max_entries
		<< ", is_delta=" << is_delta
		<< ", delta_from=" << delta_from
		<< ", baseline=" << baseline
		<< ", updated_entries=" << updated_entries
		<< ", length=" << length
		<< ", update_baseline=" << update_baseline << std::endl;
}

void SvcTempEntities::parse(BitReader& reader)
{
	num_entries = reader.read_bits(8);
	length = reader.read_var_int32(); // maybe just 17??
	data = reader.read_many_bits(length);

	std::cout << "SvcTempEntities: num_entries=" << num_entries
		<< ", length=" << length << " bits" << std::endl;
}

void SvcPrefetch::parse(BitReader& reader)
{
	sound_index = reader.read_bits(14);
	std::cout << "SvcPrefetch: sound_index=" << sound_index << std::endl;
}

void SvcMenu::parse(BitReader& reader)
{
	menu_type = reader.read_int16();
	length = reader.read_uint16();
	std::cout << "SvcMenu: menu_type=" << menu_type
		<< ", length=" << length << std::endl;
}

void SvcGameEventList::parse(BitReader& reader)
{
	events = reader.read_bits(9);
	length = reader.read_bits(20);
	data = reader.read_many_bits(length);
	std::cout << "SvcGameEventList: events=" << events
		<< ", length=" << length << " bits" << std::endl;
}

void SvcGetCvarValue::parse(BitReader& reader)
{
	cookie = reader.read_int32();
	cvar_name = reader.read_ascii_string();
	std::cout << "SvcGetCvarValue: cookie=" << cookie
		<< ", cvar_name=" << cvar_name << std::endl;
}

void SvcCmdKeyValues::parse(BitReader& reader) {
	length = reader.read_uint32();
	if (length <= 0 || length > reader.bits_left() / 8) {
		return;
		throw std::runtime_error(
			"SvcCmdKeyValues::parse tried to read past end of buffer. Length: "
			+ std::to_string(length)
			+ ", Bits left: "
			+ std::to_string(reader.bits_left())
			+ " (bytes left: "
			+ std::to_string(reader.bits_left() / 8)
			+ ")."
		);
	}
	data = reader.read_bytes(length);
	std::cout << "SvcCmdKeyValues: length=" << length << " bits" << std::endl;
}

void SvcSetPauseTimed::parse(BitReader& reader)
{
	paused = reader.read_bool();
	expire_time = reader.read_float32();
	std::cout << "SvcSetPauseTimed: paused=" << paused
            << ", expire_time=" << expire_time << std::endl;
}
