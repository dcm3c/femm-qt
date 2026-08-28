"""Record what every pyfemm wrapper emits, driven by contract.tsv.

    python3 emit_pyfemm.py

No engine is started and no socket is opened: the session object is constructed
without __init__ and its transport replaced by a recorder, so this runs in a
second and cannot be affected by whatever FEMM is or is not installed.

Writes emitted_pyfemm.tsv:  wrapper <TAB> pass <TAB> emission
with the emission replaced by MISSING or ERROR:... when the wrapper does not
exist or refuses the contract's arity.
"""
import io
import os
import sys
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "pyfemm"))

import femm  # noqa: E402

EMIT = []


def _record(self, s):
    EMIT.append(s)
    return []


femm.FemmSession.callfemm = _record
femm.FemmSession.callfemm_noeval = _record


def sentinels(kinds, which):
    """The same values every binding's recorder uses. See PASSES in README."""
    args = []
    for i, k in enumerate(kinds, 1):
        if k == "s":
            args.append("s%d" % i)
        elif which == "int":
            args.append(i)
        elif which == "real":
            args.append(-1.5 * i + 0.25)
        else:
            args.append(complex(i, -0.25 * i))
    return args


def main():
    contract = os.path.join(HERE, "contract.tsv")
    if not os.path.exists(contract):
        print("no contract.tsv -- run probe_octavefemm.m then make_contract.py")
        return 2

    sess = femm.FemmSession.__new__(femm.FemmSession)
    sess._sock = None
    sess._proc = None

    out = io.open(os.path.join(HERE, "emitted_pyfemm.tsv"), "w",
                  encoding="utf-8", newline="\n")
    nok = nmissing = nerr = 0
    for line in io.open(contract, encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        name, arity, kinds = (line.rstrip("\n").split("\t") + ["", "", ""])[:3]
        arity = int(arity)
        fn = getattr(sess, name, None)
        if fn is None or not callable(fn):
            out.write("%s\tint\tMISSING\n" % name)
            nmissing += 1
            continue
        got_any = False
        for which in ("int", "real", "cplx"):
            del EMIT[:]
            try:
                fn(*sentinels(kinds, which))
            except Exception as exc:      # noqa: BLE001 -- any failure is data
                if not EMIT:
                    out.write("%s\t%s\tERROR:%s: %s\n"
                              % (name, which, type(exc).__name__,
                                 str(exc).replace("\t", " ").split("\n")[0]))
                    nerr += 1
                    continue
            for e in EMIT:
                out.write("%s\t%s\t%s\n" % (name, which, e.replace("\t", " ")))
                got_any = True
        if got_any:
            nok += 1
    out.close()
    print("pyfemm: %d wrappers recorded, %d missing, %d refused the contract"
          % (nok, nmissing, nerr))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception:
        traceback.print_exc()
        sys.exit(1)
