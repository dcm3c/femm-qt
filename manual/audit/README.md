# Manual audit scripts

Scripts that check the manual against the program, rather than against
itself. They were written during the 2026-08 documentation pass and each one
found real defects — in the manual, and in several cases in femm-qt.

Run them after a round of feature work, or before a release. None of them
modifies anything; they print what disagrees and leave the judgement to you.

They locate the tree from their own position, so they can be run from
anywhere:

    python manual/audit/prose_scan.py

## The scripts

| script | what it compares |
|---|---|
| `prose_scan.py` | every `.tex` against itself: doubled words, stale program names, sentences that stop mid-clause |
| `fig_audit.py` | `\includegraphics` calls against the image files on disk |
| `dump_commands.py` | *(run inside femm_headless)* writes `commands.txt`, the scripting commands the interpreter actually exposes |
| `cmd_diff.py` | the scripting chapters against `commands.txt` |
| `arg_audit.py` | each documented argument list against the binding's own signature |
| `snake_scan.py` | the manual against Original FEMM's command list, for the snake_case spellings femm-qt does not support |

## Two that need a setup step

**`cmd_diff.py`** needs the command list, and the commands only exist once a
document does — they are registered on `open`/`newdocument`/`loadsolution`
(see `py_bind.cpp`). So the list has to come from a running FEMM:

    cd manual/audit
    ../../build/bin/femm_headless -run dump_commands.py
    python cmd_diff.py

(dump_commands.py runs inside femm-qt's MicroPython, which has no
`__file__`, so it uses paths relative to the working directory --- hence the
`cd`.)

**`snake_scan.py`** needs `of_commands.txt`, the names Original FEMM
registers. Regenerate it from an OF checkout with:

    grep -rho 'lua_register(lua,[^,)]*"[a-zA-Z_]*"' femm/*.cpp \
      | grep -o '"[a-zA-Z_]*"' | tr -d '"' | sort -u > of_commands.txt

## What they will not catch

Worth knowing, because a clean run is not a clean manual:

* **Cross-references written as literal text.** `Figure~0` and `Appendix~3`
  printed as bare numbers in four places; LaTeX reports no undefined
  reference because there is no reference. Grep for `Figure~[0-9]`,
  `Section~[0-9]`, `Appendix~[0-9]`.
* **Floats that land far from their text.** A table too tall for its page is
  silently deferred — one landed 68 pages past the text referencing it, with
  a completely clean build log. Only looking at the rendered page finds this.
* **Prose that is merely wrong.** No diff catches "the volume integral of
  Lorentz torque" in a paragraph about force.

## The prior worth keeping

When the manual and the program disagree, check Original FEMM before
assuming either is right. Over this pass the split was roughly even between
femm-qt having drifted from OF, femm-qt having a bug OF does not, and the
2004 manual having been wrong from the start. `femm42src` settles which: its
`.rc` files are the authority on what a dialog label used to say, and its
`*_libdlg.cpp` on what a file is called.
