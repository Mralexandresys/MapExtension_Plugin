#pragma once

#include "map_state_types.h"

#include <string>

namespace MapStateRuntime
{
namespace Detail
{
	std::string BuildHealthJson(const CargoSnapshot& snapshot, int httpPort);
	std::string BuildCargoJson(const CargoSnapshot& snapshot);
}
}
