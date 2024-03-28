#pragma once
#include <string>
#include <vector>
#include "structs.h"

class BitReader;

struct NetMessage {
	enum class Type {
        net_nop = 0,                // nop command used for padding
        net_disconnect = 1,         // disconnect, last message in connection
        net_file = 2,               // file transmission message request/deny
        net_tick = 3,               // send last world tick
        net_string_cmd = 4,         // a string command
        net_set_con_var = 5,        // sends one/multiple convar settings
        net_signon_state = 6,       // signals current signon state
        svc_print = 7,              // print text to console
        svc_server_info = 8,        // first message from server about game, map etc
        svc_send_table = 9,         // sends a sendtable description for a game class
        svc_class_info = 10,        // info about classes (first byte is a classinfo_ define).
        svc_set_pause = 11,         // tells client if server paused or unpaused
        svc_create_string_table = 12,// inits shared string tables
        svc_update_string_table = 13,// updates a string table
        svc_voice_init = 14,        // inits used voice codecs & quality
        svc_voice_data = 15,        // voicestream data from the server
        svc_sounds = 17,            // starts playing sound
        svc_set_view = 18,          // sets entity as point of view
        svc_fix_angle = 19,         // sets/corrects players viewangle
        svc_crosshair_angle = 20,   // adjusts crosshair in auto aim mode to lock on target
        svc_bsp_decal = 21,         // add a static decal to the world bsp
        // svc_terrain_mod (22) is now unused.
        svc_user_message = 23,      // a game specific message
        svc_entity_message = 24,    // a message for an entity
        svc_game_event = 25,        // global game event fired
        svc_packet_entities = 26,   // non-delta compressed entities
        svc_temp_entities = 27,     // non-reliable event object
        svc_prefetch = 28,          // only sound indices for now
        svc_menu = 29,              // display a menu from a plugin
        svc_game_event_list = 30,   // list of known games events and fields
        svc_get_cvar_value = 31,    // server wants to know the value of a cvar on the client
        svc_cmd_key_values = 32,    // server submits keyvalues command for the client
        svc_set_pause_timed = 33    // timed pause - to avoid breaking demos
	};


    NetMessage(Type _type) : type(_type) {};
    virtual void parse(BitReader& reader) = 0;

    Type type;
};

struct NetNop : public NetMessage {
    NetNop() : NetMessage(Type::net_nop) {};
    void parse(BitReader& reader);
};

struct NetDisconnect : public NetMessage {
    NetDisconnect() : NetMessage(Type::net_disconnect) {};
    void parse(BitReader& reader);

    std::string text;
};

struct NetFile : public NetMessage {
    NetFile() : NetMessage(Type::net_file) {};
    void parse(BitReader& reader);

    int transfer_id{};
    std::string file_name;
    bool file_requested{};

};

struct NetTick : public NetMessage {
    NetTick() : NetMessage(Type::net_tick) {};
    void parse(BitReader& reader);
    void print();

    inline static const float SCALEUP = 100000.0f;

    int tick{};
    float host_frame_time{};
    float host_frame_time_std_deviation{};
};

struct NetStringCmd : public NetMessage {
    NetStringCmd() : NetMessage(Type::net_string_cmd) {};
    void parse(BitReader& reader);
    std::string command;
};

struct NetSetConVar : public NetMessage {
    NetSetConVar() : NetMessage(Type::net_set_con_var) {};
    void parse(BitReader& reader);
    std::vector<ConVar> convars;
};

struct NetSignonState : public NetMessage {
    NetSignonState() : NetMessage(Type::net_signon_state) {}
    void parse(BitReader& reader);
    int signon_state{};
    int spawn_count{};
};

struct SvcPrint : public NetMessage {
    SvcPrint() : NetMessage(Type::svc_print) {};
    void parse(BitReader& reader);
    std::string text;
};

struct SvcServerInfo : public NetMessage {
    SvcServerInfo() : NetMessage(Type::svc_server_info) {};
    void parse(BitReader& reader);

    int protocol{};
    int server_count{};
    bool is_hltv{};
    bool is_dedicated{};
    int client_crc{};
    int max_classes{};
    int map_crc{};
    int player_slot{};
    int max_clients{};
    float tick_interval{};
    char os{}; // L = linux, W = Win32
    std::string game_dir;
    std::string map_name;
    std::string sky_name;
    std::string host_name;
    bool is_replay{};
};

struct SvcSendTable : public NetMessage {
    SvcSendTable() : NetMessage(Type::svc_send_table) {};
    void parse(BitReader& reader);
    bool needs_decoder{};
    int length{};
    //int props{};
    std::vector<std::byte> data;
};

struct SvcClassInfo : public NetMessage { 
    SvcClassInfo() : NetMessage(Type::svc_class_info) {};
    void parse(BitReader& reader);

    // todo: move this somewhere else and rename?
    typedef struct class_s
    {
        int		classID;
        std::string	data_table_name;
        std::string	class_name;
    } class_t;

    int num_server_classes{};
    bool create_on_client{};
    std::vector<class_t> server_classes;
};

