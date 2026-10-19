#include "TempEntities.h"
#include "cssdemo/Net/NetMessages.h"
#include "cssdemo/Util/BitReader.h"
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

}
