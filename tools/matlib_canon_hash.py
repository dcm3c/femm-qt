# matlib_canon_hash.py -- canonical content hash of a FEMM material library.
#
#     python3 matlib_canon_hash.py matlibs/matlib.dat [more .dat files]
#
# Mirrors, token for token, the canonicalisation in libfemm/material_lib_path.cpp
# (parseLibrary / canonicalHash): blocks sorted by name, properties sorted by
# key, every number rendered "%.12g", FNV-1a 64 over the lines. The value it
# prints is what goes into kKnownShippedLibraries when a library is revised.
# Runs under CPython and under femm_headless's MicroPython (no imports beyond
# sys/os), which is how testing/MaterialLibHistoryTest.py uses it.
import sys

def _trim(s):
    return s.strip(" \t\r\n")

def parse_library(path):
    blocks = []
    cur = None
    # Bytes, decoded as UTF-8 by hand: a text-mode open() takes the platform's
    # default encoding, which on Windows CPython is the ANSI code page, and
    # "1018 Steel (1400°F)" then hashes differently from the C++ (which
    # sees bytes) -- found 2026-10-06. MicroPython and macOS were already UTF-8.
    with open(path, "rb") as f:
        for rawb in f:
            line = _trim(rawb.decode("utf-8"))
            if not line:
                continue
            if line == "<BeginBlock>":
                cur = {"name": "", "props": {}, "rows": []}
                continue
            if line == "<EndBlock>":
                if cur is not None:
                    blocks.append(cur)
                cur = None
                continue
            if cur is None:
                continue
            eq = line.find("=")
            if line[0] == "<" and eq >= 0:
                close = line.find(">")
                if close < 0 or close > eq:
                    cur["rows"].append(line)
                    continue
                key = line[1:close]
                val = _trim(line[eq + 1:])
                if len(val) >= 2 and val[0] == '"' and val[-1] == '"':
                    val = val[1:-1]
                if key == "BlockName":
                    cur["name"] = val
                elif key.endswith("Points") and val.strip() == "0":
                    pass    # "<TKPoints> = 0": the writer spelling out an absence; same content
                else:
                    cur["props"][key] = val
            else:
                cur["rows"].append(line)
    return blocks

def _num(s):
    try:
        return float(s)
    except ValueError:
        return None

def canon_token(s):
    d = _num(s)
    if d is None or s == "":
        return s
    return "%.12g" % d

def canonical_hash(blocks):
    h = 1469598103934665603
    M = (1 << 64) - 1
    def feed(s):
        nonlocal h
        for b in s.encode("utf-8"):
            h ^= b
            h = (h * 1099511628211) & M
        h ^= 10
        h = (h * 1099511628211) & M
    for b in sorted(blocks, key=lambda x: x["name"]):
        feed("B " + b["name"])
        for k in sorted(b["props"]):
            feed(k + "=" + canon_token(b["props"][k]))
        for r in b["rows"]:
            feed("R " + " ".join(canon_token(t) for t in r.split()))
    return h if h else 1

if __name__ == "__main__":
    for p in sys.argv[1:]:
        bl = parse_library(p)
        print("%20d  %3d blocks  %s" % (canonical_hash(bl), len(bl), p))
