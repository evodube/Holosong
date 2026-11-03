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

// Keyboard and mouse, read where Fallout 4 reads them: DirectInput 8 (the same layer F4SE hooks).
// Nothing here depends on the game's version - no game addresses, so OG, NG and AE all work the same.
// Compiled without the precompiled header (it needs the real <dinput.h>).

#include <cstdint>

namespace Input
{
	enum class Kind : std::uint8_t
	{
		Key,          // code = DirectInput key code (DIK_*), down = pressed / released
		MouseButton,  // code = 0 left, 1 right, 2 middle, 3-7 extra buttons
		Wheel,        // y = notches (+ up, - down)
		MouseMove     // x, y = movement in mouse counts
	};

	struct Event
	{
		Kind          kind = Kind::Key;
		std::uint32_t code = 0;
		bool          down = false;
		float         x = 0.0f, y = 0.0f;
	};

	using Callback = void (*)(const Event&);

	// Hooks DirectInput's keyboard/mouse reads (plugin load). Every key/mouse event the game reads goes to a_callback,
	// on the thread that reads it. False if DirectInput couldn't be reached.
	bool Install(Callback a_callback);

	// Cursor mode: the game (and every mod reading input through it) gets no key presses, mouse buttons, wheel or
	// mouse movement - only releases, so nothing stays held when the cursor goes away.
	void SetBlocking(bool a_block);

	// For the log: has the game read the keyboard / mouse through DirectInput yet?
	bool KeyboardSeen();
	bool MouseSeen();
}
