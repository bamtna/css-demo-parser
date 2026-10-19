#include "TempEntities.h"
#include "cssdemo/Net/NetMessages.h"
#include "cssdemo/Util/BitReader.h"
#include "cssdemo/Util/BitWriter.h"
#include <cmath>
#include <stdexcept>

namespace cssdemo {

std::vector<TempEntity> decode_temp_entities(const SvcTempEntities& message, const DataTables& tables)
{
	std::vector<TempEntity> events;
	BitReader reader = message.data.reader();
	const int class_bits = tables.server_class_bits();

	// 0 means one reliable event
	int count = message.num_entries == 0 ? 1 : message.num_entries;
	for (int i = 0; i < count; i++) {
		TempEntity event{};
		if (reader.read_bool()) {
			event.delay = reader.read_signed_bits(8) / 100.0f;
		}

		if (reader.read_bool()) {
			// 1-based here
			int class_id = static_cast<int>(reader.read_bits(class_bits)) - 1;
			event.server_class = tables.find_class(class_id);
			if (!event.server_class) {
				throw std::runtime_error("Temp entity class " + std::to_string(class_id) + " out of range");
			}
			event.props.resize(event.server_class->flattened_props.size());
		}
		else {
			if (events.empty()) {
				throw std::runtime_error("Temp entity delta without a previous event");
			}
			event.server_class = events.back().server_class;
			event.props = events.back().props;
		}

		event.changes = read_prop_changes(reader, *event.server_class);
		for (const auto& change : event.changes) {
			event.props[change.index] = change.value;
		}
		events.push_back(std::move(event));
	}

	if (!reader.at_end()) {
		throw std::runtime_error("Temp entities: " + std::to_string(reader.bits_left()) + " bits left unread");
	}
	return events;
}

void set_temp_entity_changes(std::vector<TempEntity>& events)
{
	for (size_t i = 0; i < events.size(); i++) {
		auto& event = events[i];
		bool is_delta = i > 0 && events[i - 1].server_class == event.server_class;
		static const std::vector<PropValue> zero;
		event.changes = diff_props(*event.server_class, is_delta ? events[i - 1].props : zero, event.props);
	}
}

SvcTempEntities encode_temp_entities(const std::vector<TempEntity>& events, const DataTables& tables, bool reliable)
{
	if (reliable && events.size() != 1) {
		throw std::runtime_error("Reliable temp entities carry one event");
	}
	SvcTempEntities message;
	message.num_entries = reliable ? 0 : static_cast<int>(events.size());
	const int class_bits = tables.server_class_bits();

	BitWriter writer;
	const TempEntity* previous = nullptr;
	for (const auto& event : events) {
		if (!event.server_class) {
			throw std::runtime_error("Temp entity without a server class");
		}
		int delay = static_cast<int>(std::lround(event.delay * 100.0f));
		writer.write_bool(delay != 0);
		if (delay != 0) {
			writer.write_signed_bits(delay, 8);
		}

		bool is_delta = previous && previous->server_class == event.server_class;
		writer.write_bool(!is_delta);
		if (!is_delta) {
			writer.write_bits(event.server_class->class_id + 1, class_bits);
		}
		write_prop_changes(writer, *event.server_class, event.changes);
		previous = &event;
	}
	message.data = writer.take_buffer();
	return message;
}

}
