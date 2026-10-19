#include "Sounds.h"
#include "cssdemo/Net/NetMessages.h"
#include "cssdemo/Protocol/Constants.h"
#include "cssdemo/Util/BitReader.h"
#include <stdexcept>

namespace cssdemo {

bool SoundInfo::is_stop() const
{
	return flags == SND_STOP;
}

// SoundInfo_t::ReadDelta
static SoundInfo read_sound_delta(BitReader& reader, const SoundInfo& delta, int protocol)
{
	SoundInfo sound = delta;
	auto read_delta_uint = [&](int& field, int bits) {
		if (reader.read_bool()) field = static_cast<int>(reader.read_bits(bits));
	};
	auto read_delta_sint = [&](int& field, int bits) {
		if (reader.read_bool()) field = reader.read_signed_bits(bits);
	};
	auto read_delta_sint_scale = [&](float& field, float scale, int bits) {
		if (reader.read_bool()) field = scale * reader.read_signed_bits(bits);
	};

	if (reader.read_bool()) {
		sound.entity_index = reader.read_bool()
			? static_cast<int>(reader.read_bits(5))
			: static_cast<int>(reader.read_bits(MAX_EDICT_BITS));
	}

	read_delta_uint(sound.sound_num, protocol > PROTOCOL_VERSION_22 ? MAX_SOUND_INDEX_BITS : 13);
	read_delta_uint(sound.flags, protocol > PROTOCOL_VERSION_18 ? SND_FLAG_BITS_ENCODE : 9);
	read_delta_uint(sound.channel, 3);
	sound.is_ambient = reader.read_bool();
	sound.is_sentence = reader.read_bool();

	if (sound.flags != SND_STOP) {
		if (reader.read_bool()) {
			sound.sequence_number = delta.sequence_number;
		}
		else if (reader.read_bool()) {
			sound.sequence_number = delta.sequence_number + 1;
		}
		else {
			sound.sequence_number = reader.read_bits(SOUND_SEQNUMBER_BITS);
		}

		if (reader.read_bool()) {
			sound.volume = reader.read_bits(7) / 127.0f;
		}
		if (reader.read_bool()) {
			sound.sound_level = reader.read_bits(MAX_SNDLVL_BITS);
		}
		read_delta_uint(sound.pitch, 8);
		if (protocol > PROTOCOL_VERSION_21) {
			read_delta_uint(sound.special_dsp, 8);
		}

		if (reader.read_bool()) {
			sound.delay = reader.read_signed_bits(MAX_SOUND_DELAY_MSEC_ENCODE_BITS) / 1000.0f;
			if (sound.delay < 0) {
				sound.delay *= 10.0f;
			}
			sound.delay -= SOUND_DELAY_OFFSET;
		}

		read_delta_sint_scale(sound.origin.x, 8.0f, COORD_INTEGER_BITS - 2);
		read_delta_sint_scale(sound.origin.y, 8.0f, COORD_INTEGER_BITS - 2);
		read_delta_sint_scale(sound.origin.z, 8.0f, COORD_INTEGER_BITS - 2);
		read_delta_sint(sound.speaker_entity, MAX_EDICT_BITS + 1);
	}
	else {
		// ClearStopFields
		sound.volume = 0;
		sound.sound_level = 0;
		sound.pitch = 100;
		sound.special_dsp = 0;
		sound.delay = 0;
		sound.sequence_number = 0;
		sound.origin = {};
		sound.speaker_entity = -1;
	}
	return sound;
}

static bool try_decode_sounds(const SvcSounds& message, int protocol, std::vector<SoundInfo>& sounds)
{
	sounds.clear();
	try {
		BitReader reader = message.data.reader();
		SoundInfo previous{};
		for (int i = 0; i < message.num_sounds; i++) {
			previous = read_sound_delta(reader, previous, protocol);
			sounds.push_back(previous);
		}
		return reader.at_end();
	}
	catch (const BitReaderOverflow&) {
		return false;
	}
}

std::vector<SoundInfo> decode_sounds(const SvcSounds& message, const ProtocolVersion& protocol)
{
	std::vector<SoundInfo> sounds;
	if (try_decode_sounds(message, protocol.network, sounds)) {
		return sounds;
	}
	// same fallback as the engine, 18/19 demos can be mislabeled
	if (protocol.network == 18 || protocol.network == 19) {
		if (try_decode_sounds(message, protocol.network == 18 ? 19 : 18, sounds)) {
			return sounds;
		}
	}
	throw std::runtime_error("Sounds: payload doesn't match the expected layout");
}

}
