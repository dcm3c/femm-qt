# luafemm -- Lua 4 Client for femm-qt

A pure-Lua scripting client that drives femm-qt over its TCP/IPC protocol.
Peer to `juliafemm`, `pyfemm`, `octavefemm`, `mathfemm`, and `scifemm`.

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

If `femm_gui`/`femm_headless` aren't on your PATH, set the `FEMM_DIR`
environment variable to the directory containing them:

```bash
set FEMM_DIR=C:\path\to\femm-qt\build\bin
```

The client also looks in `../build/bin/` relative to `femm.lua`.

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
    README.md
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
