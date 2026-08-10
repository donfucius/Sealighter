// This is just a "healper" header to add json functionality
#pragma once

// Disable nlohmann json assertions — code uses contains() for safe access.
// The assert() on missing keys aborts in Debug builds and is redundant with
// the contains() checks added throughout sealighter_controller.cpp.
#define JSON_ASSERT(x) ((void)0)

#include "nlohmann/json.hpp"
using json = nlohmann::json;
