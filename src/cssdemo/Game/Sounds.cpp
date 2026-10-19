#include "Sounds.h"
#include "cssdemo/Net/NetMessages.h"
#include "cssdemo/Protocol/Constants.h"
#include "cssdemo/Util/BitReader.h"
#include "cssdemo/Util/BitWriter.h"
#include <algorithm>
#include <cmath>
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
	sound.sent_unchanged = 0;
	auto read_delta_uint = [&](int& field, int bits) {
		if (reader.read_bool()) field = static_cast<int>(reader.read_bits(bits));
	};
	auto read_delta_sint = [&](int& field, int bits) {
		if (reader.read_bool()) field = reader.read_signed_bits(bits);
	};
	auto read_delta_sint_scale = [&](float& field, float scale, int bits, uint8_t sent) {
		if (reader.read_bool()) {
			float from = field;
			field = scale * reader.read_signed_bits(bits);
			if (field == from) sound.sent_unchanged |= sent;
		}
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
			if (sound.volume == delta.volume) sound.sent_unchanged |= SoundInfo::SENT_VOLUME;
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
			if (sound.delay == delta.delay) sound.sent_unchanged |= SoundInfo::SENT_DELAY;
		}

		read_delta_sint_scale(sound.origin.x, 8.0f, COORD_INTEGER_BITS - 2, SoundInfo::SENT_ORIGIN_X);
		read_delta_sint_scale(sound.origin.y, 8.0f, COORD_INTEGER_BITS - 2, SoundInfo::SENT_ORIGIN_Y);
		read_delta_sint_scale(sound.origin.z, 8.0f, COORD_INTEGER_BITS - 2, SoundInfo::SENT_ORIGIN_Z);
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

// rounded, not truncated, so decoded sounds come back exact
static void write_sound_delta(BitWriter& writer, const SoundInfo& sound, const SoundInfo& delta, int protocol)
{
	auto write_delta_uint = [&](int value, int from, int bits) {
		writer.write_bool(value != from);
		if (value != from) writer.write_bits(static_cast<uint32_t>(value), bits);
	};

	writer.write_bool(sound.entity_index != delta.entity_index);
	if (sound.entity_index != delta.entity_index) {
		bool small = sound.entity_index <= 31;
		writer.write_bool(small);
		writer.write_bits(sound.entity_index, small ? 5 : MAX_EDICT_BITS);
	}

	write_delta_uint(sound.sound_num, delta.sound_num, protocol > PROTOCOL_VERSION_22 ? MAX_SOUND_INDEX_BITS : 13);
	write_delta_uint(sound.flags, delta.flags, protocol > PROTOCOL_VERSION_18 ? SND_FLAG_BITS_ENCODE : 9);
	write_delta_uint(sound.channel, delta.channel, 3);
	writer.write_bool(sound.is_ambient);
	writer.write_bool(sound.is_sentence);

	if (sound.flags == SND_STOP) {
		return;
	}

	if (sound.sequence_number == delta.sequence_number) {
		writer.write_bool(true);
	}
	else if (sound.sequence_number == delta.sequence_number + 1) {
		writer.write_bool(false);
		writer.write_bool(true);
	}
	else {
		writer.write_bits(0, 2);
		writer.write_bits(sound.sequence_number, SOUND_SEQNUMBER_BITS);
	}

	auto send = [&](float value, float from, uint8_t sent) { return value != from || (sound.sent_unchanged & sent); };

	bool volume = send(sound.volume, delta.volume, SoundInfo::SENT_VOLUME);
	writer.write_bool(volume);
	if (volume) {
		writer.write_bits(static_cast<uint32_t>(std::lround(sound.volume * 127.0f)), 7);
	}
	write_delta_uint(sound.sound_level, delta.sound_level, MAX_SNDLVL_BITS);
	write_delta_uint(sound.pitch, delta.pitch, 8);
	if (protocol > PROTOCOL_VERSION_21) {
		write_delta_uint(sound.special_dsp, delta.special_dsp, 8);
	}

	bool delay_sent = send(sound.delay, delta.delay, SoundInfo::SENT_DELAY);
	writer.write_bool(delay_sent);
	if (delay_sent) {
		float msec = (sound.delay + SOUND_DELAY_OFFSET) * 1000.0f;
		constexpr int max_delay = (1 << (MAX_SOUND_DELAY_MSEC_ENCODE_BITS - 1)) - 1;
		int delay = static_cast<int>(std::lround(msec < 0 ? msec / 10.0f : msec));
		writer.write_signed_bits(std::clamp(delay, -max_delay, max_delay), MAX_SOUND_DELAY_MSEC_ENCODE_BITS);
	}

	auto write_origin = [&](float value, float from, uint8_t sent) {
		bool changed = send(value, from, sent);
		writer.write_bool(changed);
		if (changed) writer.write_signed_bits(static_cast<int>(std::lround(value / 8.0f)), COORD_INTEGER_BITS - 2);
	};
	write_origin(sound.origin.x, delta.origin.x, SoundInfo::SENT_ORIGIN_X);
	write_origin(sound.origin.y, delta.origin.y, SoundInfo::SENT_ORIGIN_Y);
	write_origin(sound.origin.z, delta.origin.z, SoundInfo::SENT_ORIGIN_Z);
	writer.write_bool(sound.speaker_entity != delta.speaker_entity);
	if (sound.speaker_entity != delta.speaker_entity) {
		writer.write_signed_bits(sound.speaker_entity, MAX_EDICT_BITS + 1);
	}
}

SvcSounds encode_sounds(const std::vector<SoundInfo>& sounds, const ProtocolVersion& protocol, bool reliable)
{
	if (reliable && sounds.size() != 1) {
		throw std::runtime_error("Reliable sounds carry one sound");
	}
	SvcSounds message;
	message.reliable_sound = reliable;
	message.num_sounds = static_cast<int>(sounds.size());
	BitWriter writer;
	SoundInfo previous{};
	for (const auto& sound : sounds) {
		write_sound_delta(writer, sound, previous, protocol.network);
		previous = sound;
		if (sound.flags == SND_STOP) {
			// the reader clears these
			previous.volume = 0;
			previous.sound_level = 0;
			previous.pitch = 100;
			previous.special_dsp = 0;
			previous.delay = 0;
			previous.sequence_number = 0;
			previous.origin = {};
			previous.speaker_entity = -1;
		}
	}
	message.data = writer.take_buffer();
	return message;
}

}
