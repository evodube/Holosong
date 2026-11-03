// Holosong - an in-game music widget for Fallout 4
// Copyright (C) 2026 evodube
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//
// Additional permissions: the Modding Exception and the GPL-3.0 Linking
// Exception from CommonLibF4 apply (see EXCEPTIONS.txt).
// Source code: https://github.com/evodube/holosong

#pragma once

// CommonLibF4 must come before any Windows API header (it checks for this).
#pragma warning(push)
#include "F4SE/F4SE.h"
#include "RE/Fallout.h"
#include "REX/REX.h"
#pragma warning(pop)

#include <spdlog/spdlog.h>

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <ShlObj.h>

// wingdi.h defines ERROR, which collides with REX::ERROR
#ifdef ERROR
#	undef ERROR
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std::literals;
