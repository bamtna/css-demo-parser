#pragma once
#include "cssdemo/Protocol/Types.h"
#include "cssdemo/Util/BitReader.h"
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cssdemo {

class BitReader;
class BitWriter;
struct SvcCreateStringTable;
struct SvcUpdateStringTable;

struct StringTableEntry {
	std::string name;
	std::vector<std::byte> data;
};

// one entry of an update as sent
struct StringTableEntryUpdate {
	int index{};
	std::optional<std::string> name; // new entries
	std::optional<std::vector<std::byte>> data;
};

struct StringTable {
	int id{};
	std::string name;
	bool is_filenames{};
	int max_entries{};
	bool user_data_fixed_size{};
	int user_data_size{};
	int user_data_size_bits{};

	std::vector<StringTableEntry> entries;
	std::vector<StringTableEntry> client_entries; // only in snapshots
	bool has_client_entries{};

	// CNetworkStringTable::ParseUpdate, returns changed entry indices
	std::vector<int> parse_update(BitReader& reader, int num_changed_entries,
		std::vector<StringTableEntryUpdate>* sent = nullptr);
	// CNetworkStringTable::ReadStringTable, name already read
	void parse_snapshot(BitReader& reader);
};

// CNetworkStringTable::WriteUpdate, table is the state before the update
BitBuffer write_string_table_update(const StringTable& table, std::span<const StringTableEntryUpdate> updates);
SvcUpdateStringTable make_update_string_table(const StringTable& table, std::span<const StringTableEntryUpdate> updates);
// CNetworkStringTable::WriteBaselines, every entry, uncompressed
SvcCreateStringTable make_create_string_table(const StringTable& table);
std::vector<StringTableEntryUpdate> all_entries_update(const StringTable& table);

// dem_stringtables, CNetworkStringTableContainer::WriteStringTables
std::vector<StringTable> read_string_table_snapshot(std::span<const std::byte> payload);
std::vector<std::byte> write_string_table_snapshot(std::span<const StringTable> tables);
void write_string_table_snapshot(BitWriter& writer, std::span<const StringTable> tables);

// table pointer is invalidated when a new table is created
struct StringTableChange {
	const StringTable* table{};
	std::vector<int> entries;
};

class StringTables {
public:
	StringTableChange create(const SvcCreateStringTable& message);
	StringTableChange update(const SvcUpdateStringTable& message);
	std::vector<StringTableChange> apply_snapshot(std::span<const std::byte> payload);
	void clear() { tables.clear(); }

	const StringTable* find(const std::string& name) const;
	const StringTable* get(int id) const;
	const std::vector<StringTable>& all() const { return tables; }

private:
	std::vector<StringTable> tables;
};

std::optional<PlayerInfo> parse_player_info(std::span<const std::byte> data);
std::vector<std::byte> write_player_info(const PlayerInfo& info);

}
