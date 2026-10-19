#pragma once
#include "cssdemo/Protocol/Types.h"
#include <ostream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cssdemo {

struct SvcUserMessage;

struct UserMessage {
	using Value = std::variant<int, float, std::string, Vector>;

	int type{};
	std::string name;
	std::vector<std::pair<std::string, Value>> fields; // strings as sent
	std::string text;   // chat/text messages
	std::string error;
	bool decoded{};

	friend std::ostream& operator<<(std::ostream& os, const UserMessage& message);
};

std::string user_message_name(int type);
int user_message_type(const std::string& name); // -1 if unknown

UserMessage decode_user_message(const SvcUserMessage& message);
// writes fields in the order decode_user_message produces them, throws for messages it can't decode
SvcUserMessage encode_user_message(const UserMessage& message);

}
