#pragma once
#include "DataTables.h"
#include "Props.h"
#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace cssdemo {

class BitReader;
class BitWriter;
struct SvcPacketEntities;

struct Entity {
	int index{};
	int serial{};
	const ServerClass* server_class{};
	std::vector<PropValue> props; // indexed like server_class->flattened_props

	void apply(const std::vector<PropChange>& changes);
	const PropValue* find(const std::string& prop_name) const;
};

struct EntityUpdate {
	enum class Kind { Enter, Delta, Leave, Delete };
	Kind kind{};
	int index{};
	const ServerClass* server_class{}; // null for deletes of entities we never saw
	int serial{};
	std::vector<PropChange> changes;
};

const char* entity_update_kind_name(EntityUpdate::Kind kind);

// SV_WriteEntitiesToBuffer layout, updates in index order
// explicit deletions only go into delta packets
void write_entity_updates(BitWriter& writer, std::span<const EntityUpdate> updates,
	std::span<const int> explicit_deletions, bool is_delta, int class_bits);

// CL_ParsePacketEntities
class EntityState {
public:
	std::vector<EntityUpdate> apply(const SvcPacketEntities& message, const DataTables& tables);

	// from the instancebaseline table, keyed by class id
	void set_instance_baseline(int class_id, std::span<const std::byte> data);
	void clear();

	const Entity* find(int index) const;
	const std::unordered_map<int, Entity>& all() const { return entities; }

private:
	std::unordered_map<int, Entity> entities;
	std::unordered_map<int, std::vector<std::byte>> raw_instance_baselines;
	std::unordered_map<int, std::vector<PropValue>> decoded_instance_baselines;
	std::array<std::unordered_map<int, Entity>, 2> entity_baselines;

	const std::vector<PropValue>& instance_baseline(const ServerClass& server_class);
};

}
