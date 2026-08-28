# Bulk getters: why local and wire cannot be one function

*Design, 2026-08-26. Not started. Follows `docs/array-pipeline.md`, which
establishes why in-process bulk mesh access is worth having at all.*

## The constraint that forces the answer

It is not that the two transports return different data. It is a mechanic in
`py_state.cpp`.

Every bound function's return value is formatted into the wire buffer by
`capture_result()` (`py_state.cpp:238`) — it walks the returned object and
appends its text to `s_last_result`. Separately, `py_set_result()`
(`py_state.cpp:146`) writes that buffer directly, which is what the existing
`mo_getall*` use to emit a megabyte of tab-separated numbers without building a
Python object at all.

**A function can do one or the other, not both.** If `mo_getallelements`
returned an ndarray *and* called `py_set_result`, `capture_result` would then
append a formatted rendering of that ndarray to the buffer the function had just
filled, and the wire client would receive both.

So today's `mo_getall*` set the buffer and return `None` — which is why they
return `None` in-process, and why in-process they do all the string-building work
and hand the caller nothing.

Note in passing that `py_set_result`'s stated justification is now obsolete. Its
comment says it exists because "result strings can exceed the GC heap capacity
for large meshes." bdwgc removed that capacity limit. The mechanism is still
worth keeping — building a string is cheaper than building a Python object the
wire will immediately re-flatten — but the reason has changed.

## What the clients already produce

Worth knowing before designing, because it is less divergent than it looks:

| | user-visible result of `mo_getallelements` |
|---|---|
| octavefemm | `N×7` double matrix |
| pyfemm | list of `N` seven-element lists |
| in-process | **`None`** |

The two wire clients already reconstruct a table of the same shape and meaning,
each in its host language's natural type. In-process is the odd one out, and it
is the odd one out by being *empty*, not by being differently typed.

## The design

**Two families of names, each with exactly one contract.** No mode flag, no
function whose return type depends on who called it.

### Family 1 — `mo_getall*`, unchanged on the wire, fixed in-process

Keep the wire behaviour byte-for-byte: it is the IPC contract, five bindings
depend on it, and `project_bulk_getters` records that it is wire-only *by
design*.

In-process, return **the same structure pyfemm builds** — a list of N tuples —
rather than `None`. That is a strict improvement on returning nothing, and it
closes a real gap: a pyFEMM script run in-process under the drop-in shim
(`pyfemm-inprocess-compat.md`) currently receives `None` and fails on the next
line.

It is not fast. It builds N Python tuples, which is the cost the array family
exists to avoid. That is the right trade for a compatibility path.

Implementation: the function needs to know its transport. The honest way is a
flag set by `tcp_server.cpp` around its `exec_python()` call, since "the wire"
means precisely "the TCP server is the caller". Not a guess about the callee.

### Family 2 — new names, in-process only, returning typed arrays

```
mo_getnodes()     -> (nn, 2) f8                     coordinates
mo_getelements()  -> ((ne,3) i4, (ne,2) f8,         connectivity, centroids,
                      (ne,) f8, (ne,) i4)            area, group
mo_getb()         -> (ne, 2) f8   or (ne,) c16      per element, no point search
mo_geta()         -> (ne,) f8     or (ne,) c16
```

Typed, split, and built straight into `gc_alloc_atomic()` payloads so the
collector never scans them. Splitting is the point: connectivity and group are
integers, and an `N×7` all-double matrix both wastes half the memory on indices
and loses the distinction.

**Over the wire these do not exist**, and must say so usefully rather than
"unknown command":

> `mo_getelements returns arrays and is available only in-process; over the
> scripting interface use mo_getallelements, which returns the same data as
> text.`

That is a two-line registration in the wire dispatch, and it is the difference
between a user finding the answer and filing a bug.

### Why not one name with a mode-dependent return

It is achievable once the transport flag exists, and it is still wrong. The same
script, run in-process and over the wire, would receive an ndarray in one case
and a parsed list in the other. Every script that touches the result would need
to branch on its own transport, which is exactly the property the pyFEMM
drop-in shim exists to remove.

One name, one type.

## What to build first, and how to know it worked

1. **`mo_getb` / `mo_geta`, scalar per-element form.** Owed anyway — they are in
   the 62 of `pyfemm-inprocess-compat.md`, and `mySPMLossScriptAGB.py:164`
   carries the comment "No mo_getb". Small, and independently useful.
2. **`mo_getnodes` / `mo_getelements`.** The measured win: 12.7 µs/element
   becomes sub-microsecond (`array-pipeline.md`).
3. **`mo_getall*` returning a list in-process.** Compatibility, not speed.

**The benchmark trap, restated because it will mislead whoever measures this:**
reading B per element today goes through `mo_getpointvalues(cx, cy)` at the
centroid, which is a *point location* — a search for the element containing a
point the caller already identified, 4.5 µs measured. A bulk getter removes the
search as well as the loop. Comparing "loop vs bulk" without separating those
two credits the array API with a speedup that belongs to not searching.

So the honest comparison is three-way: per-element loop with point location
(today), per-element loop with direct element read, and bulk.

## Testing

`MOBlockIntegralTest.py` and friends already pin the values these getters
expose, against Original FEMM. So the test for a bulk getter is **agreement with
the scalar getter it replaces**, element by element, over a real mesh — not a
table of expected numbers:

* `mo_getelements()` columns against `mo_getelement(k+1)` for every k;
* `mo_getb()` against `mo_getpointvalues()` at each centroid, which is also the
  check that the point-location and direct-read paths agree;
* dtypes explicitly: connectivity and group must come back integer, not float.

And one property test the wire cannot give you: the arrays must survive a
`gc.collect()` with the results document closed, since they are copies rather
than views into `s_rdoc`.
