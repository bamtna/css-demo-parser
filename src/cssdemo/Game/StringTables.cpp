#include "StringTables.h"
#include "cssdemo/Net/NetMessages.h"
#include "cssdemo/Protocol/Constants.h"
#include "cssdemo/Util/BitReader.h"
#include "cssdemo/Util/Compression.h"
#include <cstring>
#include <deque>
#include <stdexcept>

namespace cssdemo {

std::vector<int> StringTable::parse_update(BitReader& reader, int num_changed_entries)
{
	std::vector<int> changed;
	std::deque<std::string> history;
	int entry_bits = Q_log2(max_entries);
	int last_entry = -1;

	for (int i = 0; i < num_changed_entries; i++) {
		int entry_index = last_entry + 1;
		if (!reader.read_bool()) {
			entry_index = reader.read_bits(entry_bits);
		}
		last_entry = entry_index;
		if (entry_index < 0 || entry_index >= max_entries) {
			throw std::runtime_error("Bogus string index " + std::to_string(entry_index) + " for table " + name);
		}

		std::optional<std::string> entry_name;
		if (reader.read_bool()) {
			if (reader.read_bool()) {
				// prefix of one of the last 32 strings
				size_t index = reader.read_bits(5);
				size_t bytes_to_copy = reader.read_bits(SUBSTRING_BITS);
				if (index >= history.size()) {
					throw std::runtime_error("Bogus substring index in table " + name);
				}
				entry_name = history[index].substr(0, bytes_to_copy) + reader.read_string(1024);
			}
			else {
				entry_name = reader.read_string(1024);
			}
		}

		std::vector<std::byte> user_data;
		if (reader.read_bool()) {
			if (user_data_fixed_size) {
				user_data = reader.read_buffer(user_data_size_bits).bytes;
				user_data.resize(user_data_size);
			}
			else {
				user_data = reader.read_bytes(reader.read_bits(MAX_USERDATA_BITS));
			}
		}

		if (entry_index < static_cast<int>(entries.size())) {
			// existing entries keep their name, data is replaced or cleared
			entries[entry_index].data = std::move(user_data);
		}
		else {
			entries.resize(entry_index + 1);
			entries[entry_index].name = entry_name.value_or("");
			entries[entry_index].data = std::move(user_data);
		}
		changed.push_back(entry_index);

		if (history.size() >= STRING_HISTORY_SIZE) {
			history.pop_front();
		}
		history.push_back(entries[entry_index].name);
	}
	return changed;
}

void StringTable::parse_snapshot(BitReader& reader)
{
	auto read_entries = [&](std::vector<StringTableEntry>& out) {
		out.clear();
		int count = reader.read_uint16();
		for (int i = 0; i < count; i++) {
			StringTableEntry entry;
			entry.name = reader.read_string(4096);
			if (reader.read_bool()) {
				entry.data = reader.read_bytes(reader.read_uint16());
			}
			out.push_back(std::move(entry));
		}
	};

	read_entries(entries);
	if (reader.read_bool()) {
		read_entries(client_entries);
	}
	else {
		client_entries.clear();
	}
}

StringTableChange StringTables::create(const SvcCreateStringTable& message)
{
	StringTable table{};
	table.id = static_cast<int>(tables.size());
	table.name = message.table_name;
	table.is_filenames = message.is_filenames;
	table.max_entries = message.max_entries;
	table.user_data_fixed_size = message.user_data_fixed_size;
	table.user_data_size = message.user_data_size;
	table.user_data_size_bits = message.user_data_size_bits;

	StringTableChange change;
	BitReader reader = message.data.reader();
	if (message.data_compressed) {
		reader.read_uint32(); // uncompressed size
		uint32_t compressed_size = reader.read_uint32();
		auto payload = decompress_buffer(reader.read_bytes(compressed_size));
		BitReader payload_reader(payload);
		change.entries = table.parse_update(payload_reader, message.num_entries);
	}
	else {
		change.entries = table.parse_update(reader, message.num_entries);
		if (!reader.at_end()) {
			throw std::runtime_error("String table " + table.name + ": " + std::to_string(reader.bits_left())
				+ " bits left unread");
		}
	}

	tables.push_back(std::move(table));
	change.table = &tables.back();
	return change;
}

StringTableChange StringTables::update(const SvcUpdateStringTable& message)
{
	if (message.table_id < 0 || message.table_id >= static_cast<int>(tables.size())) {
		throw std::runtime_error("Update for unknown string table " + std::to_string(message.table_id));
	}
	StringTable& table = tables[message.table_id];

	BitReader reader = message.data.reader();
	StringTableChange change{ &table, table.parse_update(reader, message.num_changed_entries) };
	if (!reader.at_end()) {
		throw std::runtime_error("String table update " + table.name + ": " + std::to_string(reader.bits_left())
			+ " bits left unread");
	}
	return change;
}

std::vector<StringTableChange> StringTables::apply_snapshot(std::span<const std::byte> payload)
{
	std::vector<size_t> touched;
	BitReader reader(payload);
	int num_tables = reader.read_uint8();
	for (int t = 0; t < num_tables; t++) {
		std::string name = reader.read_string(256);

		size_t index = 0;
		while (index < tables.size() && tables[index].name != name) {
			index++;
		}
		if (index == tables.size()) {
			// the engine ignores unknown tables, keep them anyway
			StringTable& table = tables.emplace_back();
			table.id = static_cast<int>(index);
			table.name = name;
		}
		tables[index].parse_snapshot(reader);
		touched.push_back(index);
	}

	std::vector<StringTableChange> changes;
	for (size_t index : touched) {
		StringTableChange change{ &tables[index], {} };
		for (int i = 0; i < static_cast<int>(tables[index].entries.size()); i++) {
			change.entries.push_back(i);
		}
		changes.push_back(std::move(change));
	}
	return changes;
}

const StringTable* StringTables::find(const std::string& name) const
{
	for (const auto& table : tables) {
		if (table.name == name) return &table;
	}
	return nullptr;
}

const StringTable* StringTables::get(int id) const
{
	if (id < 0 || id >= static_cast<int>(tables.size())) return nullptr;
	return &tables[id];
}

// raw player_info_t:
//   char name[32]; int userID; char guid[33]; (pad) uint32 friendsID; char friendsName[32];
//   bool fakeplayer; bool ishltv; bool isreplay; (pad) CRC32_t customFiles[4]; uint8 filesDownloaded;
std::optional<PlayerInfo> parse_player_info(std::span<const std::byte> data)
{
	constexpr size_t PLAYER_INFO_SIZE = 132;
	if (data.size() < PLAYER_INFO_SIZE) {
		return std::nullopt;
	}

	auto read_string = [&](size_t offset, size_t length) {
		std::string s(reinterpret_cast<const char*>(data.data()) + offset, length);
		return std::string(s.c_str());
	};
	auto read_u32 = [&](size_t offset) {
		uint32_t value;
		std::memcpy(&value, data.data() + offset, sizeof(value));
		return value;
	};
	auto read_u8 = [&](size_t offset) { return std::to_integer<uint8_t>(data[offset]); };

	PlayerInfo info{};
	info.name = read_string(0, MAX_PLAYER_NAME_LENGTH);
	info.user_id = static_cast<int>(read_u32(32));
	info.guid = read_string(36, SIGNED_GUID_LEN + 1);
	info.friends_id = read_u32(72);
	info.friends_name = read_string(76, MAX_PLAYER_NAME_LENGTH);
	info.fake_player = read_u8(108) != 0;
	info.is_hltv = read_u8(109) != 0;
	info.is_replay = read_u8(110) != 0;
	for (size_t i = 0; i < info.custom_files.size(); i++) {
		info.custom_files[i] = read_u32(112 + i * 4);
	}
	info.files_downloaded = read_u8(128);
	return info;
}

}
