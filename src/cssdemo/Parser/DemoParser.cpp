#include "DemoParser.h"
#include "cssdemo/Game/Sounds.h"
#include "cssdemo/Game/TempEntities.h"
#include "cssdemo/Game/UserCmd.h"
#include "cssdemo/Game/UserMessages.h"
#include "cssdemo/Net/NetMessages.h"
#include <charconv>
#include <stdexcept>

namespace cssdemo {

DemoParser::DemoParser(DemoFile& demo_file)
	: file(demo_file)
{
	game.protocol = { file.header().demo_protocol, file.header().network_protocol };
}

void DemoParser::add_listener(DemoListener& listener)
{
	listeners.push_back(&listener);
}

void DemoParser::run()
{
	while (step()) {
	}
}

bool DemoParser::step()
{
	if (finished) {
		return false;
	}
	if (!started) {
		started = true;
		notify([&](DemoListener& l) { l.on_header(file.header()); });
	}

	try {
		if (file.next_frame(current)) {
			handle_frame(current);
			return true;
		}
		if (file.truncated()) {
			error("demo ends without a stop frame");
		}
	}
	catch (const std::exception& e) {
		error(std::string("can't read the rest of the demo: ") + e.what());
	}

	finished = true;
	notify([&](DemoListener& l) { l.on_finish(game); });
	return false;
}

void DemoParser::handle_frame(const DemoFrame& frame)
{
	frame_tick = frame.tick;
	notify([&](DemoListener& l) { l.on_frame(frame); });

	try {
		switch (frame.command) {
		case DemoCommand::SignOn:
		case DemoCommand::Packet:
			handle_packet(frame);
			break;
		case DemoCommand::UserCmd: {
			UserCmd cmd = decode_user_cmd(frame.payload);
			notify([&](DemoListener& l) { l.on_user_cmd(cmd); });
			break;
		}
		case DemoCommand::DataTables:
			game.data_tables.parse(frame.payload);
			// entities point at the old classes
			game.entities.clear();
			if (const StringTable* baselines = game.string_tables.find("instancebaseline")) {
				std::vector<int> all(baselines->entries.size());
				for (size_t i = 0; i < all.size(); i++) all[i] = static_cast<int>(i);
				handle_string_table_change({ baselines, all });
			}
			entities_broken = false;
			notify([&](DemoListener& l) { l.on_data_tables(game); });
			break;
		case DemoCommand::StringTables:
			for (const auto& change : game.string_tables.apply_snapshot(frame.payload)) {
				handle_string_table_change(change);
			}
			break;
		default:
			break;
		}
	}
	catch (const std::exception& e) {
		error(std::string(demo_command_name(frame.command)) + ": " + e.what());
	}
}

void DemoParser::handle_packet(const DemoFrame& frame)
{
	ParsedPacket packet = parse_packet(frame.payload, game.protocol);
	for (const auto& message : packet.messages) {
		notify([&](DemoListener& l) { l.on_net_message(*message); });
		try {
			handle_net_message(*message);
		}
		catch (const std::exception& e) {
			error(e.what());
		}
	}
	if (!packet.error.empty()) {
		error("packet parse error: " + packet.error);
	}
	notify([&](DemoListener& l) { l.on_packet_end(frame, game); });
}

void DemoParser::handle_net_message(const NetMessage& message)
{
	using T = NetMessage::Type;
	switch (message.type) {
	case T::net_tick:
		game.tick = static_cast<const NetTick&>(message).tick;
		break;

	case T::svc_server_info: {
		const auto& info = static_cast<const SvcServerInfo&>(message);
		game.server = { info.protocol, info.is_hltv, info.is_dedicated, info.max_classes, info.player_slot,
			info.max_clients, info.tick_interval, info.os, info.game_dir, info.map_name, info.sky_name, info.host_name };
		game.has_server_info = true;
		break;
	}

	case T::svc_create_string_table:
		handle_string_table_change(game.string_tables.create(static_cast<const SvcCreateStringTable&>(message)));
		break;

	case T::svc_update_string_table:
		handle_string_table_change(game.string_tables.update(static_cast<const SvcUpdateStringTable&>(message)));
		break;

	case T::svc_game_event_list:
		game.game_events.load(static_cast<const SvcGameEventList&>(message));
		break;

	case T::svc_game_event: {
		GameEvent event = game.game_events.decode(static_cast<const SvcGameEvent&>(message));
		notify([&](DemoListener& l) { l.on_game_event(event); });
		break;
	}

	case T::svc_user_message: {
		UserMessage user_message = decode_user_message(static_cast<const SvcUserMessage&>(message));
		notify([&](DemoListener& l) { l.on_user_message(user_message); });
		if (!user_message.error.empty()) {
			error("user message " + user_message.name + ": " + user_message.error);
		}
		break;
	}

	case T::svc_packet_entities: {
		if (!decode_entities || entities_broken) {
			break;
		}
		if (game.data_tables.empty()) {
			entities_broken = true;
			throw std::runtime_error("packet entities before data tables, not decoding entities");
		}
		try {
			auto updates = game.entities.apply(static_cast<const SvcPacketEntities&>(message), game.data_tables);
			for (const auto& update : updates) {
				notify([&](DemoListener& l) { l.on_entity_update(update); });
			}
		}
		catch (const std::exception& e) {
			// every later delta depends on this one
			entities_broken = true;
			throw std::runtime_error(std::string("entity decoding failed, skipping the rest: ") + e.what());
		}
		break;
	}

	case T::svc_temp_entities: {
		if (game.data_tables.empty()) {
			break;
		}
		for (const auto& event : decode_temp_entities(static_cast<const SvcTempEntities&>(message), game.data_tables)) {
			notify([&](DemoListener& l) { l.on_temp_entity(event); });
		}
		break;
	}

	case T::svc_sounds:
		for (const auto& sound : decode_sounds(static_cast<const SvcSounds&>(message), game.protocol)) {
			notify([&](DemoListener& l) { l.on_sound(sound); });
		}
		break;

	default:
		break;
	}
}

void DemoParser::handle_string_table_change(const StringTableChange& change)
{
	if (change.table->name == "instancebaseline") {
		for (int index : change.entries) {
			const auto& entry = change.table->entries[index];
			int class_id = 0;
			auto result = std::from_chars(entry.name.data(), entry.name.data() + entry.name.size(), class_id);
			if (result.ec == std::errc()) {
				game.entities.set_instance_baseline(class_id, entry.data);
			}
		}
	}
	notify([&](DemoListener& l) { l.on_string_table_change(change); });
}

void DemoParser::error(const std::string& message)
{
	parse_errors.push_back({ frame_tick, message });
	notify([&](DemoListener& l) { l.on_error(frame_tick, message); });
}

}