struct SvcSetPause : public NetMessage {
    SvcSetPause() : NetMessage(Type::svc_set_pause) {};
    void parse(BitReader& reader);

    bool paused{};
};

struct SvcCreateStringTable : public NetMessage {
    SvcCreateStringTable() : NetMessage(Type::svc_create_string_table) {};
    void parse(BitReader& reader);

    std::string table_name;
    int max_entries{};
    int num_entries{};
    int length{};
    bool user_data_fixed_size{};
    int user_data_size{};
    int user_data_size_bits{};
    bool data_compressed{};
    std::vector<std::byte> data;
};

struct SvcUpdateStringTable : public NetMessage {
    SvcUpdateStringTable() : NetMessage(Type::svc_update_string_table) {};
    void parse(BitReader& reader);

    int table_id{};
    int num_changed_entries{};
    int length{};
    std::vector<std::byte> data;
};

struct SvcVoiceInit : public NetMessage {
    SvcVoiceInit() : NetMessage(Type::svc_voice_init) {};
    void parse(BitReader& reader);

    std::string codec;
    int legacy_quality{};
    int sample_rate{};
};

struct SvcVoiceData : public NetMessage {
    SvcVoiceData() : NetMessage(Type::svc_voice_data) {};
    void parse(BitReader& reader);

    int from_client{};
    bool proximity{};
    int length{};
    std::vector<std::byte> data;
};

struct SvcSounds : public NetMessage {
    SvcSounds() : NetMessage(Type::svc_sounds) {};
    void parse(BitReader& reader);

    bool reliable_sound{};
    int num_sounds{};
    int length{};
    std::vector<std::byte> data;
};

struct SvcSetView : public NetMessage {
    SvcSetView() : NetMessage(Type::svc_set_view) {};
    void parse(BitReader& reader);

    int entity_index{};
};

struct SvcFixAngle : public NetMessage {
    SvcFixAngle() : NetMessage(Type::svc_fix_angle) {};
    void parse(BitReader& reader);

    bool relative{};
    QAngle angle{};
};

struct SvcCrosshairAngle : public NetMessage {
    SvcCrosshairAngle() : NetMessage(Type::svc_crosshair_angle) {};
    void parse(BitReader& reader);

    QAngle angle{};
};

struct SvcBSPDecal : public NetMessage {
    SvcBSPDecal() : NetMessage(Type::svc_bsp_decal) {};
    void parse(BitReader& reader);

    Vector pos{};
    int decal_texture_index{};
    int entity_index{};
    int model_index{};
    bool low_priority{};
};

struct SvcUserMessage : public NetMessage {
    SvcUserMessage() : NetMessage(Type::svc_user_message) {};
    void parse(BitReader& reader);

    int msg_type{};
    int length{};
    std::vector<std::byte> data;
};

struct SvcEntityMessage : public NetMessage {
    SvcEntityMessage() : NetMessage(Type::svc_entity_message) {};
    void parse(BitReader& reader);

    int entity_index{};
    int class_id{};
    int length{};
    std::vector<std::byte> data;
};

struct SvcGameEvent : public NetMessage {
    SvcGameEvent() : NetMessage(Type::svc_game_event) {};
    void parse(BitReader& reader);

    int length{};
    std::vector<std::byte> data;
};

struct SvcPacketEntities : public NetMessage {
    SvcPacketEntities() : NetMessage(Type::svc_packet_entities) {};
    void parse(BitReader& reader);

    int max_entries{};
    bool is_delta{};
    int delta_from{};
    bool baseline{};
    int updated_entries{};
    int length{};
    bool update_baseline{};
    std::vector<std::byte> data;
};

struct SvcTempEntities : public NetMessage {
    SvcTempEntities() : NetMessage(Type::svc_temp_entities) {};
    void parse(BitReader& reader);

    int num_entries{};
    int length{};
    std::vector<std::byte> data;
};

struct SvcPrefetch : public NetMessage {
    SvcPrefetch() : NetMessage(Type::svc_prefetch) {};
    void parse(BitReader& reader);

    int sound_index{};
};

struct SvcMenu : public NetMessage {
    SvcMenu() : NetMessage(Type::svc_menu) {};
    void parse(BitReader& reader);

    int menu_type{};
    int length{};
    std::vector<std::byte> data;
};

struct SvcGameEventList : public NetMessage {
    SvcGameEventList() : NetMessage(Type::svc_game_event_list) {};
    void parse(BitReader& reader);

    int events{};
    int length{};
    std::vector<std::byte> data;
};

struct SvcGetCvarValue : public NetMessage {
    SvcGetCvarValue() : NetMessage(Type::svc_get_cvar_value) {};
    void parse(BitReader& reader);

    std::string cookie;
    std::string cvar_name;
};

struct SvcCmdKeyValues : public NetMessage {
    SvcCmdKeyValues() : NetMessage(Type::svc_cmd_key_values) {};
    void parse(BitReader& reader);

    int length{};
    std::vector<std::byte> data;
};

struct SvcSetPauseTimed : public NetMessage {
    SvcSetPauseTimed() : NetMessage(Type::svc_set_pause_timed) {};
    void parse(BitReader& reader);

    bool paused{};
    float expire_time{};
};
