#pragma once
#include <string>

constexpr int COORD_INTEGER_BITS = 14;
constexpr int COORD_FRACTIONAL_BITS = 5;
constexpr int COORD_DENOMINATOR = 1 << COORD_FRACTIONAL_BITS;
constexpr double COORD_RESOLUTION = 1.0 / COORD_DENOMINATOR;

constexpr int COORD_INTEGER_BITS_MP = 11;
constexpr int COORD_FRACTIONAL_BITS_MP_LOWPRECISION = 3;
constexpr int COORD_DENOMINATOR_LOWPRECISION = 1 << COORD_FRACTIONAL_BITS_MP_LOWPRECISION;
constexpr double COORD_RESOLUTION_LOWPRECISION = 1.0 / COORD_DENOMINATOR_LOWPRECISION;

constexpr int NORMAL_FRACTIONAL_BITS = 11;
constexpr int NORMAL_DENOMINATOR = (1 << NORMAL_FRACTIONAL_BITS) - 1;
constexpr double NORMAL_RESOLUTION = 1.0 / NORMAL_DENOMINATOR;
constexpr double DIST_EPSILON = 0.03125;

struct QAngle
{
	float x{}, y{}, z{};
};
struct Vector
{
	float x{}, y{}, z{};
};

struct ConVar {
	std::string name;
	std::string value;
};

struct CmdInfo {
	int     flags{};
	// original origin/viewangles
	Vector  view_origin;
	QAngle  view_angles;
	QAngle  local_view_angles;
	// Resampled origin/viewangles
	Vector  view_origin2;
	QAngle  view_angles2;
	QAngle  local_view_angles2;
};

