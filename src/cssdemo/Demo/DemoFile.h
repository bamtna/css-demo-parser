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
	// a stop command at the very end with fewer than 4 bytes of tick, the frame has what's there
	bool stop_without_tick() const { return short_stop; }
	int stop_tick_bytes() const { return short_stop_bytes; }

private:
	std::vector<std::byte> data;
	ByteReader reader;
	DemoHeader demo_header;
	bool finished = false;
	bool was_truncated = false;
	bool short_stop = false;
	int short_stop_bytes = 0;

	void parse_header();
	std::span<const std::byte> read_raw_data();
};

// CDemoFile / CDemoRecorder writing side, frames are buffered until finish
class DemoWriter {
public:
	explicit DemoWriter(DemoHeader header = {});

	DemoHeader& header() { return demo_header; }

	// frame.payload (or console_command) is copied
	void write_frame(const DemoFrame& frame);
	void write_packet(DemoCommand command, int tick, const CmdInfo& cmd_info, int in_sequence, int out_sequence,
		std::span<const std::byte> payload);
	void write_user_cmd(int tick, int sequence, std::span<const std::byte> payload);
	void write_console_cmd(int tick, const std::string& command);
	void write_data(DemoCommand command, int tick, std::span<const std::byte> payload); // data/string tables
	void write_sync_tick(int tick);
	void write_stop(int tick);
	// some recordings end on a stop byte and part of its tick
	void write_stop_without_tick(int tick = 0, int tick_bytes = 0);

	// CDemoRecorder::CloseDemoFile, plus the signon length
	void update_header(float tick_interval);

	size_t frame_count() const { return frames; }
	std::vector<std::byte> finish() const;
	void save(const std::string& path) const;

private:
	DemoHeader demo_header;
	std::vector<std::byte> body;
	size_t frames = 0;
	size_t packets = 0;
	int last_tick = 0;
	size_t signon_bytes = 0;
	bool in_signon = true;

	void command_header(DemoCommand command, int tick);
	void put_int(int32_t value);
	void put_bytes(std::span<const std::byte> bytes);
	void put_raw(std::span<const std::byte> bytes);
};

std::vector<std::byte> write_demo_header(const DemoHeader& header);

}
