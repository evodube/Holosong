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

#include "Input.h"
#include "Media.h"
#include "Theme.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_impl_dx11.h>

#include <cfloat>

namespace fs = std::filesystem;

namespace Widget
{
	namespace
	{
		// ================================================================ settings
		constexpr float kMinW = 240.0f, kMaxW = 1000.0f, kMinH = 84.0f, kMaxH = 680.0f;  // widget size at 1080p
		constexpr float kDefW = 340.0f, kDefH = 380.0f;

		enum KeySlot
		{
			kKeyToggle,
			kKeyCursor,
			kKeyUndo,
			kKeyPlayPause,
			kKeyNext,
			kKeyPrevious,
			kKeyVolUp,
			kKeyVolDown,
			kKeySettings,
			kKeyCount
		};
		const char* const kKeyIni[kKeyCount] = { "iToggleKey", "iCursorKey", "iUndoKey", "iPlayPauseKey", "iNextKey", "iPreviousKey", "iVolumeUpKey", "iVolumeDownKey", "iSettingsKey" };
		const char* const kKeySection[kKeyCount] = { "General", "General", "Keys", "Keys", "Keys", "Keys", "Keys", "Keys", "General" };
		// Page Down show/hide, Page Up cursor mode (F6/F7 clash with other mods); settings: the gear in cursor mode, no key by default
		const int         kKeyDefault[kKeyCount] = { VK_NEXT, VK_PRIOR, 'Z', 0, 0, 0, 0, 0, 0 };
		const char* const kKeyLabel[kKeyCount] = { "Show / hide widget", "Cursor mode", "Undo move (with Ctrl)", "Play / pause", "Next track", "Previous track",
			"Volume up", "Volume down", "Settings window" };
		const char* const kKeyHelp[kKeyCount] = {
			"Shows or hides the widget. Works any time you're playing.",
			"Shows a cursor so you can click, drag and resize the widget. While it's on, the game and other mods get no keys or clicks.",
			"In cursor mode: Ctrl + this key puts the widget back where it was before the last move or resize.",
			"Works without the cursor, any time you're playing. Your keyboard's own media keys work too.",
			"Works without the cursor.",
			"Works without the cursor.",
			"Raises the music app's volume in the Windows volume mixer by the volume step.",
			"Lowers the music app's volume in the Windows volume mixer by the volume step.",
			"Opens the widget's settings (the cursor comes up with them). Press it again to close them.",
		};

		struct Config
		{
			int   keyVk[kKeyCount]{};
			bool  cursorHold = false;  // cursor mode while the key is held, instead of press on / press off
			bool  visible = true;
			float mouseSpeed = 1.0f;
			float uiScale = 1.0f;  // settings window
			float fontSize = 18.0f;
			// widget
			float posX = 0.70f, posY = 0.06f;  // top-left corner, as a fraction of the screen
			float w = kDefW, h = kDefH;        // size at 1080p and 100 % scale
			int   scalePct = 100;
			int   opacityPct = 95;
			int   controlsOpacityPct = 100;  // album cover and controls (buttons, bars, sliders); text never fades
			// look: 0 = Hearthsong gold, 1 = the game's HUD colour, 2 = the game's Pip-Boy colour, 3 = custom
			int   colorMode = 1;
			int   customR = 20, customG = 255, customB = 23;
			bool  tintArt = true;  // the album cover in the colour too (else its own colours)
			bool  showHeader = true, showArt = true, showProgress = true, showSound = true;
			bool  locked = false;
			// safety
			bool  leaveCursorInCombat = true;
			bool  noCursorInCombat = true;
			int   dragDelayMs = 150;
			int   dragDistance = 6;
			// hiding
			bool  autoHideInCombat = false;
			float showAgainAfter = 2.0f;
			bool  hideInMenus = true;
			bool  hideWhenNoPlayer = false;
			// sound
			bool        muteGameMusic = true;
			bool        muteRadio = false;
			int         volumeStep = 5;
			std::string musicApp = "Spotify";
			bool        anyPlayer = false;
			// diagnostics
			bool        showFps = false;
		} cfg;

		DebugSwitches dbg;
		std::string   g_iniPath;

		std::uint32_t VirtualKeyToDIK(int a_vk)
		{
			if (a_vk <= 0) {
				return 0;
			}
			UINT sc = ::MapVirtualKeyW(static_cast<UINT>(a_vk), MAPVK_VK_TO_VSC_EX);
			if (sc == 0) {
				return 0;
			}
			// keys Windows doesn't always mark as extended (seen: Page Up came out as Num 9)
			switch (a_vk) {
			case VK_PRIOR:
			case VK_NEXT:
			case VK_END:
			case VK_HOME:
			case VK_INSERT:
			case VK_DELETE:
			case VK_LEFT:
			case VK_RIGHT:
			case VK_UP:
			case VK_DOWN:
			case VK_RCONTROL:
			case VK_RMENU:
			case VK_DIVIDE:
			case VK_LWIN:
			case VK_RWIN:
			case VK_APPS:
				sc |= 0xE000;
				break;
			default:
				break;
			}
			if ((sc & 0xFF00) == 0xE000 || (sc & 0xFF00) == 0xE100) {
				return (sc & 0x7F) | 0x80;  // extended keys: DirectInput sets the high bit
			}
			return sc & 0xFF;
		}

		int DIKToVirtualKey(std::uint32_t a_dik)
		{
			const UINT sc = (a_dik & 0x7F) | ((a_dik & 0x80) ? 0xE000u : 0u);
			return static_cast<int>(::MapVirtualKeyW(sc, MAPVK_VSC_TO_VK_EX));
		}

		std::atomic<std::uint32_t> g_keyDik[kKeyCount]{};

		void RefreshKeyCodes()
		{
			for (int i = 0; i < kKeyCount; ++i) {
				g_keyDik[i] = VirtualKeyToDIK(cfg.keyVk[i]);
			}
		}

		std::string ToUtf8(const std::wstring& a_s)
		{
			if (a_s.empty()) {
				return {};
			}
			const int   n = ::WideCharToMultiByte(CP_UTF8, 0, a_s.c_str(), static_cast<int>(a_s.size()), nullptr, 0, nullptr, nullptr);
			std::string out(static_cast<std::size_t>(n), '\0');
			::WideCharToMultiByte(CP_UTF8, 0, a_s.c_str(), static_cast<int>(a_s.size()), out.data(), n, nullptr, nullptr);
			return out;
		}

		std::string KeyName(int a_vk)
		{
			if (a_vk <= 0) {
				return "None";
			}
			const std::uint32_t dik = VirtualKeyToDIK(a_vk);
			wchar_t             name[64]{};
			const LONG          lparam = static_cast<LONG>(((dik & 0x7F) << 16) | ((dik & 0x80) ? (1 << 24) : 0));
			if (dik && ::GetKeyNameTextW(lparam, name, 64) > 0) {
				return ToUtf8(name);
			}
			return std::format("Key {}", a_vk);
		}

		// ---- INI
		float ReadFloat(const char* a_sec, const char* a_key, float a_def, float a_min, float a_max)
		{
			char buf[64]{};
			::GetPrivateProfileStringA(a_sec, a_key, std::format("{}", a_def).c_str(), buf, sizeof buf, g_iniPath.c_str());
			const float v = static_cast<float>(std::atof(buf));
			return std::isfinite(v) ? std::clamp(v, a_min, a_max) : a_def;
		}
		int ReadInt(const char* a_sec, const char* a_key, int a_def, int a_min, int a_max)
		{
			return std::clamp(static_cast<int>(::GetPrivateProfileIntA(a_sec, a_key, a_def, g_iniPath.c_str())), a_min, a_max);
		}
		bool ReadBool(const char* a_sec, const char* a_key, bool a_def)
		{
			return ::GetPrivateProfileIntA(a_sec, a_key, a_def ? 1 : 0, g_iniPath.c_str()) != 0;
		}
		void Write(const char* a_sec, const char* a_key, const std::string& a_v)
		{
			::WritePrivateProfileStringA(a_sec, a_key, a_v.c_str(), g_iniPath.c_str());
		}
		std::string B(bool a_v) { return a_v ? "1" : "0"; }
		std::string F(float a_v) { return std::format("{:.4f}", a_v); }

		void SaveAll(const Config& c)
		{
			for (int i = 0; i < kKeyCount; ++i) {
				Write(kKeySection[i], kKeyIni[i], std::to_string(c.keyVk[i]));
			}
			Write("General", "bCursorHold", B(c.cursorHold));
			Write("General", "bVisible", B(c.visible));
			Write("General", "fMouseSpeed", std::format("{:.2f}", c.mouseSpeed));
			Write("General", "fUIScale", std::format("{:.2f}", c.uiScale));
			Write("Widget", "fPosX", F(c.posX));
			Write("Widget", "fPosY", F(c.posY));
			Write("Widget", "fWidth", std::format("{:.0f}", c.w));
			Write("Widget", "fHeight", std::format("{:.0f}", c.h));
			Write("Widget", "iScale", std::to_string(c.scalePct));
			Write("Widget", "iOpacity", std::to_string(c.opacityPct));
			Write("Widget", "iCoverButtonsOpacity", std::to_string(c.controlsOpacityPct));
			Write("Look", "iColorMode", std::to_string(c.colorMode));
			Write("Look", "iCustomRed", std::to_string(c.customR));
			Write("Look", "iCustomGreen", std::to_string(c.customG));
			Write("Look", "iCustomBlue", std::to_string(c.customB));
			Write("Look", "bTintAlbumArt", B(c.tintArt));
			Write("Widget", "bShowTitleBar", B(c.showHeader));
			Write("Widget", "bShowAlbumArt", B(c.showArt));
			Write("Widget", "bShowProgress", B(c.showProgress));
			Write("Widget", "bShowSound", B(c.showSound));
			Write("Widget", "bLocked", B(c.locked));
			Write("Safety", "bLeaveCursorInCombat", B(c.leaveCursorInCombat));
			Write("Safety", "bNoCursorInCombat", B(c.noCursorInCombat));
			Write("Safety", "iDragDelayMs", std::to_string(c.dragDelayMs));
			Write("Safety", "iDragDistance", std::to_string(c.dragDistance));
			Write("Hiding", "bAutoHideInCombat", B(c.autoHideInCombat));
			Write("Hiding", "fShowAgainAfter", std::format("{:.1f}", c.showAgainAfter));
			Write("Hiding", "bHideInMenus", B(c.hideInMenus));
			Write("Hiding", "bHideWhenNoPlayer", B(c.hideWhenNoPlayer));
			Write("Sound", "bMuteGameMusic", B(c.muteGameMusic));
			Write("Sound", "bMuteRadio", B(c.muteRadio));
			Write("Sound", "iVolumeStep", std::to_string(c.volumeStep));
			Write("Sound", "sMusicApp", c.musicApp);
			Write("Sound", "bAnyPlayer", B(c.anyPlayer));
			Write("Debug", "bShowFps", B(c.showFps));
		}

		// Writing the INI opens and rewrites the file for every key (tens of milliseconds for all of them), so it
		// happens on a background thread from a copy of the settings - never inside the game's frame.
		void SaveAllAsync()
		{
			static std::mutex saving;  // one save at a time, in order
			std::thread([c = cfg]() {
				std::lock_guard lk(saving);
				SaveAll(c);
			}).detach();
		}

		Media::Settings MediaSettings()
		{
			Media::Settings s;
			s.targetApp = cfg.musicApp;
			s.anyPlayer = cfg.anyPlayer;
			return s;
		}

		// ================================================================ state
		struct InputMsg
		{
			enum class Kind
			{
				MouseMove,
				MouseButton,
				Wheel,
				Key,
				Char
			} kind;
			float    x = 0.0f, y = 0.0f;
			int      button = 0;
			ImGuiKey key = ImGuiKey_None;
			bool     down = false;
		};
		std::mutex            g_inputMtx;
		std::vector<InputMsg> g_input;

		std::atomic<bool> g_cursor{ false };      // cursor mode is on (applied in Present)
		std::atomic<bool> g_wantCursor{ false };  // requested by the input thread
		std::atomic<bool> g_toggleVisible{ false };
		std::atomic<bool> g_undoRequest{ false };
		std::atomic<bool> g_escRequest{ false };
		std::atomic<bool> g_settingsKey{ false };  // settings key pressed (applied in Present)
		bool              g_cursorForSettings = false;  // the cursor came up for the settings key: it goes away with them
		std::atomic<bool> g_shiftDown{ false }, g_ctrlDown{ false }, g_altDown{ false };
		std::atomic<int>  g_captureSlot{ -1 };       // settings: waiting for a key for this slot
		std::atomic<int>  g_capturedVk{ 0 };         // result (-1 cancelled, -2 cleared)
		std::atomic<bool> g_inputDisabled{ false };
		std::atomic<bool> g_gameLoaded{ false };
		std::atomic<bool> g_inCombatInput{ false };  // combat state as last seen by Present
		std::atomic<bool> g_pausedInput{ false };

		bool                 g_imguiReady = false, g_imguiFailed = false;
		IDXGISwapChain*      g_sc = nullptr;
		ID3D11Device*        g_dev = nullptr;
		ID3D11DeviceContext* g_ctx = nullptr;
		float                g_displayW = 1920.0f, g_displayH = 1080.0f;
		float                g_mouseX = 0.0f, g_mouseY = 0.0f;
		std::chrono::steady_clock::time_point g_lastFrame{};
		bool                 g_pendingFonts = false;
		bool                 g_settingsOpen = false;
		int                  g_settingsTab = 0;
		bool                 g_dirty = false;
		float                g_dirtyAt = 0.0f;
		float                g_time = 0.0f;
		float                g_alpha = 0.0f;   // whole widget fades in/out
		float                g_combatEndedAt = -100.0f;
		float                g_fpsAvg = 0.0f;   // game frames per second (smoothed)
		float                g_costMs = 0.0f;   // time Holosong spends in a frame (smoothed)
		bool                 g_wasInCombat = false;
		ImFont*              g_fText = nullptr;   // Inter Regular, widget text
		ImFont*              g_fSemi = nullptr;   // Inter SemiBold, song title
		ImFont*              g_fTitle = nullptr;  // Cinzel, "Now Playing"
		float                g_fontK = 1.0f;      // widget scale the fonts were made for
		ID3D11ShaderResourceView* g_art = nullptr;
		int                  g_artW = 0, g_artH = 0;
		std::uint64_t        g_artVersion = 0;
		float                g_volDrag = -1.0f;   // volume slider value while dragging (and a moment after)
		float                g_volDragUntil = 0.0f;
		bool                 g_inputHooked = false;  // DirectInput hook in place (keys, cursor, input blocker)
		std::string          g_status;            // short message under the settings footer
		char                 g_appBuf[64]{};

		struct Rect
		{
			float x, y, w, h;  // x, y: screen fractions; w, h: size at 1080p
		};
		std::vector<Rect> g_undo;

		// drag / resize
		enum class Grab
		{
			None,
			Move,
			N,
			S,
			E,
			W,
			NE,
			NW,
			SE,
			SW
		};
		struct DragState
		{
			Grab   mode = Grab::None;
			bool   active = false;  // passed the delay and the distance
			ImVec2 start{};
			float  startTime = 0.0f;
			ImVec2 pos0{}, size0{};  // panel in pixels when the press started
		} g_drag;

		void MarkDirty()
		{
			g_dirty = true;
			g_dirtyAt = g_time;
		}

		void PushInput(const InputMsg& a_m)
		{
			std::lock_guard lk(g_inputMtx);
			if (g_input.size() < 1024) {
				g_input.push_back(a_m);
			}
		}

		void Notify(std::string a_text)
		{
			if (auto* t = F4SE::GetTaskInterface()) {
				t->AddTask([a_text]() { RE::SendHUDMessage::ShowHUDMessage(a_text.c_str(), "", false, false); });
			}
		}

		// ================================================================ game state
		// Every read of the game's own data is fenced: if one ever fails on some game build, that one feature
		// switches off (and says so in the log) instead of the game crashing.
		std::atomic<bool> g_gameStateBroken{ false };

		template <class F>
		bool Fenced(F a_fn, const char* a_what)  // no C++ objects with destructors in here: __try needs that
		{
			if (g_gameStateBroken) {
				return false;
			}
			__try {
				return a_fn();
			} __except (SehFilter(GetExceptionInformation(), a_what)) {
				g_gameStateBroken = true;
				return false;
			}
		}

		bool MenuOpen(std::string_view a_name)
		{
			auto* ui = RE::UI::GetSingleton();
			return ui && ui->GetMenuOpen(RE::BSFixedString(a_name));
		}

		bool PlayerInCombatRaw()
		{
			auto* pc = RE::PlayerCharacter::GetSingleton();
			return pc && pc->IsInCombat();
		}

