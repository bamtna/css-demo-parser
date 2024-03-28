#include "Demo/Demo.h"
#include "Demo/DemoMessage.h"
#include "Util/BinaryReader.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include <stdexcept> 

void Demo::load(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Error opening file: " + file_path);
    }

    BinaryReader reader(file);

    parse_header(reader);

    if (!supported_demo_protocol()) {
        throw std::runtime_error("Unsupported demo protocol: " + std::to_string(header.demo_protocol));
    }
    if (!supported_network_protocol()) {
        throw std::runtime_error("Unsupported network protocol: " + std::to_string(header.network_protocol));
    }

    parse_messages(reader);

    std::cout << "Parsed " << messages.size() << " messages.\n";
}

bool Demo::supported_network_protocol()
{
    return header.network_protocol == DEMO_NETWORK_PROTOCOL;
}

bool Demo::supported_demo_protocol()
{
    return header.demo_protocol == DEMO_PROTOCOL;
}

void Demo::parse_header(BinaryReader& reader) {
    header.file_stamp = reader.read_string(8);
    if (header.file_stamp != DEMO_FILE_STAMP) {
        throw std::runtime_error("Invalid demo file stamp: " + header.file_stamp);
    }
    header.demo_protocol = reader.read_int32();
    header.network_protocol = reader.read_int32();
    header.server_name = reader.read_string(DEMO_MAXPATH);
    header.client_name = reader.read_string(DEMO_MAXPATH);
    header.map_name = reader.read_string(DEMO_MAXPATH);
    header.game_directory = reader.read_string(DEMO_MAXPATH);
    header.playback_time = reader.read_float32();
    header.playback_ticks = reader.read_int32();
    header.playback_frames = reader.read_int32();
    header.signon_length = reader.read_int32();
}

void Demo::parse_messages(BinaryReader& reader) {
    while (!reader.eof()) {
        auto type = static_cast<DemoMessage::Type>(reader.read_byte());
        auto tick = reader.read_int32();

        std::unique_ptr<DemoMessage> message = create_message(type, tick);
        message->parse(reader);
        messages.push_back(std::move(message));
    }
}

std::unique_ptr<DemoMessage> Demo::create_message(DemoMessage::Type type, int tick) {
    switch (type) {
    case DemoMessage::Type::SIGN_ON:
        return std::make_unique<SignOn>(tick);
    case DemoMessage::Type::PACKET:
        return std::make_unique<Packet>(tick);
    case DemoMessage::Type::SYNC_TICK:
        return std::make_unique<SyncTick>(tick);
    case DemoMessage::Type::CONSOLE_CMD:
        return std::make_unique<ConsoleCmd>(tick);
    case DemoMessage::Type::USER_CMD:
        return std::make_unique<UserCmd>(tick);
    case DemoMessage::Type::DATA_TABLES:
        return std::make_unique<DataTable>(tick);
    case DemoMessage::Type::STRING_TABLES:
        return std::make_unique<StringTable>(tick);
    case DemoMessage::Type::STOP:
        return std::make_unique<Stop>(tick);
    default:
        throw std::runtime_error("create_message: Unhandled message type encountered: " + std::to_string(static_cast<int>(type)));
    }
}