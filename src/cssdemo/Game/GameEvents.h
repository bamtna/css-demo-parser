#pragma once
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace cssdemo {

class BitReader;
struct SvcGameEventList;
struct SvcGameEvent;

struct GameEventDescriptor {
	enum class KeyType {
		Local = 0, // terminates the key list
		String,
		Float,
		Long,
		Short,
		Byte,
		Bool,
	};

	struct Key {
		KeyType type{};
		std::string name;
	};

	int event_id{};
	std::string name;
	std::vector<Key> keys;
};

struct GameEvent {
	using Value = std::variant<std::string, float, int, bool>;

	int event_id{ -1 };
	std::string name;
	std::vector<std::pair<std::string, Value>> keys;

	const Value* find(const std::string& key) const;
	int get_int(const std::string& key, int fallback = 0) const;
	float get_float(const std::string& key, float fallback = 0.0f) const;
	std::string get_string(const std::string& key) const;

	friend std::ostream& operator<<(std::ostream& os, const GameEvent& event);
};

class GameEventList {
public:
	void load(const SvcGameEventList& message);
	GameEvent decode(const SvcGameEvent& message) const;
	// CGameEventManager::SerializeEvent, keys by descriptor order, missing ones are zero
	SvcGameEvent encode(const GameEvent& event) const;
	SvcGameEventList write() const;
	void add(GameEventDescriptor descriptor);

	bool empty() const { return descriptors.empty(); }
	const GameEventDescriptor* find(int event_id) const;
	const std::unordered_map<int, GameEventDescriptor>& all() const { return descriptors; }
	const GameEventDescriptor* find(const std::string& name) const;

private:
	std::unordered_map<int, GameEventDescriptor> descriptors;
	std::vector<int> order; // as listed
};

}
