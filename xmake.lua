-- Holosong: an in-game music widget for Fallout 4 (F4SE). Controls Spotify and other players through
-- Windows' media controls. One DLL for Old-gen (1.10.163), Next-gen (1.10.984) and Anniversary (1.11.x).
-- Built against Dear-Modding CommonLibF4 (lib/commonlibf4).
includes("lib/commonlibf4")

set_project("Holosong")
set_version("0.1.3")
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")
set_encodings("utf-8")
set_allowedarchs("windows|x64")
set_allowedmodes("debug", "releasedbg")
set_defaultarchs("windows|x64")
set_defaultmode("releasedbg")

add_rules("mode.debug", "mode.releasedbg")

-- REL::ID / REL::Offset initializer slots are [OG, NG, AE].
add_defines("COMMONLIB_RUNTIMECOUNT=3")

add_requires("imgui v1.90", { configs = { dx11 = true } })
add_requires("stb 2024.06.01")

local common_cxxflags = {
    "/permissive-",
    "/Zc:preprocessor",
    "/EHsc",
    "/wd4100", -- unreferenced parameter
    "/wd4189", -- local variable initialized but not referenced
    "/wd4244", -- conversion, possible loss of data
    "/wd4267", -- size_t conversion
    "/wd4996", -- deprecated CRT functions
    "/wd5054"  -- enum bitwise ops
}

-- Windows media controls (C++/WinRT) and DirectInput: real Windows headers, no CommonLib in sight,
-- so these two files are built on their own without the precompiled header.
target("HolosongWin", function()
    set_kind("static")
    set_default(false)
    add_files("src/Media.cpp", "src/Input.cpp")
    add_headerfiles("src/Media.h", "src/Input.h")
    add_includedirs("src")
    add_packages("spdlog", "stb")
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS", "_WIN32_WINNT=0x0A00", "WINVER=0x0A00")
    add_cxxflags(common_cxxflags)
    add_cxxflags("/bigobj")
    set_runtimes("MD")
    set_symbols("debug")
    set_optimize("fastest")
end)

target("Holosong", function()
    add_rules("commonlibf4.plugin", {
        name = "Holosong",
        author = "evodube",
        description = "In-game music widget: control Spotify and other players",
        plugin_template = path.join(os.projectdir(), "res/commonlibf4-plugin.cpp.in"),
    })

    -- don't run `xmake install` after every build (CI has no game folder)
    set_values("commonlib.plugin.install", false)

    add_deps("HolosongWin")
    add_packages("imgui")

    add_files("src/main.cpp", "src/Widget.cpp", "src/Theme.cpp")
    add_headerfiles("src/Widget.h", "src/Theme.h", "src/PCH.h")
    add_includedirs("src")

    add_defines("NOMINMAX", "_CRT_SECURE_NO_WARNINGS", "_WIN32_WINNT=0x0A00", "WINVER=0x0A00")
    add_cxxflags(common_cxxflags)
    add_cxxflags("/Zi")

    add_syslinks("d3d11", "dxgi", "dxguid", "ole32", "shell32", "user32", "kernel32", "uuid", "windowsapp")

    set_pcxxheader("src/PCH.h")
    set_runtimes("MD")
    set_symbols("debug")
    set_optimize("fastest")
    set_targetdir("build/out/F4SE/Plugins")
end)
