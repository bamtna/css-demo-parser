#include "StringTables.h"
#include "cssdemo/Net/NetMessages.h"
#include "cssdemo/Protocol/Constants.h"
#include "cssdemo/Util/BitReader.h"
#include "cssdemo/Util/BitWriter.h"
#include "cssdemo/Util/Compression.h"
#include <algorithm>
#include <cstring>
#include <deque>
#include <stdexcept>

namespace cssdemo {

std::vector<int> StringTable::parse_update(BitReader& reader, int num_changed_entries,
	std::vector<StringTableEntryUpdate>* sent)
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
		bool has_data = reader.read_bool();
		if (has_data) {
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
		if (sent) {
			StringTableEntryUpdate update{ entry_index, entry_name, std::nullopt };
			if (has_data) update.data = entries[entry_index].data;
			sent->push_back(std::move(update));
		}

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
	has_client_entries = reader.read_bool();
	if (has_client_entries) {
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

std::vector<StringTable> read_string_table_snapshot(std::span<const std::byte> payload)
{
	std::vector<StringTable> result;
	BitReader reader(payload);
	int num_tables = reader.read_uint8();
	for (int t = 0; t < num_tables; t++) {
		StringTable& table = result.emplace_back();
		table.id = t;
		table.name = reader.read_string(256);
		table.parse_snapshot(reader);
	}
	return result;
}

std::vector<std::byte> write_string_table_snapshot(std::span<const StringTable> tables)
{
	BitWriter writer;
	write_string_table_snapshot(writer, tables);
	return writer.take_bytes();
}

void write_string_table_snapshot(BitWriter& writer, std::span<const StringTable> tables)
{
	auto write_entries = [&](const std::vector<StringTableEntry>& list) {
		writer.write_uint16(static_cast<uint16_t>(list.size()));
		for (const auto& entry : list) {
			writer.write_string(entry.name);
			writer.write_bool(!entry.data.empty());
			if (!entry.data.empty()) {
				writer.write_uint16(static_cast<uint16_t>(entry.data.size()));
				writer.write_bytes(entry.data);
			}
		}
	};

	writer.write_uint8(static_cast<uint8_t>(tables.size()));
	for (const auto& table : tables) {
		writer.write_string(table.name);
		write_entries(table.entries);
		writer.write_bool(table.has_client_entries);
		if (table.has_client_entries) {
			write_entries(table.client_entries);
		}
	}
}

BitBuffer write_string_table_update(const StringTable& table, std::span<const StringTableEntryUpdate> updates)
{
	BitWriter writer;
	std::deque<std::string> history;
	std::vector<std::string> added; // names of entries past the end of table
	int entry_bits = Q_log2(table.max_entries);
	int last_entry = -1;

	for (const auto& update : updates) {
		if (update.index < 0 || update.index >= table.max_entries) {
			throw std::runtime_error("String index " + std::to_string(update.index) + " out of range for table " + table.name);
		}
		if (update.index == last_entry + 1) {
			writer.write_bool(true);
		}
		else {
			writer.write_bool(false);
			writer.write_bits(update.index, entry_bits);
		}
		last_entry = update.index;

		writer.write_bool(update.name.has_value());
		if (update.name) {
			// GetBestPreviousString
			const std::string& name = *update.name;
			int best_index = -1;
			int best_count = 0;
			for (size_t i = 0; i < history.size(); i++) {
				const std::string& previous = history[i];
				int similar = 0;
				while (similar < static_cast<int>(previous.size()) && similar < static_cast<int>(name.size())
					&& previous[similar] == name[similar] && similar < (1 << SUBSTRING_BITS) - 1) {
					similar++;
				}
				if (similar >= 3 && similar > best_count) {
					best_count = similar;
					best_index = static_cast<int>(i);
				}
			}
			writer.write_bool(best_index != -1);
			if (best_index != -1) {
				writer.write_bits(best_index, 5);
				writer.write_bits(best_count, SUBSTRING_BITS);
				writer.write_string(name.substr(best_count));
			}
			else {
				writer.write_string(name);
			}
		}

		writer.write_bool(update.data.has_value());
		if (update.data) {
			if (table.user_data_fixed_size) {
				std::vector<std::byte> data = *update.data;
				data.resize((table.user_data_size_bits + 7) / 8);
				BitReader bits(data, table.user_data_size_bits);
				writer.write_from(bits, table.user_data_size_bits);
			}
			else {
				writer.write_bits(static_cast<uint32_t>(update.data->size()), MAX_USERDATA_BITS);
				writer.write_bytes(*update.data);
			}
		}

		// the history gets the name the entry ends up with
		std::string name;
		size_t existing = table.entries.size();
		if (update.index < static_cast<int>(existing)) {
			name = table.entries[update.index].name;
		}
		else {
			size_t slot = update.index - existing;
			if (slot >= added.size()) {
				added.resize(slot + 1);
				added[slot] = update.name.value_or("");
			}
			name = added[slot];
		}
		if (history.size() >= STRING_HISTORY_SIZE) {
			history.pop_front();
		}
		// StringHistoryEntry keeps 31 characters
		history.push_back(name.substr(0, (1 << SUBSTRING_BITS) - 1));
	}
	return writer.take_buffer();
}

std::vector<StringTableEntryUpdate> all_entries_update(const StringTable& table)
{
	std::vector<StringTableEntryUpdate> updates;
	for (size_t i = 0; i < table.entries.size(); i++) {
		const auto& entry = table.entries[i];
		StringTableEntryUpdate update{ static_cast<int>(i), entry.name, std::nullopt };
		if (!entry.data.empty()) update.data = entry.data;
		updates.push_back(std::move(update));
	}
	return updates;
}

SvcUpdateStringTable make_update_string_table(const StringTable& table, std::span<const StringTableEntryUpdate> updates)
{
	SvcUpdateStringTable message;
	message.table_id = table.id;
	message.num_changed_entries = static_cast<int>(updates.size());
	message.data = write_string_table_update(table, updates);
	return message;
}

SvcCreateStringTable make_create_string_table(const StringTable& table)
{
	SvcCreateStringTable message;
	message.table_name = table.name;
	message.is_filenames = table.is_filenames;
	message.max_entries = table.max_entries;
	message.num_entries = static_cast<int>(table.entries.size());
	message.user_data_fixed_size = table.user_data_fixed_size;
	message.user_data_size = table.user_data_size;
	message.user_data_size_bits = table.user_data_size_bits;
	message.data_compressed = false;
	StringTable empty = table;
	empty.entries.clear();
	message.data = write_string_table_update(empty, all_entries_update(table));
	return message;
}

std::vector<StringTableChange> StringTables::apply_snapshot(std::span<const std::byte> payload)
{
	std::vector<size_t> touched;
	for (auto& snapshot : read_string_table_snapshot(payload)) {
		size_t index = 0;
		while (index < tables.size() && tables[index].name != snapshot.name) {
			index++;
		}
		if (index == tables.size()) {
			// the engine ignores unknown tables, keep them anyway
			StringTable& table = tables.emplace_back();
			table.id = static_cast<int>(index);
			table.name = snapshot.name;
		}
		tables[index].entries = std::move(snapshot.entries);
		tables[index].client_entries = std::move(snapshot.client_entries);
		tables[index].has_client_entries = snapshot.has_client_entries;
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

std::vector<std::byte> write_player_info(const PlayerInfo& info)
{
	std::vector<std::byte> data(132);
	auto write_string = [&](size_t offset, size_t length, const std::string& s) {
		std::memcpy(data.data() + offset, s.data(), std::min(s.size(), length - 1));
	};
	auto write_u32 = [&](size_t offset, uint32_t value) { std::memcpy(data.data() + offset, &value, sizeof(value)); };
	write_string(0, MAX_PLAYER_NAME_LENGTH, info.name);
	write_u32(32, static_cast<uint32_t>(info.user_id));
	write_string(36, SIGNED_GUID_LEN + 1, info.guid);
	write_u32(72, info.friends_id);
	write_string(76, MAX_PLAYER_NAME_LENGTH, info.friends_name);
	data[108] = std::byte{ info.fake_player };
	data[109] = std::byte{ info.is_hltv };
	data[110] = std::byte{ info.is_replay };
	for (size_t i = 0; i < info.custom_files.size(); i++) {
		write_u32(112 + i * 4, info.custom_files[i]);
	}
	data[128] = std::byte{ info.files_downloaded };
	return data;
}

}
