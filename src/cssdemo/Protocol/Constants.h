#pragma once

namespace cssdemo {

// demoformat.h
constexpr const char* DEMO_HEADER_ID = "HL2DEMO";
constexpr int DEMO_PROTOCOL = 3;
constexpr int MAX_OSPATH = 260;

// proto_version.h
constexpr int PROTOCOL_VERSION = 24;
constexpr int PROTOCOL_VERSION_14 = 14;
constexpr int PROTOCOL_VERSION_17 = 17;
constexpr int PROTOCOL_VERSION_18 = 18;
constexpr int PROTOCOL_VERSION_21 = 21;
constexpr int PROTOCOL_VERSION_22 = 22;
constexpr int PROTOCOL_VERSION_23 = 23;
constexpr int PROTOCOL_VERSION_REPLAY = 16;
constexpr int PROTOCOL_VERSION_MIN_SUPPORTED = PROTOCOL_VERSION_14;

// net.h / protocol.h
constexpr int NETMSG_TYPE_BITS = 6;
constexpr int NETMSG_LENGTH_BITS = 11;
constexpr int NET_MAX_PAYLOAD_BITS_V23 = 17;
constexpr int DELTASIZE_BITS = 20;
constexpr float NET_TICK_SCALEUP = 100000.0f;

// const.h
constexpr int MAX_EDICT_BITS = 11;
constexpr int MAX_EDICTS = 1 << MAX_EDICT_BITS;
constexpr int MAX_SERVER_CLASS_BITS = 9;
constexpr int NUM_NETWORKED_EHANDLE_SERIAL_NUMBER_BITS = 10;
constexpr int SP_MODEL_INDEX_BITS = 13;
constexpr int MAX_PLAYER_NAME_LENGTH = 32;
constexpr int SIGNED_GUID_LEN = 32;
constexpr int MAX_CUSTOM_FILES = 4;

// qlimits.h
constexpr int MAX_DECAL_INDEX_BITS = 9;

// soundflags.h / soundinfo.h
constexpr int MAX_SOUND_INDEX_BITS = 14;
constexpr int SND_FLAG_BITS_ENCODE = 11;
constexpr int MAX_SNDLVL_BITS = 9;
constexpr int MAX_SOUND_DELAY_MSEC_ENCODE_BITS = 13;
constexpr float SOUND_DELAY_OFFSET = 0.100f;
constexpr int SOUND_SEQNUMBER_BITS = 10;
constexpr int SND_STOP = 1 << 2;

// igameevents.h
constexpr int MAX_EVENT_BITS = 9;

// event_system.h
constexpr int EVENT_INDEX_BITS = 8;

// networkstringtabledefs.h / networkstringtable.h
constexpr int MAX_TABLES = 32;
constexpr int SUBSTRING_BITS = 5;
constexpr int MAX_USERDATA_BITS = 14;
constexpr int STRING_HISTORY_SIZE = 32;

// dt.h / dt_common.h
constexpr int PROPINFOBITS_NUMPROPS = 10;
constexpr int PROPINFOBITS_TYPE = 5;
constexpr int PROPINFOBITS_FLAGS = 16; // SPROP_NUMFLAGBITS_NETWORKED
constexpr int PROPINFOBITS_NUMELEMENTS = 10;
constexpr int PROPINFOBITS_NUMBITS = 7;
constexpr int DT_MAX_STRING_BITS = 9;
constexpr int MAX_DATATABLE_PROPS = 4096;

// usercmd.cpp
constexpr int WEAPON_SUBTYPE_BITS = 6;

// bitbuf.h
constexpr int COORD_INTEGER_BITS = 14;
constexpr int COORD_FRACTIONAL_BITS = 5;
constexpr int COORD_DENOMINATOR = 1 << COORD_FRACTIONAL_BITS;
constexpr float COORD_RESOLUTION = 1.0f / COORD_DENOMINATOR;
constexpr int COORD_INTEGER_BITS_MP = 11;
constexpr int COORD_FRACTIONAL_BITS_MP_LOWPRECISION = 3;
constexpr int COORD_DENOMINATOR_LOWPRECISION = 1 << COORD_FRACTIONAL_BITS_MP_LOWPRECISION;
constexpr float COORD_RESOLUTION_LOWPRECISION = 1.0f / COORD_DENOMINATOR_LOWPRECISION;
constexpr int NORMAL_FRACTIONAL_BITS = 11;
constexpr int NORMAL_DENOMINATOR = (1 << NORMAL_FRACTIONAL_BITS) - 1;
constexpr float NORMAL_RESOLUTION = 1.0f / NORMAL_DENOMINATOR;

inline int Q_log2(int val)
{
	int answer = 0;
	while (val >>= 1)
		answer++;
	return answer;
}

}
