#pragma once
#include "DataTables.h"
#include "Entities.h"
#include "GameEvents.h"
#include "StringTables.h"
#include "cssdemo/Protocol/Types.h"
#include <optional>
#include <string>
#include <vector>

namespace cssdemo {

struct ServerInfo {
	int protocol{};
	bool is_hltv{};
	bool is_dedicated{};
	int max_classes{};
	int player_slot{};
	int max_clients{};
	float tick_interval{};
	char os{};
	std::string game_dir;
	std::string map_name;
	std::string sky_name;
	std::string host_name;
};

struct Player {
	int entity_index{};
	PlayerInfo info;
};

struct GameState {
	ProtocolVersion protocol;
	ServerInfo server;
	bool has_server_info{};
	DataTables data_tables;
	StringTables string_tables;
	GameEventList game_events;
	EntityState entities;
	int tick{}; // server tick from the last net_Tick

	std::vector<Player> players() const;
	std::optional<Player> find_player(int user_id) const;
};

}
