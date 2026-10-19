#include "Props.h"
#include "cssdemo/Protocol/Constants.h"
#include "cssdemo/Util/BitReader.h"
#include "cssdemo/Util/BitWriter.h"
#include <algorithm>
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

// dt_send.cpp: AssignRangeMultiplier
static float range_multiplier(int num_bits, double range)
{
	uint32_t high = num_bits == 32 ? 0xFFFFFFFEu : ((1u << num_bits) - 1);
	float multiplier = static_cast<float>(high / range);
	if (std::fabs(range) < 1e-6) {
		multiplier = static_cast<float>(high);
	}
	auto too_big = [&](float m) {
		return static_cast<uint32_t>(m * range) > high || (m * range) > static_cast<double>(high);
	};
	if (too_big(multiplier)) {
		static const float multipliers[] = { 0.9999f, 0.99f, 0.9f, 0.8f, 0.7f };
		for (float m : multipliers) {
			multiplier = static_cast<float>(high / range) * m;
			if (!too_big(multiplier)) break;
		}
	}
	return multiplier;
}

static float decode_scaled(uint32_t interp, const SendProp& prop)
{
	float fraction = static_cast<float>(interp) / ((1u << prop.num_bits) - 1);
	return prop.low_value + (prop.high_value - prop.low_value) * fraction;
}

// dt_encode.cpp: EncodeFloat, checks in DecodeFloat's order
static void encode_float(BitWriter& writer, const SendProp& prop, float value)
{
	if (prop.has_flag(SPROP_COORD)) {
		writer.write_bit_coord(value);
		return;
	}
	if (prop.has_flag(SPROP_COORD_MP | SPROP_COORD_MP_LOWPRECISION | SPROP_COORD_MP_INTEGRAL)) {
		writer.write_bit_coord_mp(value, prop.has_flag(SPROP_COORD_MP_INTEGRAL), prop.has_flag(SPROP_COORD_MP_LOWPRECISION));
		return;
	}
	if (prop.has_flag(SPROP_NOSCALE)) {
		writer.write_float32(value);
		return;
	}
	if (prop.has_flag(SPROP_NORMAL)) {
		writer.write_bit_normal(value);
		return;
	}

	uint32_t max = (1u << prop.num_bits) - 1;
	uint32_t interp;
	if (value < prop.low_value) {
		interp = 0;
	}
	else if (value > prop.high_value) {
		interp = max;
	}
	else {
		// FastFloatToSmallInt rounds to nearest on the 32 bit server
		float scaled = (value - prop.low_value) * range_multiplier(prop.num_bits, static_cast<double>(prop.high_value) - prop.low_value);
		interp = static_cast<uint32_t>(std::min<double>(std::nearbyint(scaled), max));
		if (decode_scaled(interp, prop) != value) {
			if (interp < max && decode_scaled(interp + 1, prop) == value) interp++;
			else if (interp > 0 && decode_scaled(interp - 1, prop) == value) interp--;
		}
	}
	writer.write_bits(interp, prop.num_bits);
}

static PropValue zero_value(const SendProp& prop)
{
	PropValue result;
	switch (prop.type) {
	case SendPropType::Int: result.value = 0; break;
	case SendPropType::Float: result.value = 0.0f; break;
	case SendPropType::Vector:
	case SendPropType::VectorXY: result.value = Vector{}; break;
	case SendPropType::String: result.value = std::string(); break;
	case SendPropType::Array: result.value = PropArray(); break;
	default: result.value = 0; break;
	}
	return result;
}

