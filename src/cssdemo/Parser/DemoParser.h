#pragma once
#include "DemoListener.h"
#include "cssdemo/Demo/DemoFile.h"
#include "cssdemo/Game/GameState.h"
#include <string>
#include <vector>

namespace cssdemo {

struct NetMessage;

struct ParseError {
	int tick{};
	std::string message;
};

class DemoParser {
public:
	explicit DemoParser(DemoFile& file);

	void add_listener(DemoListener& listener);
	void set_decode_entities(bool enabled) { decode_entities = enabled; }

	void run();
	// one frame, false once the demo is done (on_finish has been called then)
	bool step();
	const DemoFrame& frame() const { return current; }

	const GameState& state() const { return game; }
	const std::vector<ParseError>& errors() const { return parse_errors; }

private:
	DemoFile& file;
	std::vector<DemoListener*> listeners;
	GameState game;
	std::vector<ParseError> parse_errors;
	int frame_tick{};
	DemoFrame current;
	bool started{};
	bool finished{};
	bool decode_entities = true;
	bool entities_broken{};

	void handle_frame(const DemoFrame& frame);
	void handle_packet(const DemoFrame& frame);
	void handle_net_message(const NetMessage& message);
	void handle_string_table_change(const StringTableChange& change);
	void error(const std::string& message);

	template <typename F>
	void notify(F&& f) {
		for (auto* listener : listeners) f(*listener);
	}
};

}
