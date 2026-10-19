#include "Entities.h"
#include "cssdemo/Net/NetMessages.h"
#include "cssdemo/Protocol/Constants.h"
#include "cssdemo/Util/BitReader.h"
#include "cssdemo/Util/BitWriter.h"
#include <stdexcept>

namespace cssdemo {

const char* entity_update_kind_name(EntityUpdate::Kind kind)
{
	switch (kind) {
	case EntityUpdate::Kind::Enter: return "Enter";
	case EntityUpdate::Kind::Delta: return "Delta";
	case EntityUpdate::Kind::Leave: return "Leave";
	case EntityUpdate::Kind::Delete: return "Delete";
	}
	return "Unknown";
}

void write_entity_updates(BitWriter& writer, std::span<const EntityUpdate> updates,
	std::span<const int> explicit_deletions, bool is_delta, int class_bits)
{
	int last_index = -1;
	for (const auto& update : updates) {
		if (update.index <= last_index || update.index >= MAX_EDICTS) {
			throw std::runtime_error("Entity index " + std::to_string(update.index) + " out of order or range");
		}
		writer.write_ubit_var(static_cast<uint32_t>(update.index - last_index - 1));
		last_index = update.index;

		using Kind = EntityUpdate::Kind;
		switch (update.kind) {
		case Kind::Enter:
		case Kind::Delta:
			if (!update.server_class) {
				throw std::runtime_error("Entity " + std::to_string(update.index) + " has no server class");
			}
			writer.write_bool(false);
			writer.write_bool(update.kind == Kind::Enter);
			if (update.kind == Kind::Enter) {
				writer.write_bits(update.server_class->class_id, class_bits);
				writer.write_bits(update.serial, NUM_NETWORKED_EHANDLE_SERIAL_NUMBER_BITS);
			}
			write_prop_changes(writer, *update.server_class, update.changes);
			break;
		case Kind::Leave:
		case Kind::Delete:
			writer.write_bool(true);
			writer.write_bool(update.kind == Kind::Delete);
			break;
		}
	}

	if (is_delta) {
		for (int index : explicit_deletions) {
			writer.write_bool(true);
			writer.write_bits(index, MAX_EDICT_BITS);
		}
		writer.write_bool(false);
	}
}

void Entity::apply(const std::vector<PropChange>& changes)
{
	for (const auto& change : changes) {
		props[change.index] = change.value;
	}
}

const PropValue* Entity::find(const std::string& prop_name) const
{
	int prop_index = server_class->find_prop(prop_name);
	if (prop_index < 0 || props[prop_index].empty()) {
		return nullptr;
	}
	return &props[prop_index];
}

const Entity* EntityState::find(int index) const
{
	auto it = entities.find(index);
	return it == entities.end() ? nullptr : &it->second;
}

void EntityState::clear()
{
	entities.clear();
	raw_instance_baselines.clear();
	decoded_instance_baselines.clear();
	for (auto& slot : entity_baselines) slot.clear();
}

void EntityState::set_instance_baseline(int class_id, std::span<const std::byte> data)
{
	raw_instance_baselines[class_id].assign(data.begin(), data.end());
	decoded_instance_baselines.erase(class_id);
}

const std::vector<PropValue>& EntityState::instance_baseline(const ServerClass& server_class)
{
	auto it = decoded_instance_baselines.find(server_class.class_id);
	if (it != decoded_instance_baselines.end()) {
		return it->second;
	}

	std::vector<PropValue> props(server_class.flattened_props.size());
	auto raw = raw_instance_baselines.find(server_class.class_id);
	if (raw == raw_instance_baselines.end()) {
		throw std::runtime_error("No instance baseline for class " + server_class.class_name);
	}
	BitReader reader(raw->second);
	for (auto& change : read_prop_changes(reader, server_class)) {
		props[change.index] = std::move(change.value);
	}
	return decoded_instance_baselines[server_class.class_id] = std::move(props);
}

std::vector<EntityUpdate> EntityState::apply(const SvcPacketEntities& message, const DataTables& tables)
{
	std::vector<EntityUpdate> updates;
	BitReader reader = message.data.reader();
	const int class_bits = tables.server_class_bits();
	const int baseline = message.baseline;
	const int new_baseline = baseline == 0 ? 1 : 0;

	if (!message.is_delta) {
		// full update
		entities.clear();
	}
	if (message.update_baseline) {
		entity_baselines[new_baseline] = entity_baselines[baseline];
	}

	int entity_index = -1;
	for (int i = 0; i < message.updated_entries; i++) {
		// CL_ParseDeltaHeader
		entity_index += 1 + static_cast<int>(reader.read_ubit_var());
		if (entity_index >= MAX_EDICTS) {
			throw std::runtime_error("Entity index out of range: " + std::to_string(entity_index));
		}

		EntityUpdate update{};
		update.index = entity_index;

		if (!reader.read_bool()) {
			if (reader.read_bool()) {
				// enter pvs, CL_CopyNewEntity
				update.kind = EntityUpdate::Kind::Enter;
				int class_id = reader.read_bits(class_bits);
				update.serial = reader.read_bits(NUM_NETWORKED_EHANDLE_SERIAL_NUMBER_BITS);
				const ServerClass* server_class = tables.find_class(class_id);
				if (!server_class) {
					throw std::runtime_error("Server class " + std::to_string(class_id) + " out of range");
				}
				update.server_class = server_class;

				Entity entity{ entity_index, update.serial, server_class, {} };
				auto& baselines = entity_baselines[baseline];
				auto base = baselines.find(entity_index);
				if (message.is_delta && base != baselines.end() && base->second.server_class == server_class) {
					entity.props = base->second.props;
				}
				else {
					entity.props = instance_baseline(*server_class);
				}

				update.changes = read_prop_changes(reader, *server_class);
				entity.apply(update.changes);

				if (message.update_baseline) {
					entity_baselines[new_baseline][entity_index] = entity;
				}
				entities[entity_index] = std::move(entity);
			}
			else {
				update.kind = EntityUpdate::Kind::Delta;
				auto it = entities.find(entity_index);
				if (it == entities.end()) {
					throw std::runtime_error("Delta for unknown entity " + std::to_string(entity_index));
				}
				update.server_class = it->second.server_class;
				update.serial = it->second.serial;
				update.changes = read_prop_changes(reader, *update.server_class);
				it->second.apply(update.changes);
			}
		}
		else {
			update.kind = reader.read_bool() ? EntityUpdate::Kind::Delete : EntityUpdate::Kind::Leave;
			if (auto it = entities.find(entity_index); it != entities.end()) {
				update.server_class = it->second.server_class;
				update.serial = it->second.serial;
				entities.erase(it);
			}
		}
		updates.push_back(std::move(update));
	}

	if (message.is_delta) {
		// CL_ReadDeletions
		while (reader.read_bool()) {
			EntityUpdate update{};
			update.kind = EntityUpdate::Kind::Delete;
			update.index = reader.read_bits(MAX_EDICT_BITS);
			if (auto it = entities.find(update.index); it != entities.end()) {
				update.server_class = it->second.server_class;
				update.serial = it->second.serial;
				entities.erase(it);
			}
			updates.push_back(std::move(update));
		}
	}

	if (!reader.at_end()) {
		throw std::runtime_error("Packet entities: " + std::to_string(reader.bits_left()) + " bits left unread");
	}
	return updates;
}

}
