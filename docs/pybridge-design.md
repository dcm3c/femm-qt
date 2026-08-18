# PyBridge: driving CPython packages from femm-qt scripts

femm-qt scripts can use `matplotlib` — and any other package installed in the
user's own CPython — by proxying object access to a separate process over a
localhost socket. femm-qt never links or ships those packages.

```python
import matplotlib.pyplot as plt
fig, ax = plt.subplots()
ax.plot(x, np.sin(x))          # x is a ulab array and stays one
plt.show()
```

**This document is the design and the wire protocol.** The user-facing guide is
chapter 4 of the manual, "Plotting with matplotlib"; this is for anyone
changing the bridge itself. Section 2 is a specification: three separate
codebases — the CPython worker, the MicroPython client and the C++ host —
implement it independently, so a change there is a change to a contract.

Built and in service on Windows, and **verified on Ubuntu 24.04** — it builds,
runs, and plots there. macOS is the one platform still unexercised; section 11
says which two lines to watch, since Linux compiles neither of them.

---

**Goal:** Allow femm-qt's embedded MicroPython scripting console to use matplotlib
by transparently proxying object access to a resident CPython process.
(femm-qt already provides numpy/scipy-style numerics natively via ulab; this
bridge is for matplotlib, and the ulab↔bridge boundary is addressed in §5.4.) From the user's point of view:

```python
import matplotlib.pyplot as plt
fig, ax = plt.subplots()
ax.plot([0, 1, 2], [0, 1, 4], 'r-')
ax.set_xlabel("x")
plt.show()
```

...just works, with a real, fully interactive matplotlib window (zoom, pan,
save toolbar) rendered by the user's own CPython installation. femm-qt never
links against or ships matplotlib; the worker is a separate process talking
over a localhost TCP socket, consistent with femm-qt's existing IPC-only
architecture.

**Deliberately out of scope:**

- Reimplementing any part of the matplotlib API surface. This is a *generic*
  object proxy; matplotlib support falls out of it.
- Callbacks crossing the boundary (pick events, widgets, `FuncAnimation` with
  MicroPython-side callables). Document as unsupported.
- Embedding figures inside femm-qt widgets. The worker owns its windows.
- Bundling a Python distribution. We discover the user's Python.

---
---

## 1. Architecture Overview

```
┌────────────────────────── femm-qt GUI process ──────────────────────────┐
│                                                                          │
│  MicroPython VM                                                          │
│  ┌──────────────────────────────┐                                        │
│  │ matplotlib/ (shims)  + ulab  │   frozen/filesystem .py packages       │
│  │        │                     │                                        │
│  │   pybridge.py                │   Proxy class, codec, public API       │
│  │        │                     │                                        │
│  │   _pybridge (C module)       │   blocking rpc(bytes)->bytes           │
│  └────────┼─────────────────────┘                                        │
│           │                                                              │
│  C++ pybridge_host in libfemm (sockets + spawn, no Qt; discovery,        │
│  lifecycle) -- see §4.1                                                  │
└───────────┼──────────────────────────────────────────────────────────────┘
            │  localhost TCP, length-prefixed frames, token-authenticated
            ▼
┌─────────────────── CPython worker process (user's Python) ──────────────┐
│  femmqt_pybridge_worker.py  (single file, stdlib + numpy/matplotlib)     │
│  - object handle table {int -> PyObject}                                 │
│  - RPC dispatch (import/getattr/call/getitem/binop/eval/...)             │
│  - single-threaded select() loop interleaved with GUI event pumping      │
│  - matplotlib in interactive (ion) mode, qtagg/macosx/tkagg backend      │
└───────────────────────────────────────────────────────────────────────────┘
```

Key design decisions (rationale in the sections below):

1. **Handle-based transparent proxying**, not an API shim. One `_Proxy`
   class with `__getattr__`/`__call__`/dunder forwarding covers the entire
   matplotlib (and numpy) API without enumerating it.
2. **Single-threaded worker.** The worker's main thread runs a
   `select()`-with-timeout loop, dispatching RPCs synchronously and pumping
   GUI events between them. No threads, no cross-thread marshaling, and the
   macOS "GUI on main thread" requirement is satisfied by construction.
3. **Server-side data residency.** Arrays returned by CPython calls stay in
   the worker as handles by default. Big data only crosses the wire when the
   client sends it (fast binary path) or explicitly fetches it.
4. **Synchronous request/response RPC.** Every request gets exactly one
   response, in order. No pipelining, no out-of-order completion. Simple to
   implement, simple to debug.

---

## 2. Wire Protocol

### 2.1 Framing

Both directions use identical framing over TCP (localhost only):

```
[u32 length LE] [u8 kind] [payload: length-1 bytes... wait, see below]
```

Precisely: `length` is the byte count of `kind + payload`. Kinds:

- `0x4A` (`'J'`): payload is a UTF-8 JSON document.
- `0x42` (`'B'`): payload is `[u32 header_len LE][header JSON, UTF-8][binary region]`.
  The header JSON is a normal message whose value encodings may reference
  offsets into the binary region (see §2.3).

Maximum frame size: 256 MiB; reject larger with a protocol error rather than
allocating.

### 2.2 Messages

Requests (client → worker) and responses (worker → client) are JSON objects.
Every request carries a monotonically increasing integer `"id"`; the response
echoes it. Because the protocol is strictly synchronous, the id is a sanity
check, not a correlation mechanism.

Request ops:

