#include "UserCmd.h"
#include "cssdemo/Protocol/Constants.h"
#include "cssdemo/Util/BitReader.h"
#include <array>

namespace cssdemo {

// in_buttons.h
static constexpr std::array<const char*, 26> BUTTON_NAMES = {
	"attack", "jump", "duck", "forward", "back", "use", "cancel", "left",
	"right", "moveleft", "moveright", "attack2", "run", "reload", "alt1", "alt2",
	"score", "speed", "walk", "zoom", "weapon1", "weapon2", "bullrush", "grenade1",
	"grenade2", "attack3",
};

UserCmd decode_user_cmd(std::span<const std::byte> payload)
{
	BitReader reader(payload);
	UserCmd cmd{};

	cmd.command_number = reader.read_bool() ? reader.read_int32() : 1;
	cmd.tick_count = reader.read_bool() ? reader.read_int32() : 1;
	if (reader.read_bool()) cmd.view_angles.x = reader.read_float32();
	if (reader.read_bool()) cmd.view_angles.y = reader.read_float32();
	if (reader.read_bool()) cmd.view_angles.z = reader.read_float32();
	if (reader.read_bool()) cmd.forward_move = reader.read_float32();
	if (reader.read_bool()) cmd.side_move = reader.read_float32();
	if (reader.read_bool()) cmd.up_move = reader.read_float32();
	if (reader.read_bool()) cmd.buttons = reader.read_uint32();
	if (reader.read_bool()) cmd.impulse = reader.read_uint8();
	if (reader.read_bool()) {
		cmd.weapon_select = reader.read_bits(MAX_EDICT_BITS);
		if (reader.read_bool()) cmd.weapon_subtype = reader.read_bits(WEAPON_SUBTYPE_BITS);
	}
	if (reader.read_bool()) cmd.mouse_dx = reader.read_int16();
	if (reader.read_bool()) cmd.mouse_dy = reader.read_int16();
	return cmd;
}

std::string UserCmd::button_names() const
{
	std::string result;
	for (size_t i = 0; i < BUTTON_NAMES.size(); i++) {
		if (buttons & (1u << i)) {
			if (!result.empty()) result += "|";
			result += BUTTON_NAMES[i];
		}
	}
	return result;
}

std::ostream& operator<<(std::ostream& os, const UserCmd& cmd)
{
	os << "command_number=" << cmd.command_number
		<< ", tick_count=" << cmd.tick_count
		<< ", view_angles=(" << cmd.view_angles.x << ", " << cmd.view_angles.y << ", " << cmd.view_angles.z << ")"
		<< ", move=(" << cmd.forward_move << ", " << cmd.side_move << ", " << cmd.up_move << ")"
		<< ", buttons=" << (cmd.buttons ? cmd.button_names() : "none");
	if (cmd.impulse) os << ", impulse=" << cmd.impulse;
	if (cmd.weapon_select) os << ", weapon_select=" << cmd.weapon_select;
	os << ", mouse=(" << cmd.mouse_dx << ", " << cmd.mouse_dy << ")";
	return os;
}

}
