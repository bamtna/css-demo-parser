#pragma once
#include "cssdemo/Protocol/Types.h"
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

	bool is_stop() const;
};

// CClientState::ProcessSounds
std::vector<SoundInfo> decode_sounds(const SvcSounds& message, const ProtocolVersion& protocol);

}
