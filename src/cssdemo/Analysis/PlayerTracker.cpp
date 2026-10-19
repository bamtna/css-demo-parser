#include "PlayerTracker.h"
#include "cssdemo/Demo/DemoFile.h"
#include "cssdemo/Game/GameState.h"

namespace cssdemo {

PlayerTracker::PlayerTracker(PlayerTrackerOptions tracker_options)
	: options(tracker_options)
{
}

void PlayerTracker::on_user_cmd(const UserCmd& cmd)
{
	pending_cmd = cmd;
}

void PlayerTracker::on_packet_end(const DemoFrame& frame, const GameState& state)
{
	// signon packets carry no player state worth sampling
	if (frame.command != DemoCommand::Packet || state.tick == last_tick) {
		return;
	}
	last_tick = state.tick;

	for (auto& player : player_snapshots(state)) {
		if (player.info.is_hltv && !options.include_hltv) continue;
		if (!player.has_entity && !options.include_without_entity) continue;
		if (player.has_entity && !player.alive() && !options.include_dead) continue;

		auto& samples = by_user_id[player.info.user_id];
		PlayerSample sample{};
		sample.tick = state.tick;
		sample.demo_tick = frame.tick;

		if (player.velocity) {
			sample.velocity = *player.velocity;
		}
		else if (!samples.empty() && samples.back().player.has_entity && player.has_entity
			&& sample.tick > samples.back().tick && state.server.tick_interval > 0) {
			const auto& prev = samples.back();
			float dt = (sample.tick - prev.tick) * state.server.tick_interval;
			sample.velocity = {
				(player.origin.x - prev.player.origin.x) / dt,
				(player.origin.y - prev.player.origin.y) / dt,
				(player.origin.z - prev.player.origin.z) / dt,
			};
			sample.velocity_estimated = true;
		}

		if (player.is_local && pending_cmd) {
			sample.user_cmd = pending_cmd;
		}
		sample.player = std::move(player);
		samples.push_back(std::move(sample));
	}
	pending_cmd.reset();
}

const std::vector<PlayerSample>* PlayerTracker::samples_for(int user_id) const
{
	auto it = by_user_id.find(user_id);
	return it == by_user_id.end() ? nullptr : &it->second;
}

}
