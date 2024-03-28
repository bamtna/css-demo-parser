#include "Dumper.h"
#include "Demo/Demo.h"

bool Dumper::open(const std::string& output_file_path) const {
    file.open(output_file_path, std::ios::out);
    if (!file) {
        std::cerr << "Failed to open output file: " << output_file_path << std::endl;
        return false;
    }
    return true;
}

void Dumper::dump_header() const {
    if (!file.is_open()) {
        std::cerr << "Output file is not open. Cannot dump header." << std::endl;
        return;
    }

    const auto& header = demo.header;
    file << std::left << "HEADER\n"
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
}

void Dumper::dump_messages() const {
    for (const auto& msg : demo.messages) {
        switch (msg->type)
        {
        case DemoMessage::Type::SIGN_ON:
        case DemoMessage::Type::PACKET:
            break;
        case DemoMessage::Type::CONSOLE_CMD:
            break;

        default:
            break;
        }
    }
}

void Dumper::close() const {
    if (file.is_open()) {
        file.close();
    }
}
