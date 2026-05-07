# WolknerProtocol

> Live-state editor for **Suzerain** by Torpor Games. Read and write any of the game's 400+ Lua variables while you play. Cross-platform native — Linux + Windows. Built in C++20 with Qt6.

<p align="center">
  <img src="resources/demo.gif" alt="WolknerProtocol demo" width="700">
</p>

<p align="center">
  <i>Named for the President's quietest, most loyal driver.</i>
</p>

---

## What this is

A native desktop tool that attaches to a running `Suzerain.exe` process, walks its IL2CPP heap to discover the live Lua variable table, and lets you read or rewrite any value through a polished GUI. No save-file editing, no game restarts — values change in real time, both directions.

It is **not** a generic memory scanner. The tool understands Suzerain's data model (PixelCrushers Dialogue System on a custom Lua VM) and follows the actual indirection chain the game uses, so values stay accurate even when Suzerain swaps out the underlying objects mid-play.

## Features

- **428+ live variables** auto-discovered on attach — every numeric flag, counter, opinion, and resource the game tracks
- **1 Hz polling** — the table and the hero strip tick automatically as the game changes values
- **Editable in place** — double-click any value, type a new number, hit Enter
- **Campaign-aware hero strip** — Sordland shows Budget · Wealth; Rizia shows Authority · Budget · Energy
- **Smart filter** — search by name (`budget`, `opinion`, `unrest`) or by value comparison (`=5`, `>10`, `!=0`, `<=15`)
- **Manual sort** — click a column to sort, rows hold their position afterwards (live updates never reshuffle the table)
- **Frameless window** — custom drag, minimise, close. Rounded corners, dark slate palette, fade-in animation, sliding mode toggle
- **Self-healing memory tracking** — survives Suzerain's Lua GC swapping `LuaNumber` instances on every value change

## How it works

This was the interesting part. Brief tour:

1. **Reverse-engineered Suzerain's data model** with [Cpp2IL](https://github.com/SamboyCoding/Cpp2IL), which dumps IL2CPP metadata into readable C# pseudocode. Found that Torpor Games uses [PixelCrushers Dialogue System](https://www.pixelcrushers.com/dialogue-system/), which embeds a custom Lua VM in the `Language.Lua` namespace.
2. **Detected the `LuaNumber` class pointer** at runtime by sampling double-valued objects on the heap and clustering by their type pointer. The most common type pointer is the `LuaNumber` class.
3. **Located dict entries** by scanning writable memory for adjacent `(key_string_pointer, LuaNumber_pointer)` pairs. Resolved the key strings (matching known prefixes like `BaseGame.*`, `RiziaDLC.*`) on the fly through System.String or `LuaString` indirection.
4. **Stored the dict entry's value-slot address** rather than the LuaNumber address. The slot is stable across the game's lifetime (Boehm GC is non-moving and the dict's entry array never relocates). Each read/write does a two-hop dereference: `slot → current LuaNumber → double`. This is what makes values stay live across in-game state changes — Suzerain replaces `LuaNumber` instances when values mutate, so caching their addresses directly would go stale.

The result: a 30 MB native binary that reliably tracks ~428 live variables, all of which round-trip through whatever IL2CPP, the Lua VM, and the game's own write-back logic happen to do at any given moment.

## Build

### Linux (native)

Requires Qt6 (≥ 6.2), CMake 3.20+, GCC 11+ or Clang 13+.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo setcap cap_sys_ptrace+ep build/wolknerprotocol   # one-time, lets it ptrace
./build/wolknerprotocol
```

### Windows (cross-compile from Linux)

The repo ships a CMake toolchain file for `mingw-w64`. Pull Qt for both targets via [`aqtinstall`](https://github.com/miurahr/aqtinstall):

```bash
sudo apt install mingw-w64 zip
python3 -m venv ~/aqt-env
~/aqt-env/bin/pip install aqtinstall
~/aqt-env/bin/aqt install-qt windows desktop 6.5.3 win64_mingw -O ~/qt-win
~/aqt-env/bin/aqt install-qt linux   desktop 6.5.3 gcc_64      -O ~/qt-host

cmake -B build-win \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64.cmake \
    -DCMAKE_PREFIX_PATH=$HOME/qt-win/6.5.3/mingw_64 \
    -DQT_HOST_PATH=$HOME/qt-host/6.5.3/gcc_64 \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-win -j$(nproc)
```

Bundle the runtime DLLs alongside the .exe (Qt6Core/Gui/Widgets, the MinGW runtime, and the `platforms/qwindows.dll` plugin) — see `dist-win/` in the release artifacts for the layout.

### Windows (native)

Install Qt6 + MinGW via the Qt online installer, then:

```cmd
cmake -B build -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=C:/Qt/6.5.3/mingw_64
cmake --build build -j
windeployqt build\wolknerprotocol.exe
```

## Usage

1. Launch Suzerain (Steam works fine, native or Proton).
2. Get past the main menu — load a save or start a new campaign.
3. Run WolknerProtocol.
4. Click **Attach**. The first scan takes ~20 s while it walks the heap.
5. Toggle **Sordland** ↔ **Rizia** to focus the hero strip on the relevant resources for your campaign.
6. Double-click any value — in the hero strip or in the variable table — to edit it.

The status bar shows `◆ Connected · PID … · N variables`. Refresh re-walks the heap in case the game has been heavily restructured (rare).

## Caveats

- **Single-player only.** Suzerain has no multiplayer or anti-cheat. This tool exists for personal enjoyment of a story-driven game; please don't repurpose it for online cheating elsewhere.
- **Defender will probably flag the Windows binary** as a HackTool. Whitelist the folder if you trust it. Linux package managers leave it alone.
- **Linux requires either `cap_sys_ptrace` on the binary or running as root** — the kernel's default `ptrace_scope=1` policy blocks reading other processes' memory otherwise.
- **Tested against Suzerain 3.1.0.1.153** with Unity 6 + IL2CPP. Major game updates that change the data model would require re-tuning the heuristics; the auto-detect is robust within version families but isn't magic.

## Repo layout

```
suzerain-qt/
├── CMakeLists.txt
├── cmake/
│   └── toolchain-mingw-w64.cmake     ← Linux→Windows cross-compile recipe
├── resources/
│   ├── fonts.qrc                     ← Qt resource manifest
│   ├── fonts/                        ← Embedded Inter + JetBrains Mono
│   ├── icons/serge.png / serge.ico   ← Window/taskbar/.exe icons
│   └── wolknerprotocol.rc            ← Win32 resource file (icon + version)
└── src/
    ├── main.cpp                      ← QApplication + global QSS theme
    ├── MainWindow.{cpp,h}            ← Frameless GUI, hero strip, table, animations
    ├── ProcessMem.{cpp,h}            ← Cross-platform memory backend
    └── Scanner.{cpp,h}               ← LuaNumber detection + dict-entry enumeration
```

## Credits

- **Game**: [Suzerain](https://torporgames.com/) by Torpor Games.
- **Serge Wolkner portrait**: [Suzerain Wiki](https://suzerain.wiki.gg/wiki/Serge_Wolkner).
- **Fonts**: [JetBrains Mono](https://github.com/JetBrains/JetBrainsMono) (OFL), [Inter](https://github.com/rsms/inter) (OFL).
- **IL2CPP dumping**: [Cpp2IL](https://github.com/SamboyCoding/Cpp2IL) by SamboyCoding.
- Built by [wleeaf](https://wleeaf.dev).

## License

MIT — see [LICENSE](LICENSE).

This project is not affiliated with or endorsed by Torpor Games. Suzerain is the property of Torpor Games.
