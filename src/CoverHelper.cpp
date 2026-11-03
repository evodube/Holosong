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

// HolosongCover.exe - fetches the album cover of the music app's current song from Windows' media controls and writes
// it to a file. Holosong runs it (hidden) when the song changes: inside Fallout 4's process the cover transfer never
// finishes (song details do), in a clean little process of its own it does.
//
//   HolosongCover.exe <app id, e.g. Spotify.exe> <output file>
//
// Writes <output file> (the picture as the app gives it, PNG/JPEG) and <output file>.txt (the song's title, so
// Holosong can tell which song it belongs to). Exit code: 0 ok, 2 app not in the media controls, 3 no picture,
// 4 Windows error, 1 bad arguments.

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <string>
#include <vector>

namespace wmc = winrt::Windows::Media::Control;
namespace wss = winrt::Windows::Storage::Streams;

namespace
{
	std::wstring Lower(std::wstring a_s)
	{
		std::transform(a_s.begin(), a_s.end(), a_s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
		return a_s;
	}

	int Run(const std::wstring& a_app, const std::wstring& a_out)
	{
		auto mgr = wmc::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
		if (!mgr) {
			return 4;
		}
		const std::wstring                             needle = Lower(a_app);
		wmc::GlobalSystemMediaTransportControlsSession session{ nullptr };
		for (const auto& s : mgr.GetSessions()) {  // the same app Holosong controls: exact id first, then part of it
			if (Lower(std::wstring(s.SourceAppUserModelId())) == needle) {
				session = s;
				break;
			}
		}
		if (!session) {
			for (const auto& s : mgr.GetSessions()) {
				if (!needle.empty() && Lower(std::wstring(s.SourceAppUserModelId())).find(needle) != std::wstring::npos) {
					session = s;
					break;
				}
			}
		}
		if (!session && needle.empty()) {
			session = mgr.GetCurrentSession();
		}
		if (!session) {
			return 2;
		}
		const auto props = session.TryGetMediaPropertiesAsync().get();
		const auto thumb = props.Thumbnail();
		if (!thumb) {
			return 3;
		}
		const auto stream = thumb.OpenReadAsync().get();
		const auto size = static_cast<std::uint32_t>((std::min)(stream.Size(), static_cast<std::uint64_t>(16u << 20)));
		if (size == 0) {
			return 3;
		}
		wss::DataReader reader(stream.GetInputStreamAt(0));
		const auto      got = reader.LoadAsync(size).get();
		std::vector<std::uint8_t> bytes(got);
		reader.ReadBytes(bytes);
		if (bytes.empty()) {
			return 3;
		}
		// the title first (written as UTF-8), then the picture: Holosong waits for the picture file
		{
			const std::wstring t(props.Title());
			std::string        u8(static_cast<std::size_t>(::WideCharToMultiByte(CP_UTF8, 0, t.c_str(), static_cast<int>(t.size()), nullptr, 0, nullptr, nullptr)), '\0');
			::WideCharToMultiByte(CP_UTF8, 0, t.c_str(), static_cast<int>(t.size()), u8.data(), static_cast<int>(u8.size()), nullptr, nullptr);
			std::ofstream(a_out + L".txt", std::ios::binary | std::ios::trunc) << u8;
		}
		const std::wstring tmp = a_out + L".part";
		{
			std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
			f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			if (!f) {
				return 4;
			}
		}
		return ::MoveFileExW(tmp.c_str(), a_out.c_str(), MOVEFILE_REPLACE_EXISTING) ? 0 : 4;
	}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	int    argc = 0;
	LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
	if (!argv || argc < 3) {
		return 1;
	}
	const std::wstring app = argv[1], out = argv[2];
	::LocalFree(argv);
	try {
		winrt::init_apartment(winrt::apartment_type::multi_threaded);
		return Run(app, out);
	} catch (...) {
		return 4;
	}
}
