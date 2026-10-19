#pragma once
#include "cssdemo/Parser/DemoListener.h"
#include "cssdemo/Parser/DemoParser.h"
#include <ostream>
#include <string>
#include <vector>

namespace cssdemo {

struct DumpOptions {
	bool messages = true;
	bool entities = false;
	bool temp_entities = false;
	bool sounds = false;
	bool user_cmds = false;
	bool data_tables = false;
	bool string_tables = false;
};

class TextDumper : public DemoListener {
public:
	TextDumper(std::ostream& out, const GameState& state, DumpOptions options = {});

	void on_header(const DemoHeader& header) override;
	void on_frame(const DemoFrame& frame) override;
	void on_net_message(const NetMessage& message) override;
	void on_user_cmd(const UserCmd& cmd) override;
	void on_string_table_change(const StringTableChange& change) override;
	void on_game_event(const GameEvent& event) override;
	void on_user_message(const UserMessage& message) override;
	void on_entity_update(const EntityUpdate& update) override;
	void on_temp_entity(const TempEntity& event) override;
	void on_sound(const SoundInfo& sound) override;
	void on_error(int tick, const std::string& message) override;
	void on_finish(const GameState& state) override;

private:
	std::ostream& out;
	DumpOptions options;
	std::vector<ParseError> errors;
	const GameState& state;

	void dump_server(const GameState& state);
	void dump_players(const GameState& state);
	void dump_data_tables(const GameState& state);
	void dump_string_tables(const GameState& state);
	void dump_errors();
	void dump_props(const ServerClass& server_class, const std::vector<PropChange>& changes, const char* indent);
};

}