		// Paused, loading, or in a menu that owns the screen or the mouse (the Pip-Boy doesn't pause Fallout 4,
		// but it's still no place for the widget)
		bool GamePausedRaw()
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return true;
			}
			if (ui->GameIsPaused()) {
				return true;
			}
			static constexpr std::string_view menus[] = { "MainMenu", "LoadingMenu", "PauseMenu", "Console", "PipboyMenu", "TerminalMenu",
				"ContainerMenu", "BarterMenu", "ExamineMenu", "CookingMenu", "LooksMenu", "LockpickingMenu", "PowerArmorModMenu", "BookMenu",
				"SPECIALMenu", "WorkshopMenu", "LevelUpMenu", "VATSMenu", "MessageBoxMenu", "FavoritesMenu", "CreditsMenu" };
			for (const auto m : menus) {
				if (MenuOpen(m)) {
					return true;
				}
			}
			return false;
		}

		bool PlayerInCombat() { return Fenced([] { return PlayerInCombatRaw(); }, "PlayerInCombat"); }
		bool GamePaused() { return Fenced([] { return GamePausedRaw(); }, "GamePaused"); }
		bool ConsoleOpen() { return Fenced([] { return MenuOpen("Console"); }, "ConsoleOpen"); }
		bool PauseMenuOpen() { return Fenced([] { return MenuOpen("PauseMenu"); }, "PauseMenuOpen"); }

		// ---- Fallout 4's music and radio: turned down while the music app plays
		struct GameSound
		{
			RE::BGSSoundCategory* cat = nullptr;
			bool                  muted = false;
			float                 saved = 1.0f;
		};
		GameSound g_music, g_radio;

		std::string Lower(std::string_view a_s)
		{
			std::string out(a_s);
			std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		std::string CatName(RE::BGSSoundCategory* a_cat)
		{
			const char* n = static_cast<RE::TESFullName*>(a_cat)->GetFullName();
			return Lower(n ? n : "");
		}

		void FindSoundCategories()
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh) {
				return;
			}
			// The ones with a slider in Settings > Audio are named "Music" and "Radio" (any language: then by editor ID)
			for (auto* cat : dh->GetFormArray<RE::BGSSoundCategory>()) {
				if (!cat) {
					continue;
				}
				const std::string name = CatName(cat);
				const std::string eid = Lower(cat->GetFormEditorID() ? cat->GetFormEditorID() : "");
				spdlog::info("  sound category {:08X} \"{}\" {}", cat->GetFormID(), name, eid);
				if (!g_music.cat && (name == "music" || eid == "audiocategorymus")) {
					g_music.cat = cat;
				}
				if (!g_radio.cat && (name == "radio" || eid == "audiocategoryradio")) {
					g_radio.cat = cat;
				}
			}
			if (!g_music.cat) {  // a translated game: the first category with "mus" in its name
				for (auto* cat : dh->GetFormArray<RE::BGSSoundCategory>()) {
					if (cat && CatName(cat).find("mus") != std::string::npos) {
						g_music.cat = cat;
						break;
					}
				}
			}
			spdlog::info("Game music: {}, radio: {}", g_music.cat ? std::format("{:08X}", g_music.cat->GetFormID()) : "NOT FOUND",
				g_radio.cat ? std::format("{:08X}", g_radio.cat->GetFormID()) : "NOT FOUND");
		}

		void SetMuted(GameSound& a_s, bool a_mute)
		{
			if (a_mute == a_s.muted || !a_s.cat) {
				return;
			}
			a_s.muted = a_mute;
			auto* cat = static_cast<RE::BSISoundCategory*>(a_s.cat);
			auto* s = &a_s;
			if (auto* t = F4SE::GetTaskInterface()) {
				t->AddTask([cat, s, a_mute]() {
					if (a_mute) {
						s->saved = cat->GetCategoryVolume();
						cat->SetCategoryVolume(0.0f);
					} else {
						cat->SetCategoryVolume(s->saved);
					}
				});
			}
		}

		// ---- the game's own controls: off in cursor mode (an input layer, the way the game's menus do it)
		// This is the wall no other hook or mod can open: even a click that gets past every hook does nothing in the game.
		// on the heap and never freed: releasing it while the game shuts down would call into the game too late
		auto*             g_inputLayer = new RE::BSTSmartPointer<RE::BSInputEnableLayer>();

		// Three of the game's own ways, each one fenced on its own (one failing on some game build leaves the others):
		//  1. an input layer with every control off (looking, firing, activating, moving, menus, VATS...)
		//  2. the "cursor" input context, the one the game's own cursor menus switch to: the mouse stops being the camera
		//  3. the player-input block the game uses during scenes
		using UEF = RE::UserEvents::USER_EVENT_FLAG;
		constexpr std::int32_t kAllControls = 0xFFF;  // every flag one by one (kMovement ... kVATS), not the -1 shortcut

		void LayerRaw(bool a_off)
		{
			auto* mgr = RE::BSInputEnableManager::GetSingleton();
			if (!mgr) {
				return;
			}
			auto& layer = *g_inputLayer;
			if (!layer && !mgr->AllocateNewLayer(layer, "Holosong")) {
				return;
			}
			if (layer) {
				mgr->EnableUserEvent(layer->layerID, static_cast<UEF>(kAllControls), !a_off, RE::UserEvents::SENDER_ID::kMenu);
			}
		}

		bool g_contextPushed = false;
		void ContextRaw(bool a_off)
		{
			auto* cm = RE::ControlMap::GetSingleton();
			if (!cm) {
				return;
			}
			if (a_off && !g_contextPushed) {
				cm->PushInputContext(RE::UserEvents::INPUT_CONTEXT_ID::kCursor);
				g_contextPushed = true;
			} else if (!a_off && g_contextPushed) {
				cm->PopInputContext(RE::UserEvents::INPUT_CONTEXT_ID::kCursor);
				g_contextPushed = false;
			}
		}

		bool g_blockSet = false, g_blockWas = false;
		void BlockRaw(bool a_off)
		{
			auto* pc = RE::PlayerControls::GetSingleton();
			if (!pc) {
				return;
			}
			if (a_off && !g_blockSet) {
				g_blockWas = pc->blockPlayerInput;
				pc->blockPlayerInput = true;
				g_blockSet = true;
			} else if (!a_off && g_blockSet) {
				pc->blockPlayerInput = g_blockWas;
				g_blockSet = false;
			}
		}

		std::atomic<bool> g_lockBroken[3]{};
		bool SafeLock(int a_which, void (*a_fn)(bool), bool a_off, const char* a_what)  // no C++ objects here: __try needs that
		{
			if (g_lockBroken[a_which]) {
				return false;
			}
			__try {
				a_fn(a_off);
				return true;
			} __except (SehFilter(GetExceptionInformation(), a_what)) {
				g_lockBroken[a_which] = true;
				return false;
			}
		}

		bool SafeSetGameControls(bool a_off)
		{
			const bool layer = SafeLock(0, LayerRaw, a_off, "GameControls.Layer");
			const bool context = SafeLock(1, ContextRaw, a_off, "GameControls.Context");
			const bool block = SafeLock(2, BlockRaw, a_off, "GameControls.Block");
			spdlog::info("Game controls {}: input layer {}, cursor context {}, player input block {}", a_off ? "off (cursor mode)" : "back on",
				layer ? "ok" : "failed", context ? "ok" : "failed", block ? "ok" : "failed");
			return layer || context || block;
		}

		void SetGameControls(bool a_off)
		{
			if (auto* t = F4SE::GetTaskInterface()) {
				t->AddTask([a_off]() { SafeSetGameControls(a_off); });
			}
		}

		// ================================================================ cursor mode
		void ApplyCursor(bool a_on)
		{
			if (g_cursor == a_on) {
				return;
			}
			g_cursor = a_on;
			g_wantCursor = a_on;
			// The game (and every mod reading input through it) gets no presses, clicks or mouse movement - only releases
			Input::SetBlocking(a_on && dbg.blockInput);
			if (dbg.blockInput) {
				if (a_on) {
					Input::Rehook(2);  // ours in front, so holding input back is final
				}
				SetGameControls(a_on);
			}
			if (g_imguiReady) {
				ImGui::GetIO().MouseDrawCursor = a_on;
			}
			if (!a_on) {
				g_settingsOpen = false;
				g_cursorForSettings = false;
				g_drag = {};
				g_captureSlot = -1;
				std::lock_guard lk(g_inputMtx);
				g_input.clear();
			}
			spdlog::info("Cursor mode {}", a_on ? "on" : "off");
		}

		// Input thread: may the cursor come up right now?
		bool CursorAllowed(bool a_tellPlayer)
		{
			if (!g_gameLoaded || g_pausedInput) {
				return false;
			}
			if (cfg.noCursorInCombat && g_inCombatInput) {
				if (a_tellPlayer) {
					Notify("Holosong: the widget can't be moved during combat");
				}
				return false;
			}
			return true;
		}

		// ================================================================ input (game input events)
		wchar_t KeyToChar(std::uint32_t a_dik, bool a_shift, bool a_altGr)
		{
			const UINT sc = (a_dik & 0x7F) | ((a_dik & 0x80) ? 0xE000u : 0u);
			HKL        layout = ::GetKeyboardLayout(::GetWindowThreadProcessId(::GetForegroundWindow(), nullptr));
			const UINT vk = ::MapVirtualKeyExW(sc, MAPVK_VSC_TO_VK_EX, layout);
			if (vk == 0) {
				return 0;
			}
			BYTE state[256]{};
			if (a_shift) {
				state[VK_SHIFT] = state[VK_LSHIFT] = 0x80;
			}
			if (::GetKeyState(VK_CAPITAL) & 1) {
				state[VK_CAPITAL] = 0x01;
			}
			if (a_altGr) {
				state[VK_CONTROL] = state[VK_LCONTROL] = state[VK_MENU] = state[VK_RMENU] = 0x80;
			}
			wchar_t   buf[8]{};
			const int n = ::ToUnicodeEx(vk, sc, state, buf, 8, 0x4, layout);
			return (n == 1 && buf[0] >= 0x20 && buf[0] != 0x7F) ? buf[0] : 0;
		}

		ImGuiKey KeyToImGui(std::uint32_t a_dik)
		{
			switch (a_dik) {
			case 0x01: return ImGuiKey_Escape;
			case 0xCB: return ImGuiKey_LeftArrow;
			case 0xCD: return ImGuiKey_RightArrow;
			case 0xC8: return ImGuiKey_UpArrow;
			case 0xD0: return ImGuiKey_DownArrow;
			case 0x0E: return ImGuiKey_Backspace;
			case 0xD3: return ImGuiKey_Delete;
			case 0x1C: return ImGuiKey_Enter;
			case 0x9C: return ImGuiKey_KeypadEnter;
			case 0xC7: return ImGuiKey_Home;
			case 0xCF: return ImGuiKey_End;
			case 0x0F: return ImGuiKey_Tab;
			case 0x1E: return ImGuiKey_A;
			case 0x2E: return ImGuiKey_C;
			case 0x2F: return ImGuiKey_V;
			case 0x2D: return ImGuiKey_X;
			case 0x2C: return ImGuiKey_Z;
			default: return ImGuiKey_None;
			}
		}

		bool TextBoxFocused()
		{
			return g_imguiReady && g_cursor && ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput;
		}

		void HandleInput(const Input::Event& a_e)
		{
			if (dbg.logInput && a_e.kind != Input::Kind::MouseMove) {
				spdlog::info("input: kind {} code {:X} down {} y {}", static_cast<int>(a_e.kind), a_e.code, a_e.down, a_e.y);
			}
			do {
				if (a_e.kind == Input::Kind::MouseMove) {
					if (g_cursor) {
						PushInput({ InputMsg::Kind::MouseMove, a_e.x, a_e.y });
					}
					continue;
				}
				if (a_e.kind == Input::Kind::Wheel) {
					if (g_cursor && a_e.y != 0.0f) {
						InputMsg m{ InputMsg::Kind::Wheel };
						m.y = a_e.y;
						PushInput(m);
					}
					continue;
				}
				const auto code = a_e.code;
				const bool down = a_e.down;
				const bool up = !a_e.down;
				const bool keyboard = a_e.kind == Input::Kind::Key;
				if (keyboard) {
					if (code == 0x2A || code == 0x36) {
						g_shiftDown = down;
						if (g_cursor) {
							InputMsg m{ InputMsg::Kind::Key };
							m.key = ImGuiMod_Shift;
							m.down = down;
							PushInput(m);
						}
					} else if (code == 0x1D || code == 0x9D) {
						g_ctrlDown = down;
						if (g_cursor) {
							InputMsg m{ InputMsg::Kind::Key };
							m.key = ImGuiMod_Ctrl;
							m.down = down;
							PushInput(m);
						}
					} else if (code == 0x38 || code == 0xB8) {
						g_altDown = down;
					}
				}

				// Settings: "press a key" for one of the keys
				if (keyboard && down && g_captureSlot >= 0) {
					constexpr std::uint32_t unsafe[] = { 0x0F, 0x38, 0xB8, 0x1D, 0x9D, 0x2A, 0x36, 0xDB, 0xDC, 0x1C, 0x9C };
					if (code == 0x01) {
						g_capturedVk = -1;  // Esc: keep the old key
					} else if (code == 0x0E || code == 0xD3) {
						g_capturedVk = -2;  // Backspace / Delete: no key
					} else if (std::find(std::begin(unsafe), std::end(unsafe), code) == std::end(unsafe)) {
						const int vk = DIKToVirtualKey(code);
						g_capturedVk = vk > 0 ? vk : -1;
					} else {
						continue;
					}
					continue;
				}

				if (keyboard) {
					const bool typing = TextBoxFocused();
					// cursor mode key (works while hidden too: it brings the widget back)
					if (code == g_keyDik[kKeyCursor] && !typing && code != 0) {
						if (cfg.cursorHold) {
							if (down && !g_wantCursor && CursorAllowed(true)) {
								g_wantCursor = true;
							} else if (up) {
								g_wantCursor = false;
							}
						} else if (down) {
							if (g_wantCursor) {
								g_wantCursor = false;
							} else if (CursorAllowed(true)) {
								g_wantCursor = true;
							}
						}
						continue;
					}
					if (down && !typing && !ConsoleOpen()) {
						if (code == g_keyDik[kKeySettings] && code != 0) {
							if (g_cursor) {
								g_settingsKey = true;  // open or close the settings window
							} else if (CursorAllowed(true)) {
								g_wantCursor = true;
								g_settingsKey = true;
							}
							continue;
						}
						if (code == g_keyDik[kKeyToggle] && code != 0 && !g_pausedInput) {
							g_toggleVisible = true;
							continue;
						}
						if (code == g_keyDik[kKeyPlayPause] && code != 0) {
							Media::PlayPause();
							continue;
						}
						if (code == g_keyDik[kKeyNext] && code != 0) {
							Media::Next();
							continue;
						}
						if (code == g_keyDik[kKeyPrevious] && code != 0) {
							Media::Previous();
							continue;
						}
						if (code == g_keyDik[kKeyVolUp] && code != 0) {
							Media::ChangeVolume(cfg.volumeStep / 100.0f);
							continue;
						}
						if (code == g_keyDik[kKeyVolDown] && code != 0) {
							Media::ChangeVolume(-cfg.volumeStep / 100.0f);
							continue;
						}
					}
					if (!g_cursor) {
						continue;
					}
					if (down && code == 0x01 && !typing) {
						g_escRequest = true;  // Esc: close settings, then leave cursor mode
						continue;
					}
					if (down && g_ctrlDown && code == g_keyDik[kKeyUndo] && code != 0 && !typing) {
						g_undoRequest = true;
						continue;
					}
					if (const ImGuiKey key = KeyToImGui(code); key != ImGuiKey_None) {
						InputMsg m{ InputMsg::Kind::Key };
						m.key = key;
						m.down = down;
						PushInput(m);
					}
					const bool altGr = g_ctrlDown && g_altDown;
					if (down && (!g_ctrlDown || altGr)) {
						if (const wchar_t c = KeyToChar(code, g_shiftDown, altGr); c != 0) {
							InputMsg m{ InputMsg::Kind::Char };
							m.button = static_cast<int>(c);
							PushInput(m);
						}
					}
				} else if (a_e.kind == Input::Kind::MouseButton && g_cursor) {
					if (code <= 4) {  // left, right, middle, X1, X2 (the wheel is handled above)
						InputMsg m{ InputMsg::Kind::MouseButton };
						m.button = static_cast<int>(code);
						m.down = down;
						PushInput(m);
					}
				}
			} while (false);
		}

		void SafeHandleInput(const Input::Event& a_event)
		{
			__try {
				HandleInput(a_event);
			} __except (SehFilter(GetExceptionInformation(), "HandleInput")) {
				g_inputDisabled = true;
			}
		}

		// DirectInput calls this on the game's input thread for every key / mouse event (Input.cpp)
		// Keys come from up to three places (the keyboard hook, the game's own input events, Windows' key state) - each
		// press and release counts once, whichever reports it first.
		std::atomic<bool> g_keyState[256]{};

		void OnInputEvent(const Input::Event& a_e)
		{
			if (a_e.kind == Input::Kind::Key) {
				if (a_e.code >= 256 || g_keyState[a_e.code].exchange(a_e.down) == a_e.down) {
					return;  // another source already reported this one
				}
			}
			static std::atomic<int> logged{ 0 };
			if (a_e.kind == Input::Kind::Key && a_e.down && (logged < 12 || dbg.logInput)) {
				++logged;
				spdlog::info("Key pressed: {} (code {:02X}){}", KeyName(DIKToVirtualKey(a_e.code)), a_e.code,
					a_e.code == g_keyDik[kKeyCursor] ? " = cursor mode key" : a_e.code == g_keyDik[kKeyToggle] ? " = show/hide key" :
					a_e.code == g_keyDik[kKeySettings] && a_e.code ? " = settings key" : "");
			}
			if (!g_inputDisabled) {
				SafeHandleInput(a_e);
			}
		}

		// ---- one more source for the keys: Windows' key state, read every frame while Fallout 4 is in front
		// (if something in front of our keyboard hook keeps the keys from it, the key state still changes).
		// Always on: earlier it stopped for good once the hook saw a single key, and after an Alt+Tab that left no
		// source at all.
		bool g_polledDown[256]{};
		bool g_pollingUsed = false;
		bool g_wasInFront = true;

		bool GameWindowInFront()
		{
			HWND  w = ::GetForegroundWindow();
			DWORD pid = 0;
			::GetWindowThreadProcessId(w, &pid);
			return w && pid == ::GetCurrentProcessId();
		}

		void PollKeys()
		{
			const bool front = GameWindowInFront();
			if (!front && g_wasInFront) {
				// Alt+Tab, the Steam overlay...: keys let go while another window had them never reach us - let them
				// all go now, so nothing stays "held" (and the next press of a hotkey counts)
				for (std::uint32_t k = 0; k < 256; ++k) {
					if (g_keyState[k]) {
						OnInputEvent({ Input::Kind::Key, k, false });
					}
				}
				spdlog::info("Fallout 4 lost focus: all keys let go");
			} else if (front && !g_wasInFront) {
				spdlog::info("Fallout 4 has focus again");
			}
			g_wasInFront = front;
			// Ctrl / Shift / Alt that Windows says are up but we still have down (a release that got lost): let go.
			// A stuck Ctrl turned a click on a settings slider into a Ctrl+click and the mouse wheel into "scale".
			if (front && g_gameLoaded) {
				static int stale[6]{};
				const int  mods[6] = { VK_LSHIFT, VK_RSHIFT, VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU };
				for (int i = 0; i < 6; ++i) {
					const std::uint32_t dik = VirtualKeyToDIK(mods[i]);
					const bool          up = (::GetAsyncKeyState(mods[i]) & 0x8000) == 0;
					stale[i] = (dik && dik < 256 && g_keyState[dik] && up) ? stale[i] + 1 : 0;
					if (stale[i] >= 3) {  // three frames in a row
						stale[i] = 0;
						OnInputEvent({ Input::Kind::Key, dik, false });
					}
				}
			}
			for (int vk = 0x08; vk < 0xFF; ++vk) {
				if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || (vk >= VK_LBUTTON && vk <= VK_XBUTTON2)) {
					continue;  // the left/right versions are read instead; mouse buttons come from the mouse hook
				}
				const bool down = front && (::GetAsyncKeyState(vk) & 0x8000) != 0;
				if (down == g_polledDown[vk]) {
					continue;
				}
				g_polledDown[vk] = down;
				if (!g_gameLoaded) {
					continue;
				}
				const std::uint32_t dik = VirtualKeyToDIK(vk);
				if (!dik || dik >= 256) {
					continue;
				}
				if (!g_pollingUsed && down && !g_keyState[dik]) {
					g_pollingUsed = true;
					spdlog::info("Keys: the key state got a key before the hooks did (something in front of the keyboard hook keeps them) - it covers for them");
				}
				OnInputEvent({ Input::Kind::Key, dik, down });
			}
		}

		// ---- the game's own input events: the keys as Fallout 4 itself sees them (after DirectInput / raw input, untouched by
		// overlays and focus changes). A listener among the game's menu input handlers.
		class GameKeys final : public RE::BSInputEventUser
		{
		public:
			bool ShouldHandleEvent(const RE::InputEvent* a_e) override
			{
				return a_e && a_e->device.get() == RE::INPUT_DEVICE::kKeyboard;
			}
			void OnButtonEvent(const RE::ButtonEvent* a_e) override
			{
				if (!a_e || a_e->device.get() != RE::INPUT_DEVICE::kKeyboard || a_e->idCode <= 0 || a_e->idCode >= 256) {
					return;
				}
				// Fallout 4's keyboard events carry Windows virtual-key codes (not scan codes like Skyrim's): Page Up is
				// 0x21, which read as a scan code is "F" - convert
				int vk = a_e->idCode;
				if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) {
					return;  // which side isn't known: the other sources report Left/Right
				}
				const std::uint32_t dik = VirtualKeyToDIK(vk);
				if (!dik || dik >= 256) {
					return;
				}
				if (a_e->QJustPressed()) {
					OnInputEvent({ Input::Kind::Key, dik, true });
				} else if (a_e->QReleased()) {
					OnInputEvent({ Input::Kind::Key, dik, false });
				}
			}
		};
		GameKeys* g_gameKeys = nullptr;

		bool RegisterGameKeysRaw()
		{
			auto* mc = RE::MenuControls::GetSingleton();
			if (!mc) {
				return false;
			}
			if (!g_gameKeys) {
				g_gameKeys = new GameKeys();  // lives as long as the game
			}
			mc->RegisterHandler(g_gameKeys);
			return true;
		}

		bool SafeRegisterGameKeys()  // no C++ objects here: __try needs that
		{
			__try {
				return RegisterGameKeysRaw();
			} __except (SehFilter(GetExceptionInformation(), "RegisterGameKeys")) {
				return false;
			}
		}

		// Ctrl + wheel over the widget: its scale
		bool  g_wheelScale = false;
		float g_wheelFrame = 0.0f;  // wheel ticks this frame (+ up / - down), used by the settings panel and the volume slider

		void DrainInput()
		{
			std::vector<InputMsg> msgs;
			{
				std::lock_guard lk(g_inputMtx);
				msgs.swap(g_input);
			}
			auto& io = ImGui::GetIO();
			g_wheelFrame = 0.0f;
			for (const auto& m : msgs) {
				switch (m.kind) {
				case InputMsg::Kind::MouseMove:
					g_mouseX = std::clamp(g_mouseX + m.x * cfg.mouseSpeed, 0.0f, io.DisplaySize.x - 1.0f);
					g_mouseY = std::clamp(g_mouseY + m.y * cfg.mouseSpeed, 0.0f, io.DisplaySize.y - 1.0f);
					io.AddMousePosEvent(g_mouseX, g_mouseY);
					break;
				case InputMsg::Kind::MouseButton:
					io.AddMouseButtonEvent(m.button, m.down);
					break;
				case InputMsg::Kind::Wheel:
					if (g_ctrlDown && g_wheelScale) {
						if (!cfg.locked) {
							cfg.scalePct = std::clamp(cfg.scalePct + (m.y > 0.0f ? 5 : -5), 50, 100);
							MarkDirty();
							g_pendingFonts = true;
						}
					} else {
						g_wheelFrame += m.y;  // scrolling is done by hand where it's needed (not left to ImGui's window routing)
					}
					break;
				case InputMsg::Kind::Key:
					io.AddKeyEvent(m.key, m.down);
					break;
				case InputMsg::Kind::Char:
					io.AddInputCharacterUTF16(static_cast<ImWchar16>(m.button));
					break;
				}
			}
			// Ctrl / Shift as they really are now (a lost release must never leave ImGui thinking they're held)
			io.AddKeyEvent(ImGuiMod_Ctrl, g_ctrlDown.load());
			io.AddKeyEvent(ImGuiMod_Shift, g_shiftDown.load());
		}

		// ================================================================ drawing helpers
		float g_k = 1.0f;  // widget pixels per 1080p unit (screen size x widget scale)
		float U(float a_v) { return a_v * g_k; }

		float g_fade = 1.0f;  // extra opacity while drawing the album cover and the controls (FadeControls)

		ImU32 Col(int r, int g, int b, int a = 255)
		{
			return IM_COL32(r, g, b, static_cast<int>(a * std::clamp(g_alpha, 0.0f, 1.0f) * g_fade));
		}

		// While one of these is alive, everything drawn uses the "Album cover and buttons" opacity. Text is drawn
		// outside it, so it stays solid.
		struct FadeControls
		{
			float prev;
			FadeControls() :
				prev(g_fade) { g_fade = std::clamp(cfg.controlsOpacityPct, 10, 100) / 100.0f; }
			~FadeControls() { g_fade = prev; }
			FadeControls(const FadeControls&) = delete;
			FadeControls& operator=(const FadeControls&) = delete;
		};
		ImU32 kText() { return Col(238, 234, 226); }
		ImU32 kDim() { return Col(168, 164, 156); }
		ImU32 kFaint() { return Col(138, 134, 127); }
		ImU32 kGold() { return Col(218, 184, 124); }
		ImU32 kGoldDim() { return Col(150, 126, 88); }

		// ================================================================ colour (Pip-Boy look)
		// Everything is drawn as usual, then every colour on screen is mapped by its brightness onto one colour, like
		// the Pip-Boy's monochrome screen: dark stays dark, light becomes the full colour (Recolour, after Render).
		bool  PipBoy() { return cfg.colorMode != 0; }
		float Corner(float a_r) { return PipBoy() ? 0.0f : a_r; }  // square corners in the Pip-Boy look

		float       g_tint[3] = { 20.0f / 255.0f, 1.0f, 23.0f / 255.0f };  // the colour in use (0-1)
		std::string g_tintSource = "default green";
		float       g_tintCheckAt = -100.0f;

		struct KeepColour
		{
			ImDrawList* dl;
			int         from, to;
		};
		std::vector<KeepColour> g_keepColour;  // vertices left in their own colours (the album cover, if not tinted)

		// The game's own settings, live: Settings > Display > HUD Color / Pip-Boy Color
		RE::Setting*      g_hudSet[3]{};
		RE::Setting*      g_pipSet[3]{};
		bool              g_colourSearched = false;
		std::atomic<bool> g_liveColourBroken{ false };

		void FindColourSettingsRaw()
		{
			static constexpr const char* hud[3] = { "iHUDColorR:Interface", "iHUDColorG:Interface", "iHUDColorB:Interface" };
			static constexpr const char* pip[3] = { "fPipboyEffectColorR:Pipboy", "fPipboyEffectColorG:Pipboy", "fPipboyEffectColorB:Pipboy" };
			for (int i = 0; i < 3; ++i) {
				g_hudSet[i] = RE::GetINISetting(hud[i]);
				g_pipSet[i] = RE::GetINISetting(pip[i]);
			}
		}

		bool ReadLiveColourRaw(int a_mode, float* a_out)
		{
			if (a_mode == 1) {
				if (!g_hudSet[0] || !g_hudSet[1] || !g_hudSet[2]) {
					return false;
				}
				for (int i = 0; i < 3; ++i) {
					a_out[i] = std::clamp(g_hudSet[i]->GetInt() / 255.0f, 0.0f, 1.0f);
				}
				return true;
			}
			if (!g_pipSet[0] || !g_pipSet[1] || !g_pipSet[2]) {
				return false;
			}
			for (int i = 0; i < 3; ++i) {
				const float v = g_pipSet[i]->GetFloat();
				a_out[i] = std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f;
			}
			return true;
		}

		bool SafeReadLiveColour(int a_mode, float* a_out)  // no C++ objects here: __try needs that
		{
			if (g_liveColourBroken) {
				return false;
			}
			__try {
				if (!g_colourSearched) {
					g_colourSearched = true;
					FindColourSettingsRaw();
				}
				return ReadLiveColourRaw(a_mode, a_out);
			} __except (SehFilter(GetExceptionInformation(), "ReadGameColour")) {
				g_liveColourBroken = true;
				return false;
			}
		}

		// Fallback: the same settings as saved in Documents\My Games\Fallout4\Fallout4Prefs.ini (read at start)
		bool  g_fileHudOk = false, g_filePipOk = false;
		float g_fileHud[3]{}, g_filePip[3]{};

		void ReadPrefsFile()
		{
			PWSTR docs = nullptr;
			if (FAILED(::SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) || !docs) {
				return;
			}
			const fs::path ini = fs::path(docs) / L"My Games" / L"Fallout4" / L"Fallout4Prefs.ini";
			::CoTaskMemFree(docs);
			std::error_code ec;
			if (!fs::exists(ini, ec)) {
				spdlog::info("Fallout4Prefs.ini not found - the game's colours come from the game itself only");
				return;
			}
			const std::string path = ini.string();
			const char*       hud[3] = { "iHUDColorR", "iHUDColorG", "iHUDColorB" };
			const char*       pip[3] = { "fPipboyEffectColorR", "fPipboyEffectColorG", "fPipboyEffectColorB" };
			g_fileHudOk = g_filePipOk = true;
			for (int i = 0; i < 3; ++i) {
				const int v = static_cast<int>(::GetPrivateProfileIntA("Interface", hud[i], -1, path.c_str()));
				g_fileHudOk = g_fileHudOk && v >= 0;
				g_fileHud[i] = std::clamp(v / 255.0f, 0.0f, 1.0f);
				char buf[32]{};
				::GetPrivateProfileStringA("Pipboy", pip[i], "", buf, sizeof buf, path.c_str());
				const float f = buf[0] ? static_cast<float>(std::atof(buf)) : -1.0f;
				g_filePipOk = g_filePipOk && f >= 0.0f;
				g_filePip[i] = std::clamp(f, 0.0f, 1.0f);
			}
		}

		// Which colour to draw in (twice a second: a change in the game's Display settings shows right away)
		void UpdateTint()
		{
			if (!PipBoy() || g_time - g_tintCheckAt < 0.5f) {
				return;
			}
			g_tintCheckAt = g_time;
			float       c[3] = { 20.0f / 255.0f, 1.0f, 23.0f / 255.0f };
			std::string src = "default green";
			if (cfg.colorMode == 3) {
				c[0] = cfg.customR / 255.0f, c[1] = cfg.customG / 255.0f, c[2] = cfg.customB / 255.0f;
				src = "custom";
			} else if (g_gameLoaded && SafeReadLiveColour(cfg.colorMode, c)) {
				src = cfg.colorMode == 1 ? "the game's HUD colour" : "the game's Pip-Boy colour";
			} else if (cfg.colorMode == 1 && g_fileHudOk) {
				std::copy(std::begin(g_fileHud), std::end(g_fileHud), c);
				src = "HUD colour from Fallout4Prefs.ini";
			} else if (cfg.colorMode == 2 && g_filePipOk) {
				std::copy(std::begin(g_filePip), std::end(g_filePip), c);
				src = "Pip-Boy colour from Fallout4Prefs.ini";
			}
			// always full brightness, so a dark colour choice can't make the widget unreadable
			const float mx = (std::max)({ c[0], c[1], c[2] });
			if (mx < 0.02f) {
				c[0] = 20.0f / 255.0f, c[1] = 1.0f, c[2] = 23.0f / 255.0f;
				src += " (black - default green used)";
			} else {
				for (float& v : c) {
					v /= mx;
				}
			}
			const bool changed = std::fabs(c[0] - g_tint[0]) + std::fabs(c[1] - g_tint[1]) + std::fabs(c[2] - g_tint[2]) > 0.01f || src != g_tintSource;
			std::copy(std::begin(c), std::end(c), g_tint);
			if (changed) {
				g_tintSource = src;
				spdlog::info("Colour: {} ({:.0f}, {:.0f}, {:.0f})", src, c[0] * 255.0f, c[1] * 255.0f, c[2] * 255.0f);
			}
		}

		// After ImGui::Render: every vertex colour -> the same brightness of the one colour
		void Recolour(ImDrawData* a_dd)
		{
			if (!PipBoy() || !a_dd) {
				return;
			}
			float top[3], base[3];
			for (int i = 0; i < 3; ++i) {
				top[i] = g_tint[i] * 255.0f;
				base[i] = top[i] * 0.025f;  // black becomes a very dark shade of the colour
			}
			for (int n = 0; n < a_dd->CmdListsCount; ++n) {
				ImDrawList* dl = a_dd->CmdLists[n];
				for (int i = 0; i < dl->VtxBuffer.Size; ++i) {
					bool keep = false;
					for (const auto& k : g_keepColour) {
						if (k.dl == dl && i >= k.from && i < k.to) {
							keep = true;
							break;
						}
					}
					if (keep) {
						continue;
					}
					ImU32&      col = dl->VtxBuffer[i].col;
					const float r = static_cast<float>((col >> IM_COL32_R_SHIFT) & 0xFF), g = static_cast<float>((col >> IM_COL32_G_SHIFT) & 0xFF),
								b = static_cast<float>((col >> IM_COL32_B_SHIFT) & 0xFF);
					const float t = (std::min)(1.0f, (0.299f * r + 0.587f * g + 0.114f * b) / 255.0f * 1.15f);
					const auto  ch = [&](int c) { return static_cast<ImU32>(base[c] + (top[c] - base[c]) * t + 0.5f); };
					col = (col & IM_COL32_A_MASK) | (ch(0) << IM_COL32_R_SHIFT) | (ch(1) << IM_COL32_G_SHIFT) | (ch(2) << IM_COL32_B_SHIFT);
				}
			}
		}

		float Ease(ImGuiID a_id, bool a_on, float a_speed = 14.0f)
		{
			float*      v = ImGui::GetStateStorage()->GetFloatRef(a_id, 0.0f);
			const float k = 1.0f - std::exp(-a_speed * ImGui::GetIO().DeltaTime);
			*v += ((a_on ? 1.0f : 0.0f) - *v) * k;
			return *v;
		}

		ImFont* FText() { return g_fText ? g_fText : Theme::Body(); }
		ImFont* FSemi() { return g_fSemi ? g_fSemi : Theme::Label(); }
		ImFont* FTitle() { return g_fTitle ? g_fTitle : Theme::Bold(); }

		float TextW(ImFont* a_f, float a_size, const char* a_t)
		{
			return a_f->CalcTextSizeA(a_size, FLT_MAX, 0.0f, a_t).x;
		}

		// One line of text, shortened with "..." to fit a_maxW. a_align: 0 left, 0.5 centred.
		void Line(ImDrawList* a_dl, ImFont* a_f, float a_size, ImVec2 a_p, ImU32 a_col, const std::string& a_t, float a_maxW, float a_align = 0.0f)
		{
			std::string t = a_t;
			if (a_maxW > 0.0f && TextW(a_f, a_size, t.c_str()) > a_maxW) {
				const float dots = TextW(a_f, a_size, "...");
				while (!t.empty() && TextW(a_f, a_size, t.c_str()) + dots > a_maxW) {
					while (!t.empty()) {  // drop one whole UTF-8 character
						const auto c = static_cast<unsigned char>(t.back());
						t.pop_back();
						if ((c & 0xC0) != 0x80) {
							break;
						}
					}
				}
				while (!t.empty() && t.back() == ' ') {
					t.pop_back();
				}
				t += "...";
			}
			if (a_align > 0.0f) {
				a_p.x += (a_maxW - TextW(a_f, a_size, t.c_str())) * a_align;
			}
			a_dl->AddText(a_f, a_size, a_p, a_col, t.c_str());
		}

		// Soft drop shadow: 1 px rings around the shape (never under it) fading out smoothly, a little lower than
		// the shape. (10 stacked filled shapes showed as light/dark steps and darkened the see-through panel.)
		void SoftShadow(ImDrawList* a_dl, ImVec2 a_a, ImVec2 a_b, float a_r, float a_spread, int a_alpha)
		{
			const int    n = (std::max)(2, static_cast<int>(a_spread + 0.5f));
			const ImVec2 drop(0.0f, a_spread * 0.18f);
			for (int i = 1; i <= n; ++i) {
				const float t = (i - 0.5f) / n;
				const int   al = static_cast<int>(a_alpha * 0.5f * std::pow(1.0f - t, 2.2f));
				if (al <= 0) {
					continue;
				}
				const float  g = i - 0.5f;
				const ImVec2 o = drop * t;
				a_dl->AddRect(a_a + o - ImVec2(g, g), a_b + o + ImVec2(g, g), Col(0, 0, 0, al), a_r > 0.0f ? a_r + g : 0.0f, 0, 1.5f);
			}
		}

		// Vertical gradients on any shape, rounded corners included: draw it, then shade the vertices it added.
		void FadeVertsY(ImDrawList* a_dl, int a_first, float a_y0, float a_y1, float a_top, float a_bottom)
		{
			const float h = (std::max)(1.0f, a_y1 - a_y0);
			for (int i = a_first; i < a_dl->VtxBuffer.Size; ++i) {
				ImDrawVert& v = a_dl->VtxBuffer[i];
				const float t = std::clamp((v.pos.y - a_y0) / h, 0.0f, 1.0f);
				const float m = a_top + (a_bottom - a_top) * t;
				const ImU32 al = (v.col >> IM_COL32_A_SHIFT) & 0xFF;
				v.col = (v.col & ~IM_COL32_A_MASK) | (static_cast<ImU32>(al * m + 0.5f) << IM_COL32_A_SHIFT);
			}
		}
		void TintVertsY(ImDrawList* a_dl, int a_first, float a_y0, float a_y1, ImU32 a_top, ImU32 a_bottom)
		{
			const float h = (std::max)(1.0f, a_y1 - a_y0);
			for (int i = a_first; i < a_dl->VtxBuffer.Size; ++i) {
				ImDrawVert& v = a_dl->VtxBuffer[i];
				const float t = std::clamp((v.pos.y - a_y0) / h, 0.0f, 1.0f);
				ImU32       c = 0;
				for (int sh = 0; sh < 32; sh += 8) {
					const float x0 = static_cast<float>((a_top >> sh) & 0xFF), x1 = static_cast<float>((a_bottom >> sh) & 0xFF);
					float       x = x0 + (x1 - x0) * t;
					if (sh == IM_COL32_A_SHIFT) {
						x *= static_cast<float>((v.col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;  // keep the soft anti-aliased edge
					}
					c |= static_cast<ImU32>(x + 0.5f) << sh;
				}
				v.col = c;
			}
		}
		// Highlight (or shade) across the top of a rounded shape: same rounded corners, fades to nothing by a_h.
		void TopGlow(ImDrawList* a_dl, ImVec2 a_a, ImVec2 a_b, float a_r, float a_h, ImU32 a_col)
		{
			const float h = (std::min)(a_h, a_b.y - a_a.y);
			const int   first = a_dl->VtxBuffer.Size;
			a_dl->AddRectFilled(a_a, ImVec2(a_b.x, a_a.y + h), a_col, a_r, ImDrawFlags_RoundCornersTop);
			FadeVertsY(a_dl, first, a_a.y, a_a.y + h, 1.0f, 0.0f);
		}
		// 1 px outline on the pixel grid (not between two pixels, which blurs it to a soft 2 px line)
		void CrispRect(ImDrawList* a_dl, ImVec2 a_a, ImVec2 a_b, ImU32 a_col, float a_r)
		{
			a_dl->AddRect(ImVec2(std::floor(a_a.x) + 0.5f, std::floor(a_a.y) + 0.5f), ImVec2(std::ceil(a_b.x) - 0.5f, std::ceil(a_b.y) - 0.5f), a_col, a_r, 0, 1.0f);
		}

		// Smoked-glass panel of Hearthframe's Modern look
		void Panel(ImDrawList* a_dl, ImVec2 a_a, ImVec2 a_b, bool a_cursor)
		{
			const float r = Corner(U(12.0f));
			const int   op = std::clamp(cfg.opacityPct, 30, 100);
			a_dl->AddRectFilled(a_a, a_b, Col(13, 13, 15, static_cast<int>(255 * op / 100.0f)), r);
			// glass sheen across the full width with the panel's own rounded corners (no hard side edges)
			TopGlow(a_dl, a_a + ImVec2(1.0f, 1.0f), a_b - ImVec2(1.0f, 1.0f), (std::max)(0.0f, r - 1.0f), U(120.0f), Col(255, 255, 255, 10));
			CrispRect(a_dl, a_a, a_b, PipBoy() ? Col(218, 184, 124, a_cursor ? 220 : 150) : a_cursor ? Col(218, 184, 124, 120) : Col(255, 255, 255, 26), r);
			// warm accent line along the top edge, brightest in the middle
			const float mid = (a_a.x + a_b.x) * 0.5f, half = (a_b.x - a_a.x) * 0.32f;
			const ImU32 acc = Col(218, 184, 124, 200), clear = Col(218, 184, 124, 0);
			a_dl->AddRectFilledMultiColor(ImVec2(mid - half, a_a.y), ImVec2(mid, a_a.y + U(1.5f)), clear, acc, acc, clear);
			a_dl->AddRectFilledMultiColor(ImVec2(mid, a_a.y), ImVec2(mid + half, a_a.y + U(1.5f)), acc, clear, clear, acc);
		}

		void HairlineFade(ImDrawList* a_dl, float a_x0, float a_x1, float a_y)
		{
			const float mid = (a_x0 + a_x1) * 0.5f;
			a_dl->AddRectFilledMultiColor(ImVec2(a_x0, a_y), ImVec2(mid, a_y + 1.0f), Col(255, 255, 255, 30), Col(255, 255, 255, 18), Col(255, 255, 255, 18), Col(255, 255, 255, 30));
			a_dl->AddRectFilledMultiColor(ImVec2(mid, a_y), ImVec2(a_x1, a_y + 1.0f), Col(255, 255, 255, 18), Col(255, 255, 255, 4), Col(255, 255, 255, 4), Col(255, 255, 255, 18));
		}

		// ---- icons (centre, size of the icon box, colour)
		using IconFn = void (*)(ImDrawList*, ImVec2, float, ImU32);

		void IconNone(ImDrawList*, ImVec2, float, ImU32) {}
		void IconPlay(ImDrawList* dl, ImVec2 c, float s, ImU32 col)
		{
			dl->AddTriangleFilled(c + ImVec2(-s * 0.32f, -s * 0.42f), c + ImVec2(s * 0.45f, 0), c + ImVec2(-s * 0.32f, s * 0.42f), col);
		}
		void IconPause(ImDrawList* dl, ImVec2 c, float s, ImU32 col)
		{
			dl->AddRectFilled(c + ImVec2(-s * 0.36f, -s * 0.4f), c + ImVec2(-s * 0.1f, s * 0.4f), col, s * 0.06f);
			dl->AddRectFilled(c + ImVec2(s * 0.1f, -s * 0.4f), c + ImVec2(s * 0.36f, s * 0.4f), col, s * 0.06f);
		}
		void IconPrev(ImDrawList* dl, ImVec2 c, float s, ImU32 col)
		{
			const float t = (std::max)(1.0f, s * 0.1f);
			dl->AddLine(c + ImVec2(-s * 0.36f, -s * 0.36f), c + ImVec2(-s * 0.36f, s * 0.36f), col, t);
			dl->AddTriangle(c + ImVec2(s * 0.38f, -s * 0.36f), c + ImVec2(-s * 0.2f, 0), c + ImVec2(s * 0.38f, s * 0.36f), col, t);
		}
		void IconNext(ImDrawList* dl, ImVec2 c, float s, ImU32 col)
		{
			const float t = (std::max)(1.0f, s * 0.1f);
			dl->AddLine(c + ImVec2(s * 0.36f, -s * 0.36f), c + ImVec2(s * 0.36f, s * 0.36f), col, t);
			dl->AddTriangle(c + ImVec2(-s * 0.38f, -s * 0.36f), c + ImVec2(s * 0.2f, 0), c + ImVec2(-s * 0.38f, s * 0.36f), col, t);
		}
		void IconShuffle(ImDrawList* dl, ImVec2 c, float s, ImU32 col)
		{
			const float t = (std::max)(1.0f, s * 0.09f), h = s * 0.5f;
			dl->AddLine(c + ImVec2(-h, -h * 0.5f), c + ImVec2(-h * 0.45f, -h * 0.5f), col, t);
			dl->AddLine(c + ImVec2(-h * 0.45f, -h * 0.5f), c + ImVec2(h * 0.45f, h * 0.5f), col, t);
			dl->AddLine(c + ImVec2(h * 0.45f, h * 0.5f), c + ImVec2(h, h * 0.5f), col, t);
			dl->AddLine(c + ImVec2(-h, h * 0.5f), c + ImVec2(-h * 0.45f, h * 0.5f), col, t);
			dl->AddLine(c + ImVec2(-h * 0.45f, h * 0.5f), c + ImVec2(h * 0.45f, -h * 0.5f), col, t);
			dl->AddLine(c + ImVec2(h * 0.45f, -h * 0.5f), c + ImVec2(h, -h * 0.5f), col, t);
			dl->AddLine(c + ImVec2(h * 0.72f, -h * 0.78f), c + ImVec2(h, -h * 0.5f), col, t);
			dl->AddLine(c + ImVec2(h * 0.72f, -h * 0.22f), c + ImVec2(h, -h * 0.5f), col, t);
			dl->AddLine(c + ImVec2(h * 0.72f, h * 0.22f), c + ImVec2(h, h * 0.5f), col, t);
			dl->AddLine(c + ImVec2(h * 0.72f, h * 0.78f), c + ImVec2(h, h * 0.5f), col, t);
		}
		void IconRepeat(ImDrawList* dl, ImVec2 c, float s, ImU32 col)
		{
			const float t = (std::max)(1.0f, s * 0.09f), h = s * 0.46f;
			dl->AddRect(c + ImVec2(-h, -h * 0.55f), c + ImVec2(h, h * 0.55f), col, h * 0.35f, 0, t);
			dl->AddTriangleFilled(c + ImVec2(h * 0.05f, -h * 0.55f - h * 0.32f), c + ImVec2(h * 0.45f, -h * 0.55f), c + ImVec2(h * 0.05f, -h * 0.55f + h * 0.32f), col);
			dl->AddTriangleFilled(c + ImVec2(-h * 0.05f, h * 0.55f - h * 0.32f), c + ImVec2(-h * 0.45f, h * 0.55f), c + ImVec2(-h * 0.05f, h * 0.55f + h * 0.32f), col);
		}
		void IconGear(ImDrawList* dl, ImVec2 c, float s, ImU32 col)
		{
			const float t = (std::max)(1.0f, s * 0.09f);
			dl->AddCircle(c, s * 0.16f, col, 16, t);
			for (int i = 0; i < 8; ++i) {
				const float a = i * 3.14159265f / 4.0f;
				const ImVec2 d(std::cos(a), std::sin(a));
				dl->AddLine(c + d * (s * 0.27f), c + d * (s * 0.44f), col, t * 1.4f);
			}
			dl->AddCircle(c, s * 0.3f, col, 20, t);
		}
		void IconClose(ImDrawList* dl, ImVec2 c, float s, ImU32 col)
		{
			const float k = s * 0.22f, t = (std::max)(1.0f, s * 0.08f);
			dl->AddLine(c - ImVec2(k, k), c + ImVec2(k, k), col, t);
			dl->AddLine(c + ImVec2(-k, k), c + ImVec2(k, -k), col, t);
		}
		void IconSpeaker(ImDrawList* dl, ImVec2 c, float s, ImU32 col)
		{
			const float t = (std::max)(1.0f, s * 0.08f), h = s * 0.5f;
			dl->AddQuadFilled(c + ImVec2(-h * 0.9f, -h * 0.3f), c + ImVec2(-h * 0.4f, -h * 0.3f), c + ImVec2(-h * 0.4f, h * 0.3f), c + ImVec2(-h * 0.9f, h * 0.3f), col);
			dl->AddTriangleFilled(c + ImVec2(-h * 0.45f, -h * 0.3f), c + ImVec2(h * 0.1f, -h * 0.8f), c + ImVec2(h * 0.1f, h * 0.8f), col);
			dl->AddTriangleFilled(c + ImVec2(-h * 0.45f, -h * 0.3f), c + ImVec2(h * 0.1f, h * 0.8f), c + ImVec2(-h * 0.45f, h * 0.3f), col);
			dl->PathArcTo(c + ImVec2(h * 0.1f, 0), h * 0.55f, -0.9f, 0.9f, 10);
			dl->PathStroke(col, 0, t);
		}
		void IconNote(ImDrawList* dl, ImVec2 c, float s, ImU32 col)
		{
			const float t = (std::max)(1.0f, s * 0.06f), h = s * 0.5f;
			dl->AddLine(c + ImVec2(-h * 0.25f, h * 0.55f), c + ImVec2(-h * 0.25f, -h * 0.65f), col, t);
			dl->AddLine(c + ImVec2(-h * 0.25f, -h * 0.65f), c + ImVec2(h * 0.7f, -h * 0.85f), col, t);
			dl->AddLine(c + ImVec2(h * 0.7f, -h * 0.85f), c + ImVec2(h * 0.7f, h * 0.35f), col, t);
			dl->AddCircle(c + ImVec2(-h * 0.5f, h * 0.55f), h * 0.25f, col, 16, t);
			dl->AddCircle(c + ImVec2(h * 0.45f, h * 0.35f), h * 0.25f, col, 16, t);
		}
		void IconLock(ImDrawList* dl, ImVec2 c, float s, ImU32 col)
		{
			const float t = (std::max)(1.0f, s * 0.08f), h = s * 0.5f;
			dl->AddRect(c + ImVec2(-h * 0.6f, -h * 0.1f), c + ImVec2(h * 0.6f, h * 0.7f), col, h * 0.15f, 0, t);
			dl->PathArcTo(c + ImVec2(0, -h * 0.1f), h * 0.38f, 3.14159265f, 2.0f * 3.14159265f, 12);
			dl->PathStroke(col, 0, t);
		}

		enum class BtnStyle
		{
			Ghost,  // icon only, gold on hover
			Flat,   // hairline outline
			Gold,   // the main button
			Round   // round close button
		};

		// A widget button. Returns true when clicked (only possible in cursor mode).
		bool Button(const char* a_id, ImVec2 a_p, ImVec2 a_size, BtnStyle a_style, IconFn a_icon, float a_iconSize, bool a_enabled = true, bool a_on = false,
			const char* a_tip = nullptr)
		{
			FadeControls fade;
			ImGui::SetCursorScreenPos(a_p);
			const bool pressed = ImGui::InvisibleButton(a_id, a_size);
			const bool hov = ImGui::IsItemHovered() && a_enabled;
			const bool held = ImGui::IsItemActive() && a_enabled;
			const float h = Ease(ImGui::GetItemID(), hov);
			auto*       dl = ImGui::GetWindowDrawList();
			const ImVec2 a = a_p, b = a_p + a_size, c = (a + b) * 0.5f;
			const float  r = Corner(U(6.0f));
			ImU32        icon = a_enabled ? kText() : Col(255, 255, 255, 60);
			switch (a_style) {
			case BtnStyle::Ghost:
				if (h > 0.01f) {
					dl->AddRectFilled(a, b, Col(255, 255, 255, static_cast<int>(16 * h)), r);
				}
				icon = !a_enabled ? Col(255, 255, 255, 50) : (a_on || hov ? kGold() : kDim());
				break;
			case BtnStyle::Flat:
				dl->AddRectFilled(a, b, Col(255, 255, 255, held ? 28 : static_cast<int>(8 + 10 * h)), r);
				CrispRect(dl, a, b, hov ? kGold() : Col(255, 255, 255, 44), r);
				icon = a_enabled ? Col(232, 226, 216) : Col(255, 255, 255, 60);
				break;
			case BtnStyle::Gold:
				{
					const ImU32 top = held ? Col(200, 166, 110) : Col(232, 204, 152), bot = held ? Col(176, 142, 90) : Col(192, 156, 98);
					if (h > 0.01f) {
						dl->AddRect(a - ImVec2(U(2), U(2)), b + ImVec2(U(2), U(2)), Col(218, 184, 124, static_cast<int>(70 * h)), r + U(2), 0, U(3.0f));
					}
					const int first = dl->VtxBuffer.Size;
					dl->AddRectFilled(a, b, top, r);
					TintVertsY(dl, first, a.y, b.y, top, bot);  // one rounded gradient (was a square band inside the rounded button)
					CrispRect(dl, a, b, Col(236, 210, 162), r);
					icon = a_enabled ? Col(26, 21, 14) : Col(26, 21, 14, 120);
					break;
				}
			case BtnStyle::Round:
				{
					const float rr = (b.x - a.x) * 0.5f;
					if (PipBoy()) {  // square, like the Pip-Boy's boxes
						dl->AddRectFilled(a, b, hov ? Col(255, 255, 255, 26) : Col(255, 255, 255, 8));
						CrispRect(dl, a, b, hov ? kGold() : Col(255, 255, 255, 70), 0.0f);
					} else {
						dl->AddCircleFilled(c, rr, hov ? Col(255, 255, 255, 26) : Col(255, 255, 255, 8), 32);
						dl->AddCircle(c, rr, hov ? kGold() : Col(255, 255, 255, 50), 32, U(1.2f));
					}
					icon = hov ? kGold() : kText();
					break;
				}
			}
			a_icon(dl, c, a_iconSize, icon);
			if (a_tip && ImGui::IsItemHovered() && g_drag.mode == Grab::None) {
				ImGui::SetTooltip("%s", a_tip);
			}
			return pressed && a_enabled;
		}

		// Switch like Hearthframe's modern toggle
		bool Switch(const char* a_id, ImVec2 a_p, bool* a_v)
		{
			FadeControls fade;
			const ImVec2 size(U(40.0f), U(22.0f));
			ImGui::SetCursorScreenPos(a_p);
			const bool pressed = ImGui::InvisibleButton(a_id, size);
			if (pressed) {
				*a_v = !*a_v;
			}
			const bool  hov = ImGui::IsItemHovered();
			const float t = Ease(ImGui::GetItemID(), *a_v, 14.0f);
			auto*       dl = ImGui::GetWindowDrawList();
			const ImVec2 a = a_p, b = a_p + size;
			const float  r = PipBoy() ? 0.0f : size.y * 0.5f;
			const ImVec4 off = ImGui::ColorConvertU32ToFloat4(Col(255, 255, 255, 28)), on = ImGui::ColorConvertU32ToFloat4(kGold());
			const ImVec4 mix(off.x + (on.x - off.x) * t, off.y + (on.y - off.y) * t, off.z + (on.z - off.z) * t, off.w + (on.w - off.w) * t);
			dl->AddRectFilled(a, b, ImGui::ColorConvertFloat4ToU32(mix), r);
			dl->AddRect(a, b, hov ? kGold() : Col(255, 255, 255, static_cast<int>(40 * (1 - t))), r, 0, U(1.0f));
			if (PipBoy()) {  // square box sliding along a square track
				const float  k = size.y * 0.5f - U(3.5f);
				const ImVec2 kc(a.x + size.y * 0.5f + (size.x - size.y) * t, (a.y + b.y) * 0.5f);
				dl->AddRectFilled(kc - ImVec2(k, k), kc + ImVec2(k, k), t > 0.5f ? Col(0, 0, 0) : Col(250, 248, 244));
				return pressed;
			}
			const ImVec2 kn(a.x + r + (size.x - 2 * r) * t, (a.y + b.y) * 0.5f);
			dl->AddCircleFilled(kn + ImVec2(0, U(1.5f)), r - U(3.0f), Col(0, 0, 0, 80), 24);
			dl->AddCircleFilled(kn, r - U(3.5f), Col(250, 248, 244), 24);
			return pressed;
		}

		std::string Clock(double a_s)
		{
			const int t = static_cast<int>((std::max)(0.0, a_s));
			return t >= 3600 ? std::format("{}:{:02}:{:02}", t / 3600, (t / 60) % 60, t % 60) : std::format("{}:{:02}", t / 60, t % 60);
		}

		// Hairline track with glowing gold fill. Seeks when clicked (if the app allows it).
		void Progress(const Media::Snapshot& a_m, float a_x0, float a_x1, float a_y, bool a_times)
		{
			auto*        dl = ImGui::GetWindowDrawList();
			const double pos = Media::CurrentPosition(a_m);
			const float  frac = a_m.duration > 0.0 ? static_cast<float>(pos / a_m.duration) : 0.0f;
			const float  ts = U(11.0f);
			float        bx0 = a_x0, bx1 = a_x1;
			if (a_times) {
				const std::string l = Clock(pos), r = Clock(a_m.duration);
				dl->AddText(FText(), ts, ImVec2(a_x0, a_y - ts * 0.55f), kText(), l.c_str());
				const float rw = TextW(FText(), ts, r.c_str());
				dl->AddText(FText(), ts, ImVec2(a_x1 - rw, a_y - ts * 0.55f), kDim(), r.c_str());
				bx0 = a_x0 + (std::max)(TextW(FText(), ts, "00:00"), TextW(FText(), ts, l.c_str())) + U(10.0f);
				bx1 = a_x1 - (std::max)(TextW(FText(), ts, "00:00"), rw) - U(10.0f);
			}
			ImGui::SetCursorScreenPos(ImVec2(bx0, a_y - U(7.0f)));
			const bool  clicked = ImGui::InvisibleButton("##seek", ImVec2((std::max)(1.0f, bx1 - bx0), U(14.0f)));
			const bool  hov = ImGui::IsItemHovered() && a_m.canSeek;
			const float h = Ease(ImGui::GetItemID(), hov);
			if (clicked && a_m.canSeek && a_m.duration > 0.0) {
				const float f = std::clamp((ImGui::GetIO().MousePos.x - bx0) / (bx1 - bx0), 0.0f, 1.0f);
				Media::Seek(f * a_m.duration);
			}
			const float  gx = bx0 + (bx1 - bx0) * std::clamp(frac, 0.0f, 1.0f);
			FadeControls fade;  // the bar (the times above are text and stay solid)
			dl->AddRectFilled(ImVec2(bx0, a_y - U(1.0f)), ImVec2(bx1, a_y + U(1.0f)), Col(255, 255, 255, 34), U(1.0f));
			{  // soft glow around the filled part: 1 px lines fading out above and below (no hard band edge)
				const int   peak = static_cast<int>(22 + 30 * h);
				const int   reach = (std::max)(2, static_cast<int>(U(4.0f) + 0.5f));
				for (int d = 1; d <= reach; ++d) {
					const int al = static_cast<int>(peak * std::pow(1.0f - static_cast<float>(d) / (reach + 1), 2.0f));
					for (const float yy : { a_y - U(1.0f) - d, a_y + U(1.0f) + d - 1.0f }) {
						dl->AddRectFilledMultiColor(ImVec2(bx0, yy), ImVec2(gx, yy + 1.0f), Col(218, 184, 124, 0), Col(218, 184, 124, al), Col(218, 184, 124, al), Col(218, 184, 124, 0));
					}
				}
			}
			dl->AddRectFilledMultiColor(ImVec2(bx0, a_y - U(1.0f)), ImVec2(gx, a_y + U(1.0f)), kGoldDim(), kGold(), kGold(), kGoldDim());
			const float hw = U(2.0f + 1.0f * h), hh = U(5.0f + 2.0f * h);
			dl->AddRectFilled(ImVec2(gx - hw, a_y - hh), ImVec2(gx + hw, a_y + hh), kGold(), Corner(hw));
			if (hov && g_drag.mode == Grab::None) {
				ImGui::SetTooltip("Click to jump there");
			}
		}

		// Volume slider for the music app's entry in the Windows volume mixer
		void Volume(const Media::Snapshot& a_m, float a_x0, float a_x1, float a_y, bool a_number)
		{
			auto*       dl = ImGui::GetWindowDrawList();
			const bool  known = a_m.volume >= 0.0f;
			const float ts = U(12.0f);
			float       bx1 = a_x1;
			if (a_number) {
				bx1 = a_x1 - U(44.0f);
			}
			ImGui::SetCursorScreenPos(ImVec2(a_x0, a_y - U(9.0f)));
			ImGui::InvisibleButton("##vol", ImVec2((std::max)(1.0f, bx1 - a_x0), U(18.0f)));
			const bool  active = ImGui::IsItemActive() && known;
			const bool  hov = (ImGui::IsItemHovered() || active) && known;
			const float h = Ease(ImGui::GetItemID(), hov);
			if (active) {
				const float f = std::clamp((ImGui::GetIO().MousePos.x - a_x0) / (bx1 - a_x0), 0.0f, 1.0f);
				if (std::fabs(f - g_volDrag) > 0.004f) {
					g_volDrag = f;
					Media::SetVolume(f);
				}
				g_volDragUntil = g_time + 0.8f;
			}
			if (ImGui::IsItemHovered() && known && g_wheelFrame != 0.0f) {
				const float base = g_volDrag >= 0.0f ? g_volDrag : a_m.volume;
				g_volDrag = std::clamp(base + g_wheelFrame * cfg.volumeStep / 100.0f, 0.0f, 1.0f);
				Media::SetVolume(g_volDrag);
				g_volDragUntil = g_time + 0.8f;
			}
			if (g_time > g_volDragUntil) {
				g_volDrag = -1.0f;
			}
			const float v = g_volDrag >= 0.0f ? g_volDrag : (known ? (a_m.muted ? 0.0f : a_m.volume) : 0.0f);
			const float gx = a_x0 + (bx1 - a_x0) * v;
			{
				FadeControls fade;  // the slider (the number next to it is text and stays solid)
				dl->AddRectFilled(ImVec2(a_x0, a_y - U(1.0f)), ImVec2(bx1, a_y + U(1.0f)), Col(255, 255, 255, 34), U(1.0f));
				if (known) {
					dl->AddRectFilledMultiColor(ImVec2(a_x0, a_y - U(1.0f)), ImVec2(gx, a_y + U(1.0f)), kGoldDim(), kGold(), kGold(), kGoldDim());
					const float hw = U(2.0f + 1.0f * h), hh = U(8.0f + 2.0f * h);
					dl->AddRectFilled(ImVec2(gx - hw, a_y - hh), ImVec2(gx + hw, a_y + hh), kGold(), Corner(hw));
				}
			}
			if (a_number) {
				const std::string n = known ? std::format("{}", static_cast<int>(std::lround(v * 100.0f))) : "--";
				const float       nw = TextW(FText(), ts, n.c_str()), pw = TextW(FText(), ts, " %");
				dl->AddText(FText(), ts, ImVec2(a_x1 - nw - pw, a_y - ts * 0.55f), kText(), n.c_str());
				dl->AddText(FText(), ts, ImVec2(a_x1 - pw, a_y - ts * 0.55f), kFaint(), " %");
			}
			if (ImGui::IsItemHovered() && !known && g_drag.mode == Grab::None) {
				ImGui::SetTooltip("The music app isn't in the Windows volume mixer yet (start playing something)");
			}
		}

		void ArtBox(ImDrawList* a_dl, ImVec2 a_a, float a_size, float a_r, bool a_label)
		{
			const ImVec2 b = a_a + ImVec2(a_size, a_size);
			a_r = Corner(a_r);
			FadeControls fade;  // the cover uses the "Album cover and buttons" opacity
			SoftShadow(a_dl, a_a, b, a_r, U(10.0f), 140);
			if (g_art) {
				const int first = a_dl->VtxBuffer.Size;
				a_dl->AddImageRounded(reinterpret_cast<ImTextureID>(g_art), a_a, b, ImVec2(0, 0), ImVec2(1, 1), Col(255, 255, 255), a_r);
				if (PipBoy() && !cfg.tintArt) {
					g_keepColour.push_back({ a_dl, first, a_dl->VtxBuffer.Size });  // the cover keeps its own colours
				}
				if (PipBoy()) {  // the screen's scan lines over the cover
					const float step = (std::max)(2.0f, std::round(U(3.0f)));
					for (float y = std::floor(a_a.y) + 1.0f; y < b.y; y += step) {
						a_dl->AddRectFilled(ImVec2(a_a.x, y), ImVec2(b.x, y + 1.0f), Col(0, 0, 0, 46));
					}
				}
				a_dl->AddRect(a_a, b, PipBoy() ? Col(218, 184, 124, 140) : Col(255, 255, 255, 26), a_r, 0, U(1.0f));
				return;
			}
			a_dl->AddRectFilled(a_a, b, Col(10, 10, 12), a_r);
			a_dl->AddRect(a_a, b, Col(255, 255, 255, 26), a_r, 0, U(1.0f));
			const ImVec2 c = a_a + ImVec2(a_size, a_size) * 0.5f - ImVec2(0, a_label ? U(6.0f) : 0.0f);
			IconNote(a_dl, c, (std::min)(a_size * 0.4f, U(36.0f)), kFaint());
			(void)a_label;
		}

		// ================================================================ the widget
		enum class Layout
		{
			Standard,
			Wide,
			Tall
		};
		Layout LayoutFor(float a_w, float a_h)
		{
			if (a_w / a_h >= 2.2f) {
				return Layout::Wide;
			}
			if (a_h / a_w >= 1.3f) {
				return Layout::Tall;
			}
			return Layout::Standard;
		}
		const char* LayoutName(Layout a_l)
		{
			return a_l == Layout::Wide ? "Horizontal bar" : (a_l == Layout::Tall ? "Portrait" : "Standard");
		}

		struct Zones
		{
			ImVec2 gripA{}, gripB{};  // where the widget can be picked up (title bar or grip strip)
		} g_zones;

		void GripDots(ImDrawList* a_dl, ImVec2 a_c, bool a_vertical)
		{
			for (int i = -1; i <= 1; ++i) {
				for (int j = 0; j < 2; ++j) {
					const ImVec2 o = a_vertical ? ImVec2((j - 0.5f) * U(5.0f), i * U(5.0f)) : ImVec2(i * U(5.0f), (j - 0.5f) * U(5.0f));
					a_dl->AddCircleFilled(a_c + o, U(1.3f), kFaint(), 8);
				}
			}
		}

		void SideButtons(float a_right, float a_y, float a_size, bool a_round)
		{
			// close + settings, right-aligned at a_right
			const float gap = U(6.0f);
			if (a_round) {
				if (Button("##close", ImVec2(a_right - a_size, a_y), ImVec2(a_size, a_size), BtnStyle::Round, IconClose, a_size, true, false, "Hide the widget")) {
					cfg.visible = false;
					g_wantCursor = false;
					MarkDirty();
				}
			} else if (Button("##close", ImVec2(a_right - a_size, a_y), ImVec2(a_size, a_size), BtnStyle::Ghost, IconClose, a_size * 0.9f, true, false, "Hide the widget")) {
				cfg.visible = false;
				g_wantCursor = false;
				MarkDirty();
			}
			const float gs = a_round ? a_size - U(2.0f) : a_size;
			if (Button("##gear", ImVec2(a_right - a_size - gap - gs, a_y + (a_size - gs) * 0.5f), ImVec2(gs, gs), BtnStyle::Ghost, IconGear, gs * 0.62f, true, g_settingsOpen,
					"Widget settings")) {
				g_settingsOpen = !g_settingsOpen;
			}
			if (cfg.locked) {
				IconLock(ImGui::GetWindowDrawList(), ImVec2(a_right - a_size - gap - gs - U(14.0f), a_y + a_size * 0.5f), U(14.0f), kGold());
			}
		}

		void Transport(const Media::Snapshot& a_m, float a_cx, float a_y, float a_avail, float a_unit)
		{
			// a_unit 1 = full size row (stacked layouts)
			const float sh = U(34.0f * a_unit), nb = U(42.0f * a_unit), pb = U(54.0f * a_unit), ph = U(46.0f * a_unit), gap = U(10.0f * a_unit);
			const bool  extras = a_avail >= sh * 2 + nb * 2 + pb + gap * 4;
			const float total = (extras ? sh * 2 + gap * 2 : 0.0f) + nb * 2 + pb + gap * 2;
			float       x = a_cx - total * 0.5f;
			const float cy = a_y + ph * 0.5f;
			if (extras) {
				if (Button("##shuffle", ImVec2(x, cy - sh * 0.5f), ImVec2(sh, sh), BtnStyle::Ghost, IconShuffle, sh * 0.5f, a_m.shuffleKnown, a_m.shuffle,
						a_m.shuffle ? "Shuffle: on" : "Shuffle: off")) {
					Media::ToggleShuffle();
				}
				x += sh + gap;
			}
			if (Button("##prev", ImVec2(x, cy - nb * 0.5f), ImVec2(nb, nb), BtnStyle::Flat, IconPrev, nb * 0.38f, a_m.canPrev, false, "Previous track")) {
				Media::Previous();
			}
			x += nb + gap;
			if (Button("##play", ImVec2(x, cy - ph * 0.5f), ImVec2(pb, ph), BtnStyle::Gold, a_m.playing ? IconPause : IconPlay, ph * 0.36f, a_m.canPlayPause, false,
					a_m.playing ? "Pause" : "Play")) {
				Media::PlayPause();
			}
			x += pb + gap;
			if (Button("##next", ImVec2(x, cy - nb * 0.5f), ImVec2(nb, nb), BtnStyle::Flat, IconNext, nb * 0.38f, a_m.canNext, false, "Next track")) {
				Media::Next();
			}
			x += nb + gap;
			if (extras) {
				const char* tip = a_m.repeat == 1 ? "Repeat: this track" : (a_m.repeat == 2 ? "Repeat: on" : "Repeat: off");
				if (Button("##repeat", ImVec2(x, cy - sh * 0.5f), ImVec2(sh, sh), BtnStyle::Ghost, IconRepeat, sh * 0.5f, a_m.repeatKnown, a_m.repeat != 0, tip)) {
					Media::CycleRepeat();
				}
				if (a_m.repeat == 1) {
					auto* dl = ImGui::GetWindowDrawList();
					const float fs = U(9.0f * a_unit);
					dl->AddText(FSemi(), fs, ImVec2(x + sh * 0.5f - TextW(FSemi(), fs, "1") * 0.5f, cy - fs * 0.55f), kGold(), "1");
				}
			}
		}

		std::string Subtitle(const Media::Snapshot& a_m)
		{
			std::string s = a_m.artist;
			if (!a_m.album.empty() && a_m.album != a_m.title) {
				s += (s.empty() ? "" : "  \xC2\xB7  ") + a_m.album;
			}
			return s;
		}

		const char* EmptyText(const Media::Snapshot& a_m)
		{
			if (a_m.unavailable) {
				return "Windows' media controls aren't available";
			}
			if (!a_m.ready) {
				return "Connecting...";
			}
			if (a_m.appRunning) {
				return "Spotify is open, but Windows can't see it";
			}
			return cfg.anyPlayer ? "Nothing is playing" : "Spotify isn't open";
		}

		// second line under EmptyText
		const char* EmptyHint(const Media::Snapshot& a_m)
		{
			if (a_m.unavailable) {
				return "Needs Windows 10 (1809) or newer";
			}
			if (a_m.appRunning) {
				return "Play a song in Spotify. Still nothing? See the Sound tab in settings";
			}
			return "Start it and play something";
		}

		void DrawWide(const Media::Snapshot& a_m, ImVec2 a, ImVec2 b)
		{
			auto*       dl = ImGui::GetWindowDrawList();
			const float hgt = b.y - a.y;
			// grip strip on the left
			g_zones.gripA = a;
			g_zones.gripB = ImVec2(a.x + U(16.0f), b.y);
			if (g_cursor) {
				GripDots(dl, ImVec2(a.x + U(9.0f), (a.y + b.y) * 0.5f), true);
			}
			float x = a.x + U(20.0f);
			const float right = b.x - U(10.0f);
			// close + gear stacked on the right - only in cursor mode
			const float sb = g_cursor ? (std::min)(U(26.0f), (hgt - U(16.0f)) * 0.5f) : 0.0f;
			if (g_cursor) {
				if (Button("##close", ImVec2(right - sb, a.y + U(8.0f)), ImVec2(sb, sb), BtnStyle::Ghost, IconClose, sb * 0.9f, true, false, "Hide the widget")) {
					cfg.visible = false;
					g_wantCursor = false;
					MarkDirty();
				}
				if (Button("##gear", ImVec2(right - sb, a.y + U(8.0f) + sb + U(2.0f)), ImVec2(sb, sb), BtnStyle::Ghost, IconGear, sb * 0.62f, true, g_settingsOpen,
						"Widget settings")) {
					g_settingsOpen = !g_settingsOpen;
				}
			}
			float x1 = g_cursor ? right - sb - U(10.0f) : right - U(4.0f);
			const bool ok = a_m.hasSession;
			if (cfg.showArt) {
				const float as = std::clamp(hgt - U(24.0f), U(40.0f), U(120.0f));
				ArtBox(dl, ImVec2(x, (a.y + b.y - as) * 0.5f), as, U(6.0f), false);
				x += as + U(14.0f);
			}
			if (!ok) {
				const float ts = U(14.0f);
				Line(dl, FSemi(), ts, ImVec2(x, (a.y + b.y) * 0.5f - ts * 0.6f), kText(), EmptyText(a_m), x1 - x - U(130.0f));
				if (a_m.ready && !a_m.unavailable && !cfg.anyPlayer && !a_m.appRunning) {
					const float bw = U(120.0f), bh = U(36.0f);
					const ImVec2 bp(x1 - bw, (a.y + b.y - bh) * 0.5f);
					if (Button("##openapp", bp, ImVec2(bw, bh), BtnStyle::Flat, IconNone, 0.0f, true, false, "Start Spotify")) {
						Media::OpenApp();
					}
					dl->AddText(FText(), U(12.0f), bp + ImVec2((bw - TextW(FText(), U(12.0f), "Open Spotify")) * 0.5f, bh * 0.5f - U(7.0f)), kText(), "Open Spotify");
				}
				return;
			}
			// transport on the right
			const bool  wideExtras = (b.x - a.x) >= U(560.0f);
			const bool  vol = g_cursor && cfg.showSound && (b.x - a.x) >= U(760.0f);
			const float unit = std::clamp((hgt - U(20.0f)) / U(46.0f), 0.7f, 1.0f);
			const float tw = (wideExtras ? U(34.0f * unit) * 2 + U(10.0f * unit) * 2 : 0.0f) + U(42.0f * unit) * 2 + U(54.0f * unit) + U(10.0f * unit) * 2;
			if (vol) {
				const float vw = U(130.0f);
				IconSpeaker(dl, ImVec2(x1 - vw + U(8.0f), (a.y + b.y) * 0.5f), U(15.0f), kDim());
				Volume(a_m, x1 - vw + U(22.0f), x1, (a.y + b.y) * 0.5f, false);
				x1 -= vw + U(14.0f);
			}
			Transport(a_m, x1 - tw * 0.5f, (a.y + b.y) * 0.5f - U(23.0f * unit), wideExtras ? tw + 1.0f : tw - 1.0f, unit);
			x1 -= tw + U(14.0f);
			// title + artist, progress under it
			const float ts = U(15.0f);
			const bool  prog = cfg.showProgress && hgt >= U(70.0f);
			const float blockH = ts + (prog ? U(18.0f) : 0.0f);
			float       y = (a.y + b.y - blockH) * 0.5f;
			const float maxW = x1 - x;
			const float tw2 = TextW(FSemi(), ts, a_m.title.c_str());
			Line(dl, FSemi(), ts, ImVec2(x, y), kText(), a_m.title, maxW);
			if (tw2 + U(20.0f) < maxW && !a_m.artist.empty()) {
				Line(dl, FText(), U(13.0f), ImVec2(x + tw2 + U(8.0f), y + U(1.5f)), kDim(), "\xC2\xB7  " + a_m.artist, maxW - tw2 - U(8.0f));
			}
			if (prog) {
				Progress(a_m, x, x1, y + ts + U(14.0f), true);
			}
		}

		void DrawStacked(const Media::Snapshot& a_m, ImVec2 a, ImVec2 b, bool a_tall)
		{
			auto*       dl = ImGui::GetWindowDrawList();
			const float x0 = a.x + U(20.0f), x1 = b.x - U(20.0f);
			float       y = a.y + U(14.0f);
			const float yEnd = b.y - U(16.0f);
			if (cfg.showHeader) {
				const float hh = U(46.0f);
				const float ts = U(18.0f);
				const char* title = PipBoy() ? "NOW PLAYING" : "Now Playing";
				dl->AddText(FTitle(), ts, ImVec2(x0 + U(1.0f), y + U(4.0f)), Col(0, 0, 0, 120), title);
				dl->AddText(FTitle(), ts, ImVec2(x0, y + U(2.0f)), kText(), title);
				const std::string sub = a_m.hasSession ? (a_m.playing ? "Spotify  \xC2\xB7  playing" : "Spotify  \xC2\xB7  paused") : "Spotify";
				dl->AddText(FText(), U(11.0f), ImVec2(x0, y + U(26.0f)), kDim(), cfg.anyPlayer && a_m.hasSession ? (a_m.playing ? "Playing" : "Paused") : sub.c_str());
				if (g_cursor) {
					SideButtons(x1, y + U(6.0f), U(32.0f), true);
				}
				g_zones.gripA = ImVec2(a.x, a.y);
				g_zones.gripB = ImVec2(x1 - U(32.0f) - U(6.0f) - U(30.0f) - U(20.0f), y + hh);
				HairlineFade(dl, x0, x1, y + hh);
				y += hh + U(14.0f);
			} else {
				// no title bar: a slim grip strip along the top, with small settings/close buttons
				const float gh = U(24.0f);
				g_zones.gripA = ImVec2(a.x + U(10.0f), a.y);
				g_zones.gripB = ImVec2(x1 - U(56.0f), a.y + gh);
				if (g_cursor) {
					GripDots(dl, ImVec2((a.x + b.x) * 0.5f, a.y + gh * 0.5f), false);
					SideButtons(x1 + U(8.0f), a.y + U(3.0f), U(22.0f), false);
				}
				y = a.y + gh + U(4.0f);
			}
			if (!a_m.hasSession) {
				const float ts = U(15.0f);
				const float cy = (y + yEnd) * 0.5f;
				Line(dl, FSemi(), ts, ImVec2(x0, cy - U(34.0f)), kText(), EmptyText(a_m), x1 - x0, 0.5f);
				Line(dl, FText(), U(12.0f), ImVec2(x0, cy - U(12.0f)), kDim(), EmptyHint(a_m), x1 - x0, 0.5f);
				if (a_m.ready && !a_m.unavailable && !cfg.anyPlayer && !a_m.appRunning && yEnd - cy > U(30.0f)) {
					const float bw = U(150.0f), bh = U(40.0f);
					const ImVec2 bp((x0 + x1 - bw) * 0.5f, cy + U(10.0f));
					if (Button("##openapp", bp, ImVec2(bw, bh), BtnStyle::Gold, IconNone, 0.0f, true, false, "Start Spotify")) {
						Media::OpenApp();
					}
					dl->AddText(FSemi(), U(13.0f), bp + ImVec2((bw - TextW(FSemi(), U(13.0f), "Open Spotify")) * 0.5f, bh * 0.5f - U(8.0f)), Col(26, 21, 14), "Open Spotify");
				}
				return;
			}

			// what fits (the same rules as the preview)
			const float progH = cfg.showProgress ? U(27.0f) : 0.0f, ctrlH = U(46.0f), soundH = U(118.0f);
			float       left = (yEnd - y) - progH - ctrlH;
			float       bigArt = 0.0f;
			bool        rowArt = false;
			if (cfg.showArt && a_tall) {
				bigArt = (std::min)(x1 - x0, left - U(58.0f) - (cfg.showSound ? soundH : 0.0f) - U(14.0f));
				if (bigArt < U(80.0f)) {
					bigArt = (std::min)(x1 - x0, left - U(58.0f) - U(14.0f));
				}
				if (bigArt >= U(80.0f)) {
					left -= bigArt + U(14.0f) + U(58.0f);
				} else {
					bigArt = 0.0f;
				}
			}
			if (cfg.showArt && bigArt <= 0.0f && left >= U(96.0f)) {
				rowArt = true;
				left -= U(96.0f);
			}
			const bool textOnly = bigArt <= 0.0f && !rowArt;
			if (textOnly) {
				left -= U(30.0f);
			}
			const bool sound = g_cursor && cfg.showSound && left >= soundH;  // volume + music mute: cursor mode only

			if (bigArt > 0.0f) {
				ArtBox(dl, ImVec2((x0 + x1 - bigArt) * 0.5f, y), bigArt, U(8.0f), true);
				y += bigArt + U(14.0f);
				Line(dl, FSemi(), U(17.0f), ImVec2(x0, y), kText(), a_m.title, x1 - x0, 0.5f);
				Line(dl, FText(), U(13.0f), ImVec2(x0, y + U(24.0f)), kDim(), Subtitle(a_m), x1 - x0, 0.5f);
				y += U(58.0f);
			} else if (rowArt) {
				const float as = U(80.0f);
				ArtBox(dl, ImVec2(x0, y), as, U(6.0f), true);
				const float tx = x0 + as + U(14.0f);
				Line(dl, FSemi(), U(16.0f), ImVec2(tx, y + U(18.0f)), kText(), a_m.title, x1 - tx);
				Line(dl, FText(), U(13.0f), ImVec2(tx, y + U(42.0f)), kDim(), Subtitle(a_m), x1 - tx);
				y += U(96.0f);
			} else {
				const float tw = TextW(FSemi(), U(14.0f), a_m.title.c_str());
				const float maxW = x1 - x0;
				if (a_tall || tw + U(30.0f) > maxW || a_m.artist.empty()) {
					Line(dl, FSemi(), U(14.0f), ImVec2(x0, y), kText(), a_m.title, maxW, a_tall ? 0.5f : 0.0f);
				} else {
					Line(dl, FSemi(), U(14.0f), ImVec2(x0, y), kText(), a_m.title, maxW);
					Line(dl, FText(), U(13.0f), ImVec2(x0 + tw + U(8.0f), y + U(1.0f)), kDim(), "\xC2\xB7  " + a_m.artist, maxW - tw - U(8.0f));
				}
				y += U(30.0f);
			}
			if (cfg.showProgress) {
				Progress(a_m, x0, x1, y + U(7.0f), true);
				y += progH;
			}
			Transport(a_m, (x0 + x1) * 0.5f, y, x1 - x0, 1.0f);
			y += ctrlH;
			if (sound) {
				y += U(18.0f);
				dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + 1.0f), Col(255, 255, 255, 18));
				y += U(14.0f);
				dl->AddText(FSemi(), U(11.0f), ImVec2(x0, y), kGold(), "SOUND");
				y += U(26.0f);
				dl->AddText(FText(), U(13.0f), ImVec2(x0, y - U(8.0f)), Col(214, 210, 202), "Volume");
				Volume(a_m, x0 + U(70.0f), x1, y, true);
				y += U(26.0f);
				Line(dl, FText(), U(13.0f), ImVec2(x0, y + U(2.0f)), Col(214, 210, 202), "Mute the game's music while playing", x1 - x0 - U(52.0f));
				if (Switch("##duck", ImVec2(x1 - U(40.0f), y), &cfg.muteGameMusic)) {
					MarkDirty();
				}
			}
		}

		// Clamp the widget to the screen (also after a resolution change)
		void KeepOnScreen()
		{
			const float k = g_k;
			float       w = cfg.w * k, h = cfg.h * k;
			if (w > g_displayW) {
				cfg.w = (std::max)(kMinW, g_displayW / k);
				w = cfg.w * k;
			}
			if (h > g_displayH) {
				cfg.h = (std::max)(kMinH, g_displayH / k);
				h = cfg.h * k;
			}
			const float maxX = (std::max)(0.0f, (g_displayW - w) / g_displayW), maxY = (std::max)(0.0f, (g_displayH - h) / g_displayH);
			cfg.posX = std::clamp(cfg.posX, 0.0f, maxX);
			cfg.posY = std::clamp(cfg.posY, 0.0f, maxY);
		}

		void PushUndo()
		{
			g_undo.push_back({ cfg.posX, cfg.posY, cfg.w, cfg.h });
			if (g_undo.size() > 30) {
				g_undo.erase(g_undo.begin());
			}
		}

		void Undo()
		{
			if (g_undo.empty()) {
				g_status = "Nothing to undo";
				return;
			}
			const Rect r = g_undo.back();
			g_undo.pop_back();
			cfg.posX = r.x;
			cfg.posY = r.y;
			cfg.w = r.w;
			cfg.h = r.h;
			MarkDirty();
			g_status = "Moved back";
		}

		void ApplyPreset(float a_w, float a_h)
		{
			PushUndo();
			cfg.w = a_w;
			cfg.h = a_h;
			MarkDirty();
		}

		Grab HitTest(ImVec2 a_p, ImVec2 a_a, ImVec2 a_b)
		{
			const float e = (std::max)(4.0f, U(6.0f)), c = (std::max)(10.0f, U(14.0f));  // grabbable at any scale
			const bool  inX = a_p.x >= a_a.x - e && a_p.x <= a_b.x + e, inY = a_p.y >= a_a.y - e && a_p.y <= a_b.y + e;
			if (!inX || !inY) {
				return Grab::None;
			}
			if (!cfg.locked) {
				const bool l = a_p.x <= a_a.x + e, r = a_p.x >= a_b.x - e, t = a_p.y <= a_a.y + e, bo = a_p.y >= a_b.y - e;
				const bool lc = a_p.x <= a_a.x + c, rc = a_p.x >= a_b.x - c, tc = a_p.y <= a_a.y + c, bc = a_p.y >= a_b.y - c;
				if ((t && lc) || (l && tc)) return Grab::NW;
				if ((t && rc) || (r && tc)) return Grab::NE;
				if ((bo && lc) || (l && bc)) return Grab::SW;
				if ((bo && rc) || (r && bc)) return Grab::SE;
				if (t) return Grab::N;
				if (bo) return Grab::S;
				if (l) return Grab::W;
				if (r) return Grab::E;
				// anywhere else on the panel moves it (buttons and sliders keep their clicks: HandleDrag only starts
				// a drag when no control is under the cursor)
				if (a_p.x >= a_a.x && a_p.x <= a_b.x && a_p.y >= a_a.y && a_p.y <= a_b.y) {
					return Grab::Move;
				}
			}
			return Grab::None;
		}

		ImGuiMouseCursor CursorFor(Grab a_g)
		{
			switch (a_g) {
			case Grab::N:
			case Grab::S: return ImGuiMouseCursor_ResizeNS;
			case Grab::E:
			case Grab::W: return ImGuiMouseCursor_ResizeEW;
			case Grab::NW:
			case Grab::SE: return ImGuiMouseCursor_ResizeNWSE;
			case Grab::NE:
			case Grab::SW: return ImGuiMouseCursor_ResizeNESW;
			case Grab::Move: return ImGuiMouseCursor_Hand;
			default: return ImGuiMouseCursor_Arrow;
			}
		}

		// Moving / resizing with the safeguards: only in cursor mode, never from a button or slider, and only after a
		// short hold and a few pixels of mouse travel.
		void HandleDrag(ImVec2 a_a, ImVec2 a_b)
		{
			auto&      io = ImGui::GetIO();
			const auto mouse = io.MousePos;
			if (g_drag.mode == Grab::None) {
				const Grab g = HitTest(mouse, a_a, a_b);
				g_wheelScale = mouse.x >= a_a.x && mouse.x <= a_b.x && mouse.y >= a_a.y && mouse.y <= a_b.y;
				if (g != Grab::None && !ImGui::IsAnyItemHovered()) {
					ImGui::SetMouseCursor(CursorFor(g));
					if (ImGui::IsMouseClicked(0)) {
						g_drag.mode = g;
						g_drag.active = false;
						g_drag.start = mouse;
						g_drag.startTime = g_time;
						g_drag.pos0 = a_a;
						g_drag.size0 = a_b - a_a;
					}
				}
				return;
			}
			ImGui::SetMouseCursor(CursorFor(g_drag.mode));
			if (!io.MouseDown[0]) {
				if (g_drag.active) {
					MarkDirty();
				}
				g_drag = {};
				return;
			}
			const ImVec2 d = mouse - g_drag.start;
			if (!g_drag.active) {
				const bool held = (g_time - g_drag.startTime) * 1000.0f >= static_cast<float>(cfg.dragDelayMs);
				const bool moved = std::sqrt(d.x * d.x + d.y * d.y) >= static_cast<float>(cfg.dragDistance);
				if (!(held && moved)) {
					return;
				}
				g_drag.active = true;
				PushUndo();
			}
			const float k = g_k;
			ImVec2      pos = g_drag.pos0, size = g_drag.size0;
			const auto  m = g_drag.mode;
			if (m == Grab::Move) {
				pos += d;
			} else {
				const bool e = m == Grab::E || m == Grab::NE || m == Grab::SE, w = m == Grab::W || m == Grab::NW || m == Grab::SW;
				const bool s = m == Grab::S || m == Grab::SE || m == Grab::SW, n = m == Grab::N || m == Grab::NE || m == Grab::NW;
				if (e) {
					size.x = std::clamp(g_drag.size0.x + d.x, kMinW * k, (std::min)(kMaxW * k, g_displayW - pos.x));
				}
				if (w) {
					size.x = std::clamp(g_drag.size0.x - d.x, kMinW * k, (std::min)(kMaxW * k, g_drag.pos0.x + g_drag.size0.x));
					pos.x = g_drag.pos0.x + g_drag.size0.x - size.x;
				}
				if (s) {
					size.y = std::clamp(g_drag.size0.y + d.y, kMinH * k, (std::min)(kMaxH * k, g_displayH - pos.y));
				}
				if (n) {
					size.y = std::clamp(g_drag.size0.y - d.y, kMinH * k, (std::min)(kMaxH * k, g_drag.pos0.y + g_drag.size0.y));
					pos.y = g_drag.pos0.y + g_drag.size0.y - size.y;
				}
			}
			cfg.w = size.x / k;
			cfg.h = size.y / k;
			cfg.posX = pos.x / g_displayW;
			cfg.posY = pos.y / g_displayH;
			KeepOnScreen();
		}

		// The sound section's height when it fits in a stacked layout (0 if it doesn't). Outside cursor mode the
		// section is hidden and the widget is that much shorter (its top edge stays put). Same rules as DrawStacked.
		float SoundSectionHeight(bool a_tall)
		{
			constexpr float progH = 27.0f, ctrlH = 46.0f, soundH = 118.0f;
			const float     y = cfg.showHeader ? 14.0f + 46.0f + 14.0f : 24.0f + 4.0f;
			float           left = (cfg.h - 16.0f - y) - (cfg.showProgress ? progH : 0.0f) - ctrlH;
			float           bigArt = 0.0f;
			bool            rowArt = false;
			if (cfg.showArt && a_tall) {
				bigArt = (std::min)(cfg.w - 40.0f, left - 58.0f - (cfg.showSound ? soundH : 0.0f) - 14.0f);
				if (bigArt < 80.0f) {
					bigArt = (std::min)(cfg.w - 40.0f, left - 58.0f - 14.0f);
				}
				if (bigArt >= 80.0f) {
					left -= bigArt + 14.0f + 58.0f;
				} else {
					bigArt = 0.0f;
				}
			}
			if (cfg.showArt && bigArt <= 0.0f && left >= 96.0f) {
				rowArt = true;
				left -= 96.0f;
			}
			if (bigArt <= 0.0f && !rowArt) {
				left -= 30.0f;
			}
			return cfg.showSound && left >= soundH ? soundH : 0.0f;
		}

		void DrawWidget(const Media::Snapshot& a_m)
		{
			KeepOnScreen();
			const float  k = g_k;
			const ImVec2 a(std::round(cfg.posX * g_displayW), std::round(cfg.posY * g_displayH));
			const ImVec2 bFull = a + ImVec2(std::round(cfg.w * k), std::round(cfg.h * k));  // the size the player set
			const Layout layout = LayoutFor(cfg.w, cfg.h);
			const float  cut = (!g_cursor && a_m.hasSession && layout != Layout::Wide) ? SoundSectionHeight(layout == Layout::Tall) : 0.0f;
			const ImVec2 b(bFull.x, std::round(bFull.y - cut * k));  // what's drawn: shorter outside cursor mode
			const float  margin = U(10.0f);
			ImGui::SetNextWindowPos(a - ImVec2(margin, margin), ImGuiCond_Always);
			ImGui::SetNextWindowSize(b - a + ImVec2(margin, margin) * 2.0f, ImGuiCond_Always);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
			ImGui::Begin("##hearthsong", nullptr,
				ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
					ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);
			ImGui::PopStyleVar(3);
			auto* dl = ImGui::GetWindowDrawList();
			dl->PushClipRect(a - ImVec2(margin, margin), b + ImVec2(margin, margin), false);
			Panel(dl, a, b, g_cursor);
			dl->PushClipRect(a, b, true);
			if (layout == Layout::Wide) {
				DrawWide(a_m, a, b);
			} else {
				DrawStacked(a_m, a, bFull, layout == Layout::Tall);  // laid out for the full size, so nothing moves when the cursor comes up
			}
			dl->PopClipRect();
			// resize corner hint
			if (g_cursor && !cfg.locked) {
				dl->AddLine(b - ImVec2(U(4.0f), U(12.0f)), b - ImVec2(U(12.0f), U(4.0f)), Col(255, 255, 255, 70), U(1.2f));
				dl->AddLine(b - ImVec2(U(4.0f), U(8.0f)), b - ImVec2(U(8.0f), U(4.0f)), Col(255, 255, 255, 70), U(1.2f));
			}
			if (g_cursor) {
				HandleDrag(a, b);
				if (g_drag.active && g_drag.mode != Grab::Move) {  // size readout above the widget
					const std::string t = std::format("{:.0f} x {:.0f}  \xC2\xB7  {}", cfg.w, cfg.h, LayoutName(layout));
					const float       ts = U(12.0f), tw = TextW(FSemi(), ts, t.c_str());
					const ImVec2      p((a.x + b.x - tw) * 0.5f - U(10.0f), a.y - U(34.0f) < 0 ? b.y + U(8.0f) : a.y - U(34.0f));
					auto*             fg = ImGui::GetForegroundDrawList();
					fg->AddRectFilled(p, p + ImVec2(tw + U(20.0f), U(24.0f)), kGold(), U(6.0f));
					fg->AddText(FSemi(), ts, p + ImVec2(U(10.0f), U(5.0f)), Col(26, 21, 14), t.c_str());
				}
			}
			dl->PopClipRect();
			ImGui::End();
		}

		// ================================================================ settings window
		float SS(float a_v) { return Theme::S(a_v); }

		void Row(const char* a_label, const char* a_help)
		{
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(a_label);
			if (a_help && ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s", a_help);
			}
			ImGui::SameLine(SS(320.0f));
		}

		bool ToggleRow(const char* a_label, const char* a_help, bool* a_v)
		{
			Row(a_label, a_help);
			ImGui::PushID(a_label);
			const bool c = Theme::Toggle("##t", a_v);
			ImGui::PopID();
			if (c) {
				MarkDirty();
			}
			return c;
		}

		void Gap() { ImGui::Dummy(ImVec2(0, SS(8.0f))); }

		void DrawSettings(const Media::Snapshot& a_m)
		{
			auto& io = ImGui::GetIO();
			ImGui::SetNextWindowPos(io.DisplaySize * 0.5f, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowSize(ImVec2((std::min)(SS(860.0f), io.DisplaySize.x * 0.95f), 0));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(SS(30.0f), SS(26.0f)));
			const bool vis = Theme::BeginWindow("Holosong settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
			ImGui::PopStyleVar();
			if (!vis) {
				Theme::EndWindow();
				return;
			}
			if (Theme::Header("Holosong", "Changes save to Holosong.ini by themselves")) {
				g_settingsOpen = false;
			}
			const char* tabs[] = { "Layout", "Behaviour", "Keys", "Sound" };
			for (int t = 0; t < 4; ++t) {
				if (t) {
					ImGui::SameLine(0.0f, SS(8.0f));
				}
				ImGui::PushID(900 + t);
				if (Theme::Tab(tabs[t], g_settingsTab == t, SS(46.0f), -1)) {
					g_settingsTab = t;
				}
				ImGui::PopID();
			}
			const float h = (std::min)(SS(560.0f), io.DisplaySize.y * 0.62f);
			if (Theme::BeginParchment("##hs_settings", ImVec2(0.0f, h), true)) {
				// mouse wheel over the panel scrolls it (done here directly, so nothing else can swallow the wheel)
				if (g_wheelFrame != 0.0f && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
					ImGui::SetScrollY(std::clamp(ImGui::GetScrollY() - g_wheelFrame * SS(90.0f), 0.0f, ImGui::GetScrollMaxY()));
				}
				const float ctl = ImGui::GetContentRegionAvail().x - SS(330.0f);
				if (g_settingsTab == 0) {
					const Layout l = LayoutFor(cfg.w, cfg.h);
					Theme::SectionLabel("Shape");
					Row("Layout", "Drag any edge or corner of the widget to resize it. The layout follows the shape:\nwide and short = horizontal bar, tall and narrow = portrait.");
					if (Theme::Chip("Standard", l == Layout::Standard)) {
						ApplyPreset(kDefW, kDefH);
					}
					ImGui::SameLine(0.0f, SS(6.0f));
					if (Theme::Chip("Horizontal bar", l == Layout::Wide)) {
						ApplyPreset(640.0f, 104.0f);
					}
					ImGui::SameLine(0.0f, SS(6.0f));
					if (Theme::Chip("Portrait", l == Layout::Tall)) {
						ApplyPreset(280.0f, 520.0f);
					}
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + SS(320.0f));
					Theme::TextItalic("Now: %s, %.0f x %.0f", LayoutName(l), cfg.w, cfg.h);
					Row("Scale", "Makes everything in the widget bigger or smaller. Ctrl + mouse wheel over the widget works too.");
					if (Theme::SliderInt("##scale", &cfg.scalePct, 50, 100, "%d%%", ctl)) {
						MarkDirty();
						g_pendingFonts = true;  // remade once the slider is let go
					}
					Row("Background opacity", "How see-through the widget's background is");
					if (Theme::SliderInt("##opacity", &cfg.opacityPct, 30, 100, "%d%%", ctl)) {
						MarkDirty();
					}
					Row("Cover and buttons opacity", "How see-through the album cover, the buttons, the progress bar and the sliders are.\nText never fades, so the song and times stay readable.");
					if (Theme::SliderInt("##ctlopacity", &cfg.controlsOpacityPct, 10, 100, "%d%%", ctl)) {
						MarkDirty();
					}
					Gap();
					Theme::SectionLabel("Colour");
					Row("Colour", "Holosong draws itself in one colour, like the Pip-Boy's screen.\nHUD / Pip-Boy colour: the one you picked in the game's Settings > Display - a change there shows right away.");
					{
						const char* names[] = { "HUD colour", "Pip-Boy colour", "Custom", "Original gold" };
						const int   modes[] = { 1, 2, 3, 0 };
						for (int i = 0; i < 4; ++i) {
							if (i == 2) {
								ImGui::SetCursorPosX(ImGui::GetCursorPosX() + SS(320.0f));
							} else if (i) {
								ImGui::SameLine(0.0f, SS(6.0f));
							}
							ImGui::PushID(700 + i);
							if (Theme::Chip(names[i], cfg.colorMode == modes[i]) && cfg.colorMode != modes[i]) {
								const bool fontsChange = (cfg.colorMode == 0) != (modes[i] == 0);
								cfg.colorMode = modes[i];
								g_tintCheckAt = -100.0f;
								if (fontsChange) {
									g_pendingFonts = true;  // Roboto Condensed <-> Inter + Cinzel
								}
								MarkDirty();
							}
							ImGui::PopID();
						}
					}
					if (PipBoy()) {
						ImGui::SetCursorPosX(ImGui::GetCursorPosX() + SS(320.0f));
						Theme::TextItalic("Now: %s", g_tintSource.c_str());
					}
					if (cfg.colorMode == 3) {
						const char* ch[] = { "Red", "Green", "Blue" };
						int*        v[] = { &cfg.customR, &cfg.customG, &cfg.customB };
						for (int i = 0; i < 3; ++i) {
							Row(ch[i], "Your own colour (0 - 255 each). It's always shown at full brightness, so it stays readable.");
							ImGui::PushID(710 + i);
							if (Theme::SliderInt("##rgb", v[i], 0, 255, "%d", ctl)) {
								g_tintCheckAt = -100.0f;
								MarkDirty();
							}
							ImGui::PopID();
						}
					}
					if (PipBoy()) {
						ToggleRow("Album cover in the colour too", "On: the cover is shown in the same colour, like a picture on the Pip-Boy.\nOff: the cover keeps its own colours.", &cfg.tintArt);
					}
					Gap();
					Theme::SectionLabel("Show");
					ToggleRow("Title bar", "\"Now Playing\" with the settings and close buttons. Without it, there's a slim strip along the top for the settings and close buttons.", &cfg.showHeader);
					ToggleRow("Album art", nullptr, &cfg.showArt);
					ToggleRow("Progress bar", "Song position. Click it to jump (if the app allows).", &cfg.showProgress);
					ToggleRow("Volume and music mute", "Shown when there's room for them", &cfg.showSound);
					Gap();
					Theme::SectionLabel("Position");
					ToggleRow("Lock position and size", "Nothing can move or resize the widget, even with the cursor", &cfg.locked);
					Row("Undo", "Puts the widget back where it was before the last move, resize or layout change");
					ImGui::BeginDisabled(g_undo.empty());
					if (Theme::Button(std::format("Undo last move  (Ctrl+{})", KeyName(cfg.keyVk[kKeyUndo])).c_str(), ImVec2(SS(320.0f), SS(40.0f)))) {
						Undo();
					}
					ImGui::EndDisabled();
					Row("Reset", "Default size and place");
					if (Theme::Button("Reset position and size", ImVec2(SS(320.0f), SS(40.0f)))) {
						PushUndo();
						cfg.posX = 0.70f;
						cfg.posY = 0.06f;
						cfg.w = kDefW;
						cfg.h = kDefH;
						cfg.scalePct = 100;
						g_pendingFonts = true;
						MarkDirty();
					}
					Gap();
					Theme::SectionLabel("This window");
					Row("Settings window size", nullptr);
					int ui = static_cast<int>(std::lround(cfg.uiScale * 100.0f));
					if (Theme::SliderInt("##ui", &ui, 60, 200, "%d%%", ctl)) {
						cfg.uiScale = ui / 100.0f;
						MarkDirty();
						g_pendingFonts = true;
					}
				} else if (g_settingsTab == 1) {
					Theme::SectionLabel("Combat");
					ToggleRow("Leave cursor mode when combat starts", "If a fight starts while the cursor is up, you get the camera and your weapons back at once", &cfg.leaveCursorInCombat);
					ToggleRow("No cursor mode during combat", "The cursor key does nothing until the fight is over", &cfg.noCursorInCombat);
					ToggleRow("Hide during combat", "The widget fades out when a fight starts and comes back afterwards", &cfg.autoHideInCombat);
					ImGui::BeginDisabled(!cfg.autoHideInCombat);
					Row("Show again after", "Seconds after the fight ends");
					if (Theme::SliderFloat("##again", &cfg.showAgainAfter, 0.0f, 10.0f, "%.1f s", ctl)) {
						MarkDirty();
					}
					ImGui::EndDisabled();
					Gap();
					Theme::SectionLabel("Moving and resizing");
					Row("Hold before it moves", "How long the mouse button must be held on the widget before it moves or resizes");
					if (Theme::SliderInt("##delay", &cfg.dragDelayMs, 0, 600, "%d ms", ctl)) {
						MarkDirty();
					}
					Row("Move before it moves", "How far the mouse must travel before the widget follows (a click never nudges it)");
					if (Theme::SliderInt("##dist", &cfg.dragDistance, 0, 40, "%d px", ctl)) {
						MarkDirty();
					}
					Row("Mouse speed", "Cursor speed in cursor mode");
					if (Theme::SliderFloat("##mouse", &cfg.mouseSpeed, 0.2f, 5.0f, "%.1f x", ctl)) {
						MarkDirty();
					}
					Row("Cursor key", "Toggle: press once for the cursor, again to put it away.\nHold: the cursor is there while you hold the key.");
					if (Theme::Chip("Press to toggle", !cfg.cursorHold)) {
						cfg.cursorHold = false;
						MarkDirty();
					}
					ImGui::SameLine(0.0f, SS(6.0f));
					if (Theme::Chip("Hold", cfg.cursorHold)) {
						cfg.cursorHold = true;
						MarkDirty();
					}
					Gap();
					Theme::SectionLabel("Hiding");
					ToggleRow("Hide in menus", "Hidden while the game is paused (inventory, map, journal...)", &cfg.hideInMenus);
					ToggleRow("Hide when Spotify isn't open", "Otherwise the widget shows an \"Open Spotify\" button", &cfg.hideWhenNoPlayer);
					Gap();
					Theme::SectionLabel("Diagnostics");
					ToggleRow("Show frame rate", "Top-left corner: the game's frames per second and how long Holosong takes of each frame", &cfg.showFps);
				} else if (g_settingsTab == 2) {
					Theme::SectionLabel("Keys");
					Theme::TextItalic("Click a key, then press the new one. Esc keeps the old key, Backspace removes it.");
					Gap();
					const int capturing = g_captureSlot;
					for (int i = 0; i < kKeyCount; ++i) {
						Row(kKeyLabel[i], kKeyHelp[i]);
						ImGui::PushID(i);
						const bool  waiting = capturing == i;
						std::string label = waiting ? std::string("Press a key...") : (i == kKeyUndo ? "Ctrl + " + KeyName(cfg.keyVk[i]) : KeyName(cfg.keyVk[i]));
						if (Theme::Button(label.c_str(), ImVec2(SS(waiting ? 260.0f : 200.0f), SS(40.0f)), waiting) && !waiting) {
							g_capturedVk = 0;
							g_captureSlot = i;
						}
						ImGui::PopID();
					}
					Gap();
					Theme::TextItalic("Your keyboard's own media keys (play/pause, next...) control Spotify too.");
				} else {
					Theme::SectionLabel("Fallout 4's music");
					ToggleRow("Mute the game's music while playing", "Turns the game's music down to silence while Spotify plays, and back up when it stops.\n(Back to normal in the pause menu, so your audio settings stay as they are.)", &cfg.muteGameMusic);
					if (!g_music.cat) {
						ImGui::SetCursorPosX(ImGui::GetCursorPosX() + SS(320.0f));
						Theme::TextColoredWrapped(Theme::Warning(), "The game's music sound category wasn't found - see Holosong.log");
					}
					ToggleRow("Mute the radio while playing", "Diamond City Radio and the other Pip-Boy stations go silent while Spotify plays.\n(Back to normal in the pause menu.)", &cfg.muteRadio);
					if (!g_radio.cat) {
						ImGui::SetCursorPosX(ImGui::GetCursorPosX() + SS(320.0f));
						Theme::TextColoredWrapped(Theme::Warning(), "The radio sound category wasn't found - see Holosong.log");
					}
					Gap();
					Theme::SectionLabel("Volume");
					Row("Volume step", "How much the volume keys and the mouse wheel over the slider change the volume");
					if (Theme::SliderInt("##step", &cfg.volumeStep, 1, 25, "%d%%", ctl)) {
						MarkDirty();
					}
					Gap();
					Theme::SectionLabel("Music app");
					Row("App name", "Part of the app's name as Windows knows it. \"Spotify\" for Spotify; other players work too\n(e.g. \"foobar\", \"MusicBee\", \"chrome\" for a browser tab).");
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(SS(10.0f), SS(10.0f)));
					ImGui::SetNextItemWidth(ctl);
					if (ImGui::InputText("##app", g_appBuf, sizeof g_appBuf)) {
						cfg.musicApp = g_appBuf;
						Media::SetSettings(MediaSettings());
						MarkDirty();
					}
					ImGui::PopStyleVar();
					if (ToggleRow("Use any player if it isn't found", "Control whatever Windows says is playing when the app above isn't running", &cfg.anyPlayer)) {
						Media::SetSettings(MediaSettings());
					}
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + SS(320.0f));
					Theme::TextItalic("Now controlling: %s", a_m.hasSession ? a_m.app.c_str() : "nothing");
					Row("Apps Windows sees", "Every app that's sharing what it plays with Windows right now. Click one to control it.");
					if (a_m.sessions.empty()) {
						Theme::TextItalic("none");
					} else {
						for (std::size_t i = 0; i < a_m.sessions.size(); ++i) {
							if (i) {
								ImGui::SameLine(0.0f, SS(6.0f));
							}
							ImGui::PushID(static_cast<int>(1000 + i));
							if (Theme::Chip(a_m.sessions[i].c_str(), a_m.hasSession && a_m.sessions[i] == a_m.app)) {
								cfg.musicApp = a_m.sessions[i];
								std::snprintf(g_appBuf, sizeof g_appBuf, "%s", cfg.musicApp.c_str());
								Media::SetSettings(MediaSettings());
								MarkDirty();
							}
							ImGui::PopID();
						}
					}
					if (!a_m.hasSession && a_m.appRunning) {
						Gap();
						Theme::SectionLabel("Spotify is open but not in the list?");
						Theme::TextColoredWrapped(Theme::Cream(),
							"1. Play a song in Spotify - it only shows up in Windows once something has played.\n"
							"2. In Spotify: Settings > Display > turn on \"Show desktop overlay when using media keys\", "
							"then restart Spotify. With that setting off, Spotify can stop sharing what it plays with Windows.\n"
							"3. Use the Spotify desktop app, not Spotify in a browser (a browser tab shows up as the browser).");
					}
				}
			}
			Theme::EndParchment();
			ImGui::Dummy(ImVec2(0, SS(6.0f)));
			if (Theme::Button("Reset everything", ImVec2(SS(220.0f), SS(52.0f)))) {
				PushUndo();
				int keys[kKeyCount];
				std::copy(std::begin(cfg.keyVk), std::end(cfg.keyVk), std::begin(keys));
				cfg = Config{};
				std::copy(std::begin(keys), std::end(keys), std::begin(cfg.keyVk));  // keys stay
				std::snprintf(g_appBuf, sizeof g_appBuf, "%s", cfg.musicApp.c_str());
				Media::SetSettings(MediaSettings());
				g_pendingFonts = true;
				MarkDirty();
				g_status = "Everything but the keys is back to default";
			}
			ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - SS(180.0f));
			if (Theme::Button("Done", ImVec2(SS(180.0f), SS(52.0f)), true)) {
				g_settingsOpen = false;
			}
			if (!g_status.empty()) {
				Theme::TextItalic("%s", g_status.c_str());
			} else {
				Theme::TextItalic("%s", g_iniPath.c_str());
			}
			Theme::EndWindow();
		}

		// ================================================================ fonts / ImGui
		std::string FontDir()
		{
			std::error_code ec;
			return fs::absolute(fs::path(L"Data\\F4SE\\Plugins\\Holosong\\Fonts"), ec).string() + "\\";
		}

		const ImWchar* GlyphRanges()
		{
			// Latin (with accents), Greek, Cyrillic and punctuation: song titles in most European languages
			static const ImWchar ranges[] = { 0x0020, 0x024F, 0x0370, 0x03FF, 0x0400, 0x052F, 0x1E00, 0x1EFF, 0x2000, 0x206F, 0x20A0, 0x20CF, 0x2100, 0x214F, 0 };
			return ranges;
		}

		void ApplyFonts()
		{
			char windir[MAX_PATH]{};
			::GetWindowsDirectoryA(windir, MAX_PATH);
			const std::string sys = std::string(windir) + "\\Fonts\\";
			const std::string regular = sys + "georgia.ttf", bold = sys + "georgiab.ttf", italic = sys + "georgiai.ttf";
			if (g_imguiReady) {
				ImGui_ImplDX11_InvalidateDeviceObjects();
			}
			const std::string own = FontDir();
			std::error_code   fec;
			// Pip-Boy look: Roboto Condensed (the game's own interface typeface), else Inter + Cinzel
			const bool        pb = PipBoy() && fs::exists(fs::path(own + "RobotoCondensed-Regular.ttf"), fec) && fs::exists(fs::path(own + "RobotoCondensed-Bold.ttf"), fec);
			const std::string display = own + (pb ? "RobotoCondensed-Bold.ttf" : "Cinzel-SemiBold.ttf"), uiReg = own + (pb ? "RobotoCondensed-Regular.ttf" : "Inter-Regular.ttf"),
							  uiSemi = own + (pb ? "RobotoCondensed-Bold.ttf" : "Inter-SemiBold.ttf"), uiItal = own + (pb ? "RobotoCondensed-Regular.ttf" : "Inter-Italic.ttf");
			Theme::FontFiles files{ regular.c_str(), bold.c_str(), italic.c_str() };
			files.display = display.c_str();
			files.uiRegular = uiReg.c_str();
			files.uiSemiBold = uiSemi.c_str();
			files.uiItalic = uiItal.c_str();
			const float screen = (std::max)(1.0f, g_displayH / 1080.0f);
			Theme::Setup(screen * cfg.uiScale, cfg.fontSize, files);
			// the widget's own fonts, made at the widget's size so they stay sharp at any scale
			g_fontK = screen * cfg.scalePct / 100.0f;
			auto& io = ImGui::GetIO();
			auto  load = [&](const std::string& a_path, float a_px) -> ImFont* {
                std::error_code ec;
                if (!fs::exists(fs::path(a_path), ec)) {
                    return nullptr;
                }
                ImFontConfig fc;
                fc.OversampleH = 2;
                fc.OversampleV = 2;
                return io.Fonts->AddFontFromFileTTF(a_path.c_str(), a_px, &fc, GlyphRanges());
			};
			// at very small scales the text is drawn smaller than the font was made (fonts below ~6 px can't be built)
			g_fText = load(uiReg, (std::max)(6.0f, 13.0f * g_fontK));
			g_fSemi = load(uiSemi, (std::max)(6.0f, 17.0f * g_fontK));
			g_fTitle = load(display, (std::max)(6.0f, 18.0f * g_fontK));
			spdlog::info("UI: screen scale {:.2f}, widget {}%, fonts {}", screen, cfg.scalePct, g_fText ? (pb ? "Roboto Condensed" : "Inter + Cinzel") : "MISSING (Data\\F4SE\\Plugins\\Holosong\\Fonts)");
		}

		bool InitImGui()
		{
			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			auto& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.LogFilename = nullptr;
			io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
			for (int i = 0; i < Theme::StyleCount(); ++i) {
				if (std::strcmp(Theme::StyleName(i), "Modern") == 0) {
					Theme::SetStyle(i);
				}
			}
			ApplyFonts();
			if (!ImGui_ImplDX11_Init(g_dev, g_ctx)) {
				return false;
			}
			io.MouseDrawCursor = g_cursor;
			return true;
		}

		ID3D11ShaderResourceView* MakeSRV(const std::uint8_t* a_px, int a_w, int a_h)
		{
			D3D11_TEXTURE2D_DESC d{};
			d.Width = static_cast<UINT>(a_w);
			d.Height = static_cast<UINT>(a_h);
			d.MipLevels = 1;
			d.ArraySize = 1;
			d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			d.SampleDesc.Count = 1;
			d.Usage = D3D11_USAGE_DEFAULT;
			d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			D3D11_SUBRESOURCE_DATA sd{ a_px, static_cast<UINT>(a_w) * 4, 0 };
			ID3D11Texture2D*       tex = nullptr;
			if (FAILED(g_dev->CreateTexture2D(&d, &sd, &tex)) || !tex) {
				return nullptr;
			}
			ID3D11ShaderResourceView* srv = nullptr;
			g_dev->CreateShaderResourceView(tex, nullptr, &srv);
			tex->Release();
			return srv;
		}

		void UpdateArt()
		{
			std::vector<std::uint8_t> px;
			int                       w = 0, h = 0;
			if (!Media::TakeArt(g_artVersion, px, w, h)) {
				return;
			}
			if (g_art) {
				g_art->Release();
				g_art = nullptr;
			}
			if (!px.empty() && w > 0 && h > 0) {
				g_art = MakeSRV(px.data(), w, h);
			}
		}
	}

	// ==================================================================== public
	void LoadSettings()
	{
		std::error_code ec;
		g_iniPath = fs::absolute(fs::path(L"Data\\F4SE\\Plugins\\Holosong.ini"), ec).string();
		for (int i = 0; i < kKeyCount; ++i) {
			cfg.keyVk[i] = ReadInt(kKeySection[i], kKeyIni[i], kKeyDefault[i], 0, 255);
		}
		RefreshKeyCodes();
		cfg.cursorHold = ReadBool("General", "bCursorHold", false);
		cfg.visible = ReadBool("General", "bVisible", true);
		cfg.mouseSpeed = ReadFloat("General", "fMouseSpeed", 1.0f, 0.1f, 10.0f);
		cfg.uiScale = ReadFloat("General", "fUIScale", 1.0f, 0.6f, 2.0f);
		cfg.fontSize = ReadFloat("General", "fFontSize", 18.0f, 10.0f, 40.0f);
		cfg.posX = ReadFloat("Widget", "fPosX", 0.70f, 0.0f, 1.0f);
		cfg.posY = ReadFloat("Widget", "fPosY", 0.06f, 0.0f, 1.0f);
		cfg.w = ReadFloat("Widget", "fWidth", kDefW, kMinW, kMaxW);
		cfg.h = ReadFloat("Widget", "fHeight", kDefH, kMinH, kMaxH);
		cfg.scalePct = ReadInt("Widget", "iScale", 100, 50, 100);
		cfg.opacityPct = ReadInt("Widget", "iOpacity", 95, 30, 100);
		cfg.controlsOpacityPct = ReadInt("Widget", "iCoverButtonsOpacity", 100, 10, 100);
		cfg.colorMode = ReadInt("Look", "iColorMode", 1, 0, 3);
		cfg.customR = ReadInt("Look", "iCustomRed", 20, 0, 255);
		cfg.customG = ReadInt("Look", "iCustomGreen", 255, 0, 255);
		cfg.customB = ReadInt("Look", "iCustomBlue", 23, 0, 255);
		cfg.tintArt = ReadBool("Look", "bTintAlbumArt", true);
		ReadPrefsFile();
		cfg.showHeader = ReadBool("Widget", "bShowTitleBar", true);
		cfg.showArt = ReadBool("Widget", "bShowAlbumArt", true);
		cfg.showProgress = ReadBool("Widget", "bShowProgress", true);
		cfg.showSound = ReadBool("Widget", "bShowSound", true);
		cfg.locked = ReadBool("Widget", "bLocked", false);
		cfg.leaveCursorInCombat = ReadBool("Safety", "bLeaveCursorInCombat", true);
		cfg.noCursorInCombat = ReadBool("Safety", "bNoCursorInCombat", true);
		cfg.dragDelayMs = ReadInt("Safety", "iDragDelayMs", 150, 0, 2000);
		cfg.dragDistance = ReadInt("Safety", "iDragDistance", 6, 0, 100);
		cfg.autoHideInCombat = ReadBool("Hiding", "bAutoHideInCombat", false);
		cfg.showAgainAfter = ReadFloat("Hiding", "fShowAgainAfter", 2.0f, 0.0f, 60.0f);
		cfg.hideInMenus = ReadBool("Hiding", "bHideInMenus", true);
		cfg.hideWhenNoPlayer = ReadBool("Hiding", "bHideWhenNoPlayer", false);
		cfg.muteGameMusic = ReadBool("Sound", "bMuteGameMusic", true);
		cfg.muteRadio = ReadBool("Sound", "bMuteRadio", false);
		cfg.volumeStep = ReadInt("Sound", "iVolumeStep", 5, 1, 50);
		{
			char buf[64]{};
			::GetPrivateProfileStringA("Sound", "sMusicApp", "Spotify", buf, sizeof buf, g_iniPath.c_str());
			cfg.musicApp = buf;
			std::snprintf(g_appBuf, sizeof g_appBuf, "%s", buf);
		}
		cfg.anyPlayer = ReadBool("Sound", "bAnyPlayer", false);
		cfg.showFps = ReadBool("Debug", "bShowFps", false);
		dbg.hookPresent = ReadBool("Debug", "bHookPresent", true);
		dbg.inputSink = ReadBool("Debug", "bInputSink", true);
		dbg.logInput = ReadBool("Debug", "bLogInput", false);
		dbg.lowLevelHooks = ReadBool("Debug", "bLowLevelHooks", true);
		dbg.blockInput = ReadBool("Debug", "bBlockInput", true);
		spdlog::info("INI: {} ({})", g_iniPath, fs::exists(g_iniPath, ec) ? "found" : "NOT FOUND - using defaults");
		spdlog::info("Keys: show/hide {} ({}), cursor {} ({}), music app \"{}\"", KeyName(cfg.keyVk[kKeyToggle]), cfg.keyVk[kKeyToggle], KeyName(cfg.keyVk[kKeyCursor]),
			cfg.keyVk[kKeyCursor], cfg.musicApp);
	}

	const DebugSwitches& GetDebugSwitches() { return dbg; }

	bool InstallInput()
	{
		if (!dbg.inputSink) {
			spdlog::warn("Input DISABLED by bInputSink=0 - no keys will work");
			return false;
		}
		g_inputHooked = Input::Install(&OnInputEvent, dbg.lowLevelHooks);
		return g_inputHooked;
	}

	void OnDataLoaded()
	{
		Media::Start(MediaSettings());  // now, not at plugin load: Windows' media controls can stall that early in the game's start
		if (dbg.inputSink) {
			spdlog::info(SafeRegisterGameKeys() ? "Keys: listening to the game's own input events too" : "Keys: couldn't listen to the game's input events");
		}
		if (!Fenced([] { FindSoundCategories(); return true; }, "FindSoundCategories")) {
			spdlog::warn("Could not read the game's sound categories - muting the game's music is off");
			g_gameStateBroken = false;  // this one failing doesn't say anything about the menus / combat checks
		}
	}

	void OnGameLoaded()
	{
		g_gameLoaded = true;
		Input::Rehook(1);  // after every mod has set up its own hooks: ours in front
		spdlog::info("Game loaded. Low-level hooks: keyboard {} ({}), mouse {} ({}). DirectInput: keyboard {}, mouse {}",
			Input::LowLevelKeys() ? "on" : "off", Input::LowLevelKeySeen() ? "keys seen" : "no keys yet", Input::LowLevelMouse() ? "on" : "off",
			Input::LowLevelMouseSeen() ? "mouse seen" : "no mouse yet", Input::KeyboardSeen() ? "seen" : "not seen", Input::MouseSeen() ? "seen" : "not seen");
	}

	int SehFilter(EXCEPTION_POINTERS* a_info, const char* a_where)
	{
		const auto* rec = a_info ? a_info->ExceptionRecord : nullptr;
		const auto  addr = rec ? reinterpret_cast<std::uintptr_t>(rec->ExceptionAddress) : 0;
		HMODULE     mod = nullptr;
		wchar_t     modPath[MAX_PATH]{};
		if (addr && ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(addr), &mod)) {
			::GetModuleFileNameW(mod, modPath, MAX_PATH);
		}
		const auto offset = mod ? addr - reinterpret_cast<std::uintptr_t>(mod) : addr;
		spdlog::critical("CRASH CAUGHT in {}: exception 0x{:08X} at {}+0x{:X} - this part is now switched off so the game keeps running", a_where,
			rec ? static_cast<std::uint32_t>(rec->ExceptionCode) : 0u, ToUtf8(fs::path(modPath).filename().wstring()), offset);
		return EXCEPTION_EXECUTE_HANDLER;
	}

	void OnPresent(IDXGISwapChain* a_sc)
	{
		if (!g_sc) {
			DXGI_SWAP_CHAIN_DESC desc{};
			if (FAILED(a_sc->GetDesc(&desc)) || !desc.OutputWindow) {
				return;
			}
			if (FAILED(a_sc->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_dev))) || !g_dev) {
				return;
			}
			g_dev->GetImmediateContext(&g_ctx);
			g_sc = a_sc;
			spdlog::info("Attached to the game's swap chain ({}x{})", desc.BufferDesc.Width, desc.BufferDesc.Height);
		}
		if (a_sc != g_sc) {
			return;
		}

		PollKeys();

		const auto now = std::chrono::steady_clock::now();
		struct CostTimer  // how long this call takes, for the frame-rate readout
		{
			std::chrono::steady_clock::time_point t0;
			~CostTimer()
			{
				const float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
				g_costMs = g_costMs <= 0.0f ? ms : g_costMs + (ms - g_costMs) * 0.05f;
			}
		} costTimer{ now };
		float      dt = g_lastFrame.time_since_epoch().count() ? std::chrono::duration<float>(now - g_lastFrame).count() : 1.0f / 60.0f;
		g_lastFrame = now;
		dt = std::clamp(dt, 1.0f / 1000.0f, 0.5f);
		g_time += dt;
		g_fpsAvg = g_fpsAvg <= 0.0f ? 1.0f / dt : g_fpsAvg + (1.0f / dt - g_fpsAvg) * 0.05f;

		// ---- game state
		const bool paused = GamePaused();
		const bool combat = g_gameLoaded && PlayerInCombat();
		g_pausedInput = paused;
		g_inCombatInput = combat;
		if (g_wasInCombat && !combat) {
			g_combatEndedAt = g_time;
		}
		const bool combatStarted = combat && !g_wasInCombat;
		g_wasInCombat = combat;

		if (g_toggleVisible.exchange(false)) {
			cfg.visible = !cfg.visible;
			if (!cfg.visible) {
				g_wantCursor = false;
			}
			MarkDirty();
		}
		if (g_wantCursor && !cfg.visible) {
			cfg.visible = true;  // the cursor key brings a hidden widget back
			MarkDirty();
		}
		if (paused || !g_gameLoaded) {
			g_wantCursor = false;
		}
		if (combatStarted && cfg.leaveCursorInCombat && g_wantCursor) {
			g_wantCursor = false;
			Notify("Holosong: combat - cursor mode off");
		}
		if (g_wantCursor != g_cursor) {
			ApplyCursor(g_wantCursor);
		}

		const Media::Snapshot m = Media::Get();

		// The game's music / radio: silent while the app plays (normal again in the pause menu, where the audio settings are)
		{
			const bool playing = g_gameLoaded && m.hasSession && m.playing && !PauseMenuOpen();
			SetMuted(g_music, cfg.muteGameMusic && playing);
			SetMuted(g_radio, cfg.muteRadio && playing);
		}

		// ---- how visible
		bool show = g_gameLoaded && cfg.visible;
		if (cfg.hideInMenus && paused) {
			show = false;
		}
		if (!g_cursor) {
			if (cfg.autoHideInCombat && (combat || g_time - g_combatEndedAt < cfg.showAgainAfter)) {
				show = false;
			}
			if (cfg.hideWhenNoPlayer && !m.hasSession) {
				show = false;
			}
		}
		if (g_cursor) {
			show = true;
		}
		const float target = show ? 1.0f : 0.0f;
		g_alpha += (target - g_alpha) * (1.0f - std::exp(-dt * 10.0f));
		if (std::fabs(target - g_alpha) < 0.01f) {
			g_alpha = target;
		}

		// save settings a moment after the last change (not in the middle of a drag)
		if (g_dirty && g_drag.mode == Grab::None && g_time - g_dirtyAt > 0.5f) {
			g_dirty = false;
			SaveAllAsync();
		}
		// a key chosen in the settings window
		if (const int slot = g_captureSlot; slot >= 0) {
			if (const int vk = g_capturedVk.exchange(0); vk != 0) {
				if (vk > 0) {
					for (int i = 0; i < kKeyCount; ++i) {  // one key, one job
						if (i != slot && cfg.keyVk[i] == vk) {
							cfg.keyVk[i] = 0;
							g_status = std::format("{} was used for \"{}\" - that one has no key now", KeyName(vk), kKeyLabel[i]);
						}
					}
					cfg.keyVk[slot] = vk;
				} else if (vk == -2) {
					cfg.keyVk[slot] = 0;
				}
				g_captureSlot = -1;
				RefreshKeyCodes();
				MarkDirty();
			}
		}

		if (g_alpha <= 0.0f && !g_cursor) {
			std::lock_guard lk(g_inputMtx);
			g_input.clear();
			if (!cfg.showFps || !g_gameLoaded) {
				return;
			}
		}
		if (g_imguiFailed) {
			return;
		}

		ID3D11Texture2D* back = nullptr;
		if (FAILED(a_sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) || !back) {
			return;
		}
		D3D11_TEXTURE2D_DESC bd{};
		back->GetDesc(&bd);
		const bool sizeChanged = std::fabs(g_displayH - static_cast<float>(bd.Height)) > 0.5f || std::fabs(g_displayW - static_cast<float>(bd.Width)) > 0.5f;
		g_displayW = static_cast<float>(bd.Width);
		g_displayH = static_cast<float>(bd.Height);
		if (!g_imguiReady) {
			if (!InitImGui()) {
				g_imguiFailed = true;
				spdlog::error("ImGui failed to initialise");
				back->Release();
				return;
			}
			g_imguiReady = true;
		} else if (sizeChanged) {
			g_pendingFonts = true;
		}
		if (g_pendingFonts && g_drag.mode == Grab::None && !ImGui::IsAnyItemActive()) {
			g_pendingFonts = false;
			ApplyFonts();
		}

		auto& io = ImGui::GetIO();
		io.DisplaySize = ImVec2(g_displayW, g_displayH);
		io.DeltaTime = dt;
		io.MouseDrawCursor = g_cursor;
		g_k = (std::max)(1.0f, g_displayH / 1080.0f) * cfg.scalePct / 100.0f;

		static bool lastCursor = false;
		if (g_cursor && !lastCursor) {  // cursor starts in the middle of the widget
			g_mouseX = std::clamp(cfg.posX * g_displayW + cfg.w * g_k * 0.5f, 0.0f, g_displayW - 1.0f);
			g_mouseY = std::clamp(cfg.posY * g_displayH + cfg.h * g_k * 0.5f, 0.0f, g_displayH - 1.0f);
			io.AddMousePosEvent(g_mouseX, g_mouseY);
		}
		if (!g_cursor && lastCursor) {
			io.ClearInputKeys();  // nothing stays held for next time
			io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
			for (int b = 0; b < 5; ++b) {
				io.AddMouseButtonEvent(b, false);
			}
		}
		const bool lastCursorBeforeKey = lastCursor;  // was the cursor already up before this frame?
		lastCursor = g_cursor;
		if (g_cursor) {
			DrainInput();
			if (g_settingsKey.exchange(false)) {
				if (g_settingsOpen) {
					g_settingsOpen = false;
					if (g_cursorForSettings) {
						g_wantCursor = false;  // the cursor came up for the settings: it goes with them
					}
					g_cursorForSettings = false;
				} else {
					g_cursorForSettings = !lastCursorBeforeKey;
					g_settingsOpen = true;
				}
			}
			if (g_undoRequest.exchange(false)) {
				Undo();
			}
			if (g_escRequest.exchange(false)) {
				if (g_settingsOpen) {
					g_settingsOpen = false;
				} else {
					g_wantCursor = false;
				}
			}
		} else {
			g_undoRequest = false;
			g_escRequest = false;
			g_wheelFrame = 0.0f;
		}
		UpdateArt();
		UpdateTint();
		g_keepColour.clear();

		ImGui_ImplDX11_NewFrame();
		ImGui::NewFrame();
		if (g_alpha > 0.0f || g_cursor) {
			DrawWidget(m);
		}
		if (cfg.showFps) {  // diagnostics: top-left corner
			const std::string t = std::format("{:.0f} fps  \xC2\xB7  frame {:.1f} ms  \xC2\xB7  Holosong {:.2f} ms", g_fpsAvg, g_fpsAvg > 0.0f ? 1000.0f / g_fpsAvg : 0.0f, g_costMs);
			auto*             fg = ImGui::GetForegroundDrawList();
			const float       ts = Theme::S(14.0f);
			const ImVec2      ts2 = Theme::Body()->CalcTextSizeA(ts, FLT_MAX, 0.0f, t.c_str());
			const ImVec2      p(Theme::S(12.0f), Theme::S(12.0f));
			fg->AddRectFilled(p, p + ts2 + ImVec2(Theme::S(16.0f), Theme::S(10.0f)), IM_COL32(10, 10, 12, 200), Theme::S(6.0f));
			fg->AddText(Theme::Body(), ts, p + ImVec2(Theme::S(8.0f), Theme::S(5.0f)), IM_COL32(238, 234, 226, 255), t.c_str());
		}
		if (g_settingsOpen && g_cursor) {
			DrawSettings(m);
		}
		if (!g_settingsOpen) {
			g_status.clear();
		}
		ImGui::Render();
		Recolour(ImGui::GetDrawData());

		ID3D11RenderTargetView* rtv = nullptr;
		g_dev->CreateRenderTargetView(back, nullptr, &rtv);
		back->Release();
		if (rtv) {
			g_ctx->OMSetRenderTargets(1, &rtv, nullptr);
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
			rtv->Release();
		}
	}
}
