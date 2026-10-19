#include "Props.h"
#include "cssdemo/Protocol/Constants.h"
#include "cssdemo/Util/BitReader.h"
#include <cmath>
#include <stdexcept>

namespace cssdemo {

int PropValue::as_int(int fallback) const
{
	if (auto v = std::get_if<int>(&value)) return *v;
	if (auto v = std::get_if<float>(&value)) return static_cast<int>(*v);
	return fallback;
}

float PropValue::as_float(float fallback) const
{
	if (auto v = std::get_if<float>(&value)) return *v;
	if (auto v = std::get_if<int>(&value)) return static_cast<float>(*v);
	return fallback;
}

Vector PropValue::as_vector() const
{
	if (auto v = std::get_if<Vector>(&value)) return *v;
	return {};
}

std::string PropValue::as_string() const
{
	if (auto v = std::get_if<std::string>(&value)) return *v;
	return {};
}

std::ostream& operator<<(std::ostream& os, const PropValue& prop)
{
	struct Printer {
		std::ostream& os;
		void operator()(std::monostate) { os << "<unset>"; }
		void operator()(int v) { os << v; }
		void operator()(float v) { os << v; }
		void operator()(const Vector& v) { os << "(" << v.x << ", " << v.y << ", " << v.z << ")"; }
		void operator()(const std::string& v) { os << '"' << v << '"'; }
		void operator()(const PropArray& v) {
			os << "[";
			for (size_t i = 0; i < v.size(); i++) {
				if (i) os << ", ";
				os << v[i];
			}
			os << "]";
		}
	};
	std::visit(Printer{ os }, prop.value);
	return os;
}

// dt_encode.cpp: DecodeFloat
static float decode_float(BitReader& reader, const SendProp& prop)
{
	if (prop.has_flag(SPROP_COORD)) {
		return reader.read_bit_coord();
	}
	if (prop.has_flag(SPROP_COORD_MP | SPROP_COORD_MP_LOWPRECISION | SPROP_COORD_MP_INTEGRAL)) {
		return reader.read_bit_coord_mp(prop.has_flag(SPROP_COORD_MP_INTEGRAL), prop.has_flag(SPROP_COORD_MP_LOWPRECISION));
	}
	if (prop.has_flag(SPROP_NOSCALE)) {
		return reader.read_float32();
	}
	if (prop.has_flag(SPROP_NORMAL)) {
		return reader.read_bit_normal();
	}

	uint32_t interp = reader.read_bits(prop.num_bits);
	float fraction = static_cast<float>(interp) / ((1u << prop.num_bits) - 1);
	return prop.low_value + (prop.high_value - prop.low_value) * fraction;
}

static PropValue decode_single(BitReader& reader, const SendProp& prop, const SendProp* array_element_prop)
{
	PropValue result;
	switch (prop.type) {
	case SendPropType::Int:
		if (prop.has_flag(SPROP_VARINT)) {
			result.value = prop.has_flag(SPROP_UNSIGNED)
				? static_cast<int>(reader.read_var_int32())
				: reader.read_signed_var_int32();
		}
		else if (prop.has_flag(SPROP_UNSIGNED)) {
			result.value = static_cast<int>(reader.read_bits(prop.num_bits));
		}
		else {
			result.value = reader.read_signed_bits(prop.num_bits);
		}
		break;

	case SendPropType::Float:
		result.value = decode_float(reader, prop);
		break;

	case SendPropType::Vector: {
		Vector v{};
		v.x = decode_float(reader, prop);
		v.y = decode_float(reader, prop);
		if (prop.has_flag(SPROP_NORMAL)) {
			// only the sign of z is sent
			int signbit = reader.read_bit();
			float xy_sq = v.x * v.x + v.y * v.y;
			v.z = xy_sq < 1.0f ? std::sqrt(1.0f - xy_sq) : 0.0f;
			if (signbit) v.z = -v.z;
		}
		else {
			v.z = decode_float(reader, prop);
		}
		result.value = v;
		break;
	}

	case SendPropType::VectorXY: {
		Vector v{};
		v.x = decode_float(reader, prop);
		v.y = decode_float(reader, prop);
		result.value = v;
		break;
	}

	case SendPropType::String: {
		int length = reader.read_bits(DT_MAX_STRING_BITS);
		result.value = reader.read_fixed_string(length);
		break;
	}

	case SendPropType::Array: {
		if (!array_element_prop) {
			throw std::runtime_error("Array prop " + prop.name + " is missing its element prop");
		}
		int count = reader.read_bits(Q_log2(prop.num_elements) + 1);
		PropArray elements;
		elements.reserve(count);
		for (int i = 0; i < count; i++) {
			elements.push_back(decode_single(reader, *array_element_prop, nullptr));
		}
		result.value = std::move(elements);
		break;
	}

	default:
		throw std::runtime_error("Unsupported send prop type " + std::to_string(static_cast<int>(prop.type))
			+ " for " + prop.name);
	}
	return result;
}

PropValue decode_prop(BitReader& reader, const FlattenedProp& flat)
{
	return decode_single(reader, *flat.prop, flat.array_element_prop);
}

std::vector<PropChange> read_prop_changes(BitReader& reader, const ServerClass& server_class)
{
	std::vector<PropChange> changes;
	int last_index = -1;

	// ReadOneBit + ReadUBitVar per index
	while (reader.read_bool()) {
		int index = last_index + 1 + static_cast<int>(reader.read_ubit_var());
		if (index >= static_cast<int>(server_class.flattened_props.size())) {
			throw std::runtime_error("Prop index " + std::to_string(index) + " out of range for "
				+ server_class.class_name);
		}
		last_index = index;
		changes.push_back({ index, decode_prop(reader, server_class.flattened_props[index]) });
	}
	return changes;
}

}
