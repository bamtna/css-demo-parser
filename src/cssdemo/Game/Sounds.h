#pragma once
#include "cssdemo/Protocol/Types.h"
#include <cstdint>
#include <ostream>
#include <vector>

namespace cssdemo {

struct SvcSounds;

// SoundInfo_t
struct SoundInfo {
	int sequence_number{};
	int entity_index{};
	int channel{ 6 };   // CHAN_STATIC
	int sound_num{};    // index into the soundprecache string table
	int flags{};
	float volume{ 1.0f };
	int sound_level{ 75 }; // SNDLVL_NORM
	int pitch{ 100 };      // PITCH_NORM
	int special_dsp{};
	float delay{};
	Vector origin{};
	int speaker_entity{ -1 };
	bool is_ambient{};
	bool is_sentence{};
	// float fields the server sent although they quantise to the previous sound's value
	enum : uint8_t { SENT_VOLUME = 1, SENT_DELAY = 2, SENT_ORIGIN_X = 4, SENT_ORIGIN_Y = 8, SENT_ORIGIN_Z = 16 };
	uint8_t sent_unchanged{};

	bool is_stop() const;
};

// CClientState::ProcessSounds
std::vector<SoundInfo> decode_sounds(const SvcSounds& message, const ProtocolVersion& protocol);
// SoundInfo_t::WriteDelta, each sound against the one before
SvcSounds encode_sounds(const std::vector<SoundInfo>& sounds, const ProtocolVersion& protocol, bool reliable = false);

}