| op           | fields                                   | returns                       |
|--------------|------------------------------------------|-------------------------------|
| `hello`      | `token`, `protocol` (int, =1), `client`  | worker/python/mpl versions, backend chosen |
| `import`     | `name` (dotted module path)              | handle                        |
| `getattr`    | `h`, `name`                              | value                         |
| `setattr`    | `h`, `name`, `value`                     | null                          |
| `call`       | `h`, `args` [list], `kwargs` {dict}      | value                         |
| `callmethod` | `h`, `name`, `args`, `kwargs`            | value (getattr+call in one round trip) |
| `getitem`    | `h`, `key`                               | value                         |
| `setitem`    | `h`, `key`, `value`                      | null                          |
| `delitem`    | `h`, `key`                               | null                          |
| `binop`      | `operator` (str), `a`, `b`               | value                         |
| `unop`       | `operator` (str), `a`                    | value                         |
| `len`        | `h`                                      | int                           |
| `repr`       | `h`                                      | str (truncated to 4096 chars) |
| `str`        | `h`                                      | str (truncated to 4096 chars) |
| `dir`        | `h`                                      | list of str                   |
| `iter`       | `h`                                      | iterator handle               |
| `next`       | `h`                                      | value, or `{"t":"stopiter"}`  |
| `contains`   | `h`, `item`                              | bool                          |
| `truth`      | `h`                                      | bool (`bool(obj)`)            |
| `release`    | `handles` [list of int]                  | null                          |
| `clear`      | —                                        | null (empty the entire handle table; figures survive via pyplot's Gcf) |
| `fetch`      | `h`, `accept` (optional list of dtype names) | value forced by-value (ndarray → binary frame; raises if not convertible or > size limit). `accept` is what the client can construct — ulab has only f8/c16/i1/i2/u1/u2, so without it an ordinary `numpy.arange(10)` (i8) arrives undecodable; the worker narrows to f8 instead. Omit it to take whatever dtype the data already has. |
| `eval`       | `code` (str), `mode` (`"eval"`\|`"exec"`)| value (eval) / null (exec); runs in one persistent namespace |
| `put`        | `name`, `value`                          | null (binds into the eval namespace — used to push arrays server-side) |
| `shutdown`   | —                                        | null, then worker exits       |

The operator name rides in an `operator` field, **not** `op` — `op` already
names the RPC itself.

`binop`/`unop` operator strings are the `operator` module names: `add`, `sub`,
`mul`, `truediv`, `floordiv`, `mod`, `pow`, `matmul`, `eq`, `ne`, `lt`, `le`,
`gt`, `ge`, `and_`, `or_`, `xor`, `lshift`, `rshift`, `neg`, `pos`, `abs`,
`invert`. Operands may be values or handles in either position (this is how
`2 * proxied_array` works).

Responses:

```json
{"id": 17, "ok": true,  "v": <encoded value>}
{"id": 17, "ok": false, "err": {"etype": "ValueError", "msg": "...", "tb": "full formatted traceback"}}
```

### 2.3 Value Encoding

Applies to `args`, `kwargs` values, `value`, `key`, `item`, and response `v`.

**Client → worker:**

- `None`, `bool`, `int`, `float`, `str` → native JSON.
- Handle reference: `{"__h__": <int>}` (decoded via the handle table; error if unknown).
- Lists/tuples of encodable values → JSON arrays (tuples become lists; matplotlib doesn't care for argument positions where this matters, and where it does — e.g. `figsize` — lists are accepted).
- Dicts with string keys and encodable values → JSON objects (kwargs only; as a positional value, allowed but keys must not collide with reserved tags — the encoder must check and raise a clear client-side error suggesting `pybridge.put()`).
- `bytes`/`bytearray` → `{"__b__": {"off": <byte offset>, "len": <bytes>}}`, the same tag the worker uses in the other direction. (Not `__nd__`: a `bytes` argument means `bytes`, and decoding it to a `u1` array would hand the callee the wrong type.)
- `array('d'|'f'|'i'|...)`, **ulab `ndarray`s** (via the
  buffer protocol / `.tobytes()`, dtype and shape preserved — including 2-D),
  and **any numeric list with ≥ 256 elements** → binary region reference: `{"__nd__": {"dtype": "f8", "shape": [n], "off": <byte offset>, "len": <bytes>}}` in a kind-`'B'` frame. Worker decodes to a numpy array (a copy, so it is writable and does not pin the frame). Dtype strings are numpy-style and little-endian by definition: `f8`, `f4`, `i1`, `i2`, `i4`, `i8`, `u1`, `u2`, `u4`, `u8`.
- Non-finite floats → `{"__f__": "nan"|"inf"|"-inf"}`. JSON has no literal for
  them, CPython's `json` emits the non-standard `NaN`/`Infinity`, and
  MicroPython's parser accepts neither — so an axis limit of `inf` would break
  the stream. Both directions use the tag.
- **`complex` → `{"__c__": [re, im]}`**, each part encoded as a float (so a
  non-finite part is itself an `__f__` tag, and signed zero survives — a sign
  flip on the imaginary part is a 180° phase shift, not a rounding detail).
  femm-qt is a phasor program: complex is an everyday scalar here, and it
  travels by value like any other. A **complex list with ≥ 256 elements** —
  what a frequency sweep accumulated in a loop looks like — takes the binary
  path as `c16`, the same as a float list does as `f8`.
- Reserved tag keys: `__h__`, `__nd__`, `__t__`, `__b__`, `__f__`, `__c__`.

**Worker → client:**

- `None`, `bool`, `int`, `float`, `str` → native JSON. (Truncate nothing; strings are strings.)
- `tuple` → `{"__t__": [encoded items]}`, `list` → a JSON array, **whenever the sequence is ≤ 64 long**. Elements are encoded by the same rules recursively, so an element that has no by-value form simply becomes a handle *inside* the sequence. That is the whole point: `fig, ax = plt.subplots()` unpacks a tuple of two handles, and `ax.plot()` returns a one-element list holding a `Line2D` handle. A sequence is sent by handle only when it is longer than 64 or the element budget below is exhausted.
- `bytes` → `{"__b__": {"off": o, "len": n}}` in a binary frame (this is how `savefig` to an in-memory buffer + `getvalue()` returns PNG bytes if ever needed).
- **Everything else — including dicts, numpy arrays, and all matplotlib objects — by handle:** `{"__h__": id, "cls": "matplotlib.figure.Figure"}`. `cls` is informational (used in client repr fallback).
- numpy scalar types (`np.float64`, `np.complex128` etc.) are converted to native Python scalars, not handles.
- `complex` → `{"__c__": [re, im]}`, exactly as in the other direction.
- `fetch` responses override the handle rule: ndarrays are returned via `__nd__` binary encoding (C-contiguous copy, dtype preserved across the whole f4/f8/i1..i8/u1..u8 set, bool narrowed to u1, **complex64 and complex128 both carried as `c16`**, anything else cast to f8), size-capped at 128 MiB. A long numeric *list* fetches as an array too — `c16` if any element is complex — mirroring the client's ≥ 256-element rule in the other direction.

**Complex is a first-class dtype, not an afterthought** (§2.3's `c16`). The
reason it can be: ulab's complex ndarray is two `mp_float_t` per element, 16
bytes, and its `tobytes()` output is **byte-identical to numpy's
`complex128`** — verified against this build, so a phasor array crosses the
wire with no conversion at either end. One width only: ulab has exactly one
complex type, so `complex64` widens to `c16` on the way out, losslessly. The
alternative — casting complex to `f8` as an unknown dtype, or refusing it —
would have meant a phasor program's most characteristic value type either
silently losing its imaginary part or not travelling at all.

**Element-wise encodability check must be non-recursive-explosive:** cap total
encoded element count per response at 4096. On exhaustion the *inner*
containers become handles rather than the whole response failing — a handle is
always a valid encoding, so the degradation is graceful and the client sees no
error, just a proxy it can index into.

**Three ops bypass the value encoder entirely**, because they return protocol
metadata rather than a piece of the user's object graph: `hello` (a literal
JSON object — the handle rule would otherwise turn the version report into a
proxy), `dir` (a literal array of strings, which is routinely hundreds long and
must not hit the 64-element rule), and `next` (`{"t": "stopiter"}`). Since the
worker never sends a bare dict by value, `{"t": ...}` is unambiguous.


### 2.4 Handshake and Authentication

- femm-qt listens on `127.0.0.1`, ephemeral port (plain sockets, not Qt --
  see §4.1).
- Worker is spawned with `--connect 127.0.0.1:<port>`; the auth token is passed
  in the environment variable `FEMMQT_PYBRIDGE_TOKEN` (not argv — argv is
  visible in process listings).
- **The token is checked in both directions, which needs two messages.**
  On connect the worker sends one unsolicited frame,
  `{"t": "greeting", "protocol": 1, "token": ..., "pid": ...}`; femm-qt
  validates it **before** exposing the connection to MicroPython, so another
  local process cannot race in and answer on the worker's behalf. Wrong or
  missing token → close, kill process. femm-qt then sends `hello` as a normal
  request carrying the same token, which the worker checks against its own
  environment; the response is the version report. Until a `hello` succeeds the
  worker refuses every other op.
- Timeout for spawn-to-hello: 15 s (matplotlib import on a cold NFS-mounted
  Anaconda can be slow; do not be aggressive here).
- The `hello` response includes: worker script version, `sys.version`,
  `matplotlib.__version__`, `numpy.__version__`, and the interactive backend
  actually selected. femm-qt logs this to the script console at debug level.

---

## 3. CPython Worker (`femmqt_pybridge_worker.py`)

### 3.1 Constraints

- **Single file, shipped in a `pybridge-worker/` folder beside the binaries**
  — its own directory, *not* the `python/` one that holds the MicroPython
  client and the matplotlib stubs (see §5.5; sharing it makes the worker import
  those stubs as matplotlib and disables the bridge entirely). **The hyphen is
  load-bearing**: the binaries' directory is the GUI's working directory and so
  sits on MicroPython's `sys.path` *ahead* of `python/`, and a directory named
  `pybridge` there is imported as an empty namespace package in place of
  `python/pybridge.py`. A hyphen cannot be an identifier. Do *not*
  extract it to a temp directory at spawn time — writing an executable script
  to temp and immediately running it is a classic antivirus-heuristic trigger
  on Windows, and it hides the script from curious users. Shipping in-tree
  also guarantees it version-matches the femm-qt build that spawned it.
- **Stdlib only**, except numpy and matplotlib imported lazily. numpy is a
  hard dependency of matplotlib, so its presence is guaranteed whenever
  matplotlib is.
- Target CPython ≥ 3.9. No walrus-free contortions needed, but no 3.12-only
  syntax.

### 3.2 Backend Selection

At startup, before importing `matplotlib.pyplot`:

```python
import matplotlib
import matplotlib.pyplot as plt
for candidate in backend_order():   # darwin:  ["macosx", "qtagg", "tkagg"]
    try:                            # windows: ["qtagg", "tkagg"]
        plt.switch_backend(candidate)           # linux/freebsd/other posix:
        break                                   #          ["qtagg", "tkagg"]
    except Exception:
        continue
else:
    # report in hello response as fatal capability error; femm-qt surfaces
    # "no interactive matplotlib backend found; pip install matplotlib" etc.
```

**`switch_backend`, not `use` — and it bites silently.**
`matplotlib.use("qtagg", force=True)` records a *preference*; it does not
import the backend module. On a machine with matplotlib but no PyQt/PySide it
succeeds, `import matplotlib.pyplot` succeeds, and the first `plt.subplots()`
then raises `ImportError: Failed to import any of the following Qt binding
modules` — by which time the fallback chain is long gone and the user gets a
crash instead of tkagg. `switch_backend` actually loads the backend, so the
probe fails where it can still fall through. On Windows this is how tkagg
comes to be selected. **§6's discovery probe has the same
flaw and must be written the same way** — it currently says "just attempt
`matplotlib.use(b)`; don't import pyplot", which would report a backend that
cannot draw.

Backend availability realities per platform (informs the error strings, not
the code): python.org installers on Windows and macOS bundle tkinter, so
`tkagg` is a dependable fallback there; Homebrew Python needs `python-tk`;
Linux distros split tkinter into `python3-tk`; FreeBSD needs
`py311-tkinter`. `qtagg` requires a pip/pkg-installed PyQt6/PySide6
everywhere — which is why **tkagg is the realistic default**, not qtagg: the
python.org installers bundle Tk, so most users need install nothing beyond
matplotlib itself. Qt is a preference, not a requirement (this was written
wrong in the manual for a while).
The worker itself is platform-agnostic — it just probes.

Respect an explicit `FEMMQT_PYBRIDGE_BACKEND` env override (set from femm-qt
preferences) before probing. After selecting, call `plt.ion()`.

`plt.show` is monkeypatched to ignore `block=True` (log a one-line notice the
first time). In ion mode show is non-blocking anyway; the patch just prevents
a user's `plt.show(block=True)` from wedging the RPC loop.

### 3.3 Main Loop (the critical part)

Single-threaded. Pseudo-code:

```python
sock.setblocking(False)
buf = FrameBuffer()
while running:
    r, _, _ = select.select([sock], [], [], 0.02 if any_figures_open() else 0.25)
    if r:
        data = sock.recv(1 << 20)
        if not data: break            # femm-qt died or closed; exit cleanly
        buf.feed(data)
        for frame in buf.frames():    # may yield several complete frames
            resp = dispatch(frame)    # synchronous; may take arbitrarily long
            send_frame(sock, resp)    # blocking send with sendall
    pump_gui()                        # see below
```

`pump_gui()`: if any figure managers exist, run
`plt.gcf().canvas.flush_events()` guarded per-manager, or more robustly use
`matplotlib._pylab_helpers.Gcf.get_all_fig_managers()` and call
`manager.canvas.flush_events()` on each, swallowing exceptions from windows
mid-close. Do **not** use `plt.pause()` (it creates a figure if none exists
and steals focus on some backends). If no figures exist, skip.

Notes:

- Because dispatch is synchronous, a long-running call (e.g. `savefig` of a
  huge tricontourf) simply delays GUI pumping. Acceptable; document it.
- `send_frame` must handle partial sends (`sendall` semantics) and switch the
  socket to blocking for the send or loop on `EAGAIN`.
- On macOS this design puts all GUI work on the process main thread with zero
  extra machinery — this is *why* the worker is single-threaded.

### 3.4 Handle Table

- `handles: dict[int, object]`, `next_id` counter starting at 1.
- `release` op removes entries (clients batch these).
- On `shutdown` or socket EOF: `plt.close('all')`, clear table, exit 0.
- Do not attempt weakref cleverness. The table lives as long as the session.

### 3.5 Dispatch and Errors

- Every op handler runs inside one `try/except Exception`; failures return the
  `err` response with `etype = type(e).__name__`, `msg = str(e)`, and
  `tb = traceback.format_exc()`. `StopIteration` in `next` is **not** an error;
  it returns `{"t": "stopiter"}` so the client can raise its own
  `StopIteration` (raising through the RPC error path would be wrong because
  the client must convert it to the protocol-level iteration signal).
- `eval`/`exec` run in a single persistent namespace dict pre-seeded with
  `{"np": numpy, "plt": pyplot}` (lazy — only after backend init). `put` binds
  into the same namespace.
- Protocol-level malformations (bad JSON, unknown op, unknown handle) return
  `err` with `etype = "BridgeProtocolError"`.

---

## 4. The C++ host and the `_pybridge` MicroPython module

### 4.1 The host (C++)

**The host is `libfemm/pybridge_host.{h,cpp}`, and it uses no Qt** — where an
earlier design put it in `gui/` with `QProcess`/`QTcpServer`. libfemm is Qt-free by design and
`femm_headless` links only libfemm, so a Qt host would have denied the bridge
to `femm_headless -run` — which is where batch plotting belongs and where the
entire test suite runs. Both halves already had a non-Qt precedent in the tree:
sockets in `ipc/tcp_server.cpp`, process spawning in `script_callbacks.cpp`
(which also shows the GUI-override callback pattern). The GUI contributes only
its event pump. Consequences worth knowing:

- `QProcess` becomes `CreateProcessW` / `posix_spawnp`, and the token is placed
  in the environment with `SetEnvironmentVariableW`/`setenv` before the spawn
  rather than in a child environment block — the scripting thread is the only
  thread doing this, so it is safe and far shorter.
- Worker stderr is **inherited**, not captured into a Qt channel: under
  `femm_headless` in a terminal it appears directly, and under the GUI it goes
  nowhere. `CREATE_NO_WINDOW` still suppresses the console window that
  `python.exe`, a console-subsystem binary, would otherwise flash.

Responsibilities:

- **Discovery** (§6): find a suitable CPython, cache the result.
- **Lifecycle:** lazy spawn on first use, via `CreateProcessW` /
  `posix_spawnp`, with the `--connect` arg and the token in the child
  environment. **Windows gotcha:** `python.exe` is a console-subsystem binary,
  so spawning it from a GUI app pops a black console window. `CREATE_NO_WINDOW`
  suppresses it (do **not** substitute `pythonw.exe` — it discards stderr,
  which is the only diagnostic a broken worker has; `CREATE_NO_WINDOW` leaves
  the matplotlib GUI windows unaffected, and note it differs deliberately from
  the solver spawn's `DETACHED_PROCESS`, for reasons in the source). Kill on:
  script
  engine reset (if user expects a clean session — make this a preference,
  default *keep alive across script runs* so figure windows survive),
  femm-qt exit, or explicit `pybridge.shutdown()`.
- **Transport:** after handshake validation, expose a blocking
  `QByteArray rpc(const QByteArray& frame)`.

**Threading model (resolved by audit — do not revisit):** femm-qt executes
MicroPython on the GUI thread; `console_vm_hook → scriptTick()` already calls
`processEvents(AllEvents, 20)` every 30 ms throughout any running script.
`rpc()` follows the established pattern: pump events while waiting on the
socket **on the same 30 ms / 20 ms cadence** — do not invent a second pumping
policy. Moving scripting off-thread is explicitly rejected for now: every
`mi_*` call mutates the document model the GUI paints from.

What is and isn't a new hazard here (be precise, because the first draft of
this section over-guarded):

1. **General GUI re-entrancy is pre-existing, not bridge-created.** Events
   already flow throughout script execution and the GUI disables nothing
   (`scriptRunningChanged` only sets a status-bar message), so users can
   already open menus and documents mid-script. The bridge must not take on
   solving that. In particular, do **not** guard `rpc()` with
   `py_is_running()` — that returns true for the entire script, so every
   legitimate RPC would be rejected.
2. **The one genuinely new risk: nested RPC.** If event pumping re-enters
   script execution and a second `rpc()` is issued while one is in flight,
   the strictly request/response protocol desynchronises. Guard exactly this:
   a bridge-level in-flight flag whose only job is to make a nested `rpc()`
   fail fast with a clear BridgeError.
3. **Interrupts, normal case: no machinery — by construction.** Esc →
   `py_request_interrupt()` → `mp_sched_keyboard_interrupt()` sets a pending
   exception that fires at the next VM branch, which is *after* `rpc()`
   returns. "Finish the round trip, then raise" is automatic, provided one
   constraint is respected: **`rpc()` must not call `mp_handle_pending()`
   itself.** State this as a constraint; do not build interrupt handling into
   the transport for a call that is merely slow.

4. **Interrupts, wedged case: the escape hatch must be reachable — and by
   default it is not.** The obvious answer, `pybridge.restart()`, is one the
   user cannot reach. Three facts, each verified
   against the tree:

   - The console **blocks all typing while a script runs**
     (`py_console.cpp`: under `py_is_running()`, Return, Backspace and every
     printable character are swallowed), so `pybridge.restart()` cannot be
     typed.
   - Esc only *schedules* an exception (`mp_sched_exception`), which fires at
     the next VM branch — and there is no next VM branch while execution sits
     inside a C++ `rpc()`.
   - So with a wedged worker the GUI stays responsive, the status bar says
     "Script running - press Esc to stop", Esc does nothing observable, and the
     only recourse is killing femm-qt.

   **Required fix:** the pump loop polls a pending-interrupt accessor —
   `bool py_interrupt_pending()`, added to `py_state.cpp` so the host never
   reads `MP_STATE_VM(mp_pending_exception)` directly — and on seeing it stops
   waiting, **kills the worker, bumps the session generation, and throws
   `BridgeError("interrupted; worker discarded")`**. This does not violate the
   "never kill the socket mid-frame" rule, because nothing carries on: the
   worker is gone, so there is no stream left to desynchronise. Esc then means
   what the status bar already promises, and the pending `KeyboardInterrupt`
   fires normally at the next VM branch once `rpc()` has returned. Losing the
   worker is the correct price for abandoning an in-flight frame; a wedged
   worker was going to be restarted anyway.

**Both blocking entry points pump, and both honour the abort.** `rpc()` is the
obvious one, but `ensure_started()` is worse: discovery probes up to 10 s *per
candidate* (§6) plus 15 s spawn-to-hello, all on the GUI thread. Without
pumping, first use of `plt` freezes femm-qt for tens of seconds; without the
interrupt check, Esc cannot abandon a hopeless probe either.

Crash handling: if the worker process dies or the socket EOFs mid-call, `rpc()`
throws; the MicroPython layer converts this to `BridgeError("python worker
exited unexpectedly")`. The next bridge use attempts a respawn (fresh handle
space; all existing proxies on the MicroPython side become invalid and any use
of them raises a clear "stale handle from previous worker session" error —
implement by tagging proxies with a session generation counter).

### 4.2 `_pybridge` MicroPython Module (runtime-built, no regen)

Minimal surface:

```
_pybridge.ensure_started() -> None    # spawn+handshake if needed; OSError on failure
_pybridge.rpc(frame: bytes) -> bytes  # blocking round trip
_pybridge.session() -> int            # generation counter, bumps on respawn
_pybridge.running() -> bool
_pybridge.info() -> str               # the hello RESPONSE FRAME's JSON: the
                                      # report is at ["v"], since unwrapping it
                                      # would mean parsing JSON in C++
_pybridge.shutdown() -> None
_pybridge.restart() -> None
_pybridge.set_interpreter(path) / interpreter() -> str
_pybridge.set_backend(name) / backend() -> str
_pybridge.attempts() -> list[str]     # what discovery tried and why each failed
```

**Build it at runtime, not as a builtin.** Follow the pattern of
`libfemm/py_os.cpp`, which constructs `os`/`os.path` with
`mp_obj_new_module` + `femm_py::Module` — no `MP_REGISTER_MODULE`, no genhdr
regeneration, so the module lands without touching mpconfig. All protocol logic lives in Python (`pybridge.py`), not
C++. Keep the module dumb.

**Bridge lifetime vs. interpreter lifetime (previously unspecified):**

- *Script ends normally:* worker and its figure windows persist (subject to
  the keep-alive preference, default on). Handles persist too — a follow-up
  script in the same interpreter can keep using proxies it stashed.
- *Interpreter reset / `close_py()`:* all MicroPython-side proxy state is
  gone by definition. Client sends a `clear` op (new protocol op: empty the
  worker's handle table) but does **not** kill the worker — open figure
  windows survive because pyplot's `Gcf` holds them independently of our
  handle table. Bump the session generation so any proxy object that somehow
  survived (stashed in a C++-held reference) fails loudly rather than
  resolving stale handles.

  `clear` **also resets the `eval`/`put` namespace** to its seeded state
  (`np`, `plt`): a reset means the user's world is gone, and leaving
  `d = numpy.genfromtxt(...)` bound to a name no surviving script remembers is
  a leak wearing a feature's clothes. The cost is that expensively-parsed
  server-side data must be recreated after a reset — acceptable, and the
  alternative (keep the namespace) is a one-line change if it turns out to
  annoy. What is *not* acceptable is leaving it unspecified for the
  implementer to guess.
- *femm-qt exit:* `shutdown` op, then terminate-with-timeout-kill.
- *Worker crash:* as described under crash handling; respawn is lazy.

(`clear` is in the §2.2 op table.)

MicroPython build configuration this depends on. All of it is enabled; the
notes say what breaks without each, since a future mpconfig change could take
one away and the failures are silent:

- Verified already enabled: `MICROPY_MODULE_GETATTR` (CORE_FEATURES),
  `struct`, `array`, `json`; dotted filesystem package imports work, so the
  §5.5 shims need no import hook.
- **Enable `MICROPY_PY_BUILTINS_MEMORYVIEW`** — absent today; without it the
  `array('d')` bulk-encode path degrades to per-element `struct.pack`.
- **Enable `MICROPY_PY_ALL_SPECIAL_METHODS` and
  `MICROPY_PY_REVERSE_SPECIAL_METHODS`** — absent today; without them
  `__mul__`/`__truediv__`/`__pow__`/`__neg__`/`__abs__` and every reverse op
  (`__radd__` etc.) don't dispatch, so `2 * proxy` raises TypeError. These are
  load-bearing for the `binop` design in §2.2.
- **Enable `MICROPY_PY_DELATTR_SETATTR`** — absent today; required for the
  §5.2 attribute-assignment policy (without it, `proxy.attr = value` silently
  sets a local MicroPython attribute and never reaches CPython, which is the
  worst possible failure mode).
- Class `__del__` finalisers are **not** relied upon (MicroPython support is
  spotty); handle release is explicit/batched instead (§5.3).
- After the regen, re-run the dunder dispatch matrix and keep it as a
  permanent test fixture so a future mpconfig change can't silently regress
  operator forwarding.

---

## 5. MicroPython Client (`pybridge.py` + shim packages)

### 5.1 Public API

```python
import pybridge
m   = pybridge.import_module("matplotlib.animation")  # returns module proxy (escape hatch for unshimmed modules)
v   = pybridge.eval("np.linspace(0, 1, 1000)")   # returns proxy (server-side ndarray)
pybridge.exec("plt.style.use('ggplot')")
pybridge.put("bvals", my_micropython_list)        # push data, binds server-side name
pybridge.open(path, mode="r", **kw)               # convenience: server-side builtins.open, returns file proxy
arr = pybridge.fetch(some_array_proxy)            # force by-value; returns a ulab ndarray (dtype/shape preserved)
pybridge.restart()                                 # kill + respawn worker
pybridge.shutdown()
```

### 5.2 `_Proxy`

One class. **Internal state lives on the instance, stored through
`object.__setattr__`** — the CPython idiom:

```python
class _Proxy:
    def __init__(self, h, cls, gen):
        object.__setattr__(self, "_h", h)      # bypasses our own __setattr__
        object.__setattr__(self, "_cls", cls)
        object.__setattr__(self, "_gen", gen)
```

**This is why there is no id()-keyed side table, and no two-sided leak.**
`object.__setattr__` and
`object.__delattr__` are gated by `MICROPY_PY_DELATTR_SETATTR` (objobject.c),
the *same* flag that makes `__setattr__` dispatch at all — so the moment
attribute forwarding becomes possible, the way to bypass it for internal state
comes with it. The earlier audit finding ("no `object.__setattr__`") was true
of the build as it stood, and stopped being true the moment the flag landed.
Verified in `testing/ProxyDunderTest.py`: internal state stores and reads back,
other assignments still forward, and a miss still reaches `__getattr__`.
(Instance `__dict__` remains read-only regardless — `object.__setattr__` is the
way in, not dict mutation.)

**Attribute assignment forwards** (`__setattr__` → `setattr` op). Without the
flag `ax.xaxis.labelpad = 5` would silently set a dead local attribute — no
error, no effect, pasted-from-CPython snippets broken invisibly. A scripting
API may be incomplete but must never be silently wrong. Provide
`pybridge.setattr(proxy, name, value)` as well, for symmetry and for the rare
name that collides with the proxy's own internals.

Forwarded dunders:

- `__getattr__` → `getattr` op; `__setattr__` → `setattr` op
- `__call__` → `call` op (encode args/kwargs)
- `__getitem__`/`__setitem__`/`__delitem__`
- `__len__`, `__contains__`, `__bool__` (→ `truth`)
- `__iter__` → `iter` op returning an iterator proxy whose `__next__` maps
  `{"t":"stopiter"}` to `raise StopIteration`
- `__enter__`/`__exit__` → forwarded via `callmethod` (so `with` blocks work
  on proxied file objects, locks, etc.; `__exit__` returns the truthiness the
  server reports)
- `__repr__`/`__str__` → `repr`/`str` ops (fall back to
  `<py.matplotlib.figure.Figure #42>` if the RPC fails)
- Arithmetic/comparison: `__add__`, `__radd__`, `__sub__`, `__rsub__`,
  `__mul__`, `__rmul__`, `__truediv__`, `__rtruediv__`, `__pow__`, `__neg__`,
  `__abs__`, `__eq__`, `__ne__`, `__lt__`, `__le__`, `__gt__`, `__ge__`,
  `__matmul__` if the build supports it — all via `binop`/`unop` with the
  proxy encoded as `{"__h__": id}`.

**Method-call fast path:** implement `__getattr__` to return a lightweight
bound-method stub so that `obj.method(args)` issues a single `callmethod`
RPC instead of `getattr` + `call`. Attribute access that is *not* immediately
called must still work (`ax.figure` → the stub must materialize: give the stub
its own `__getattr__`/dunders that force resolution via a real `getattr` op
first). If this materialization dance gets fiddly in MicroPython, ship the
simple two-RPC version — round
trips on localhost are ~50 µs and plotting scripts issue hundreds, not
millions, of calls.

### 5.3 Handle Release

No `__del__`. Instead:

- `pybridge` keeps a client-side set of live handle ids per session.
- `release` messages are sent in batches piggybacked before the next RPC when
  the pending-release list exceeds 64 entries. (Piggyback = just send a
  `release` request first; the protocol stays strictly request/response.)
- **The leak is one-sided.** With proxy state on the
  instance rather than in a strong-referenced side table (§5.2), a proxy the
  script drops is ordinary garbage and collects normally; what accumulates is
  only the worker's `{int -> object}` table entry, which is what §5.3 always
  meant. `pybridge.release(proxy)` queues the worker-side release;
  `pybridge.restart()` and the interpreter-reset path (§4.2 `clear`) drop the
  whole table at once. Client-side memory needs no special machinery and no
  measurement gate — MicroPython's GC handles it.
- What is still true: **no `__del__`**, so a collected proxy cannot tell the
  worker to free its handle. Worker-side entries therefore accumulate for the
  session unless released explicitly. A plotting session leaks a few thousand
  small dict entries in a CPython process; that is fine, and everything drops
  on restart.

### 5.4 Codec

Mirror of §2.3. **ulab `ndarray`s are the primary bulk-data type on the client
side**: they encode directly via the binary path with dtype and shape
preserved (the worker reconstructs a numpy array of the same shape), so
`ax.plot(x, y)` with ulab arrays and `ax.tricontourf(x, y, tri, v)` with a 2-D
ulab index array both take the fast path with zero copies beyond the frame
buffer. `array('d')`/`array('f')` encode directly as 1-D. **ulab reports
`arr.dtype` as an integer typecode (e.g. 100 == ord('d')), not a string** —
the codec must map these codes to the protocol's numpy-style dtype strings
(`{ord('d'): "f8", ord('f'): "f4", ord('i'): "i4", ord('h'): "i2",
ord('b'): "i1", ord('B'): "u1"}`; verify the exact set this ulab build
exposes and fail loudly on an unmapped code rather than guessing). Plain
lists are scanned (ints and floats mixed → f8) and switch to the binary path
at ≥ 256 elements. Use `memoryview`/`.tobytes()` — no per-element
`struct.pack` calls in a loop for the bulk path (`memoryview` requires the
`MICROPY_PY_BUILTINS_MEMORYVIEW` flag from §4.2). In the reverse direction,
`pybridge.fetch()` decodes `__nd__` payloads into ulab ndarrays.

**Complex ulab arrays (audited against this build, 2026-08-16).** `ord('c')`
== 99 is the dtype code, `itemsize` is 16, and `tobytes()` returns bytes
**identical to numpy `complex128`** — so `c16` needs no conversion in either
direction. Two traps the port must handle, both verified:

- `memoryview(complex_array)` **raises `ValueError: bad typecode`**. The
  zero-copy path below does not apply to complex; `.tobytes()` is the only way
  in, and the codec must not assume a memoryview is always available.
- `np.frombuffer(buf, dtype=np.complex)` **works**, and is the way back out —
  so `pybridge.fetch()` of a phasor array returns a genuine ulab complex
  ndarray, not a list.

Two further ulab specifics (audited): ulab **implements the buffer protocol**
(`ndarray_get_buffer`), so `memoryview(arr)` is genuinely zero-copy once the
`MICROPY_PY_BUILTINS_MEMORYVIEW` is enabled, and `testing/ProxyDunderTest.py`
asserts it. But `ndarray.tobytes()`
**raises ValueError on non-dense arrays**, and slices like `x[::2]` are
exactly what users will plot. The codec must therefore densify first: attempt
the zero-copy path, catch the dense-only failure (or check contiguity up
front if the build exposes it), and fall back to materializing a dense copy
(`np.array(arr)` or equivalent) before encoding. Never let a sliced array
error out of `ax.plot()`.

### 5.5 Import Shims

Filesystem MicroPython packages, installed into the **`python/` folder beside
the binaries** — already on `sys.path` because `init_py()` appends
`exeDir() + "python"`, and already documented in the manual as where user
modules go. **Not the same directory as the worker**, which ships to
`pybridge-worker/` instead: CPython puts a script's own directory first on `sys.path`,
so a worker sitting beside `python/matplotlib/` imports the stub below as
matplotlib and reports itself broken (§9). Naming both
here so the install step is unambiguous:

```
python/matplotlib/__init__.py
python/matplotlib/pyplot.py
python/xlsxwriter.py
python/zipfile.py
```

**The rule: shim only what has no in-process equivalent.** matplotlib and
xlsxwriter qualify -- there is nothing native for them to shadow, so a ~15-line
forwarder costs nothing and makes a pasted example run unchanged. A single file
is enough even for `from xlsxwriter import Workbook`, because MicroPython
consults a module's `__getattr__` for a from-import (verified). But a shim is a
promise that the name will work, so the list stays short and deliberate rather
than growing by habit.

**A STANDARD-LIBRARY name needs one extra thing.** `zipfile` is shimmed on the
same reasoning as the other two -- there is no in-process zip, and writing one
would need DEFLATE and therefore a regen. But a user expects `import zipfile`
to work the way `import json` does, so the bare discovery error ("no usable
Python 3 found") reads as a non-sequitur where for matplotlib it reads as
expected. That shim catches `BridgeError` and re-raises an `ImportError` saying
femm-qt has no built-in zipfile and this one comes from an installed Python.
Any future stdlib-name shim should do the same: the surprising part is not that
Python is missing, it is that a *standard* module needed it.

**Do NOT ship `numpy` or `scipy` shims.** femm-qt already provides those names
natively via ulab, and shadowing them with proxies would silently replace fast
in-process numerics with RPC round trips (and break code that relies on ulab
semantics). CPython-side numpy remains reachable explicitly via
`pybridge.import_module("numpy")` or the `np` binding in the `eval` namespace
for the rare case ulab lacks something.

Each is ~10 lines:

```python
# matplotlib/pyplot.py
import pybridge
_m = None
def __getattr__(name):
    global _m
    if _m is None:
        _m = pybridge.import_module("matplotlib.pyplot")
    return getattr(_m, name)
```

`matplotlib/__init__.py` same pattern for `matplotlib` itself (users do
`matplotlib.rcParams[...]`). Submodule imports beyond pyplot
(`from matplotlib import cm`) resolve through `__init__`'s `__getattr__`
since `cm` is an attribute of the proxied top-level module. Deep imports like
`import matplotlib.gridspec` need only additional stub *files* — audit
confirmed this MicroPython build resolves dotted filesystem package imports
normally, so no import hook is required. Ship stubs for the common ones
(`pyplot`, `cm`, `colors`, `ticker`, `gridspec`, `patches`) and document
`pybridge.import_module()` as the general escape hatch.

**Worker spawn is triggered lazily by the first proxied attribute access**, so
merely having the shims on the path costs nothing.

### 5.6 Arbitrary Packages (csv as the worked example)

The bridge is matplotlib-agnostic; any importable CPython module works via
`pybridge.import_module()`. Include this csv example in the user docs, because
it demonstrates the two idioms every non-matplotlib use case needs:

```python
import pybridge
csv = pybridge.import_module("csv")

# Reading — open the file SERVER-SIDE (both processes share the filesystem):
with pybridge.open("results.csv", newline="") as f:
    for row in csv.reader(f):        # row arrives by value: a list of str
        print(row[0], float(row[2]))

# Writing:
with pybridge.open("out.csv", "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["x", "By"])
    w.writerows(data)                # list of lists of scalars — encodes directly
```

Idiom 1: **file objects are opened on the server**, never passed across (a
MicroPython file object can't cross the boundary; the shared filesystem makes
this a non-issue). Idiom 2: **iteration crosses row-by-row**, which is
perfectly fine at ~50 µs/round-trip for thousands of rows. For a 500k-row
file, don't iterate — parse server-side and pull columns in one binary
transfer:

```python
pybridge.exec("import numpy; d = numpy.genfromtxt('big.csv', delimiter=',', skip_header=1)")
col = pybridge.fetch(pybridge.eval("d[:, 2]"))    # one 4 MB frame → ulab ndarray
```

Documented limitations that apply to *all* proxied packages (not just the
matplotlib callback case): MicroPython callables cannot cross the boundary
(no `key=` functions to `sorted`, no scipy objective functions — define them
server-side via `pybridge.exec` instead), and rows/tuples wider than the
64-element by-value limit come back as handles (index into them, or `fetch`).

---

## 6. Python Environment Discovery

Order of candidates:

1. Explicit path from femm-qt preferences (`Settings → Scripting → Python interpreter`).
2. `FEMMQT_PYTHON` environment variable.
3. Platform defaults: `python3`, `python` on PATH; on Windows additionally
   `py -3`; on macOS additionally `/usr/local/bin/python3`,
   `/opt/homebrew/bin/python3`; on FreeBSD additionally
   `/usr/local/bin/python3` (pkg installs land there and the `python3` PATH
   symlink comes from the lang/python3 meta-port, which may not be installed).

**There is no separate probe script.** Discovery spawns
the *real worker* against each candidate and reads its `hello` reply, which
already reports the Python/numpy/matplotlib versions and the backend actually
brought up. A candidate that greets with `backend: null` is exactly a candidate
that cannot plot, and its `backend_error` is the message to show. This removes a
second JSON shape and a second notion of "usable", costs no more than the
original probe (which had to import matplotlib anyway), and sidesteps a Windows
trap: `popen()` from a GUI process is documented to hang, so capturing a
`python -c` child's stdout there would have needed its own pipe plumbing.

The first candidate that greets *with* a backend wins and is remembered for the
session. **If none has one, a second pass accepts a candidate that merely
answered** — the bridge is a general package proxy, and csv, xlsxwriter, scipy
and sympy need no figure window, so refusing a working Python for lacking
matplotlib denied every non-plotting use. Plotting on such an interpreter then
fails at the import, naming the missing module.
`testing/PyBridgeDiscoveryTest.py` pins both halves; losing the preference
while fixing the fallback would be easy and silent. Order: the explicit preference, then `FEMMQT_PYTHON`, then the
platform names (`python`/`python3`, plus `/opt/homebrew/bin/python3` on macOS
and `/usr/local/bin/python3` on FreeBSD). The GUI persists the explicit
preference in QSettings and pushes it into libfemm at startup, since libfemm
holds no Qt and cannot read settings itself.

Failure UX (this is the actual support surface — invest here): the MicroPython
exception message must be actionable **and platform-aware**, e.g.:

```
BridgeError: no usable Python found for matplotlib bridge.
Tried: /usr/bin/python3 (no matplotlib), C:\Python312\python.exe (no interactive backend).
Fix: pip install matplotlib   — or set the interpreter in
Settings → Scripting → Python interpreter.
```

Per-platform remediation strings, and note that **Tk, not Qt, is the thing to
name**: Windows/macOS → `pip install matplotlib` (the python.org installer
already has Tk; Homebrew's Python needs `brew install python-tk`); Linux →
`pip install matplotlib` plus the distro's `python3-tk`; FreeBSD →
**recommend pkg, not pip**
(`pkg install py311-matplotlib py311-tkinter` — there are no prebuilt
manylinux wheels on FreeBSD, so `pip install matplotlib` triggers a
from-source build needing a toolchain and freetype headers, which is exactly
the support ticket we're trying to avoid).

---

## 7. Repository Layout

Corrected to match the actual femm-qt tree (audit: the C module belongs with
the VM integration in `libfemm/`, the Qt host in `gui/`):

```
femm-qt/
  pybridge/
    worker/femmqt_pybridge_worker.py     # single-file CPython worker
    mpy/pybridge.py                       # MicroPython client core
    mpy/shims/matplotlib/__init__.py
    mpy/shims/matplotlib/pyplot.py
    mpy/shims/matplotlib/{cm,colors,ticker,gridspec,patches}.py
    mpy/shims/xlsxwriter.py               # one file; from-import resolves via __getattr__
    mpy/shims/zipfile.py                  # ditto, and re-raises with context (stdlib name)
    tests/test_protocol.py                # CPython-only, no femm-qt needed
    tests/test_worker_headless.py         # live worker, headless (agg)
    tests/fake_client.py                  # CPython reference client (mirrors pybridge.py)
    tests/demo_interactive.py             # a real window on a real backend
    tests/bench_arrays.py
  libfemm/py_pybridge.{h,cpp}             # _pybridge, runtime-built per the py_os.cpp pattern
  libfemm/pybridge_host.{h,cpp}           # spawn/socket/handshake host -- NO Qt, see §4.1
  gui/script_prefs_dialog.{h,cpp}         # Edit > Scripting Preferences
  testing/PyBridgeHostTest.py             # transport, from femm-qt's own MicroPython
  testing/PyBridgeInterruptTest.py        # worker death and the Esc abort
  testing/PyBridgeClientTest.py           # proxy, codec, shims
  testing/PyBridgeBenchTest.py            # the transfer budget, measured in-process
  testing/PyBridgeDiscoveryTest.py        # prefers a plotting python, uses a plotless one
  testing/PyBridgeNestedTest.py           # the nested-RPC guard, via the GUI's TCP server
```

`pybridge/` is the source of truth in the repo; the build stages it beside
the binaries. **The worker does NOT go in `python/`** -- that was this
paragraph's original claim and it is wrong twice over, for the two shadowing
reasons in §9: CPython puts a script's own directory first on `sys.path`, so a
worker in `python/` imports the MicroPython `matplotlib` shim and reports every
interpreter as plotless; and `bin/` is the GUI's working directory, so a
directory literally named `pybridge` there shadows `python/pybridge.py`. The
staged layout is therefore:

```
bin/python/pybridge.py                              # the client
bin/python/matplotlib/*.py                          # the shims
bin/python/datetime/*.py                            # unrelated to the bridge; see pylib/
bin/pybridge-worker/femmqt_pybridge_worker.py       # the worker, hyphen deliberate
```

`libfemm/CMakeLists.txt` stages it (target `pybridge_python`), and **both
installers ship it**: `packaging/femm-qt.iss` by wildcard into `{app}\bin`,
`macos_build.sh` into `Contents/MacOS/` where `exeDir()` looks. Until
2026-08-17 neither did, so the bridge worked in every developer build and was
absent from every installed copy -- the failure mode a test suite run from the
build tree cannot see.

**User docs note:** femm-qt already ships a native `plot()` for simple x/y
curves. The docs page must state the division of labor plainly — native
`plot()` for quick looks with zero dependencies; matplotlib via the bridge for
publication figures, contour/tri plots, subplots, and everything else — or
users will wonder which to reach for.

`tests/fake_client.py` deserves emphasis: a CPython implementation of the
client codec + Proxy lets the entire protocol and worker be developed and
tested with zero femm-qt involvement, and it remains the
reference when debugging the MicroPython port.

---
---

## 8. How it is tested

The bridge has two halves that can be tested independently, and one hazard that
can only be reached through the GUI.

**CPython only**, no femm-qt build required —
`python3 -m unittest discover -s pybridge/tests` (108 tests):

- `test_protocol.py` — framing and codec with no sockets. Several cases build a
  frame with the worker's implementation and parse it with the client's, since
  a framing bug that is consistent with itself would otherwise pass.
- `test_worker_headless.py` — a live worker under `FEMMQT_PYBRIDGE_BACKEND=agg`:
  the op matrix, iteration, error fidelity, handle release, `clear`, csv through
  server-side file objects, a 50k-triangle `tricontourf`, handshake failures and
  clean exit on EOF.
- `demo_interactive.py` — the opening example against a real backend, in a real
  window. The one thing the headless tests cannot show.

**Inside femm-qt**, all wired into `testing/_verify_all.sh` and all skipping
cleanly when no usable CPython is installed:

| test | what it pins |
|---|---|
| `PyBridgeHostTest.py` | the transport, driven from femm-qt's own MicroPython with hand-built frames — no client library in the way |
| `PyBridgeClientTest.py` | proxy, codec, shims; and that no directory beside the binaries shadows a module (§5.5) |
| `PyBridgeBenchTest.py` | the transfer budget below, with peak interpreter heap |
| `PyBridgeInterruptTest.py` | worker death mid-call, and Esc abandoning a wedged call |
| `PyBridgeNestedTest.py` | a genuine nested RPC, delivered by the GUI's TCP server while the first call is pumping events |
| `PyBridgeDiscoveryTest.py` | that a plotting-capable Python wins, *and* that a plotless one is still usable |

Two of those deserve comment. The nested-RPC test drives the real hazard rather
than simulating it, and asserts the *outer* call survives the intruder —
rejecting a nested call would be worthless if it damaged the conversation
already in progress. And the interrupt test exists because the escape hatch it
covers is one that cannot be reached by construction: Esc only schedules a
MicroPython exception, which fires at the next VM branch, and there is no next
branch while execution sits inside a blocking C++ call.

### 8.1 Transfer budget

1e6-point `float64` across the wire and decoded: budget ≤ 150 ms.
Measured inside femm-qt, so it includes the copies (frame into a MicroPython
`bytes`, and again into the host's `std::string`) that a CPython-to-CPython
benchmark cannot see:

| step | best | heap |
|---|---|---|
| `put` 1e6 f8 (8 MB): encode + transfer + decode | **59 ms** | +22.9 MB |
| `fetch` back into a ulab array | 43 ms | +22.9 MB |
| `put` 250k int32 indices (i4, not f8) | 5.8 ms | +3.8 MB |
| `put` 1e5-element plain list | 16.8 ms | +3.8 MB |
| empty round trip | **0.065 ms** | — |

The heap column is why it is measured rather than assumed: an 8 MB payload
costs about three times that in transient allocation, on a GC heap that starts
at 2 MB and grows on demand. If those copies ever matter, the fix is a second C
entry point writing straight from a buffer — but measure first.

The round-trip floor decides more than it looks. At 65 µs, the two-RPC
`getattr`+`call` path costs a 200-call plotting script about 13 ms, against
61 ms for one `ax.plot()` of a large array — which is why the `callmethod` fast
path in §5.2 was measured and deliberately not built.

---

## 9. Where the design was wrong

The reversals below are the most useful thing in this document for anyone
changing the bridge: each was a plausible design that the implementation
disproved, and several would be easy to reintroduce.

**The host cannot be Qt.** It was to live in `gui/` with `QProcess` and
`QTcpServer`. `femm_headless` links only libfemm, so that would deny the bridge
to `femm_headless -run` — where batch plotting belongs and where the whole test
suite runs. It is `libfemm/pybridge_host.*`, using the same non-Qt sockets and
process spawning the tree already had. The GUI contributes only its event pump,
by handing over the VM hook it already installs, so there is one cadence rather
than two that can drift.

**`matplotlib.use()` does not import a backend.** It records a preference, so
`qtagg` "succeeds" on a machine with no Qt bindings and raises at the first
figure — long past the point where the fallback chain could have helped.
`switch_backend()` actually loads it. The discovery probe must use the same
call for the same reason.

**A short sequence goes by value even when its elements cannot.** The first
rule required every element to be encodable, which breaks
`fig, ax = plt.subplots()` — the motivating example. Elements become handles
*inside* a by-value sequence.

**A directory beside the binaries is a name in the import namespace, twice
over.** The worker cannot live in `python/`, because CPython puts a script's
own directory first on `sys.path` and it would import the MicroPython
`matplotlib` stub. Nor can it be called `pybridge/`, because that directory is
the GUI's working directory and therefore on MicroPython's `sys.path` *ahead*
of `python/`, where it shadowed `python/pybridge.py` and broke plotting for
every GUI user. It is `pybridge-worker/`; a hyphen cannot be an identifier.
The test suite missed the second collision entirely because `-run` chdirs to
the script's directory — nothing ever ran with the working directory the GUI
actually uses.

**The bridge is not only for plotting.** Discovery originally accepted an
interpreter only if it could draw, so a Python with `xlsxwriter` and no
matplotlib was refused outright. Both halves matter and are pinned separately.

**Tk, not Qt, is the realistic requirement.** The python.org installers bundle
Tk, which is what matplotlib falls back on, so most users need install nothing
beyond matplotlib itself. Documentation said `pip install matplotlib PyQt6` for
a while, which sends people after a package they do not need and makes a
working installation look broken.

**MicroPython's exception idioms differ, and the error path is the worst place
to find out.** `RemoteError` used `Exception.__init__(self, ...)`, which does
not exist here, so every remote exception outside the built-in map died as an
`AttributeError` instead of reporting itself. `super().__init__(...)` is the
spelling. Likewise there is no `object.__hash__` to borrow.

**Complex is a first-class value.** It was to be refused rather than risk
casting it to `f8` and dropping the imaginary part. For a phasor program that
is the wrong trade: the wire carries `__c__` scalars and a `c16` array dtype,
and ulab's complex arrays are byte-identical to numpy's `complex128`, so they
cross with no conversion at either end.

---

## 10. Security and licensing

- The bridge executes arbitrary Python in a user-owned process at the user's
  request — the same trust model as the scripting console itself. The token and
  the loopback binding exist to stop *other local processes* from hijacking the
  socket, nothing more.
- No linking and no redistribution of matplotlib or numpy: a separate process,
  the user's own installation, socket IPC. No licence interaction with
  femm-qt's MIT/GPLv3 split. The worker script is femm-qt source (MIT — it
  ships with the GUI/bindings side, not the solvers).

---

## 11. Platform status

Developed on **Windows**; **Linux verified on Ubuntu 24.04** (2026-08-16) —
builds, runs, and plots. **macOS is still unexercised.**

That gap matters more than it sounds, because Linux does not compile the two
macOS-only lines: `_NSGetEnviron()` (the `environ` accessor, since a plain
`extern char **environ` is not reliably linkable there) and `SO_NOSIGPIPE`
(macOS has no `MSG_NOSIGNAL`, so that socket option is its *only* SIGPIPE
defence). Both sit under `#ifdef __APPLE__`. Run `macos_build.sh`, then the
bridge tests — the interrupt one especially, since a surviving SIGPIPE problem
shows up there as femm-qt dying outright rather than reporting an error.

The review that produced the POSIX branch found one crash and three lesser
defects. Linux has now exercised all four; they remain what to re-check first
if macOS misbehaves:

- **SIGPIPE terminates the process** when `send()` finds the worker gone. The
  "worker crashed" path is a *tested* path that merely returns an error on
  Windows. Neither remedy is universal: `MSG_NOSIGNAL` on the send (Linux,
  FreeBSD), `SO_NOSIGPIPE` on the socket (macOS). Both are applied.
- `select()` is undefined past `FD_SETSIZE` and a GUI process can hold more
  descriptors than that; the POSIX wait uses `poll()`.
- A failing `select`/`poll` read as "not readable", so a broken socket became
  an infinite pump loop — and an `rpc` waits forever by design.
- `environ` is not reliably linkable on macOS; `_NSGetEnviron()` is.

Note that `ipc/tcp_server.cpp` has the same latent SIGPIPE exposure on its own
send path. That is pre-existing and untouched here, but it would kill femm-qt
when an IPC client vanishes mid-response, and is worth its own look.
