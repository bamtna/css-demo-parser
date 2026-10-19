#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace cssdemo {

class BitReader;
class BitWriter;

// dt_common.h
enum SendPropFlags : uint32_t {
	SPROP_UNSIGNED = 1 << 0,
	SPROP_COORD = 1 << 1,
	SPROP_NOSCALE = 1 << 2,
	SPROP_ROUNDDOWN = 1 << 3,
	SPROP_ROUNDUP = 1 << 4,
	SPROP_NORMAL = 1 << 5,
	SPROP_VARINT = SPROP_NORMAL, // reused on int props
	SPROP_EXCLUDE = 1 << 6,
	SPROP_XYZE = 1 << 7,
	SPROP_INSIDEARRAY = 1 << 8,
	SPROP_PROXY_ALWAYS_YES = 1 << 9,
	SPROP_CHANGES_OFTEN = 1 << 10,
	SPROP_IS_A_VECTOR_ELEM = 1 << 11,
	SPROP_COLLAPSIBLE = 1 << 12,
	SPROP_COORD_MP = 1 << 13,
	SPROP_COORD_MP_LOWPRECISION = 1 << 14,
	SPROP_COORD_MP_INTEGRAL = 1 << 15,
};

enum class SendPropType {
	Int = 0,
	Float,
	Vector,
	VectorXY,
	String,
	Array,
	DataTable,
};

const char* send_prop_type_name(SendPropType type);

struct SendProp {
	SendPropType type{};
	std::string name;
	uint32_t flags{};
	// datatable props: child table, exclude props: table of the excluded prop
	std::string dt_name;
	int num_elements{};
	float low_value{};
	float high_value{};
	int num_bits{};

	bool has_flag(uint32_t flag) const { return (flags & flag) != 0; }
};

struct SendTable {
	std::string name;
	bool needs_decoder{};
	std::vector<SendProp> props;

	void parse(BitReader& reader);
	// SendTable_WriteInfos
	void write(BitWriter& writer) const;
};

struct FlattenedProp {
	const SendProp* prop{};
	const SendProp* array_element_prop{}; // set when prop is an array
	std::string name;                     // qualified name, e.g. "m_Local.m_vecPunchAngle"
};

struct ServerClass {
	int class_id{};
	std::string class_name;
	std::string data_table_name;
	std::vector<FlattenedProp> flattened_props;
	std::unordered_map<std::string, int> prop_lookup;

	int find_prop(const std::string& name) const;
};

class DataTables {
public:
	void parse(std::span<const std::byte> payload);
	// DataTable_WriteSendTablesBuffer + DataTable_WriteClassInfosBuffer, the dem_datatables payload
	std::vector<std::byte> write() const;
	void write(BitWriter& writer) const;

	bool empty() const { return server_classes.empty(); }
	const std::vector<SendTable>& tables() const { return send_tables; }
	const std::vector<ServerClass>& classes() const { return server_classes; }
	const SendTable* find_table(const std::string& name) const;
	const ServerClass* find_class(int class_id) const;
	const ServerClass* find_class(const std::string& class_name) const;
	int server_class_bits() const;

private:
	std::vector<SendTable> send_tables;
	std::vector<ServerClass> server_classes; // indexed by class id
	std::vector<int> class_order;            // as sent
	std::unordered_map<std::string, size_t> table_index;

	void flatten(ServerClass& server_class) const;
	void gather_excludes(const SendTable& table, std::vector<const SendProp*>& excludes) const;
	void gather_props(const SendTable& table, const std::string& prefix,
		const std::vector<const SendProp*>& excludes, std::vector<FlattenedProp>& out) const;
	void gather_props_iterate(const SendTable& table, const std::string& prefix,
		const std::vector<const SendProp*>& excludes, std::vector<FlattenedProp>& own_props,
		std::vector<FlattenedProp>& out) const;
};

}
