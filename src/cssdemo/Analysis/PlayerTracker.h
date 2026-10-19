#pragma once
#include "cssdemo/Game/Players.h"
#include "cssdemo/Game/UserCmd.h"
#include "cssdemo/Parser/DemoListener.h"
#include <map>
#include <optional>
#include <vector>

namespace cssdemo {

struct PlayerSample {
	int tick{};      // server tick (net_Tick)
	int demo_tick{}; // tick of the demo frame
	PlayerSnapshot player;
	Vector velocity{};
	bool velocity_estimated{}; // from origin deltas
	std::optional<UserCmd> user_cmd; // client demos only
};

struct PlayerTrackerOptions {
	bool include_hltv = false;
	bool include_dead = false; // dead players and spectators, whose origin is their camera
	bool include_without_entity = false;
};

class PlayerTracker : public DemoListener {
public:
	explicit PlayerTracker(PlayerTrackerOptions options = {});

	void on_packet_end(const DemoFrame& frame, const GameState& state) override;
	void on_user_cmd(const UserCmd& cmd) override;

	const std::map<int, std::vector<PlayerSample>>& samples() const { return by_user_id; }
	const std::vector<PlayerSample>* samples_for(int user_id) const;

private:
	PlayerTrackerOptions options;
	std::map<int, std::vector<PlayerSample>> by_user_id;
	std::optional<UserCmd> pending_cmd;
	int last_tick = -1;
};

}