static void encode_single(BitWriter& writer, const SendProp& prop, const SendProp* array_element_prop, const PropValue& input)
{
	if (input.empty()) {
		encode_single(writer, prop, array_element_prop, zero_value(prop));
		return;
	}
	const PropValue& value = input;
	switch (prop.type) {
	case SendPropType::Int: {
		int v = value.as_int();
		if (prop.has_flag(SPROP_VARINT)) {
			if (prop.has_flag(SPROP_UNSIGNED)) writer.write_var_int32(static_cast<uint32_t>(v));
			else writer.write_signed_var_int32(v);
		}
		else {
			writer.write_bits(static_cast<uint32_t>(v), prop.num_bits);
		}
		break;
	}

	case SendPropType::Float:
		encode_float(writer, prop, value.as_float());
		break;

	case SendPropType::Vector: {
		Vector v = value.as_vector();
		encode_float(writer, prop, v.x);
		encode_float(writer, prop, v.y);
		if (prop.has_flag(SPROP_NORMAL)) {
			writer.write_bit(std::signbit(v.z));
		}
		else {
			encode_float(writer, prop, v.z);
		}
		break;
	}

	case SendPropType::VectorXY: {
		Vector v = value.as_vector();
		encode_float(writer, prop, v.x);
		encode_float(writer, prop, v.y);
		break;
	}

	case SendPropType::String: {
		std::string s = value.as_string();
		s = s.c_str();
		if (s.size() >= (1u << DT_MAX_STRING_BITS)) s.resize((1u << DT_MAX_STRING_BITS) - 1);
		writer.write_bits(static_cast<uint32_t>(s.size()), DT_MAX_STRING_BITS);
		for (char c : s) writer.write_uint8(static_cast<uint8_t>(c));
		break;
	}

	case SendPropType::Array: {
		if (!array_element_prop) {
			throw std::runtime_error("Array prop " + prop.name + " is missing its element prop");
		}
		const PropArray* elements = std::get_if<PropArray>(&value.value);
		size_t count = elements ? std::min<size_t>(elements->size(), prop.num_elements) : 0;
		writer.write_bits(static_cast<uint32_t>(count), Q_log2(prop.num_elements) + 1);
		for (size_t i = 0; i < count; i++) {
			encode_single(writer, *array_element_prop, nullptr, (*elements)[i]);
		}
		break;
	}

	default:
		throw std::runtime_error("Unsupported send prop type " + std::to_string(static_cast<int>(prop.type))
			+ " for " + prop.name);
	}
}

void encode_prop(BitWriter& writer, const FlattenedProp& flat, const PropValue& value)
{
	encode_single(writer, *flat.prop, flat.array_element_prop, value);
}

void write_prop_changes(BitWriter& writer, const ServerClass& server_class, const std::vector<PropChange>& changes)
{
	int last_index = -1;
	for (const auto& change : changes) {
		if (change.index <= last_index || change.index >= static_cast<int>(server_class.flattened_props.size())) {
			throw std::runtime_error("Prop index " + std::to_string(change.index) + " out of order or range for "
				+ server_class.class_name);
		}
		writer.write_bool(true);
		writer.write_ubit_var(static_cast<uint32_t>(change.index - last_index - 1));
		encode_prop(writer, server_class.flattened_props[change.index], change.value);
		last_index = change.index;
	}
	writer.write_bool(false);
}

bool props_equal(const FlattenedProp& flat, const PropValue& a, const PropValue& b)
{
	BitWriter wa, wb;
	encode_prop(wa, flat, a);
	encode_prop(wb, flat, b);
	return wa.position() == wb.position() && wa.bytes() == wb.bytes();
}

std::vector<PropChange> diff_props(const ServerClass& server_class, const std::vector<PropValue>& from,
	const std::vector<PropValue>& to)
{
	std::vector<PropChange> changes;
	static const PropValue unset;
	for (size_t i = 0; i < server_class.flattened_props.size(); i++) {
		const PropValue& a = i < from.size() ? from[i] : unset;
		const PropValue& b = i < to.size() ? to[i] : unset;
		if (!props_equal(server_class.flattened_props[i], a, b)) {
			changes.push_back({ static_cast<int>(i), b });
		}
	}
	return changes;
}

}
