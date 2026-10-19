#pragma once
#include "DataTables.h"
#include "Props.h"
#include <vector>

namespace cssdemo {

struct SvcTempEntities;

struct TempEntity {
	float delay{};
	const ServerClass* server_class{};
	std::vector<PropChange> changes; // props sent for this event
	std::vector<PropValue> props;    // full state
};

// CClientState::ProcessTempEntities
std::vector<TempEntity> decode_temp_entities(const SvcTempEntities& message, const DataTables& tables);

// CBaseServer::WriteTempEntities, same class as the event before is a delta of it
// a reliable message has num_entries 0 and one event
SvcTempEntities encode_temp_entities(const std::vector<TempEntity>& events, const DataTables& tables, bool reliable = false);
// changes from props the way the server picks them: against zero, or the event before of the same class
void set_temp_entity_changes(std::vector<TempEntity>& events);

}
