#pragma once
#include "cssdemo/Protocol/Types.h"
#include "cssdemo/Util/ByteReader.h"
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace cssdemo {

// demoheader_t
struct DemoHeader {
	std::string file_stamp;
	int demo_protocol{};
	int network_protocol{};
	std::string server_name;
	std::string client_name;
	std::string map_name;
	std::string game_directory;
	float playback_time{};
	int playback_ticks{};
	int playback_frames{};
	int signon_length{};
};

enum class DemoCommand : uint8_t {
	SignOn = 1,
	Packet,
	SyncTick,
	ConsoleCmd,
	UserCmd,
	DataTables,
	Stop,
	StringTables,
};

const char* demo_command_name(DemoCommand command);

struct DemoFrame {
	DemoCommand command{};
	int tick{};

	// SignOn / Packet
	CmdInfo cmd_info{};
	int in_sequence{};
	int out_sequence{};

	// ConsoleCmd
	std::string console_command;

	// UserCmd
	int user_cmd_sequence{};

	// packet: net messages, usercmd: CUserCmd delta, tables: raw table data
	// points into the DemoFile's buffer
	std::span<const std::byte> payload;

	bool is_packet() const { return command == DemoCommand::SignOn || command == DemoCommand::Packet; }
};

class DemoFile {
public:
	explicit DemoFile(const std::string& path);
	explicit DemoFile(std::vector<std::byte> contents);
	DemoFile(const DemoFile&) = delete; // frames point into the buffer
	DemoFile& operator=(const DemoFile&) = delete;

	const DemoHeader& header() const { return demo_header; }

	// false after the stop frame or at eof
	bool next_frame(DemoFrame& frame);

	bool truncated() const { return was_truncated; }

private:
	std::vector<std::byte> data;
	ByteReader reader;
	DemoHeader demo_header;
	bool finished = false;
	bool was_truncated = false;

	void parse_header();
	std::span<const std::byte> read_raw_data();
};

}
