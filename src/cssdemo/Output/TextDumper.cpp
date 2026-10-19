#include "TextDumper.h"
#include "cssdemo/Game/Sounds.h"
#include "cssdemo/Game/TempEntities.h"
#include "cssdemo/Game/UserCmd.h"
#include "cssdemo/Game/UserMessages.h"
#include "cssdemo/Net/NetMessages.h"
#include <cmath>
#include <iomanip>

namespace cssdemo {

namespace {

const std::string line_break = "=======================================\n";

} // namespace

TextDumper::TextDumper(std::ostream& output, const GameState& game_state, DumpOptions dump_options)
	: out(output), options(dump_options), state(game_state)
{
}

void TextDumper::on_header(const DemoHeader& header)
{
	out << std::left << "HEADER\n"
		<< std::setw(20) << "Demo File Stamp: " << header.file_stamp << "\n"
		<< std::setw(20) << "Demo Protocol: " << header.demo_protocol << "\n"
		<< std::setw(20) << "Network Protocol: " << header.network_protocol << "\n"
		<< std::setw(20) << "Server Name: " << header.server_name << "\n"
		<< std::setw(20) << "Client Name: " << header.client_name << "\n"
		<< std::setw(20) << "Map Name: " << header.map_name << "\n"
		<< std::setw(20) << "Game Directory: " << header.game_directory << "\n"
		<< std::setw(20) << "Playback Time: " << header.playback_time << "\n"
		<< std::setw(20) << "Playback Ticks: " << header.playback_ticks << "\n"
		<< std::setw(20) << "Playback Frames: " << header.playback_frames << "\n"
		<< std::setw(20) << "Signon Length: " << header.signon_length << "\n"
		<< line_break << std::endl;

	if (options.messages) {
		out << "MESSAGES\n";
	}
}

void TextDumper::on_frame(const DemoFrame& frame)
{
	if (!options.messages) return;

	out << "[tick " << frame.tick << "] " << demo_command_name(frame.command);
	switch (frame.command) {
	case DemoCommand::SignOn:
	case DemoCommand::Packet:
		out << ": in_sequence=" << frame.in_sequence << ", out_sequence=" << frame.out_sequence;
		break;
	case DemoCommand::ConsoleCmd:
		out << ": " << frame.console_command;
		break;
	case DemoCommand::UserCmd:
		out << ": sequence=" << frame.user_cmd_sequence;
		break;
	case DemoCommand::DataTables:
	case DemoCommand::StringTables:
		out << ": " << frame.payload.size() << " bytes";
		break;
	default:
		break;
	}
	out << "\n";
}

void TextDumper::on_net_message(const NetMessage& message)
{
	if (!options.messages) return;
	out << "    ";
	message.print(out);
	out << "\n";
}

void TextDumper::on_user_cmd(const UserCmd& cmd)
{
	if (!options.messages || !options.user_cmds) return;
	out << "    " << cmd << "\n";
}

void TextDumper::on_string_table_change(const StringTableChange& change)
{
	if (!options.messages) return;
	if (change.table->name != "userinfo") return;

	for (int index : change.entries) {
		auto info = parse_player_info(change.table->entries[index].data);
		if (!info) continue;
		out << "        player " << index + 1 << ": " << info->name << " (userid " << info->user_id
			<< ", " << info->guid << ")\n";
	}
}

void TextDumper::on_game_event(const GameEvent& event)
{
	if (!options.messages) return;
	out << "        " << event << "\n";
}

void TextDumper::on_user_message(const UserMessage& message)
{
	if (!options.messages) return;
	if (!message.decoded && message.error.empty()) {
		out << "        " << message.name << "\n";
		return;
	}
	out << "        " << message << "\n";
}

void TextDumper::on_entity_update(const EntityUpdate& update)
{
	if (!options.messages || !options.entities) return;

	out << "        " << entity_update_kind_name(update.kind) << " #" << update.index;
	if (update.server_class) {
		out << " " << update.server_class->class_name;
	}
	if (update.kind == EntityUpdate::Kind::Enter) {
		out << " serial=" << update.serial;
	}
	out << "\n";
	if (update.server_class) {
		dump_props(*update.server_class, update.changes, "            ");
	}
}

void TextDumper::on_temp_entity(const TempEntity& event)
{
	if (!options.messages || !options.temp_entities) return;
	out << "        TempEntity " << event.server_class->class_name;
	if (event.delay != 0.0f) out << " delay=" << event.delay;
	out << "\n";
	dump_props(*event.server_class, event.changes, "            ");
}

void TextDumper::on_sound(const SoundInfo& sound)
{
	if (!options.messages || !options.sounds) return;

	out << "        Sound #" << sound.sound_num;
	if (const StringTable* precache = state.string_tables.find("soundprecache");
		precache && sound.sound_num < static_cast<int>(precache->entries.size())) {
		out << " " << precache->entries[sound.sound_num].name;
	}
	out << " entity=" << sound.entity_index << " channel=" << sound.channel;
	if (sound.is_stop()) {
		out << " stop\n";
		return;
	}
	out << " volume=" << sound.volume << " level=" << sound.sound_level << " pitch=" << sound.pitch
		<< " origin=(" << sound.origin.x << ", " << sound.origin.y << ", " << sound.origin.z << ")\n";
}

void TextDumper::on_error(int tick, const std::string& message)
{
	errors.push_back({ tick, message });
	if (options.messages) {
		out << "    ERROR: " << message << "\n";
	}
}

void TextDumper::on_finish(const GameState& final_state)
{
	if (options.messages) {
		out << line_break << std::endl;
	}
	dump_server(final_state);
	dump_players(final_state);
	if (options.data_tables) dump_data_tables(final_state);
	if (options.string_tables) dump_string_tables(final_state);
	dump_errors();
}

void TextDumper::dump_server(const GameState& s)
{
	out << std::left << "SERVER\n"
		<< std::setw(20) << "Host Name: " << s.server.host_name << "\n"
		<< std::setw(20) << "Map: " << s.server.map_name << "\n"
		<< std::setw(20) << "Game Directory: " << s.server.game_dir << "\n"
		<< std::setw(20) << "Dedicated: " << s.server.is_dedicated << "\n"
		<< std::setw(20) << "HLTV: " << s.server.is_hltv << "\n"
		<< std::setw(20) << "OS: " << s.server.os << "\n"
		<< std::setw(20) << "Max Clients: " << s.server.max_clients << "\n"
		<< std::setw(20) << "Tick Interval: " << s.server.tick_interval;
	if (s.server.tick_interval > 0) {
		out << " (" << std::lround(1.0f / s.server.tick_interval) << " tick)";
	}
	out << "\n"
		<< std::setw(20) << "Server Classes: " << s.data_tables.classes().size() << "\n"
		<< std::setw(20) << "Send Tables: " << s.data_tables.tables().size() << "\n"
		<< std::setw(20) << "String Tables: " << s.string_tables.all().size() << "\n"
		<< std::setw(20) << "Game Events: " << s.game_events.all().size() << "\n"
		<< line_break << std::endl;
}

void TextDumper::dump_players(const GameState& s)
{
	out << "PLAYERS\n";
	for (const auto& player : s.players()) {
		const auto& info = player.info;
		out << "  entity=" << std::setw(4) << player.entity_index
			<< " userid=" << std::setw(5) << info.user_id
			<< " name=" << info.name
			<< ", guid=" << info.guid;
		if (info.fake_player) out << ", bot";
		if (info.is_hltv) out << ", hltv";
		if (info.is_replay) out << ", replay";
		out << "\n";
	}
	out << line_break << std::endl;
}

void TextDumper::dump_data_tables(const GameState& s)
{
	out << "SERVER CLASSES\n";
	for (const auto& server_class : s.data_tables.classes()) {
		out << "  [" << server_class.class_id << "] " << server_class.class_name
			<< " (" << server_class.data_table_name << ", " << server_class.flattened_props.size() << " props)\n";
		for (size_t i = 0; i < server_class.flattened_props.size(); i++) {
			const auto& flat = server_class.flattened_props[i];
			out << "      " << std::setw(4) << i << " " << flat.name
				<< " " << send_prop_type_name(flat.prop->type)
				<< " bits=" << flat.prop->num_bits
				<< " flags=0x" << std::hex << flat.prop->flags << std::dec << "\n";
		}
	}
	out << line_break << std::endl;
}

void TextDumper::dump_string_tables(const GameState& s)
{
	out << "STRING TABLES\n";
	for (const auto& table : s.string_tables.all()) {
		out << "  [" << table.id << "] " << table.name << " (" << table.entries.size() << " entries)\n";
		for (size_t i = 0; i < table.entries.size(); i++) {
			const auto& entry = table.entries[i];
			out << "      " << std::setw(5) << i << " " << entry.name;
			if (!entry.data.empty()) {
				out << " (" << entry.data.size() << " bytes)";
			}
			out << "\n";
		}
	}
	out << line_break << std::endl;
}

void TextDumper::dump_errors()
{
	if (errors.empty()) return;
	out << "ERRORS (" << errors.size() << ")\n";
	for (const auto& error : errors) {
		out << "  [tick " << error.tick << "] " << error.message << "\n";
	}
	out << line_break << std::endl;
}

void TextDumper::dump_props(const ServerClass& server_class, const std::vector<PropChange>& changes, const char* indent)
{
	for (const auto& change : changes) {
		out << indent << server_class.flattened_props[change.index].name << " = " << change.value << "\n";
	}
}

}
