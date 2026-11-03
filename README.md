# Holosong

An in-game music widget for Fallout 4 (F4SE plugin) - the Fallout 4 port of Hearthsong. One DLL for Old-gen, Next-gen and Anniversary.

It controls the Spotify desktop app through Windows' own media controls (the ones behind your keyboard's
media keys), so it needs no Spotify login, no developer key and no internet calls, and it works with free
Spotify too. Spotify has to be open on the same PC.

## Requirements

- Fallout 4 **Old-gen 1.10.163**, **Next-gen 1.10.984** or **Anniversary 1.11.x** - one DLL for all of them
- F4SE for your game version
- Address Library for F4SE Plugins (the file for your game version)
- Windows 10 version 1809 or newer, or Windows 11
- The Spotify desktop app (other players work too, see `sMusicApp`)

## Using it

| Key | What it does |
| --- | --- |
| **Page Down** | Show / hide the widget |
| **Page Up** | Cursor mode: a cursor to click, drag and resize the widget (Esc or Page Up again puts it away) |
| *(none by default)* | Settings window: the gear on the widget, or the bar under it in cursor mode (a key can be set) |
| **Ctrl + Z** | In cursor mode: undo the last move or resize |

Every key can be changed in the settings (the gear on the widget) or in `Holosong.ini`. Optional
play/pause, next, previous and volume keys work without the cursor.

- **While playing** (no cursor) the widget shows just the music: no settings/close buttons and no volume section,
  and it's that much shorter. They come back in cursor mode.
- **Move:** in cursor mode, drag anywhere on the widget (except a button or slider).
- **Resize:** drag any edge or corner. Wide and short becomes a horizontal bar, tall and narrow a portrait
  layout. Parts that don't fit (volume, album art) step aside instead of being cut off.
- **Scale (50 - 100 %) / opacity / parts:** in the settings window, or Ctrl + mouse wheel over the widget for the scale.

### Safeguards against moving it by accident

- Outside cursor mode the widget ignores the mouse completely: clicks go to the game.
- Buttons and sliders never start a drag: pressing one only does what the button does.
- It only moves after the button is held for a moment (150 ms) **and** the mouse has travelled a few pixels.
- Combat: cursor mode switches off when a fight starts, and can't be switched on during one.
- Lock position and size, undo (Ctrl + Z and a button in the settings), and it always stays on screen,
  also after a resolution change.
- Optional: fade out during combat.

## Pip-Boy look

Holosong draws itself in one colour, like the Pip-Boy's screen, in the game's interface typeface (Roboto Condensed),
with square corners. By default it follows the **HUD colour** you picked in the game's Settings > Display, live; it can
follow the **Pip-Boy colour** instead, use a **custom** colour, or go back to the **original gold** look
(`[Look]` in `Holosong.ini`, or Settings > Layout > Colour). The album cover can be tinted too, or keep its own colours.

## How it works on every game version

- **Keys, cursor and input blocker:** read from DirectInput 8, where Fallout 4 reads the keyboard and mouse. No game
  addresses, so it's the same on every version.
- **Drawing:** the game's swap chain `Present` (found through the renderer; if that can't be read, through a throwaway
  swap chain of its own).
- **Game state** (menus, combat, the music and radio volume): CommonLibF4 with Address Library. Every read is fenced:
  if one ever fails, only that feature switches off and Holosong.log says so.

## Building

GitHub Actions builds it on every push to `main` (Visual Studio 2022, xmake, Dear-Modding CommonLibF4 as a submodule).
The packaged mod is published to the `dist` branch and as a workflow artifact.

Fonts: Cinzel, Inter and Roboto Condensed (SIL Open Font License, see `assets/fonts`).

## Licence

Copyright (C) 2026 evodube

Holosong is free software: you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
[LICENSE.txt](LICENSE.txt). The Modding Exception and GPL-3.0 Linking Exception from CommonLibF4
apply ([EXCEPTIONS.txt](EXCEPTIONS.txt), from CommonLibF4). Third-party components: [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt).
