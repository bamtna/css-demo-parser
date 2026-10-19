#include "GameState.h"

namespace cssdemo {

std::vector<Player> GameState::players() const
{
	std::vector<Player> result;
	const StringTable* user_info = string_tables.find("userinfo");
	if (!user_info) {
		return result;
	}
	for (size_t i = 0; i < user_info->entries.size(); i++) {
		if (auto info = parse_player_info(user_info->entries[i].data)) {
			result.push_back({ static_cast<int>(i) + 1, std::move(*info) });
		}
	}
	return result;
}

std::optional<Player> GameState::find_player(int user_id) const
{
	for (auto& player : players()) {
		if (player.info.user_id == user_id) return player;
	}
	return std::nullopt;
}

}
