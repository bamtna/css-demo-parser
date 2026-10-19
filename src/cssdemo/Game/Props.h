#pragma once
#include "DataTables.h"
#include "cssdemo/Protocol/Types.h"
#include <ostream>
#include <string>
#include <variant>
#include <vector>

namespace cssdemo {

class BitReader;

struct PropValue;
using PropArray = std::vector<PropValue>;

struct PropValue {
	std::variant<std::monostate, int, float, Vector, std::string, PropArray> value;

	bool empty() const { return std::holds_alternative<std::monostate>(value); }

	int as_int(int fallback = 0) const;
	float as_float(float fallback = 0.0f) const;
	Vector as_vector() const;
	std::string as_string() const;

	friend std::ostream& operator<<(std::ostream& os, const PropValue& prop);
};

PropValue decode_prop(BitReader& reader, const FlattenedProp& flat);

struct PropChange {
	int index{};
	PropValue value;
};

// CDeltaBitsReader
std::vector<PropChange> read_prop_changes(BitReader& reader, const ServerClass& server_class);

}
