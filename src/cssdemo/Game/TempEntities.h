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

}
