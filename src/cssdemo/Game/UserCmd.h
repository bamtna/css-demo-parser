#pragma once
#include "cssdemo/Protocol/Types.h"
#include <cstddef>
#include <ostream>
#include <span>
#include <string>

namespace cssdemo {

struct UserCmd {
	int command_number{};
	int tick_count{};
	QAngle view_angles{};
	float forward_move{};
	float side_move{};
	float up_move{};
	uint32_t buttons{};
	int impulse{};
	int weapon_select{};
	int weapon_subtype{};
	int mouse_dx{};
	int mouse_dy{};

	std::string button_names() const;
	friend std::ostream& operator<<(std::ostream& os, const UserCmd& cmd);
};

// ReadUsercmd, delta against a null command
UserCmd decode_user_cmd(std::span<const std::byte> payload);

}
