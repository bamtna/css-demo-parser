#include "DemoFile.h"
#include "cssdemo/Protocol/Constants.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace cssdemo {

const char* demo_command_name(DemoCommand command)
{
	switch (command) {
	case DemoCommand::SignOn: return "SignOn";
	case DemoCommand::Packet: return "Packet";
	case DemoCommand::SyncTick: return "SyncTick";
	case DemoCommand::ConsoleCmd: return "ConsoleCmd";
	case DemoCommand::UserCmd: return "UserCmd";
	case DemoCommand::DataTables: return "DataTables";
	case DemoCommand::Stop: return "Stop";
	case DemoCommand::StringTables: return "StringTables";
	}
	return "Unknown";
}

static std::vector<std::byte> read_file(const std::string& path)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) {
		throw std::runtime_error("Error opening file: " + path);
	}
	std::vector<std::byte> contents(static_cast<size_t>(file.tellg()));
	file.seekg(0);
	if (!file.read(reinterpret_cast<char*>(contents.data()), contents.size())) {
		throw std::runtime_error("Error reading file: " + path);
	}
	return contents;
}

DemoFile::DemoFile(const std::string& path)
	: DemoFile(read_file(path))
{
}

DemoFile::DemoFile(std::vector<std::byte> contents)
	: data(std::move(contents)), reader(data)
{
	parse_header();
}

void DemoFile::parse_header()
{
	auto& h = demo_header;
	h.file_stamp = reader.read_fixed_string(8);
	if (h.file_stamp != DEMO_HEADER_ID) {
		throw std::runtime_error("Not a Source engine demo (file stamp \"" + h.file_stamp + "\")");
	}
	h.demo_protocol = reader.read_int32();
	h.network_protocol = reader.read_int32();
	h.server_name = reader.read_fixed_string(MAX_OSPATH);
	h.client_name = reader.read_fixed_string(MAX_OSPATH);
	h.map_name = reader.read_fixed_string(MAX_OSPATH);
	h.game_directory = reader.read_fixed_string(MAX_OSPATH);
	h.playback_time = reader.read_float32();
	h.playback_ticks = reader.read_int32();
	h.playback_frames = reader.read_int32();
	h.signon_length = reader.read_int32();

	if (h.demo_protocol != DEMO_PROTOCOL) {
		throw std::runtime_error("Unsupported demo protocol " + std::to_string(h.demo_protocol)
			+ " (expected " + std::to_string(DEMO_PROTOCOL) + ")");
	}
	if (h.network_protocol < PROTOCOL_VERSION_MIN_SUPPORTED) {
		throw std::runtime_error("Unsupported network protocol " + std::to_string(h.network_protocol));
	}
}

std::span<const std::byte> DemoFile::read_raw_data()
{
	int32_t length = reader.read_int32();
	if (length < 0) {
		throw std::runtime_error("Negative data length in demo frame");
	}
	return reader.read_span(static_cast<size_t>(length));
}

bool DemoFile::next_frame(DemoFrame& frame)
{
	if (finished) {
		return false;
	}

	// cut off recordings end mid frame
	constexpr size_t FRAME_HEADER_SIZE = 5;
	if (reader.remaining() < FRAME_HEADER_SIZE) {
		// some files end with a stop command and part or none of its tick
		bool stop_without_tick = reader.remaining() > 0 && reader.read_uint8() == static_cast<uint8_t>(DemoCommand::Stop);
		finished = true;
		was_truncated = !stop_without_tick;
		short_stop = stop_without_tick;
		if (stop_without_tick) {
			frame = DemoFrame{};
			frame.command = DemoCommand::Stop;
			short_stop_bytes = static_cast<int>(reader.remaining());
			for (int i = 0; i < short_stop_bytes; i++) {
				frame.tick |= reader.read_uint8() << (8 * i);
			}
			return true;
		}
		return false;
	}

	frame = DemoFrame{};
	uint8_t command = reader.read_uint8();
	frame.tick = reader.read_int32();
	if (command < static_cast<uint8_t>(DemoCommand::SignOn) || command > static_cast<uint8_t>(DemoCommand::StringTables)) {
		throw std::runtime_error("Unexpected demo command " + std::to_string(command)
			+ " at offset " + std::to_string(reader.position() - FRAME_HEADER_SIZE));
	}
	frame.command = static_cast<DemoCommand>(command);

	try {
		switch (frame.command) {
		case DemoCommand::SignOn:
		case DemoCommand::Packet:
			frame.cmd_info = reader.read_struct<CmdInfo>();
			frame.in_sequence = reader.read_int32();
			frame.out_sequence = reader.read_int32();
			frame.payload = read_raw_data();
			break;
		case DemoCommand::ConsoleCmd: {
			auto raw = read_raw_data();
			std::string text(reinterpret_cast<const char*>(raw.data()), raw.size());
			frame.console_command = text.c_str();
			break;
		}
		case DemoCommand::UserCmd:
			frame.user_cmd_sequence = reader.read_int32();
			frame.payload = read_raw_data();
			break;
		case DemoCommand::DataTables:
		case DemoCommand::StringTables:
			frame.payload = read_raw_data();
			break;
		case DemoCommand::SyncTick:
			break;
		case DemoCommand::Stop:
			finished = true;
			break;
		}
	}
	catch (const std::out_of_range&) {
		finished = true;
		was_truncated = true;
		return false;
	}
	return true;
}

