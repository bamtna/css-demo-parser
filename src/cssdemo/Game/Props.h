#pragma once
#include "DataTables.h"
#include "cssdemo/Protocol/Types.h"
#include <ostream>
#include <string>
#include <variant>
#include <vector>

namespace cssdemo {

class BitReader;
class BitWriter;

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

// dt_encode.cpp, unset values encode as zero
void encode_prop(BitWriter& writer, const FlattenedProp& flat, const PropValue& value);

// CDeltaBitsWriter, changes in ascending index order
void write_prop_changes(BitWriter& writer, const ServerClass& server_class, const std::vector<PropChange>& changes);

// same encoded bits
bool props_equal(const FlattenedProp& flat, const PropValue& a, const PropValue& b);

// SendTable_CalcDelta, the props of to that encode differently from from
std::vector<PropChange> diff_props(const ServerClass& server_class, const std::vector<PropValue>& from,
	const std::vector<PropValue>& to);

}
