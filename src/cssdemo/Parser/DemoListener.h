#pragma once
#include <string>

namespace cssdemo {

struct DemoHeader;
struct DemoFrame;
struct NetMessage;
struct UserCmd;
struct GameEvent;
struct UserMessage;
struct EntityUpdate;
struct TempEntity;
struct SoundInfo;
struct StringTableChange;
struct GameState;

class DemoListener {
public:
	virtual ~DemoListener() = default;

	virtual void on_header(const DemoHeader&) {}
	virtual void on_frame(const DemoFrame&) {}
	virtual void on_net_message(const NetMessage&) {}
	// after all of a packet's messages are applied, sample state here once per tick
	virtual void on_packet_end(const DemoFrame&, const GameState&) {}
	virtual void on_user_cmd(const UserCmd&) {}
	virtual void on_data_tables(const GameState&) {}
	virtual void on_string_table_change(const StringTableChange&) {}
	virtual void on_game_event(const GameEvent&) {}
	virtual void on_user_message(const UserMessage&) {}
	virtual void on_entity_update(const EntityUpdate&) {}
	virtual void on_temp_entity(const TempEntity&) {}
	virtual void on_sound(const SoundInfo&) {}
	virtual void on_error(int, const std::string&) {}
	virtual void on_finish(const GameState&) {}
};

}
