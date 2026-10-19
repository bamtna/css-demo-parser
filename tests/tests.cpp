#include "cssdemo/cssdemo.h"
#include "cssdemo/Util/BitReader.h"
#include "cssdemo/Util/Compression.h"
#include <cmath>
#include <cstring>
#include <filesystem>
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

class BitWriter {
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
	BitWriter w;
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

	BitWriter partial;
	partial.bits(0x1F, 5);
	BitReader bounded = partial.reader();
	CHECK_THROWS(bounded.read_bits(6));
}

void test_coords()
{
	BitWriter w;
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

	BitWriter w;
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

	BitWriter bad;
	bad.bit(1); bad.ubit_var(10);
	BitReader bad_reader = bad.reader();
	CHECK_THROWS(read_prop_changes(bad_reader, server_class));
}

void test_string_table()
{
	StringTable table;
	table.name = "test";
	table.max_entries = 16;

	BitWriter w;
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
	BitWriter w;
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

	if (argc > 1 && std::filesystem::is_directory(argv[1])) {
		std::cerr << "demos in " << argv[1] << "\n";
		for (const auto& entry : std::filesystem::directory_iterator(argv[1])) {
			if (entry.path().extension() != ".dem") continue;
			try {
				test_demo(entry.path());
			}
			catch (const std::exception& e) {
				failures++;
				std::cerr << "threw: " << e.what() << "\n";
			}
		}
	}

	std::cerr << checks - failures << "/" << checks << " checks passed\n";
	return failures == 0 ? 0 : 1;
}
