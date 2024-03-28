#pragma once
#include <string>
#include <vector>
#include <memory>
#include "DemoMessage.h"

class BinaryReader;

constexpr auto DEMO_FILE_STAMP = "HL2DEMO";
constexpr auto DEMO_PROTOCOL = 3;
constexpr auto DEMO_NETWORK_PROTOCOL = 24;
constexpr auto DEMO_MAXPATH = 260;

struct DemoHeader {
	std::string file_stamp;
	int demo_protocol;
	int network_protocol;
	std::string server_name;
	std::string client_name;
	std::string map_name;
	std::string game_directory;
	float playback_time;
	int playback_ticks;
	int playback_frames;
	int signon_length;
};

class Demo {
public:
	DemoHeader header;
	std::vector<std::unique_ptr<DemoMessage>> messages;

    void load(const std::string& file_path);

private:
	bool supported_network_protocol();
	bool supported_demo_protocol();
	void parse_header(BinaryReader& reader);
	std::unique_ptr<DemoMessage> create_message(DemoMessage::Type type, int tick);
	void parse_messages(BinaryReader& reader);
};