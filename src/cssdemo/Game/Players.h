#pragma once
#include "cssdemo/Protocol/Types.h"
#include <optional>
#include <string>
#include <vector>

namespace cssdemo {

struct GameState;
struct Entity;

enum class DemoKind {
	Unknown,  // no server info yet
	Client,
	SourceTV,
};

const char* demo_kind_name(DemoKind kind);

// m_fFlags, multiplayer layout from const.h
enum PlayerFlags {
	FL_ONGROUND = 1 << 0,
	FL_DUCKING = 1 << 1,
	FL_ANIMDUCKING = 1 << 2,
	FL_WATERJUMP = 1 << 3,
	FL_ONTRAIN = 1 << 4,
	FL_INRAIN = 1 << 5,
	FL_FROZEN = 1 << 6,
	FL_ATCONTROLS = 1 << 7,
	FL_CLIENT = 1 << 8,
	FL_FAKECLIENT = 1 << 9,
	FL_INWATER = 1 << 10,
};

enum LifeState {
	LIFE_ALIVE = 0,
	LIFE_DYING,
	LIFE_DEAD,
	LIFE_RESPAWNABLE,
	LIFE_DISCARDBODY,
};

struct PlayerSnapshot {
	int entity_index{};
	PlayerInfo info;
	bool is_local{};   // the recording player of a client demo
	bool has_entity{};

	Vector origin{};
	bool origin_precise{}; // local player only, everyone else is 1/32 unit
	QAngle eye_angles{};
	std::optional<Vector> velocity; // only networked for the local player
	Vector base_velocity{};
	float view_offset_z{};
	int flags{};
	int move_type{};
	int health{};
	int armor{};
	int team{};
	int life_state{};
	int observer_mode{};
	int observer_target{ -1 }; // entity index
	int active_weapon{ -1 };   // entity index
	int ground_entity{ -1 };   // entity index, local only
	int tick_base{};           // local only
	bool ducked{};
	bool ducking{};
	float duck_time{};
	float stamina{};
	float velocity_modifier{ 1.0f };

	bool alive() const { return life_state == LIFE_ALIVE; }
	bool on_ground() const { return (flags & FL_ONGROUND) != 0; }
};

int handle_to_index(int handle);

DemoKind demo_kind(const GameState& state);
// -1 for sourcetv demos
int local_player_index(const GameState& state);

std::vector<PlayerSnapshot> player_snapshots(const GameState& state);
std::optional<PlayerSnapshot> player_snapshot(const GameState& state, int entity_index);

}
