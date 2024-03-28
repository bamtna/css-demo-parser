#include "DemoMessage.h"
#include "Util//BinaryReader.h"
#include "Util/BitReader.h"
#include <stdexcept>
#include <memory>
#include <iostream>
#include <iomanip> 
#include <fstream>

std::unique_ptr<NetMessage> create_net_message(NetMessage::Type msg_type) {
    switch (msg_type) {
    case NetMessage::Type::net_nop:
        return std::make_unique<NetNop>();
    case NetMessage::Type::net_disconnect:
        return std::make_unique<NetDisconnect>();
    case NetMessage::Type::net_tick:
        return std::make_unique<NetTick>();
    case NetMessage::Type::net_string_cmd:
        return std::make_unique<NetStringCmd>();
    case NetMessage::Type::net_set_con_var:
        return std::make_unique<NetSetConVar>();
    case NetMessage::Type::net_signon_state:
        return std::make_unique<NetSignonState>();
    case NetMessage::Type::svc_print:
        return std::make_unique<SvcPrint>();
    case NetMessage::Type::svc_server_info:
        return std::make_unique<SvcServerInfo>();
    case NetMessage::Type::svc_send_table:
        return std::make_unique<SvcSendTable>();
    case NetMessage::Type::svc_class_info:
        return std::make_unique<SvcClassInfo>();
    case NetMessage::Type::svc_set_pause:
        return std::make_unique<SvcSetPause>();
    case NetMessage::Type::svc_create_string_table:
        return std::make_unique<SvcCreateStringTable>();
    case NetMessage::Type::svc_update_string_table:
        return std::make_unique<SvcUpdateStringTable>();
    case NetMessage::Type::svc_voice_init:
        return std::make_unique<SvcVoiceInit>();
    case NetMessage::Type::svc_voice_data:
        return std::make_unique<SvcVoiceData>();
    case NetMessage::Type::svc_sounds:
        return std::make_unique<SvcSounds>();
    case NetMessage::Type::svc_set_view:
        return std::make_unique<SvcSetView>();
    case NetMessage::Type::svc_fix_angle:
        return std::make_unique<SvcFixAngle>();
    case NetMessage::Type::svc_crosshair_angle:
        return std::make_unique<SvcCrosshairAngle>();
    case NetMessage::Type::svc_bsp_decal:
        return std::make_unique<SvcBSPDecal>();
    case NetMessage::Type::svc_user_message:
        return std::make_unique<SvcUserMessage>();
    case NetMessage::Type::svc_entity_message:
        return std::make_unique<SvcEntityMessage>();
    case NetMessage::Type::svc_game_event:
        return std::make_unique<SvcGameEvent>();
    case NetMessage::Type::svc_packet_entities:
        return std::make_unique<SvcPacketEntities>();
    case NetMessage::Type::svc_temp_entities:
        return std::make_unique<SvcTempEntities>();
    case NetMessage::Type::svc_prefetch:
        return std::make_unique<SvcPrefetch>();
    case NetMessage::Type::svc_menu:
        return std::make_unique<SvcMenu>();
    case NetMessage::Type::svc_game_event_list:
        return std::make_unique<SvcGameEventList>();
    case NetMessage::Type::svc_get_cvar_value:
        return std::make_unique<SvcGetCvarValue>();
    case NetMessage::Type::svc_cmd_key_values:
        return std::make_unique<SvcCmdKeyValues>();
    case NetMessage::Type::svc_set_pause_timed:
        return std::make_unique<SvcSetPauseTimed>();
    default:
        throw std::runtime_error("Unhandled or unknown NetMessage type: " + std::to_string(static_cast<int>(msg_type)));
    }
}

void Packet::parse(BinaryReader& reader)
{
    std::cout << "Tick: " << tick << std::endl;
    auto buf = reader.read_bytes(sizeof(CmdInfo));
    std::memcpy(&cmd_info, buf.data(), sizeof(CmdInfo));

    in_sequence = reader.read_int32();
    out_sequence = reader.read_int32();
    auto size = reader.read_int32();
    auto data = reader.read_bytes(size);

    auto msg_reader = BitReader(data);

    while (msg_reader.bits_left() > 6) {
        auto msg_type = static_cast<NetMessage::Type>(msg_reader.read_bits(6));
        try {
            auto msg = create_net_message(msg_type);
            msg->parse(msg_reader);
            net_messages.push_back(std::move(msg));
        }
        catch (std::exception e) {
            std::cerr << e.what() << std::endl;
            break;
        }
    }
    std::cout << "=========" << std::endl;
}

void SyncTick::parse(BinaryReader& reader)
{}

void ConsoleCmd::parse(BinaryReader& reader)
{
    auto command_length = reader.read_int32();
    if (command_length < 0) {
        throw std::runtime_error("Invalid command length encountered in ConsoleCmd::parse.");
    }

    command = reader.read_string(command_length);
}

void UserCmd::parse(BinaryReader& reader)
{
    cmd = reader.read_int32();
    auto size = reader.read_int32();
    data = reader.read_bytes(size);
}

void DataTable::parse(BinaryReader& reader)
{
    auto size = reader.read_int32();
    data = reader.read_bytes(size);
}

void StringTable::parse(BinaryReader& reader)
{
    auto size = reader.read_int32();
    data = reader.read_bytes(size);
}

void Stop::parse(BinaryReader& reader)
{
}