std::vector<std::byte> write_demo_header(const DemoHeader& h)
{
	std::vector<std::byte> out;
	auto put = [&](const void* data, size_t size) {
		auto p = static_cast<const std::byte*>(data);
		out.insert(out.end(), p, p + size);
	};
	auto put_string = [&](const std::string& s, size_t length) {
		std::vector<char> buffer(length, '\0');
		std::memcpy(buffer.data(), s.data(), std::min(s.size(), length - 1));
		put(buffer.data(), length);
	};
	auto put_int = [&](int32_t v) { put(&v, sizeof(v)); };

	put_string(h.file_stamp.empty() ? DEMO_HEADER_ID : h.file_stamp, 8);
	put_int(h.demo_protocol);
	put_int(h.network_protocol);
	put_string(h.server_name, MAX_OSPATH);
	put_string(h.client_name, MAX_OSPATH);
	put_string(h.map_name, MAX_OSPATH);
	put_string(h.game_directory, MAX_OSPATH);
	put(&h.playback_time, sizeof(h.playback_time));
	put_int(h.playback_ticks);
	put_int(h.playback_frames);
	put_int(h.signon_length);
	return out;
}

DemoWriter::DemoWriter(DemoHeader header)
	: demo_header(std::move(header))
{
	if (demo_header.file_stamp.empty()) demo_header.file_stamp = DEMO_HEADER_ID;
	if (!demo_header.demo_protocol) demo_header.demo_protocol = DEMO_PROTOCOL;
	if (!demo_header.network_protocol) demo_header.network_protocol = PROTOCOL_VERSION;
}

void DemoWriter::put_int(int32_t value)
{
	put_bytes({ reinterpret_cast<const std::byte*>(&value), sizeof(value) });
}

void DemoWriter::put_bytes(std::span<const std::byte> bytes)
{
	body.insert(body.end(), bytes.begin(), bytes.end());
}

void DemoWriter::put_raw(std::span<const std::byte> bytes)
{
	put_int(static_cast<int32_t>(bytes.size()));
	put_bytes(bytes);
}

void DemoWriter::command_header(DemoCommand command, int tick)
{
	// sourcetv has its string tables in the signon
	if (in_signon && command != DemoCommand::SignOn && command != DemoCommand::DataTables
		&& command != DemoCommand::StringTables) {
		in_signon = false;
		signon_bytes = body.size();
	}
	body.push_back(static_cast<std::byte>(command));
	put_int(tick);
	frames++;
	if (command == DemoCommand::Packet) packets++;
	last_tick = tick;
}

void DemoWriter::write_packet(DemoCommand command, int tick, const CmdInfo& cmd_info, int in_sequence, int out_sequence,
	std::span<const std::byte> payload)
{
	command_header(command, tick);
	put_bytes({ reinterpret_cast<const std::byte*>(&cmd_info), sizeof(cmd_info) });
	put_int(in_sequence);
	put_int(out_sequence);
	put_raw(payload);
}

void DemoWriter::write_user_cmd(int tick, int sequence, std::span<const std::byte> payload)
{
	command_header(DemoCommand::UserCmd, tick);
	put_int(sequence);
	put_raw(payload);
}

void DemoWriter::write_console_cmd(int tick, const std::string& command)
{
	command_header(DemoCommand::ConsoleCmd, tick);
	std::string text = command.c_str();
	put_int(static_cast<int32_t>(text.size() + 1));
	put_bytes({ reinterpret_cast<const std::byte*>(text.c_str()), text.size() + 1 });
}

void DemoWriter::write_data(DemoCommand command, int tick, std::span<const std::byte> payload)
{
	command_header(command, tick);
	put_raw(payload);
}

void DemoWriter::write_sync_tick(int tick)
{
	command_header(DemoCommand::SyncTick, tick);
}

void DemoWriter::write_stop(int tick)
{
	command_header(DemoCommand::Stop, tick);
}

void DemoWriter::write_stop_without_tick(int tick, int tick_bytes)
{
	if (in_signon) {
		in_signon = false;
		signon_bytes = body.size();
	}
	body.push_back(static_cast<std::byte>(DemoCommand::Stop));
	for (int i = 0; i < tick_bytes && i < 4; i++) {
		body.push_back(static_cast<std::byte>(tick >> (8 * i)));
	}
	frames++;
	last_tick = tick;
}

void DemoWriter::write_frame(const DemoFrame& frame)
{
	switch (frame.command) {
	case DemoCommand::SignOn:
	case DemoCommand::Packet:
		write_packet(frame.command, frame.tick, frame.cmd_info, frame.in_sequence, frame.out_sequence, frame.payload);
		break;
	case DemoCommand::ConsoleCmd:
		write_console_cmd(frame.tick, frame.console_command);
		break;
	case DemoCommand::UserCmd:
		write_user_cmd(frame.tick, frame.user_cmd_sequence, frame.payload);
		break;
	case DemoCommand::DataTables:
	case DemoCommand::StringTables:
		write_data(frame.command, frame.tick, frame.payload);
		break;
	case DemoCommand::SyncTick:
		write_sync_tick(frame.tick);
		break;
	case DemoCommand::Stop:
		write_stop(frame.tick);
		break;
	}
}

void DemoWriter::update_header(float tick_interval)
{
	demo_header.playback_ticks = last_tick;
	demo_header.playback_time = tick_interval * static_cast<float>(last_tick);
	demo_header.playback_frames = static_cast<int>(packets);
	demo_header.signon_length = static_cast<int>(in_signon ? body.size() : signon_bytes);
}

std::vector<std::byte> DemoWriter::finish() const
{
	std::vector<std::byte> out = write_demo_header(demo_header);
	out.insert(out.end(), body.begin(), body.end());
	return out;
}

void DemoWriter::save(const std::string& path) const
{
	std::vector<std::byte> out = finish();
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (!file || !file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()))) {
		throw std::runtime_error("Error writing file: " + path);
	}
}

}
