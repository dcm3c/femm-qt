# luafemm -- Lua 4 Client for femm-qt

A pure-Lua scripting client that drives femm-qt over its TCP/IPC protocol.
Peer to `juliafemm`, `pyfemm`, `octavefemm`, `mathfemm`, and `scifemm`.

> **Just want to run a script or get a Lua prompt?** Use the bundled
> `femm-lua` launcher (`femm-lua.bat` on Windows, `femm-lua.sh` elsewhere) and
> see **[femm-lua.md](femm-lua.md)** for all of its options and invocation
> forms. The rest of this file documents the `femm.lua` module directly, for
> when you embed it in your own Lua program.

## Requirements

- Lua 4
- LuaSocket (`luarocks install luasocket`, or manual install)
- femm-qt (GUI or headless build)

## Install

Drop the `luafemm/` directory somewhere and add it to your Lua path:

```bash
export LUA_PATH="/path/to/luafemm/?.lua;;"
```

Or prepend at script start:

```lua
package.path = "/path/to/luafemm/?.lua;" .. package.path
dofile("/path/to/luafemm/femm.lua")
```

## Quick Start

```lua
dofile("femm.lua")

openfemm()                              -- connect (or launch femm_gui)
newdocument(0)                          -- new magnetics problem
mi_drawrectangle(0, 0, 10, 10)         -- draw a square
mi_saveas("test.fem")
mi_close()
closefemm()                             -- disconnect (server exits)
```

## Connection Model

- `openfemm()` or `openfemm(0)` -- latch onto a running FEMM on port 48015,
  or launch `femm_gui` if nothing's there.
- `openfemm(1)` -- same latch-or-spawn, but spawns `femm_headless` instead
  of the GUI if nothing is running.
- `closefemm()` -- closes the TCP socket. femm-qt sees the disconnect and
  exits automatically.

The latch step doesn't care whether the already-running process is GUI or
headless; whatever's serving the port is used.

## FEMM_DIR

When you call `openfemm()` and no femm-qt is already running, the module
launches one from the `FEMM_DIR` directory. Set `FEMM_DIR` to the folder that
holds `femm_gui` / `femm_headless`:

```bash
set FEMM_DIR=C:\path\to\femm-qt\build\bin
```

On macOS an installed `.app` has no `bin/`; point at the bundle instead:

```bash
export FEMM_DIR=/Applications/femm_gui.app/Contents/MacOS
```

The binding files themselves ship **inside** the bundle on macOS, since a
distributed `.app` has no sibling folders to put them in:

```
/Applications/femm_gui.app/Contents/Resources/bindings/luafemm
```

The examples are alongside them, in `Contents/Resources/examples/` -- but those
are read-only **masters**. Use the `Examples` folder the DMG carries next to the
app: drag it somewhere of your own and work there. Example scripts need that,
not just convenience: the FEMM idiom is `open("model.fem")` followed by
`mi_saveas("temp.fem")` so the original is never modified, and both paths are
relative to the script's own directory. Inside the read-only bundle the save
fails and the script stops.

(The `femm-lua` launcher sets this for you automatically — it only matters when
you load `femm.lua` into your own Lua program. See **[femm-lua.md](femm-lua.md)**.)

## Single-Client Note

femm-qt accepts one client connection at a time. If another binding
(e.g. an octavefemm session) is already connected, `openfemm()` will fail
with: `error:femm-qt is already connected to another client`.
Close the other client first.

## File Structure

```
luafemm/
    femm.lua            main module (transport + all wrappers)
    femm_init.lua       units, constants, I/O helpers
    femm-lua.bat        Windows launcher (interpreter + binding + connect)
    femm-lua.sh         macOS / Linux / FreeBSD launcher
    README.md
    femm-lua.md         launcher guide (options + invocation forms)
    examples/
        hello_femm.lua
        coilgun.lua
        ac_magnetics.lua
    tests/
        test_runner.lua
        test_complex.lua
        test_compat4.lua
        test_transport.lua
```

## Running Tests

```bash
cd luafemm/tests
lua test_runner.lua
```

## Future Work

- Multi-instance support (multiple ports, `femmselect()`).
