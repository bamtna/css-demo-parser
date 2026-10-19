#pragma once
#include <string>
#include <array>
#include <cstdint>

namespace cssdemo {

struct Vector {
	float x{}, y{}, z{};
};

struct QAngle {
	float x{}, y{}, z{};
};

// democmdinfo_t
struct CmdInfo {
	int32_t flags{};
	// original origin/viewangles
	Vector view_origin;
	QAngle view_angles;
	QAngle local_view_angles;
	// Resampled origin/viewangles
	Vector view_origin2;
	QAngle view_angles2;
	QAngle local_view_angles2;
};
static_assert(sizeof(CmdInfo) == 76, "CmdInfo must match democmdinfo_t");

// player_info_t
struct PlayerInfo {
	std::string name;
	int user_id{};
	std::string guid;
	uint32_t friends_id{};
	std::string friends_name;
	bool fake_player{};
	bool is_hltv{};
	bool is_replay{};
	std::array<uint32_t, 4> custom_files{};
	uint8_t files_downloaded{};
};

struct ProtocolVersion {
	int demo = 3;
	int network = 24;
};

}
