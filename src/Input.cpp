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

// DirectInput 8 hook: the two calls every DirectInput keyboard/mouse is read with (GetDeviceState, GetDeviceData)
// are replaced in the device class's function table. All of the game's devices share that table, so a throwaway
// device of our own is enough to find it - no game addresses, works on every Fallout 4 version.

#define DIRECTINPUT_VERSION 0x0800
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>
#include <dinput.h>

#include "Input.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace Input
{
	namespace
	{
		using GetDeviceState_t = HRESULT(STDMETHODCALLTYPE*)(void*, DWORD, LPVOID);
		using GetDeviceData_t = HRESULT(STDMETHODCALLTYPE*)(void*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
		using GetCapabilities_t = HRESULT(STDMETHODCALLTYPE*)(void*, LPDIDEVCAPS);

		constexpr int kSlotCaps = 3, kSlotState = 9, kSlotData = 10;  // IDirectInputDevice8 function table

		struct Table
		{
			void**           vtbl = nullptr;
			GetDeviceState_t state = nullptr;
			GetDeviceData_t  data = nullptr;
		};
		Table g_tables[2];  // the wide (W) and ANSI (A) device classes; the game uses one of them

		std::atomic<Callback> g_callback{ nullptr };
		std::atomic<bool>     g_block{ false };
		std::atomic<bool>     g_keyboardSeen{ false }, g_mouseSeen{ false };

		enum class DevType : std::uint8_t
		{
			Other,
			Keyboard,
			Mouse
		};
		struct DevInfo
		{
			DevType       type = DevType::Other;
			bool          buffered = false;  // the game reads events (GetDeviceData): edges come from there
			std::uint8_t  keys[256]{};       // last keyboard state (GetDeviceState)
			std::uint8_t  buttons[8]{};      // last mouse buttons (GetDeviceState)
		};
		std::mutex                         g_devMtx;
		std::unordered_map<void*, DevInfo> g_devs;

		DevInfo& Info(void* a_dev)
		{
			auto [it, added] = g_devs.try_emplace(a_dev);
			if (added) {
				DIDEVCAPS caps{};
				caps.dwSize = sizeof caps;
				auto** vtbl = *reinterpret_cast<void***>(a_dev);
				if (SUCCEEDED(reinterpret_cast<GetCapabilities_t>(vtbl[kSlotCaps])(a_dev, &caps))) {
					switch (GET_DIDEVICE_TYPE(caps.dwDevType)) {
					case DI8DEVTYPE_KEYBOARD:
						it->second.type = DevType::Keyboard;
						break;
					case DI8DEVTYPE_MOUSE:
						it->second.type = DevType::Mouse;
						break;
					default:
						break;
					}
				}
				spdlog::info("Input: the game reads a DirectInput {} ({})",
					it->second.type == DevType::Keyboard ? "keyboard" : it->second.type == DevType::Mouse ? "mouse" : "device (ignored)", a_dev);
			}
			return it->second;
		}

		void Emit(const Event& a_e)
		{
			if (auto cb = g_callback.load()) {
				cb(a_e);
			}
		}

		template <int I>
		HRESULT STDMETHODCALLTYPE HookGetDeviceData(void* a_dev, DWORD a_size, LPDIDEVICEOBJECTDATA a_data, LPDWORD a_inOut, DWORD a_flags)
		{
			const HRESULT hr = g_tables[I].data(a_dev, a_size, a_data, a_inOut, a_flags);
			if (FAILED(hr) || !a_data || !a_inOut || *a_inOut == 0 || a_size < 2 * sizeof(DWORD)) {
				return hr;
			}
			DevType type;
			{
				std::lock_guard lk(g_devMtx);
				auto&           info = Info(a_dev);
				info.buffered = true;
				type = info.type;
			}
			if (type == DevType::Other) {
				return hr;
			}
			(type == DevType::Keyboard ? g_keyboardSeen : g_mouseSeen) = true;

			const bool  peek = (a_flags & DIGDD_PEEK) != 0;  // looked at, not taken: report it when it's read for real
			const bool  block = g_block.load();
			auto*       base = reinterpret_cast<std::uint8_t*>(a_data);
			const DWORD n = *a_inOut;
			DWORD       kept = 0;
			for (DWORD i = 0; i < n; ++i) {
				auto*       item = reinterpret_cast<DIDEVICEOBJECTDATA*>(base + static_cast<std::size_t>(i) * a_size);
				const DWORD ofs = item->dwOfs;
				const DWORD val = item->dwData;
				bool        keep = true;
				if (type == DevType::Keyboard) {
					const bool down = (val & 0x80) != 0;
					if (!peek) {
						Emit({ Kind::Key, ofs, down });
					}
					keep = !block || !down;  // releases still go through
				} else {
					if (ofs == DIMOFS_X || ofs == DIMOFS_Y) {
						if (!peek) {
							const float d = static_cast<float>(static_cast<LONG>(val));
							Emit({ Kind::MouseMove, 0, false, ofs == DIMOFS_X ? d : 0.0f, ofs == DIMOFS_Y ? d : 0.0f });
						}
						keep = !block;
					} else if (ofs == DIMOFS_Z) {
						if (!peek) {
							Emit({ Kind::Wheel, 0, false, 0.0f, static_cast<float>(static_cast<LONG>(val)) / WHEEL_DELTA });
						}
						keep = !block;
					} else if (ofs >= DIMOFS_BUTTON0 && ofs <= DIMOFS_BUTTON7) {
						const bool down = (val & 0x80) != 0;
						if (!peek) {
							Emit({ Kind::MouseButton, static_cast<std::uint32_t>(ofs - DIMOFS_BUTTON0), down });
						}
						keep = !block || !down;
					}
				}
				if (keep) {
					if (kept != i) {
						std::memmove(base + static_cast<std::size_t>(kept) * a_size, item, a_size);
					}
					++kept;
				}
			}
			*a_inOut = kept;
			return hr;
		}

		template <int I>
		HRESULT STDMETHODCALLTYPE HookGetDeviceState(void* a_dev, DWORD a_size, LPVOID a_out)
		{
			const HRESULT hr = g_tables[I].state(a_dev, a_size, a_out);
			if (FAILED(hr) || !a_out) {
				return hr;
			}
			const bool block = g_block.load();
			std::lock_guard lk(g_devMtx);
			auto&           info = Info(a_dev);
			if (info.type == DevType::Keyboard && a_size == 256) {
				g_keyboardSeen = true;
				auto* keys = static_cast<std::uint8_t*>(a_out);
				if (!info.buffered) {
					for (std::uint32_t k = 0; k < 256; ++k) {
						const bool now = (keys[k] & 0x80) != 0, was = (info.keys[k] & 0x80) != 0;
						if (now != was) {
							Emit({ Kind::Key, k, now });
						}
					}
				}
				std::memcpy(info.keys, keys, 256);
				if (block) {
					std::memset(keys, 0, 256);  // every key looks released to the game
				}
			} else if (info.type == DevType::Mouse && (a_size == sizeof(DIMOUSESTATE) || a_size == sizeof(DIMOUSESTATE2))) {
				g_mouseSeen = true;
				auto*     ms = static_cast<DIMOUSESTATE2*>(a_out);  // DIMOUSESTATE is the first part of DIMOUSESTATE2
				const int nb = a_size == sizeof(DIMOUSESTATE2) ? 8 : 4;
				if (!info.buffered) {
					if (ms->lX || ms->lY) {
						Emit({ Kind::MouseMove, 0, false, static_cast<float>(ms->lX), static_cast<float>(ms->lY) });
					}
					if (ms->lZ) {
						Emit({ Kind::Wheel, 0, false, 0.0f, static_cast<float>(ms->lZ) / WHEEL_DELTA });
					}
					for (int b = 0; b < nb; ++b) {
						const bool now = (ms->rgbButtons[b] & 0x80) != 0, was = (info.buttons[b] & 0x80) != 0;
						if (now != was) {
							Emit({ Kind::MouseButton, static_cast<std::uint32_t>(b), now });
						}
					}
				}
				for (int b = 0; b < nb; ++b) {
					info.buttons[b] = ms->rgbButtons[b];
				}
				if (block) {
					ms->lX = ms->lY = ms->lZ = 0;
					std::memset(ms->rgbButtons, 0, static_cast<std::size_t>(nb));
				}
			}
			return hr;
		}

		bool Patch(void** a_slot, void* a_fn, void** a_orig)
		{
			DWORD old = 0;
			if (!::VirtualProtect(a_slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
				return false;
			}
			*a_orig = *a_slot;
			*a_slot = a_fn;
			::VirtualProtect(a_slot, sizeof(void*), old, &old);
			return true;
		}

		template <int I>
		bool HookTable(void** a_vtbl)
		{
			for (const auto& t : g_tables) {
				if (t.vtbl == a_vtbl) {
					return true;  // same class as the other one: already done
				}
			}
			auto& t = g_tables[I];
			t.vtbl = a_vtbl;
			const bool ok = Patch(&a_vtbl[kSlotState], reinterpret_cast<void*>(&HookGetDeviceState<I>), reinterpret_cast<void**>(&t.state)) &&
			                Patch(&a_vtbl[kSlotData], reinterpret_cast<void*>(&HookGetDeviceData<I>), reinterpret_cast<void**>(&t.data));
			if (!ok) {
				spdlog::error("Input: could not patch DirectInput ({})", ::GetLastError());
			}
			return ok;
		}

		using DirectInput8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

		template <int I, class DI, class Dev>
		bool HookClass(DirectInput8Create_t a_create, REFIID a_iid)
		{
			DI* di = nullptr;
			if (FAILED(a_create(::GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, a_iid, reinterpret_cast<void**>(&di), nullptr)) || !di) {
				return false;
			}
			Dev* dev = nullptr;
			bool ok = false;
			if (SUCCEEDED(di->CreateDevice(GUID_SysKeyboard, &dev, nullptr)) && dev) {
				ok = HookTable<I>(*reinterpret_cast<void***>(dev));
				dev->Release();
			}
			di->Release();
			return ok;
		}
	}

	bool Install(Callback a_callback)
	{
		g_callback = a_callback;
		// The same dinput8.dll the game uses (a dinput8.dll in the game folder - an ENB/proxy - is picked first, like for the game)
		HMODULE mod = ::LoadLibraryW(L"dinput8.dll");  // stays loaded: its function table is patched
		auto    create = mod ? reinterpret_cast<DirectInput8Create_t>(::GetProcAddress(mod, "DirectInput8Create")) : nullptr;
		if (!create) {
			spdlog::error("Input: dinput8.dll / DirectInput8Create not found");
			return false;
		}
		const bool w = HookClass<0, IDirectInput8W, IDirectInputDevice8W>(create, IID_IDirectInput8W);
		const bool a = HookClass<1, IDirectInput8A, IDirectInputDevice8A>(create, IID_IDirectInput8A);
		spdlog::info("Input: DirectInput hooked (wide {}, ANSI {})", w ? "yes" : "no", a ? "yes" : "no");
		return w || a;
	}

	void SetBlocking(bool a_block) { g_block = a_block; }
	bool KeyboardSeen() { return g_keyboardSeen; }
	bool MouseSeen() { return g_mouseSeen; }
}
