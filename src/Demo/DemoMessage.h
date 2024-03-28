#pragma once
#include "NetMessage.h"
#include "structs.h"
#include <vector>
#include <memory>

class BinaryReader;

struct DemoMessage {
	enum class Type {
		SIGN_ON = 1,
		PACKET,
		SYNC_TICK,
		CONSOLE_CMD,
		USER_CMD,
		DATA_TABLES,
		STOP,
		STRING_TABLES,
		LAST_CMD = STRING_TABLES
	};

	DemoMessage(Type _type, int _tick) : type(_type), tick(_tick) {};
	virtual ~DemoMessage() = default;
	virtual void parse(BinaryReader& reader) = 0;

	Type type{};
	int tick{};
};

struct Packet : public DemoMessage {
	Packet(int tick) : DemoMessage(Type::PACKET, tick) {};
	void parse(BinaryReader& reader) override;

	CmdInfo cmd_info{};
	int in_sequence{};
	int out_sequence{};

	std::vector<std::unique_ptr<NetMessage>> net_messages;
};

struct SignOn : public Packet {
	SignOn(int tick) : Packet(tick) { type = Type::SIGN_ON; };
};

struct SyncTick : public DemoMessage {
	SyncTick(int tick) : DemoMessage(Type::SYNC_TICK, tick) {}
	void parse(BinaryReader& reader) override;
};

struct ConsoleCmd : public DemoMessage {
	ConsoleCmd(int tick) : DemoMessage(Type::CONSOLE_CMD, tick) {}
	void parse(BinaryReader& reader) override;
	std::string command;
};

struct UserCmd : public DemoMessage {
	UserCmd(int tick) : DemoMessage(Type::USER_CMD, tick) {};
	void parse(BinaryReader& reader) override;

	int cmd{};
	std::vector<std::byte> data;
};

struct DataTable : public DemoMessage {
	DataTable(int tick) : DemoMessage(Type::DATA_TABLES, tick) {}
	void parse(BinaryReader& reader) override;
	std::vector<std::byte> data;
};

struct StringTable : public DemoMessage {
	StringTable(int tick) : DemoMessage(Type::STRING_TABLES, tick) {}
	void parse(BinaryReader& reader) override;
	std::vector<std::byte> data;
};

struct Stop : public DemoMessage {
	Stop(int tick) : DemoMessage(Type::STOP, tick) {}
	void parse(BinaryReader& reader) override;
};
