#include "Players.h"
#include "GameState.h"
#include "cssdemo/Protocol/Constants.h"

namespace cssdemo {

namespace {

constexpr int NUM_NETWORKED_EHANDLE_BITS = MAX_EDICT_BITS + NUM_NETWORKED_EHANDLE_SERIAL_NUMBER_BITS;
constexpr int INVALID_NETWORKED_EHANDLE_VALUE = (1 << NUM_NETWORKED_EHANDLE_BITS) - 1;

class PropReader {
	const Entity& entity;

public:
	explicit PropReader(const Entity& e) : entity(e) {}

	const PropValue* get(const char* name) const { return entity.find(name); }
	bool has(const char* name) const { return get(name) != nullptr; }

	int as_int(const char* name, int fallback = 0) const {
		const PropValue* value = get(name);
		return value ? value->as_int(fallback) : fallback;
	}
	float as_float(const char* name, float fallback = 0.0f) const {
		const PropValue* value = get(name);
		return value ? value->as_float(fallback) : fallback;
	}
	Vector as_vector(const char* name) const {
		const PropValue* value = get(name);
		return value ? value->as_vector() : Vector{};
	}
	int as_handle(const char* name) const {
		const PropValue* value = get(name);
		return value ? handle_to_index(value->as_int()) : -1;
	}
};

void fill_from_entity(PlayerSnapshot& snapshot, const Entity& entity)
{
	PropReader props(entity);
	snapshot.has_entity = true;

	if (snapshot.is_local && props.has("cslocaldata.m_vecOrigin")) {
		snapshot.origin = props.as_vector("cslocaldata.m_vecOrigin");
		snapshot.origin_precise = true;
	}
	else {
		snapshot.origin = props.as_vector("csnonlocaldata.m_vecOrigin");
	}

	snapshot.eye_angles.x = props.as_float("m_angEyeAngles[0]");
	snapshot.eye_angles.y = props.as_float("m_angEyeAngles[1]");
	snapshot.flags = props.as_int("m_fFlags");
	snapshot.move_type = props.as_int("movetype");
	snapshot.health = props.as_int("m_iHealth");
	snapshot.armor = props.as_int("m_ArmorValue");
	snapshot.team = props.as_int("m_iTeamNum");
	snapshot.life_state = props.as_int("m_lifeState");
	snapshot.observer_mode = props.as_int("m_iObserverMode");
	snapshot.observer_target = props.as_handle("m_hObserverTarget");
	snapshot.active_weapon = props.as_handle("m_hActiveWeapon");

	if (snapshot.is_local) {
		snapshot.velocity = Vector{
			props.as_float("localdata.m_vecVelocity[0]"),
			props.as_float("localdata.m_vecVelocity[1]"),
			props.as_float("localdata.m_vecVelocity[2]"),
		};
		snapshot.base_velocity = props.as_vector("localdata.m_vecBaseVelocity");
		snapshot.view_offset_z = props.as_float("localdata.m_vecViewOffset[2]");
		snapshot.ground_entity = props.as_handle("localdata.m_hGroundEntity");
		snapshot.tick_base = props.as_int("localdata.m_nTickBase");
		snapshot.ducked = props.as_int("localdata.m_Local.m_bDucked") != 0;
		snapshot.ducking = props.as_int("localdata.m_Local.m_bDucking") != 0;
		snapshot.duck_time = props.as_float("localdata.m_Local.m_flDucktime");
		snapshot.stamina = props.as_float("cslocaldata.m_flStamina");
		snapshot.velocity_modifier = props.as_float("cslocaldata.m_flVelocityModifier", 1.0f);
	}
	else {
		snapshot.ducked = (snapshot.flags & FL_DUCKING) != 0;
	}
}

} // namespace

const char* demo_kind_name(DemoKind kind)
{
	switch (kind) {
	case DemoKind::Client: return "client";
	case DemoKind::SourceTV: return "sourcetv";
	default: return "unknown";
	}
}

int handle_to_index(int handle)
{
	if (handle == INVALID_NETWORKED_EHANDLE_VALUE) return -1;
	return handle & (MAX_EDICTS - 1);
}

DemoKind demo_kind(const GameState& state)
{
	if (!state.has_server_info) return DemoKind::Unknown;
	return state.server.is_hltv ? DemoKind::SourceTV : DemoKind::Client;
}

int local_player_index(const GameState& state)
{
	return demo_kind(state) == DemoKind::Client ? state.server.player_slot + 1 : -1;
}

std::vector<PlayerSnapshot> player_snapshots(const GameState& state)
{
	std::vector<PlayerSnapshot> result;
	int local_index = local_player_index(state);
	for (auto& player : state.players()) {
		PlayerSnapshot snapshot{};
		snapshot.entity_index = player.entity_index;
		snapshot.info = std::move(player.info);
		snapshot.is_local = player.entity_index == local_index;
		const Entity* entity = state.entities.find(player.entity_index);
		if (entity && entity->server_class->class_name == "CCSPlayer") {
			fill_from_entity(snapshot, *entity);
		}
		result.push_back(std::move(snapshot));
	}
	return result;
}

std::optional<PlayerSnapshot> player_snapshot(const GameState& state, int entity_index)
{
	for (auto& snapshot : player_snapshots(state)) {
		if (snapshot.entity_index == entity_index) return snapshot;
	}
	return std::nullopt;
}

}
