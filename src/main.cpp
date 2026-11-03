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

#include "Widget.h"

namespace
{
	constexpr auto kName = "Holosong";

	// ------------------------------------------------------------------ Present hook
	// The game's own swap chain gets its Present replaced (the same approach as Hearthsong for Skyrim and Real Videos).
	// If the renderer can't be read on some game build, a throwaway swap chain of our own finds the same function
	// instead - that way needs no game addresses at all.
	using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
	PresentFn         g_originalPresent = nullptr;
	std::atomic<bool> g_presentDisabled{ false };
	IDXGISwapChain*   g_gameSc = nullptr;  // known from the renderer; null with the fallback
	IDXGISwapChain*   g_drawSc = nullptr;  // the one the widget draws on

	bool IsGameWindow(HWND a_wnd)
	{
		DWORD pid = 0;
		::GetWindowThreadProcessId(a_wnd, &pid);
		return a_wnd && pid == ::GetCurrentProcessId() && ::IsWindowVisible(a_wnd);
	}

	void SafeOnPresent(IDXGISwapChain* a_sc)
	{
		__try {
			Widget::OnPresent(a_sc);
		} __except (Widget::SehFilter(GetExceptionInformation(), "OnPresent")) {
			g_presentDisabled = true;
		}
	}

	HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* a_sc, UINT a_sync, UINT a_flags)
	{
		if (!g_presentDisabled) {
			if (!g_drawSc) {
				// first Present on the game's window (with the fallback any swap chain shares this function)
				DXGI_SWAP_CHAIN_DESC desc{};
				if ((g_gameSc && a_sc == g_gameSc) || (!g_gameSc && SUCCEEDED(a_sc->GetDesc(&desc)) && IsGameWindow(desc.OutputWindow))) {
					g_drawSc = a_sc;
				}
			}
			if (a_sc == g_drawSc) {
				SafeOnPresent(a_sc);
			}
		}
		return g_originalPresent(a_sc, a_sync, a_flags);
	}

	bool PatchPresentSlot(void** a_vtable)
	{
		if (a_vtable[8] == reinterpret_cast<void*>(&HookedPresent)) {
			return true;
		}
		DWORD old = 0;
		if (!::VirtualProtect(&a_vtable[8], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
			spdlog::error("  VirtualProtect failed ({})", ::GetLastError());
			return false;
		}
		g_originalPresent = reinterpret_cast<PresentFn>(a_vtable[8]);
		a_vtable[8] = reinterpret_cast<void*>(&HookedPresent);
		::VirtualProtect(&a_vtable[8], sizeof(void*), old, &old);
		return true;
	}

	IDXGISwapChain* GameSwapChainRaw()
	{
		auto* data = RE::BSGraphics::GetRendererData();
		return data ? reinterpret_cast<IDXGISwapChain*>(data->renderWindow[0].swapChain) : nullptr;
	}

	// The renderer's swap chain, if it really is one on the game's window
	IDXGISwapChain* SafeGameSwapChain()
	{
		IDXGISwapChain* sc = nullptr;
		__try {
			sc = GameSwapChainRaw();
			if (sc) {
				DXGI_SWAP_CHAIN_DESC desc{};
				if (FAILED(sc->GetDesc(&desc)) || !desc.OutputWindow) {
					sc = nullptr;
				} else {
					DWORD pid = 0;
					::GetWindowThreadProcessId(desc.OutputWindow, &pid);
					if (pid != ::GetCurrentProcessId()) {
						sc = nullptr;
					}
				}
			}
		} __except (Widget::SehFilter(GetExceptionInformation(), "GameSwapChain")) {
			sc = nullptr;
		}
		return sc;
	}

	// Fallback: a tiny swap chain on a hidden window of our own, just to find Present
	void** DummyPresentTable()
	{
		WNDCLASSEXW wc{ sizeof wc };
		wc.lpfnWndProc = ::DefWindowProcW;
		wc.hInstance = ::GetModuleHandleW(nullptr);
		wc.lpszClassName = L"HolosongDummy";
		::RegisterClassExW(&wc);
		HWND wnd = ::CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 8, 8, nullptr, nullptr, wc.hInstance, nullptr);
		if (!wnd) {
			return nullptr;
		}
		DXGI_SWAP_CHAIN_DESC sd{};
		sd.BufferCount = 1;
		sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.OutputWindow = wnd;
		sd.SampleDesc.Count = 1;
		sd.Windowed = TRUE;
		IDXGISwapChain*      sc = nullptr;
		ID3D11Device*        dev = nullptr;
		ID3D11DeviceContext* ctx = nullptr;
		void**               table = nullptr;
		if (SUCCEEDED(::D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev, nullptr, &ctx)) && sc) {
			table = *reinterpret_cast<void***>(sc);
		}
		if (sc) {
			sc->Release();
		}
		if (ctx) {
			ctx->Release();
		}
		if (dev) {
			dev->Release();
		}
		::DestroyWindow(wnd);
		::UnregisterClassW(wc.lpszClassName, wc.hInstance);
		return table;
	}

	void InstallPresentHook()
	{
		if (auto* sc = SafeGameSwapChain()) {
			if (PatchPresentSlot(*reinterpret_cast<void***>(sc))) {
				g_gameSc = sc;
				spdlog::info("Present hook installed on the game's swap chain {}", static_cast<void*>(sc));
				return;
			}
		}
		spdlog::warn("The game's swap chain couldn't be read - finding Present another way");
		if (auto** table = DummyPresentTable(); table && PatchPresentSlot(table)) {
			spdlog::info("Present hook installed (fallback)");
			return;
		}
		spdlog::error("Could not install the Present hook - the widget will not appear");
	}

	void OnMessage(F4SE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}
		switch (a_msg->type) {
		case F4SE::MessagingInterface::kGameDataReady:
			{
				static bool done = false;  // also sent again after a data reload
				if (done) {
					break;
				}
				done = true;
				if (Widget::GetDebugSwitches().hookPresent) {
					InstallPresentHook();
				} else {
					spdlog::warn("Present hook DISABLED by bHookPresent=0");
				}
				Widget::OnDataLoaded();
				break;
			}
		case F4SE::MessagingInterface::kPostLoadGame:
		case F4SE::MessagingInterface::kNewGame:
			Widget::OnGameLoaded();
			break;
		default:
			break;
		}
	}

	// Which game this is, and whether Address Library has the file for it
	bool CheckRuntime(const F4SE::LoadInterface* a_f4se)
	{
		const auto  ver = a_f4se->RuntimeVersion();
		const char* gen = REX::FModule::IsRuntimeOG() ? "Old-gen" : REX::FModule::IsRuntimeNG() ? "Next-gen" : "Anniversary";
		spdlog::info("Fallout 4 {} ({}), F4SE {}", ver.string(), gen, a_f4se->F4SEVersion().string());
		const auto lib = std::format("Data\\F4SE\\Plugins\\version-{}.bin", ver.string("-"));
		std::error_code ec;
		if (std::filesystem::exists(lib, ec)) {
			spdlog::info("Address Library found: {}", lib);
			return true;
		}
		spdlog::critical("Address Library NOT FOUND: {} - install 'Address Library for F4SE Plugins' for your game version. Holosong is switched off.", lib);
		return false;
	}
}

// Old-gen F4SE (0.6.x) asks this first; next-gen / Anniversary F4SE reads F4SEPlugin_Version (res/commonlibf4-plugin.cpp.in).
extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->name = kName;
	a_info->version = 1;
	if (a_f4se->IsEditor()) {
		return false;
	}
	return a_f4se->RuntimeVersion() >= F4SE::RUNTIME_1_10_163;
}

extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
	// Holosong.log in Documents\My Games\Fallout4\F4SE (the last two runs kept as Holosong.1.log / .2.log)
	F4SE::Init(a_f4se, { .log = true, .logLevel = REX::ELogLevel::Info, .logName = kName, .logPattern = "[%H:%M:%S.%e] [%l] %v", .logRotate = 2, .trampoline = false });
	if (!CheckRuntime(a_f4se)) {
		return false;
	}
	Widget::LoadSettings();
	if (Widget::InstallInput()) {
		spdlog::info("Keyboard and mouse: DirectInput hook in place");
	} else {
		spdlog::error("Could not hook DirectInput - the keys will not work");
	}
	auto* messaging = F4SE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnMessage)) {
		spdlog::error("Could not register with the F4SE messaging interface");
		return false;
	}
	return true;
}
