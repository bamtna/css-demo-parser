#include "DemoFile.h"
#include "cssdemo/Protocol/Constants.h"
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
		// some files end with a stop command and no tick
		bool stop_without_tick = reader.remaining() > 0 && reader.read_uint8() == static_cast<uint8_t>(DemoCommand::Stop);
		finished = true;
		was_truncated = !stop_without_tick;
		if (stop_without_tick) {
			frame = DemoFrame{};
			frame.command = DemoCommand::Stop;
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

}
