#pragma once
#include "cssdemo/Protocol/Types.h"
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cssdemo {

class BitReader;
struct SvcCreateStringTable;
struct SvcUpdateStringTable;

struct StringTableEntry {
	std::string name;
	std::vector<std::byte> data;
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

	// CNetworkStringTable::ParseUpdate, returns changed entry indices
	std::vector<int> parse_update(BitReader& reader, int num_changed_entries);
	// CNetworkStringTable::ReadStringTable, name already read
	void parse_snapshot(BitReader& reader);
};

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

}
