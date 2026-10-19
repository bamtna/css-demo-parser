#include "cssdemo/cssdemo.h"
#include "cssdemo/Util/BitReader.h"
#include "cssdemo/Util/BitWriter.h"
#include "cssdemo/Util/Compression.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace cssdemo;

namespace {

int failures = 0;
int checks = 0;

#define CHECK(cond) \
	do { \
		checks++; \
		if (!(cond)) { \
			failures++; \
			std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK(" #cond ") failed\n"; \
		} \
	} while (0)

#define CHECK_THROWS(expr) \
	do { \
		checks++; \
		bool threw = false; \
		try { expr; } catch (...) { threw = true; } \
		if (!threw) { \
			failures++; \
			std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK_THROWS(" #expr ") didn't throw\n"; \
		} \
	} while (0)

bool near(float a, float b, float epsilon = 0.001f) { return std::fabs(a - b) <= epsilon; }

class RawBits {
public:
	std::vector<std::byte> bytes;
	size_t num_bits = 0;

	void bits(uint32_t value, int count) {
		for (int i = 0; i < count; i++) {
			if (num_bits % 8 == 0) bytes.push_back(std::byte{ 0 });
			if ((value >> i) & 1) bytes.back() |= std::byte(1 << (num_bits % 8));
			num_bits++;
		}
	}
	void bit(bool value) { bits(value ? 1 : 0, 1); }
	void u8(uint8_t value) { bits(value, 8); }
	void u16(uint16_t value) { bits(value, 16); }
	void u32(uint32_t value) { bits(value, 32); }
	void f32(float value) {
		uint32_t raw;
		std::memcpy(&raw, &value, sizeof(raw));
		u32(raw);
	}
	void string(const std::string& s) {
		for (char c : s) u8(static_cast<uint8_t>(c));
		u8(0);
	}
	void ubit_var(uint32_t value) {
		if (value < 0x10) { bits(0, 2); bits(value, 4); }
		else if (value < 0x100) { bits(1, 2); bits(value, 8); }
		else if (value < 0x1000) { bits(2, 2); bits(value, 12); }
		else { bits(3, 2); bits(value, 32); }
	}
	void var_int(uint32_t value) {
		while (value >= 0x80) {
			u8(static_cast<uint8_t>(value | 0x80));
			value >>= 7;
		}
		u8(static_cast<uint8_t>(value));
	}

	BitReader reader() const { return BitReader(bytes, num_bits); }
};

std::vector<std::byte> to_bytes(std::initializer_list<int> values)
{
	std::vector<std::byte> result;
	for (int v : values) result.push_back(static_cast<std::byte>(v));
	return result;
}

void test_bit_reader()
{
	RawBits w;
	w.bits(5, 3);
	w.u16(0x1234);
	w.bits(static_cast<uint32_t>(-5) & 0x7F, 7);
	w.u32(0xFFFFFFFF);
	w.ubit_var(5);
	w.ubit_var(200);
	w.ubit_var(3000);
	w.ubit_var(100000);
	w.var_int(300);
	w.var_int(5); // zigzag -3
	w.string("abcdef");
	w.u8('x');

	BitReader r = w.reader();
	CHECK(r.read_bits(3) == 5);
	CHECK(r.read_uint16() == 0x1234);
	CHECK(r.read_signed_bits(7) == -5);
	CHECK(r.read_uint32() == 0xFFFFFFFF);
	CHECK(r.read_ubit_var() == 5);
	CHECK(r.read_ubit_var() == 200);
	CHECK(r.read_ubit_var() == 3000);
	CHECK(r.read_ubit_var() == 100000);
	CHECK(r.read_var_int32() == 300);
	CHECK(r.read_signed_var_int32() == -3);
	CHECK(r.read_string(4) == "abc");
	CHECK(r.read_uint8() == 'x');
	CHECK(r.at_end());
	CHECK_THROWS(r.read_bits(1));

	RawBits partial;
	partial.bits(0x1F, 5);
	BitReader bounded = partial.reader();
	CHECK_THROWS(bounded.read_bits(6));
}

void test_coords()
{
	RawBits w;
	// 12.5: integer and fraction present, positive, int-1 = 11, frac = 16/32
	w.bit(1); w.bit(1); w.bit(0); w.bits(11, COORD_INTEGER_BITS); w.bits(16, COORD_FRACTIONAL_BITS);
	// -0.25: fraction only, negative
	w.bit(0); w.bit(1); w.bit(1); w.bits(8, COORD_FRACTIONAL_BITS);
	// 0
	w.bit(0); w.bit(0);
	// coord mp, in bounds, integer + fraction: 3.5
	w.bit(1); w.bit(1); w.bit(0); w.bits(2, COORD_INTEGER_BITS_MP); w.bits(16, COORD_FRACTIONAL_BITS);
	// normal: -0.5
	w.bit(1); w.bits(1024, NORMAL_FRACTIONAL_BITS);

	BitReader r = w.reader();
	CHECK(near(r.read_bit_coord(), 12.5f));
	CHECK(near(r.read_bit_coord(), -0.25f));
	CHECK(r.read_bit_coord() == 0.0f);
	CHECK(near(r.read_bit_coord_mp(false, false), 3.5f));
	CHECK(near(r.read_bit_normal(), -0.5f, 0.001f));
	CHECK(r.at_end());
}

void test_compression()
{
	// literal "abc", then copy 9 bytes from 3 back
	auto snappy = to_bytes({ 'S', 'N', 'A', 'P', 12, 8, 'a', 'b', 'c', (5 << 2) | 1, 3 });
	auto out = decompress_buffer(snappy);
	CHECK(std::string(reinterpret_cast<const char*>(out.data()), out.size()) == "abcabcabcabc");

	// 3 literals, a back-reference (distance 3, count 3), then the terminator
	auto lzss = to_bytes({ 'L', 'Z', 'S', 'S', 6, 0, 0, 0, 0x18, 'a', 'b', 'c', 0x00, 0x22, 0x00, 0x00 });
	out = decompress_buffer(lzss);
	CHECK(std::string(reinterpret_cast<const char*>(out.data()), out.size()) == "abcabc");

	auto plain = to_bytes({ 1, 2, 3 });
	CHECK(decompress_buffer(plain) == plain);

	CHECK_THROWS(decompress_buffer(to_bytes({ 'S', 'N', 'A', 'P', 12, 8, 'a' })));
}

void test_props()
{
	std::vector<SendProp> props(7);
	props[0] = { SendPropType::Int, "a", SPROP_UNSIGNED, "", 0, 0, 0, 8 };
	props[1] = { SendPropType::Int, "b", 0, "", 0, 0, 0, 6 };
	props[2] = { SendPropType::Int, "c", SPROP_VARINT, "", 0, 0, 0, 32 };
	props[3] = { SendPropType::Float, "d", 0, "", 0, 0.0f, 100.0f, 10 };
	props[4] = { SendPropType::String, "e", 0, "", 0, 0, 0, 0 };
	props[5] = { SendPropType::Int, "f_element", SPROP_UNSIGNED | SPROP_INSIDEARRAY, "", 0, 0, 0, 4 };
	props[6] = { SendPropType::Array, "f", 0, "", 3, 0, 0, 0 };

	ServerClass server_class;
	server_class.class_name = "CTest";
	server_class.flattened_props = {
		{ &props[0], nullptr, "a" },
		{ &props[1], nullptr, "b" },
		{ &props[2], nullptr, "c" },
		{ &props[3], nullptr, "d" },
		{ &props[4], nullptr, "e" },
		{ &props[6], &props[5], "f" },
	};

	RawBits w;
	w.bit(1); w.ubit_var(0); w.bits(200, 8);                // a = 200
	w.bit(1); w.ubit_var(1); w.var_int(5);                  // c = -3
	w.bit(1); w.ubit_var(0); w.bits(1023, 10);              // d = 100
	w.bit(1); w.ubit_var(0); w.bits(2, DT_MAX_STRING_BITS); w.u8('h'); w.u8('i'); // e = "hi"
	w.bit(1); w.ubit_var(0); w.bits(2, 2); w.bits(7, 4); w.bits(9, 4); // f = [7, 9]
	w.bit(0);

	BitReader r = w.reader();
	auto changes = read_prop_changes(r, server_class);
	CHECK(r.at_end());
	CHECK(changes.size() == 5);
	if (changes.size() == 5) {
		CHECK(changes[0].index == 0 && changes[0].value.as_int() == 200);
		CHECK(changes[1].index == 2 && changes[1].value.as_int() == -3);
		CHECK(changes[2].index == 3 && near(changes[2].value.as_float(), 100.0f));
		CHECK(changes[3].index == 4 && changes[3].value.as_string() == "hi");
		const auto* array = std::get_if<PropArray>(&changes[4].value.value);
		CHECK(changes[4].index == 5 && array && array->size() == 2 && (*array)[1].as_int() == 9);
	}

	RawBits bad;
	bad.bit(1); bad.ubit_var(10);
	BitReader bad_reader = bad.reader();
	CHECK_THROWS(read_prop_changes(bad_reader, server_class));
}

void test_string_table()
{
	StringTable table;
	table.name = "test";
	table.max_entries = 16;

	RawBits w;
	w.bit(1); w.bit(1); w.bit(0); w.string("maps/a.bsp"); w.bit(1); w.bits(2, MAX_USERDATA_BITS); w.u8(0xAB); w.u8(0xCD);
	w.bit(1); w.bit(1); w.bit(1); w.bits(0, 5); w.bits(5, SUBSTRING_BITS); w.string("b.bsp"); w.bit(0);
	w.bit(0); w.bits(0, 4); w.bit(0); w.bit(0); // entry 0 again, no name, no data

	BitReader r = w.reader();
	auto changed = table.parse_update(r, 3);
	CHECK(r.at_end());
	CHECK((changed == std::vector<int>{ 0, 1, 0 }));
	CHECK(table.entries.size() == 2);
	if (table.entries.size() == 2) {
		CHECK(table.entries[0].name == "maps/a.bsp");
		CHECK(table.entries[0].data.empty());
		CHECK(table.entries[1].name == "maps/b.bsp");
	}

	std::vector<std::byte> info(132);
	std::memcpy(info.data(), "bob", 3);
	int user_id = 7;
	std::memcpy(info.data() + 32, &user_id, 4);
	std::memcpy(info.data() + 36, "STEAM_0:1:2", 11);
	info[108] = std::byte{ 1 };
	auto player = parse_player_info(info);
	CHECK(player && player->name == "bob" && player->user_id == 7 && player->guid == "STEAM_0:1:2" && player->fake_player);
	CHECK(!parse_player_info(std::vector<std::byte>(10)));
}

void test_user_cmd()
{
	RawBits w;
	w.bit(1); w.u32(100);
	w.bit(1); w.u32(200);
	w.bit(1); w.f32(10.5f);
	w.bit(0); w.bit(0);
	w.bit(1); w.f32(450.0f);
	w.bit(0); w.bit(0);
	w.bit(1); w.u32(6);
	w.bit(0); w.bit(0); w.bit(0); w.bit(0);

	UserCmd cmd = decode_user_cmd(w.bytes);
	CHECK(cmd.command_number == 100);
	CHECK(cmd.tick_count == 200);
	CHECK(cmd.view_angles.x == 10.5f);
	CHECK(cmd.forward_move == 450.0f);
	CHECK(cmd.buttons == 6);
	CHECK(cmd.button_names() == "jump|duck");
}

void test_demo(const std::filesystem::path& path)
{
	std::cerr << "  " << path.filename().string() << ": ";
	DemoFile demo(path.string());
	DemoParser parser(demo);
	// the recording player may spectate the whole demo
	PlayerTrackerOptions tracker_options;
	tracker_options.include_dead = true;
	PlayerTracker tracker(tracker_options);
	std::ostringstream dump;
	TextDumper dumper(dump, parser.state(), DumpOptions{ true, true, true, true, true, true, true });
	parser.add_listener(tracker);
	parser.add_listener(dumper);
	parser.run();

	for (const auto& error : parser.errors()) {
		std::cerr << "\n    [tick " << error.tick << "] " << error.message;
	}
	CHECK(parser.errors().empty());
	CHECK(!demo.truncated());

	const GameState& state = parser.state();
	DemoKind kind = demo_kind(state);
	CHECK(kind != DemoKind::Unknown);
	CHECK(!state.data_tables.empty());
	CHECK(!state.entities.all().empty());
	CHECK(!tracker.samples().empty());

	size_t samples = 0, precise = 0, estimated = 0, with_cmd = 0;
	for (const auto& [user_id, list] : tracker.samples()) {
		for (const auto& s : list) {
			samples++;
			precise += s.player.origin_precise;
			estimated += s.velocity_estimated;
			with_cmd += s.user_cmd.has_value();
		}
	}
	if (kind == DemoKind::Client) {
		CHECK(precise > 0);
		CHECK(with_cmd > 0);
	}
	else {
		CHECK(precise == 0);
		CHECK(with_cmd == 0);
	}
	CHECK(estimated > 0 || kind == DemoKind::Client);
	std::cerr << demo_kind_name(kind) << ", " << samples << " player samples\n";
}

bool same_bits(const BitBuffer& a, const BitBuffer& b)
{
	if (a.num_bits != b.num_bits) return false;
	BitReader ra = a.reader(), rb = b.reader();
	while (!ra.at_end()) {
		int n = static_cast<int>(std::min<size_t>(32, ra.bits_left()));
		if (ra.read_bits(n) != rb.read_bits(n)) return false;
	}
	return true;
}

// bytes start with the bits of b, the rest of the last byte can be anything
bool same_bits_padded(std::span<const std::byte> bytes, const BitBuffer& b)
{
	if (bytes.size() != (b.num_bits + 7) / 8) return false;
	return same_bits(BitBuffer(std::vector<std::byte>(bytes.begin(), bytes.end()), b.num_bits), b);
}

uint32_t next_random()
{
	static uint32_t state = 12345;
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}

uint32_t random_bits(int n)
{
	return n >= 32 ? next_random() : next_random() & ((1u << n) - 1);
}

void test_bit_writer()
{
	BitWriter w;
	w.write_bits(5, 3);
	w.write_uint16(0x1234);
	w.write_signed_bits(-5, 7);
	w.write_uint32(0xFFFFFFFF);
	w.write_ubit_var(5);
	w.write_ubit_var(200);
	w.write_ubit_var(3000);
	w.write_ubit_var(100000);
	w.write_var_int32(300);
	w.write_signed_var_int32(-3);
	w.write_string("abcdef");
	w.write_float32(-1.5f);
	w.write_bit_angle(90.0f, 16);
	w.write_fixed_string("hi", 4);

	BitReader r = w.reader();
	CHECK(r.read_bits(3) == 5);
	CHECK(r.read_uint16() == 0x1234);
	CHECK(r.read_signed_bits(7) == -5);
	CHECK(r.read_uint32() == 0xFFFFFFFF);
	CHECK(r.read_ubit_var() == 5);
	CHECK(r.read_ubit_var() == 200);
	CHECK(r.read_ubit_var() == 3000);
	CHECK(r.read_ubit_var() == 100000);
	CHECK(r.read_var_int32() == 300);
	CHECK(r.read_signed_var_int32() == -3);
	CHECK(r.read_string() == "abcdef");
	CHECK(r.read_float32() == -1.5f);
	CHECK(r.read_bit_angle(16) == 90.0f);
	CHECK(r.read_fixed_string(4) == "hi");
	CHECK(r.at_end());

	// matches the hand written reference
	RawBits raw;
	raw.ubit_var(70000);
	raw.var_int(1u << 30);
	BitWriter w2;
	w2.write_ubit_var(70000);
	w2.write_var_int32(1u << 30);
	CHECK(w2.bytes() == raw.bytes && w2.position() == raw.num_bits);

	// varints of every length and the signed edges
	for (uint32_t v : { 0u, 127u, 128u, 16383u, 16384u, 0x0FFFFFFFu, 0xFFFFFFFFu }) {
		BitWriter vw;
		vw.write_var_int32(v);
		BitReader vr = vw.reader();
		CHECK(vr.read_var_int32() == v && vr.at_end());
	}
	for (int32_t v : { 0, -1, 1, INT32_MIN, INT32_MAX }) {
		BitWriter vw;
		vw.write_signed_var_int32(v);
		BitReader vr = vw.reader();
		CHECK(vr.read_signed_var_int32() == v);
	}
}

bool same_value(float a, float b) { return a == b; }
bool same_value(const Vector& a, const Vector& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

// encode(decode(bits)) == bits for random bit patterns
template <typename Read, typename Write>
void check_bits_round_trip(int bits_needed, Read read, Write write, bool strict, int iterations = 4000)
{
	int failed = 0;
	for (int i = 0; i < iterations; i++) {
		RawBits raw;
		for (int b = 0; b < bits_needed; b += 32) raw.bits(next_random(), std::min(32, bits_needed - b));
		BitReader reader = raw.reader();
		auto value = read(reader);
		size_t used = reader.position();
		BitWriter writer;
		write(writer, value);
		BitBuffer encoded = writer.take_buffer();
		BitReader original = raw.reader();
		BitBuffer expected = original.read_buffer(used);
		if (strict) {
			if (!same_bits(encoded, expected)) failed++;
			continue;
		}
		// non-canonical inputs (flags for zero parts, out of bounds flag for small values) can't come back,
		// the value and the writer's own bits do
		BitReader again = encoded.reader();
		auto decoded = read(again);
		BitWriter rewriter;
		write(rewriter, decoded);
		// a coord can't be -0 without the zero flags
		if (!same_value(decoded, value) || !same_bits(rewriter.take_buffer(), encoded)) failed++;
	}
	CHECK(failed == 0);
}

void test_coord_encoding()
{
	check_bits_round_trip(64, [](BitReader& r) { return r.read_bit_coord(); }, [](BitWriter& w, float v) { w.write_bit_coord(v); }, false);
	check_bits_round_trip(64, [](BitReader& r) { return r.read_bit_normal(); }, [](BitWriter& w, float v) { w.write_bit_normal(v); }, true);
	for (int mode = 0; mode < 4; mode++) {
		bool integral = mode == 1, low = mode == 2;
		check_bits_round_trip(64, [&](BitReader& r) { return r.read_bit_coord_mp(integral, low); },
			[&](BitWriter& w, float v) { w.write_bit_coord_mp(v, integral, low); }, false);
	}
	check_bits_round_trip(128, [](BitReader& r) { return r.read_bit_vec3_coord(); },
		[](BitWriter& w, const Vector& v) { w.write_bit_vec3_coord(v); }, false);

	// decode(encode(x)) == x for values on the grid
	for (float v : { 0.0f, 12.5f, -0.25f, 16383.96875f, -3000.03125f }) {
		BitWriter w;
		w.write_bit_coord(v);
		BitReader r = w.reader();
		CHECK(r.read_bit_coord() == v);
	}
	for (float v : { 0.0f, 3.5f, -2047.0f, -0.125f, 5000.25f }) {
		BitWriter w;
		w.write_bit_coord_mp(v, false, false);
		w.write_bit_coord_mp(std::trunc(v), true, false);
		w.write_bit_coord_mp(v, false, true);
		BitReader r = w.reader();
		CHECK(r.read_bit_coord_mp(false, false) == v);
		CHECK(r.read_bit_coord_mp(true, false) == std::trunc(v));
		CHECK(r.read_bit_coord_mp(false, true) == v);
	}
	// engine truncation for values off the grid
	BitWriter w;
	w.write_bit_coord(1.99f);
	BitReader r = w.reader();
	CHECK(r.read_bit_coord() == 1.96875f);
}

struct TestClass {
	std::vector<SendProp> props;
	ServerClass server_class;
};

TestClass make_test_class()
{
	TestClass t;
	t.props = {
		{ SendPropType::Int, "u8", SPROP_UNSIGNED, "", 0, 0, 0, 8 },
		{ SendPropType::Int, "s6", 0, "", 0, 0, 0, 6 },
		{ SendPropType::Int, "s32", 0, "", 0, 0, 0, 32 },
		{ SendPropType::Int, "var", SPROP_VARINT, "", 0, 0, 0, 32 },
		{ SendPropType::Int, "uvar", SPROP_VARINT | SPROP_UNSIGNED, "", 0, 0, 0, 32 },
		{ SendPropType::Float, "scaled", 0, "", 0, -100.0f, 100.0f, 10 },
		{ SendPropType::Float, "angle", 0, "", 0, 0.0f, 360.0f, 11 },
		{ SendPropType::Float, "fine", SPROP_ROUNDDOWN, "", 0, 0.0f, 1.0f, 20 },
		{ SendPropType::Float, "noscale", SPROP_NOSCALE, "", 0, 0, 0, 0 },
		{ SendPropType::Float, "coord", SPROP_COORD, "", 0, 0, 0, 0 },
		{ SendPropType::Float, "coordmp", SPROP_COORD_MP, "", 0, 0, 0, 0 },
		{ SendPropType::Float, "coordmplow", SPROP_COORD_MP_LOWPRECISION, "", 0, 0, 0, 0 },
		{ SendPropType::Float, "coordmpint", SPROP_COORD_MP_INTEGRAL, "", 0, 0, 0, 0 },
		{ SendPropType::Float, "normal", SPROP_NORMAL, "", 0, 0, 0, 0 },
		{ SendPropType::Vector, "vec", SPROP_COORD, "", 0, 0, 0, 0 },
		{ SendPropType::Vector, "nvec", SPROP_NORMAL, "", 0, 0, 0, 0 },
		{ SendPropType::Vector, "fvec", SPROP_NOSCALE, "", 0, 0, 0, 0 },
		{ SendPropType::VectorXY, "xy", 0, "", 0, -4096.0f, 4096.0f, 16 },
		{ SendPropType::String, "str", 0, "", 0, 0, 0, 0 },
		{ SendPropType::Int, "elem", SPROP_UNSIGNED | SPROP_INSIDEARRAY, "", 0, 0, 0, 4 },
		{ SendPropType::Array, "arr", 0, "", 5, 0, 0, 0 },
	};
	t.server_class.class_name = "CTest";
	for (size_t i = 0; i < t.props.size(); i++) {
		if (t.props[i].has_flag(SPROP_INSIDEARRAY)) continue;
		FlattenedProp flat{ &t.props[i], nullptr, t.props[i].name };
		if (t.props[i].type == SendPropType::Array) flat.array_element_prop = &t.props[i - 1];
		t.server_class.prop_lookup[flat.name] = static_cast<int>(t.server_class.flattened_props.size());
		t.server_class.flattened_props.push_back(flat);
	}
	return t;
}

void test_prop_encoding()
{
	TestClass t = make_test_class();
	auto& flats = t.server_class.flattened_props;

	// encode(decode(bits)) == bits
	for (const auto& flat : flats) {
		if (flat.prop->type == SendPropType::String) continue;
		// every bit pattern is one the writer produces, except for coords
		bool canonical = !flat.prop->has_flag(SPROP_COORD | SPROP_COORD_MP | SPROP_COORD_MP_LOWPRECISION | SPROP_COORD_MP_INTEGRAL);
		int failed = 0;
		for (int i = 0; i < 2000; i++) {
			RawBits raw;
			if (flat.prop->type == SendPropType::Array) {
				raw.bits(next_random() % 6, 3);
			}
			else if (flat.prop->has_flag(SPROP_VARINT)) {
				uint32_t v = next_random() >> (next_random() % 32);
				raw.var_int(v);
			}
			for (int b = 0; b < 4; b++) raw.bits(next_random(), 32);
			BitReader reader = raw.reader();
			PropValue value = decode_prop(reader, flat);
			size_t used = reader.position();
			BitWriter writer;
			encode_prop(writer, flat, value);
			BitBuffer encoded = writer.take_buffer();
			BitReader original = raw.reader();
			if (canonical) {
				if (!same_bits(encoded, original.read_buffer(used))) failed++;
				continue;
			}
			BitReader again = encoded.reader();
			PropValue decoded = decode_prop(again, flat);
			BitWriter rewriter;
			encode_prop(rewriter, flat, decoded);
			if (!props_equal(flat, decoded, value) || !same_bits(rewriter.take_buffer(), encoded)) failed++;
			if (flat.prop->type == SendPropType::Float && decoded.as_float() != value.as_float()
				&& !(std::isnan(decoded.as_float()) && std::isnan(value.as_float()))) failed++;
		}
		if (failed) std::cerr << "  " << flat.name << ": " << failed << " mismatches\n";
		CHECK(failed == 0);
	}

	// decode(encode(x)) == x where x is representable, nearest otherwise
	auto round_trip = [&](const char* name, PropValue value) {
		const auto& flat = flats[t.server_class.find_prop(name)];
		BitWriter writer;
		encode_prop(writer, flat, value);
		BitReader reader = writer.reader();
		PropValue decoded = decode_prop(reader, flat);
		CHECK(reader.at_end());
		return decoded;
	};
	CHECK(round_trip("u8", { 200 }).as_int() == 200);
	CHECK(round_trip("s6", { -32 }).as_int() == -32);
	CHECK(round_trip("s32", { INT32_MIN }).as_int() == INT32_MIN);
	CHECK(round_trip("var", { -123456 }).as_int() == -123456);
	CHECK(round_trip("uvar", { 99999 }).as_int() == 99999);
	CHECK(round_trip("scaled", { 100.0f }).as_float() == 100.0f);
	CHECK(round_trip("scaled", { -100.0f }).as_float() == -100.0f);
	CHECK(round_trip("scaled", { 500.0f }).as_float() == 100.0f); // clamped
	CHECK(near(round_trip("scaled", { 33.3f }).as_float(), 33.3f, 200.0f / 1023));
	CHECK(near(round_trip("angle", { 359.9f }).as_float(), 359.9f, 360.0f / 2047));
	CHECK(round_trip("noscale", { 1.0e-7f }).as_float() == 1.0e-7f);
	CHECK(round_trip("coord", { -1234.5f }).as_float() == -1234.5f);
	CHECK(round_trip("coordmp", { 77.75f }).as_float() == 77.75f);
	CHECK(round_trip("coordmplow", { -9.125f }).as_float() == -9.125f);
	CHECK(round_trip("coordmpint", { 42.0f }).as_float() == 42.0f);
	CHECK(near(round_trip("normal", { -0.70710677f }).as_float(), -0.70710677f, 0.001f));
	Vector v = round_trip("vec", { Vector{ 1.5f, -2.0f, 3.03125f } }).as_vector();
	CHECK(v.x == 1.5f && v.y == -2.0f && v.z == 3.03125f);
	v = round_trip("nvec", { Vector{ 0.6f, 0.0f, -0.8f } }).as_vector();
	CHECK(near(v.x, 0.6f) && near(v.z, -0.8f));
	v = round_trip("fvec", { Vector{ 1.1f, 2.2f, -3.3f } }).as_vector();
	CHECK(v.x == 1.1f && v.y == 2.2f && v.z == -3.3f);
	CHECK(round_trip("str", { std::string("hello") }).as_string() == "hello");
	PropValue arr = round_trip("arr", { PropArray{ PropValue{ 1 }, PropValue{ 15 } } });
	const auto* elements = std::get_if<PropArray>(&arr.value);
	CHECK(elements && elements->size() == 2 && (*elements)[1].as_int() == 15);
	CHECK(round_trip("u8", {}).as_int() == 0);

	// change lists
	std::vector<PropChange> changes = {
		{ 0, PropValue{ 7 } }, { 3, PropValue{ -5 } }, { 18, PropValue{ std::string("x") } },
	};
	BitWriter writer;
	write_prop_changes(writer, t.server_class, changes);
	BitReader reader = writer.reader();
	auto decoded = read_prop_changes(reader, t.server_class);
	CHECK(reader.at_end());
	CHECK(decoded.size() == 3 && decoded[1].index == 3 && decoded[1].value.as_int() == -5 && decoded[2].index == 18);
	BitWriter bad;
	CHECK_THROWS(write_prop_changes(bad, t.server_class, { { 3, PropValue{ 1 } }, { 2, PropValue{ 1 } } }));

	std::vector<PropValue> from(flats.size()), to(flats.size());
	to[0] = PropValue{ 1 };
	to[5] = PropValue{ 0.0f };      // encodes like unset
	from[9] = PropValue{ 2.0f };
	to[9] = PropValue{ 2.0f };
	auto diff = diff_props(t.server_class, from, to);
	CHECK(diff.size() == 1 && diff[0].index == 0);
}

void test_entity_writing()
{
	TestClass t = make_test_class();
	t.server_class.class_id = 3;
	std::vector<EntityUpdate> updates(4);
	updates[0] = { EntityUpdate::Kind::Enter, 1, &t.server_class, 77, { { 0, PropValue{ 9 } } } };
	updates[1] = { EntityUpdate::Kind::Delta, 20, &t.server_class, 0, { { 9, PropValue{ 1.5f } } } };
	updates[2] = { EntityUpdate::Kind::Leave, 300, nullptr, 0, {} };
	updates[3] = { EntityUpdate::Kind::Delete, 301, nullptr, 0, {} };
	std::vector<int> deletions = { 500 };

	BitWriter writer;
	write_entity_updates(writer, updates, deletions, true, 4);
	BitReader reader = writer.reader();
	int index = -1;
	index += 1 + reader.read_ubit_var();
	CHECK(index == 1 && !reader.read_bool() && reader.read_bool());
	CHECK(reader.read_bits(4) == 3 && reader.read_bits(NUM_NETWORKED_EHANDLE_SERIAL_NUMBER_BITS) == 77);
	auto changes = read_prop_changes(reader, t.server_class);
	CHECK(changes.size() == 1 && changes[0].value.as_int() == 9);
	index += 1 + reader.read_ubit_var();
	CHECK(index == 20 && !reader.read_bool() && !reader.read_bool());
	changes = read_prop_changes(reader, t.server_class);
	CHECK(changes.size() == 1 && changes[0].value.as_float() == 1.5f);
	index += 1 + reader.read_ubit_var();
	CHECK(index == 300 && reader.read_bool() && !reader.read_bool());
	index += 1 + reader.read_ubit_var();
	CHECK(index == 301 && reader.read_bool() && reader.read_bool());
	CHECK(reader.read_bool() && reader.read_bits(MAX_EDICT_BITS) == 500 && !reader.read_bool());
	CHECK(reader.at_end());

	BitWriter bad;
	std::vector<EntityUpdate> unordered = { updates[1], updates[0] };
	CHECK_THROWS(write_entity_updates(bad, unordered, {}, false, 4));
}

void test_string_table_writing()
{
	StringTable table;
	table.name = "test";
	table.max_entries = 64;

	std::vector<StringTableEntryUpdate> updates = {
		{ 0, "models/player/ct_gign.mdl", std::vector<std::byte>{ std::byte{ 1 }, std::byte{ 2 } } },
		{ 1, "models/player/ct_sas.mdl", std::nullopt },
		{ 5, "abc", std::nullopt },
		{ 0, std::nullopt, std::vector<std::byte>{ std::byte{ 9 } } },
	};
	BitBuffer data = write_string_table_update(table, updates);
	StringTable parsed = table;
	BitReader reader = data.reader();
	std::vector<StringTableEntryUpdate> sent;
	auto changed = parsed.parse_update(reader, static_cast<int>(updates.size()), &sent);
	CHECK(reader.at_end());
	CHECK((changed == std::vector<int>{ 0, 1, 5, 0 }));
	CHECK(parsed.entries.size() == 6);
	if (parsed.entries.size() == 6) {
		CHECK(parsed.entries[1].name == "models/player/ct_sas.mdl");
		CHECK(parsed.entries[0].data == std::vector<std::byte>{ std::byte{ 9 } });
	}
	// the writer picks the same substrings as it gets back
	CHECK(same_bits(write_string_table_update(table, sent), data));
	// shared prefix actually used
	CHECK(data.num_bits < (25 + 24 + 3) * 8 + 4 * 8);

	StringTable fixed;
	fixed.name = "fixed";
	fixed.max_entries = 8;
	fixed.user_data_fixed_size = true;
	fixed.user_data_size = 1;
	fixed.user_data_size_bits = 2;
	BitBuffer fixed_data = write_string_table_update(fixed, std::vector<StringTableEntryUpdate>{ { 0, "a", std::vector<std::byte>{ std::byte{ 3 } } } });
	BitReader fixed_reader = fixed_data.reader();
	fixed.parse_update(fixed_reader, 1);
	CHECK(fixed_reader.at_end() && fixed.entries[0].data == std::vector<std::byte>{ std::byte{ 3 } });

	StringTable full = parsed;
	full.id = 2;
	SvcCreateStringTable create = make_create_string_table(full);
	StringTables tables;
	tables.create(create);
	CHECK(tables.get(0) && tables.get(0)->entries.size() == 6 && tables.get(0)->entries[5].name == "abc");

	std::vector<StringTable> snapshot(2);
	snapshot[0].name = "a";
	snapshot[0].entries = { { "x", { std::byte{ 1 } } }, { "y", {} } };
	snapshot[1].name = "b";
	snapshot[1].has_client_entries = true;
	snapshot[1].client_entries = { { "z", {} } };
	auto bytes = write_string_table_snapshot(snapshot);
	auto read = read_string_table_snapshot(bytes);
	CHECK(read.size() == 2 && read[0].entries.size() == 2 && read[0].entries[0].data.size() == 1
		&& read[1].has_client_entries && read[1].client_entries[0].name == "z");
	CHECK(write_string_table_snapshot(read) == bytes);

	PlayerInfo info;
	info.name = "runner";
	info.user_id = 12;
	info.guid = "STEAM_1:0:1";
	info.fake_player = true;
	auto player = parse_player_info(write_player_info(info));
	CHECK(player && player->name == "runner" && player->user_id == 12 && player->guid == "STEAM_1:0:1" && player->fake_player);
}

void test_message_writing()
{
	ProtocolVersion protocol;
	SvcServerInfo info;
	info.protocol = 24;
	info.server_count = 3;
	info.max_classes = 200;
	info.map_md5.assign(16, std::byte{ 0xAB });
	info.player_slot = 1;
	info.max_clients = 32;
	info.tick_interval = 0.015f;
	info.os = 'w';
	info.game_dir = "cstrike";
	info.map_name = "surf_test";
	info.sky_name = "sky";
	info.host_name = "host";
	NetTick tick;
	tick.tick = 1234;
	tick.host_frame_time = 0.01499f;
	NetSetConVar convars;
	convars.convars = { { "a", "1" }, { "b", "2" } };
	SvcFixAngle fix;
	fix.angle = { 10.0f, 270.0f, 0.0f };
	SvcPrint print;
	print.text = "hello";

	std::vector<const NetMessage*> messages = { &info, &tick, &convars, &fix, &print };
	auto bytes = write_packet(messages, protocol);
	ParsedPacket packet = parse_packet(bytes, protocol);
	CHECK(packet.error.empty());
	CHECK(write_packet(packet, protocol) == bytes);
	CHECK(packet.messages.size() >= 5);
	if (packet.messages.size() >= 5) {
		const auto& parsed_info = static_cast<const SvcServerInfo&>(*packet.messages[0]);
		CHECK(parsed_info.map_name == "surf_test" && parsed_info.tick_interval == 0.015f && parsed_info.map_md5 == info.map_md5);
		const auto& parsed_tick = static_cast<const NetTick&>(*packet.messages[1]);
		CHECK(parsed_tick.tick == 1234 && near(parsed_tick.host_frame_time, 0.01499f, 0.00001f));
		CHECK(static_cast<const NetSetConVar&>(*packet.messages[2]).convars.size() == 2);
		CHECK(static_cast<const SvcFixAngle&>(*packet.messages[3]).angle.y == 270.0f);
		CHECK(static_cast<const SvcPrint&>(*packet.messages[4]).text == "hello");
	}

	// the nop fills a byte that has room for one
	NetSignonState state;
	state.signon_state = 6;
	NetSetConVar empty;
	std::vector<const NetMessage*> two = { &state, &empty };
	size_t bits = 2 * NETMSG_TYPE_BITS + 8 + 32 + 8;
	auto padded = write_packet(two, protocol);
	CHECK(padded.size() == (bits + 7) / 8);
	ParsedPacket padded_packet = parse_packet(padded, protocol);
	CHECK(padded_packet.messages.size() == (bits % 8 > 0 && bits % 8 <= 2 ? 3u : 2u));

	UserCmd cmd;
	cmd.command_number = 77;
	cmd.tick_count = 1;
	cmd.view_angles = { 10.5f, -170.25f, 0.0f };
	cmd.forward_move = 450.0f;
	cmd.buttons = 6;
	cmd.weapon_select = 12;
	cmd.weapon_subtype = 3;
	cmd.mouse_dx = -4;
	UserCmd decoded = decode_user_cmd(encode_user_cmd(cmd));
	CHECK(decoded.command_number == 77 && decoded.tick_count == 1 && decoded.view_angles.y == -170.25f
		&& decoded.forward_move == 450.0f && decoded.buttons == 6 && decoded.weapon_select == 12
		&& decoded.weapon_subtype == 3 && decoded.mouse_dx == -4 && decoded.mouse_dy == 0);

	UserMessage hint;
	hint.name = "HintText";
	hint.fields = { { "text", std::string("speed 1234") } };
	SvcUserMessage hint_message = encode_user_message(hint);
	UserMessage hint_decoded = decode_user_message(hint_message);
	CHECK(hint_message.msg_type == user_message_type("HintText") && hint_decoded.text == "speed 1234");

	UserMessage say;
	say.name = "SayText2";
	say.fields = { { "client", 1 }, { "chat", 1 }, { "format", std::string("\x01 finished") }, { "player", std::string("a") } };
	UserMessage say_decoded = decode_user_message(encode_user_message(say));
	CHECK(say_decoded.error.empty() && say_decoded.fields.size() == 4
		&& std::get<std::string>(say_decoded.fields[2].second) == "\x01 finished");

	UserMessage hud;
	hud.name = "HudMsg";
	hud.fields = { { "channel", 1 }, { "x", -1.0f }, { "y", 0.1f } };
	CHECK_THROWS(encode_user_message(hud));

	GameEventList events;
	events.add({ 5, "player_death", { { GameEventDescriptor::KeyType::Short, "userid" }, { GameEventDescriptor::KeyType::String, "weapon" },
		{ GameEventDescriptor::KeyType::Bool, "headshot" } } });
	SvcGameEventList list = events.write();
	GameEventList loaded;
	loaded.load(list);
	CHECK(loaded.find(5) && loaded.find(5)->keys.size() == 3);
	GameEvent death;
	death.name = "player_death";
	death.keys = { { "userid", 3 }, { "weapon", std::string("ak47") }, { "headshot", true } };
	GameEvent death_decoded = loaded.decode(loaded.encode(death));
	CHECK(death_decoded.get_int("userid") == 3 && death_decoded.get_string("weapon") == "ak47" && death_decoded.get_int("headshot") == 1);

	SoundInfo sound;
	sound.entity_index = 40;
	sound.sound_num = 100;
	sound.volume = 0.5f;
	sound.origin = { 800.0f, -64.0f, 8.0f };
	sound.delay = 0.2f;
	SoundInfo stop;
	stop.flags = SND_STOP;
	stop.entity_index = 3;
	auto sounds_message = encode_sounds({ sound, stop, sound }, protocol);
	auto sounds = decode_sounds(sounds_message, protocol);
	CHECK(sounds.size() == 3 && sounds[0].entity_index == 40 && near(sounds[0].volume, 0.5f, 0.01f)
		&& sounds[0].origin.x == 800.0f && near(sounds[0].delay, 0.2f, 0.002f) && sounds[1].is_stop() && sounds[2].sound_num == 100);
	CHECK(same_bits(encode_sounds(sounds, protocol).data, sounds_message.data));

	DataTables tables;
	TestClass t = make_test_class();
	{
		// one table, one class
		BitWriter w;
		w.write_bool(true);
		w.write_bool(true);
		SendTable table{ "DT_Test", true, {} };
		for (const auto& prop : t.props) table.props.push_back(prop);
		table.write(w);
		w.write_bool(false);
		w.write_int16(1);
		w.write_int16(0);
		w.write_string("CTest");
		w.write_string("DT_Test");
		auto payload = w.take_bytes();
		tables.parse(payload);
		CHECK(tables.write() == payload);
		CHECK(tables.find_class("CTest") && tables.find_class("CTest")->flattened_props.size() == t.server_class.flattened_props.size());
	}

	const ServerClass* test_class = tables.find_class("CTest");
	if (test_class) {
		std::vector<TempEntity> events_out(3);
		events_out[0].server_class = test_class;
		events_out[0].changes = { { 0, PropValue{ 4 } } };
		events_out[0].delay = 0.25f;
		events_out[1].server_class = test_class;
		events_out[1].changes = { { 1, PropValue{ -2 } } };
		events_out[2].server_class = test_class;
		SvcTempEntities te = encode_temp_entities(events_out, tables);
		auto te_decoded = decode_temp_entities(te, tables);
		CHECK(te.num_entries == 3 && te_decoded.size() == 3 && te_decoded[0].delay == 0.25f
			&& te_decoded[1].props[0].as_int() == 4 && te_decoded[1].props[1].as_int() == -2);

		// from full props, a zero after a non-zero is sent
		std::vector<TempEntity> full(2);
		for (auto& event : full) {
			event.server_class = test_class;
			event.props.resize(test_class->flattened_props.size());
		}
		full[0].props[0] = PropValue{ 5 };
		full[1].props[1] = PropValue{ 3 };
		set_temp_entity_changes(full);
		auto full_decoded = decode_temp_entities(encode_temp_entities(full, tables), tables);
		CHECK(full_decoded.size() == 2 && full_decoded[1].props[0].as_int() == 0 && full_decoded[1].props[1].as_int() == 3);
	}
}

void test_demo_writer()
{
	DemoHeader header;
	header.server_name = "localhost";
	header.client_name = "runner";
	header.map_name = "surf_test";
	header.game_directory = "cstrike";
	DemoWriter writer(header);
	std::vector<std::byte> packet = { std::byte{ 1 }, std::byte{ 2 } };
	CmdInfo info{};
	info.view_origin = { 1, 2, 3 };
	writer.write_packet(DemoCommand::SignOn, 0, info, 1, 2, packet);
	writer.write_data(DemoCommand::DataTables, 0, packet);
	writer.write_sync_tick(0);
	writer.write_console_cmd(0, "say hi");
	writer.write_packet(DemoCommand::Packet, 5, info, 3, 4, packet);
	writer.write_user_cmd(5, 9, packet);
	writer.write_data(DemoCommand::StringTables, 6, packet);
	writer.write_packet(DemoCommand::Packet, 6, info, 3, 4, packet);
	writer.write_stop(7);
	writer.update_header(0.01f);
	CHECK(writer.header().playback_ticks == 7 && writer.header().playback_frames == 2);
	CHECK(writer.header().signon_length == (1 + 4 + 76 + 8 + 4 + 2) + (1 + 4 + 4 + 2));

	DemoFile demo(writer.finish());
	CHECK(demo.header().map_name == "surf_test" && demo.header().playback_ticks == 7);
	DemoFrame frame;
	std::vector<DemoCommand> commands;
	DemoWriter copy(demo.header());
	while (demo.next_frame(frame)) {
		commands.push_back(frame.command);
		copy.write_frame(frame);
		if (frame.command == DemoCommand::ConsoleCmd) CHECK(frame.console_command == "say hi");
		if (frame.command == DemoCommand::UserCmd) CHECK(frame.user_cmd_sequence == 9);
		if (frame.command == DemoCommand::SignOn) CHECK(frame.cmd_info.view_origin.z == 3.0f && frame.out_sequence == 2);
	}
	CHECK(commands.size() == 9 && !demo.truncated());
	CHECK(copy.finish() == writer.finish());
}

// re-serialises every frame and message of a demo, entity/temp entity/user message/game event/sound payloads
// after decoding them
class RoundTrip : public DemoListener {
public:
	explicit RoundTrip(const GameState& game_state) : state(game_state) {}

	void on_net_message(const NetMessage& message) override {
		check_entities();
		using T = NetMessage::Type;
		switch (message.type) {
		case T::svc_packet_entities:
			pending_entities = static_cast<const SvcPacketEntities*>(&message);
			entity_updates.clear();
			break;
		case T::svc_temp_entities:
			if (!state.data_tables.empty()) {
				const auto& te = static_cast<const SvcTempEntities&>(message);
				count(temp_entities, same_bits(encode_temp_entities(decode_temp_entities(te, state.data_tables),
					state.data_tables, te.num_entries == 0).data, te.data));
			}
			break;
		case T::svc_user_message: {
			const auto& um = static_cast<const SvcUserMessage&>(message);
			UserMessage decoded = decode_user_message(um);
			if (decoded.decoded && decoded.error.empty()) {
				count(user_messages, same_bits(encode_user_message(decoded).data, um.data));
			}
			break;
		}
		case T::svc_game_event_list: {
			const auto& list = static_cast<const SvcGameEventList&>(message);
			GameEventList loaded;
			loaded.load(list);
			SvcGameEventList written = loaded.write();
			count(game_events, written.num_events == list.num_events && same_bits(written.data, list.data));
			break;
		}
		case T::svc_game_event: {
			const auto& event = static_cast<const SvcGameEvent&>(message);
			count(game_events, same_bits(state.game_events.encode(state.game_events.decode(event)).data, event.data));
			break;
		}
		case T::svc_sounds: {
			const auto& sounds = static_cast<const SvcSounds&>(message);
			count(this->sounds, same_bits(encode_sounds(decode_sounds(sounds, state.protocol), state.protocol,
				sounds.reliable_sound).data, sounds.data));
			break;
		}
		case T::svc_create_string_table: {
			const auto& create = static_cast<const SvcCreateStringTable&>(message);
			StringTable table;
			table.name = create.table_name;
			table.max_entries = create.max_entries;
			table.user_data_fixed_size = create.user_data_fixed_size;
			table.user_data_size = create.user_data_size;
			table.user_data_size_bits = create.user_data_size_bits;
			StringTable parsed = table;
			std::vector<StringTableEntryUpdate> sent;
			if (create.data_compressed) {
				BitReader reader = create.data.reader();
				reader.read_uint32();
				uint32_t compressed_size = reader.read_uint32();
				auto payload = decompress_buffer(reader.read_bytes(compressed_size));
				BitReader payload_reader(payload);
				parsed.parse_update(payload_reader, create.num_entries, &sent);
				count(string_tables, same_bits_padded(payload, write_string_table_update(table, sent)));
			}
			else {
				BitReader reader = create.data.reader();
				parsed.parse_update(reader, create.num_entries, &sent);
				count(string_tables, same_bits(write_string_table_update(table, sent), create.data));
			}
			break;
		}
		case T::svc_update_string_table: {
			const auto& update = static_cast<const SvcUpdateStringTable&>(message);
			if (const StringTable* before = state.string_tables.get(update.table_id)) {
				StringTable parsed = *before;
				std::vector<StringTableEntryUpdate> sent;
				BitReader reader = update.data.reader();
				parsed.parse_update(reader, update.num_changed_entries, &sent);
				count(string_tables, same_bits(write_string_table_update(*before, sent), update.data));
			}
			break;
		}
		default:
			break;
		}
	}

	void on_entity_update(const EntityUpdate& update) override {
		entity_updates.push_back(update);
	}

	void on_packet_end(const DemoFrame&, const GameState&) override {
		check_entities();
	}

	void on_user_cmd(const UserCmd& cmd) override {
		last_cmd = cmd;
	}

	struct Counter {
		int checked = 0;
		int failed = 0;
	};
	Counter entities, temp_entities, user_messages, game_events, sounds, string_tables;

	void report(const char* name, const Counter& c) const {
		std::cerr << " " << name << " " << c.checked - c.failed << "/" << c.checked;
	}

	std::optional<UserCmd> last_cmd;

private:
	const GameState& state;
	const SvcPacketEntities* pending_entities = nullptr;
	std::vector<EntityUpdate> entity_updates;

	static void count(Counter& c, bool ok) {
		c.checked++;
		if (!ok) c.failed++;
	}

	void check_entities() {
		if (!pending_entities) return;
		const SvcPacketEntities& message = *pending_entities;
		pending_entities = nullptr;
		if (entity_updates.size() < static_cast<size_t>(message.updated_entries)) {
			return; // not decoded
		}
		std::span<const EntityUpdate> header(entity_updates.data(), message.updated_entries);
		std::vector<int> deletions;
		for (size_t i = message.updated_entries; i < entity_updates.size(); i++) {
			deletions.push_back(entity_updates[i].index);
		}
		BitWriter writer;
		write_entity_updates(writer, header, deletions, message.is_delta, state.data_tables.server_class_bits());
		count(entities, same_bits(writer.take_buffer(), message.data));
	}
};

void test_round_trip(const std::filesystem::path& path)
{
	std::cerr << "  " << path.filename().string() << ":";
	std::vector<std::byte> original;
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		original.resize(static_cast<size_t>(file.tellg()));
		file.seekg(0);
		file.read(reinterpret_cast<char*>(original.data()), static_cast<std::streamsize>(original.size()));
	}

	// frames, packets, user commands, tables
	DemoFile demo(original);
	ProtocolVersion protocol{ demo.header().demo_protocol, demo.header().network_protocol };
	DemoWriter writer(demo.header());
	DemoFrame frame;
	int packets = 0, packets_failed = 0, cmds = 0, cmds_failed = 0, tables = 0, tables_failed = 0;
	int data_tables_failed = 0;
	while (demo.next_frame(frame)) {
		if (frame.command == DemoCommand::Stop && demo.stop_without_tick()) {
			writer.write_stop_without_tick(frame.tick, demo.stop_tick_bytes());
			continue;
		}
		writer.write_frame(frame);
		switch (frame.command) {
		case DemoCommand::SignOn:
		case DemoCommand::Packet: {
			ParsedPacket packet = parse_packet(frame.payload, protocol);
			packets++;
			auto bytes = write_packet(packet, protocol);
			if (!packet.error.empty() || !std::equal(bytes.begin(), bytes.end(), frame.payload.begin(), frame.payload.end())) {
				packets_failed++;
			}
			break;
		}
		case DemoCommand::UserCmd: {
			cmds++;
			BitWriter w;
			write_user_cmd(w, decode_user_cmd(frame.payload));
			// the engine leaves stale bits after the command in the last byte
			if (!same_bits_padded(frame.payload, w.take_buffer())) cmds_failed++;
			break;
		}
		case DemoCommand::DataTables: {
			tables++;
			DataTables dt;
			dt.parse(frame.payload);
			BitWriter w;
			dt.write(w);
			// stale bits after the class list
			if (!same_bits_padded(frame.payload, w.take_buffer())) {
				tables_failed++;
				data_tables_failed++;
			}
			break;
		}
		case DemoCommand::StringTables: {
			tables++;
			BitWriter w;
			write_string_table_snapshot(w, read_string_table_snapshot(frame.payload));
			// stale bits after the last table
			if (!same_bits_padded(frame.payload, w.take_buffer())) tables_failed++;
			break;
		}
		default:
			break;
		}
	}
	bool file_same = writer.finish() == original;
	if (const char* dump = std::getenv("CSSDEMO_TEST_DUMP"); dump && !file_same) {
		writer.save((std::filesystem::path(dump) / path.filename()).string());
	}
	CHECK(file_same);
	CHECK(packets_failed == 0);
	CHECK(cmds_failed == 0);
	CHECK(tables_failed == 0);
	std::cerr << " file " << (file_same ? "same" : "DIFFERENT") << ", packets " << packets - packets_failed << "/" << packets
		<< ", usercmds " << cmds - cmds_failed << "/" << cmds << ", tables " << tables - tables_failed << "/" << tables
		<< (data_tables_failed ? " (data tables differ)" : "");

	// decoded payloads
	DemoFile demo2(original);
	DemoParser parser(demo2);
	RoundTrip round_trip(parser.state());
	parser.add_listener(round_trip);
	parser.run();
	round_trip.report("entities", round_trip.entities);
	round_trip.report("tempents", round_trip.temp_entities);
	round_trip.report("usermsgs", round_trip.user_messages);
	round_trip.report("events", round_trip.game_events);
	round_trip.report("sounds", round_trip.sounds);
	round_trip.report("stringtables", round_trip.string_tables);
	// what CDemoRecorder would have put in the header
	writer.update_header(parser.state().server.tick_interval);
	const DemoHeader& computed = writer.header();
	const DemoHeader& actual = demo.header();
	std::string header_diff = std::string(computed.playback_ticks == actual.playback_ticks ? "" : " ticks")
		+ (computed.playback_frames == actual.playback_frames ? "" : " frames")
		+ (computed.playback_time == actual.playback_time ? "" : " time")
		+ (computed.signon_length == actual.signon_length ? "" : " signon");
	std::cerr << ", header " << (header_diff.empty() ? "as recorded" : "differs in" + header_diff) << "\n";
	CHECK(round_trip.entities.failed == 0 && round_trip.entities.checked > 0);
	CHECK(round_trip.temp_entities.failed == 0);
	CHECK(round_trip.user_messages.failed == 0);
	CHECK(round_trip.game_events.failed == 0);
	CHECK(round_trip.sounds.failed == 0);
	CHECK(round_trip.string_tables.failed == 0);
}

} // namespace

int main(int argc, char* argv[])
{
	const std::pair<const char*, std::function<void()>> tests[] = {
		{ "bit reader", test_bit_reader },
		{ "coords", test_coords },
		{ "compression", test_compression },
		{ "props", test_props },
		{ "string tables", test_string_table },
		{ "user cmd", test_user_cmd },
		{ "bit writer", test_bit_writer },
		{ "coord encoding", test_coord_encoding },
		{ "prop encoding", test_prop_encoding },
		{ "entity writing", test_entity_writing },
		{ "string table writing", test_string_table_writing },
		{ "message writing", test_message_writing },
		{ "demo writer", test_demo_writer },
	};
	for (const auto& [name, test] : tests) {
		std::cerr << name << "\n";
		try {
			test();
		}
		catch (const std::exception& e) {
			failures++;
			std::cerr << "  threw: " << e.what() << "\n";
		}
	}

	for (int arg = 1; arg < argc; arg++) {
		if (!std::filesystem::is_directory(argv[arg])) continue;
		std::vector<std::filesystem::path> demos;
		for (const auto& entry : std::filesystem::directory_iterator(argv[arg])) {
			if (entry.path().extension() == ".dem") demos.push_back(entry.path());
		}
		std::sort(demos.begin(), demos.end());
		std::cerr << "demos in " << argv[arg] << "\n";
		for (const auto& path : demos) {
			try {
				test_demo(path);
			}
			catch (const std::exception& e) {
				failures++;
				std::cerr << "threw: " << e.what() << "\n";
			}
		}
		std::cerr << "round trip of demos in " << argv[arg] << "\n";
		for (const auto& path : demos) {
			try {
				test_round_trip(path);
			}
			catch (const std::exception& e) {
				failures++;
				std::cerr << " threw: " << e.what() << "\n";
			}
		}
	}

	std::cerr << checks - failures << "/" << checks << " checks passed\n";
	return failures == 0 ? 0 : 1;
}
