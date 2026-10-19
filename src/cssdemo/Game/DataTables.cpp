#include "DataTables.h"
#include "cssdemo/Protocol/Constants.h"
#include "cssdemo/Util/BitReader.h"
#include "cssdemo/Util/BitWriter.h"
#include <algorithm>
#include <stdexcept>

namespace cssdemo {

const char* send_prop_type_name(SendPropType type)
{
	switch (type) {
	case SendPropType::Int: return "int";
	case SendPropType::Float: return "float";
	case SendPropType::Vector: return "vector";
	case SendPropType::VectorXY: return "vectorxy";
	case SendPropType::String: return "string";
	case SendPropType::Array: return "array";
	case SendPropType::DataTable: return "datatable";
	}
	return "unknown";
}

int ServerClass::find_prop(const std::string& name) const
{
	auto it = prop_lookup.find(name);
	return it == prop_lookup.end() ? -1 : it->second;
}

// RecvTable_ReadInfos
void SendTable::parse(BitReader& reader)
{
	name = reader.read_string(256);
	int num_props = reader.read_bits(PROPINFOBITS_NUMPROPS);
	props.reserve(num_props);

	for (int i = 0; i < num_props; i++) {
		SendProp prop{};
		prop.type = static_cast<SendPropType>(reader.read_bits(PROPINFOBITS_TYPE));
		prop.name = reader.read_string(256);
		prop.flags = reader.read_bits(PROPINFOBITS_FLAGS);

		if (prop.type == SendPropType::DataTable || prop.has_flag(SPROP_EXCLUDE)) {
			prop.dt_name = reader.read_string(256);
		}
		else if (prop.type == SendPropType::Array) {
			prop.num_elements = reader.read_bits(PROPINFOBITS_NUMELEMENTS);
		}
		else {
			prop.low_value = reader.read_float32();
			prop.high_value = reader.read_float32();
			prop.num_bits = reader.read_bits(PROPINFOBITS_NUMBITS);
		}
		props.push_back(std::move(prop));
	}
}

void SendTable::write(BitWriter& writer) const
{
	writer.write_string(name);
	writer.write_bits(static_cast<uint32_t>(props.size()), PROPINFOBITS_NUMPROPS);
	for (const auto& prop : props) {
		writer.write_bits(static_cast<uint32_t>(prop.type), PROPINFOBITS_TYPE);
		writer.write_string(prop.name);
		writer.write_bits(prop.flags, PROPINFOBITS_FLAGS);
		if (prop.type == SendPropType::DataTable || prop.has_flag(SPROP_EXCLUDE)) {
			writer.write_string(prop.dt_name);
		}
		else if (prop.type == SendPropType::Array) {
			writer.write_bits(prop.num_elements, PROPINFOBITS_NUMELEMENTS);
		}
		else {
			writer.write_float32(prop.low_value);
			writer.write_float32(prop.high_value);
			writer.write_bits(prop.num_bits, PROPINFOBITS_NUMBITS);
		}
	}
}

std::vector<std::byte> DataTables::write() const
{
	BitWriter writer;
	write(writer);
	return writer.take_bytes();
}

void DataTables::write(BitWriter& writer) const
{
	for (const auto& table : send_tables) {
		writer.write_bool(true);
		writer.write_bool(table.needs_decoder);
		table.write(writer);
	}
	writer.write_bool(false);

	writer.write_int16(static_cast<int16_t>(server_classes.size()));
	for (int class_id : class_order) {
		const auto& server_class = server_classes[class_id];
		writer.write_int16(static_cast<int16_t>(class_id));
		writer.write_string(server_class.class_name);
		writer.write_string(server_class.data_table_name);
	}
}

// DataTable_LoadDataTablesFromBuffer
void DataTables::parse(std::span<const std::byte> payload)
{
	send_tables.clear();
	server_classes.clear();
	class_order.clear();
	table_index.clear();

	BitReader reader(payload);
	while (reader.read_bool()) {
		SendTable table{};
		table.needs_decoder = reader.read_bool();
		table.parse(reader);
		table_index[table.name] = send_tables.size();
		send_tables.push_back(std::move(table));
	}

	int num_classes = reader.read_int16();
	server_classes.resize(num_classes);
	for (int i = 0; i < num_classes; i++) {
		int class_id = reader.read_int16();
		if (class_id < 0 || class_id >= num_classes) {
			throw std::runtime_error("Server class id " + std::to_string(class_id) + " out of range");
		}
		auto& server_class = server_classes[class_id];
		class_order.push_back(class_id);
		server_class.class_id = class_id;
		server_class.class_name = reader.read_string(256);
		server_class.data_table_name = reader.read_string(256);
	}

	if (reader.bits_left() >= 8) {
		throw std::runtime_error("Data tables: " + std::to_string(reader.bits_left()) + " bits left unread");
	}

	for (auto& server_class : server_classes) {
		flatten(server_class);
	}
}

const SendTable* DataTables::find_table(const std::string& name) const
{
	auto it = table_index.find(name);
	return it == table_index.end() ? nullptr : &send_tables[it->second];
}

const ServerClass* DataTables::find_class(int class_id) const
{
	if (class_id < 0 || class_id >= static_cast<int>(server_classes.size())) return nullptr;
	return &server_classes[class_id];
}

const ServerClass* DataTables::find_class(const std::string& class_name) const
{
	for (const auto& server_class : server_classes) {
		if (server_class.class_name == class_name) return &server_class;
	}
	return nullptr;
}

int DataTables::server_class_bits() const
{
	return Q_log2(static_cast<int>(server_classes.size())) + 1;
}

// SendTable_BuildHierarchy + SendTable_SortByPriority
void DataTables::flatten(ServerClass& server_class) const
{
	const SendTable* table = find_table(server_class.data_table_name);
	if (!table) {
		throw std::runtime_error("Missing send table " + server_class.data_table_name
			+ " for class " + server_class.class_name);
	}

	std::vector<const SendProp*> excludes;
	gather_excludes(*table, excludes);
	gather_props(*table, "", excludes, server_class.flattened_props);

	// CHANGES_OFTEN props go first
	auto& props = server_class.flattened_props;
	size_t start = 0;
	for (size_t i = 0; i < props.size(); i++) {
		if (props[i].prop->has_flag(SPROP_CHANGES_OFTEN)) {
			std::swap(props[i], props[start]);
			start++;
		}
	}

	for (size_t i = 0; i < props.size(); i++) {
		server_class.prop_lookup[props[i].name] = static_cast<int>(i);
	}
}

void DataTables::gather_excludes(const SendTable& table, std::vector<const SendProp*>& excludes) const
{
	for (const auto& prop : table.props) {
		if (prop.has_flag(SPROP_EXCLUDE)) {
			excludes.push_back(&prop);
		}
		else if (prop.type == SendPropType::DataTable) {
			if (const SendTable* sub_table = find_table(prop.dt_name)) {
				gather_excludes(*sub_table, excludes);
			}
		}
	}
}

void DataTables::gather_props(const SendTable& table, const std::string& prefix,
	const std::vector<const SendProp*>& excludes, std::vector<FlattenedProp>& out) const
{
	// child tables' props come before this table's own
	std::vector<FlattenedProp> own_props;
	gather_props_iterate(table, prefix, excludes, own_props, out);
	out.insert(out.end(), own_props.begin(), own_props.end());
}

void DataTables::gather_props_iterate(const SendTable& table, const std::string& prefix,
	const std::vector<const SendProp*>& excludes, std::vector<FlattenedProp>& own_props,
	std::vector<FlattenedProp>& out) const
{
	for (size_t i = 0; i < table.props.size(); i++) {
		const SendProp& prop = table.props[i];
		if (prop.has_flag(SPROP_EXCLUDE) || prop.has_flag(SPROP_INSIDEARRAY)) {
			continue;
		}
		bool excluded = std::any_of(excludes.begin(), excludes.end(), [&](const SendProp* ex) {
			return ex->dt_name == table.name && ex->name == prop.name;
		});
		if (excluded) {
			continue;
		}

		if (prop.type == SendPropType::DataTable) {
			const SendTable* sub_table = find_table(prop.dt_name);
			if (!sub_table) {
				throw std::runtime_error("Missing send table " + prop.dt_name + " referenced by " + table.name);
			}
			// baseclass and collapsible tables don't add a name level
			std::string sub_prefix = prefix;
			if (!prop.has_flag(SPROP_COLLAPSIBLE) && prop.name != "baseclass") {
				sub_prefix += prop.name + ".";
			}
			if (prop.has_flag(SPROP_COLLAPSIBLE)) {
				gather_props_iterate(*sub_table, sub_prefix, excludes, own_props, out);
			}
			else {
				gather_props(*sub_table, sub_prefix, excludes, out);
			}
		}
		else {
			FlattenedProp flat{ &prop, nullptr, prefix + prop.name };
			if (prop.type == SendPropType::Array) {
				// element prop is the one right before the array
				if (i == 0) {
					throw std::runtime_error("Array prop " + prop.name + " has no element prop");
				}
				flat.array_element_prop = &table.props[i - 1];
			}
			own_props.push_back(std::move(flat));
		}
	}
}

}
