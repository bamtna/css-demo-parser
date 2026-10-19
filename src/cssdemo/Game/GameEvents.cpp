#include "GameEvents.h"
#include "cssdemo/Net/NetMessages.h"
#include "cssdemo/Protocol/Constants.h"
#include "cssdemo/Util/BitReader.h"
#include <stdexcept>
#include <type_traits>

namespace cssdemo {

// CGameEventManager::ParseEventList
void GameEventList::load(const SvcGameEventList& message)
{
	descriptors.clear();
	BitReader reader = message.data.reader();
	for (int i = 0; i < message.num_events; i++) {
		GameEventDescriptor descriptor{};
		descriptor.event_id = reader.read_bits(MAX_EVENT_BITS);
		descriptor.name = reader.read_string(32);

		auto type = static_cast<GameEventDescriptor::KeyType>(reader.read_bits(3));
		while (type != GameEventDescriptor::KeyType::Local) {
			descriptor.keys.push_back({ type, reader.read_string(32) });
			type = static_cast<GameEventDescriptor::KeyType>(reader.read_bits(3));
		}
		descriptors[descriptor.event_id] = std::move(descriptor);
	}
	if (!reader.at_end()) {
		throw std::runtime_error("Game event list: " + std::to_string(reader.bits_left()) + " bits left unread");
	}
}

const GameEventDescriptor* GameEventList::find(int event_id) const
{
	auto it = descriptors.find(event_id);
	return it == descriptors.end() ? nullptr : &it->second;
}

// CGameEventManager::UnserializeEvent
GameEvent GameEventList::decode(const SvcGameEvent& message) const
{
	BitReader reader = message.data.reader();
	GameEvent event{};
	event.event_id = reader.read_bits(MAX_EVENT_BITS);

	const GameEventDescriptor* descriptor = find(event.event_id);
	if (!descriptor) {
		throw std::runtime_error("Unknown game event id " + std::to_string(event.event_id));
	}
	event.name = descriptor->name;

	using KeyType = GameEventDescriptor::KeyType;
	for (const auto& key : descriptor->keys) {
		GameEvent::Value value;
		switch (key.type) {
		case KeyType::String: value = reader.read_string(1024); break;
		case KeyType::Float: value = reader.read_float32(); break;
		case KeyType::Long: value = static_cast<int>(reader.read_int32()); break;
		case KeyType::Short: value = static_cast<int>(reader.read_int16()); break;
		case KeyType::Byte: value = static_cast<int>(reader.read_uint8()); break;
		case KeyType::Bool: value = reader.read_bool(); break;
		default:
			throw std::runtime_error("Unknown key type in game event " + descriptor->name);
		}
		event.keys.emplace_back(key.name, std::move(value));
	}

	if (!reader.at_end()) {
		throw std::runtime_error("Game event " + event.name + ": " + std::to_string(reader.bits_left())
			+ " bits left unread");
	}
	return event;
}

const GameEvent::Value* GameEvent::find(const std::string& key) const
{
	for (const auto& [key_name, value] : keys) {
		if (key_name == key) return &value;
	}
	return nullptr;
}

int GameEvent::get_int(const std::string& key, int fallback) const
{
	const Value* value = find(key);
	if (!value) return fallback;
	if (auto i = std::get_if<int>(value)) return *i;
	if (auto b = std::get_if<bool>(value)) return *b;
	if (auto f = std::get_if<float>(value)) return static_cast<int>(*f);
	return fallback;
}

float GameEvent::get_float(const std::string& key, float fallback) const
{
	const Value* value = find(key);
	if (!value) return fallback;
	if (auto f = std::get_if<float>(value)) return *f;
	if (auto i = std::get_if<int>(value)) return static_cast<float>(*i);
	return fallback;
}

std::string GameEvent::get_string(const std::string& key) const
{
	const Value* value = find(key);
	if (auto s = value ? std::get_if<std::string>(value) : nullptr) return *s;
	return {};
}

std::ostream& operator<<(std::ostream& os, const GameEvent& event)
{
	os << event.name;
	for (const auto& [name, value] : event.keys) {
		os << " " << name << "=";
		std::visit([&](const auto& v) {
			using T = std::decay_t<decltype(v)>;
			if constexpr (std::is_same_v<T, std::string>) os << '"' << v << '"';
			else os << v;
		}, value);
	}
	return os;
}

}
