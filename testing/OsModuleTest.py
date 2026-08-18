# os / os.path check. Runs inside femm_headless:
#
#     build/bin/femm_headless -run testing/OsModuleTest.py
#
# What is actually being tested is not that mkdir makes a directory -- it is
# that os.*, the FEMM commands and Python's own open() agree about which file a
# path names, including a non-ASCII one. They did not before libfemm/py_os.cpp
# and micropython patch 05: os went through the CRT's narrow calls, which decode
# as ANSI on Windows, while every femm command widens to UTF-16. Both sides
# round-tripped consistently with themselves, so the disagreement only showed
# when a path crossed between them.

import os

fails = []


def check(label, got, want):
    if got != want:
        fails.append("%s: got %r, want %r" % (label, got, want))


base = os.getcwd()            # -run leaves us in the script's own directory
work = os.path.join(base, "_ostest")
uni = os.path.join(work, "Prüfung")     # Pruefung, with an umlaut


def clean():
    if os.path.exists(uni):
        for n in os.listdir(uni):
            os.remove(os.path.join(uni, n))
        os.rmdir(uni)
    if os.path.exists(work):
        for n in os.listdir(work):
            os.remove(os.path.join(work, n))
        os.rmdir(work)


clean()

# --- os.path is string surgery, and must match what fixpath does elsewhere ---
check("join", os.path.join("a", "b", "c.fem"), "a/b/c.fem")
check("join absolute wins", os.path.join("a", "/b"), "/b")
check("dirname", os.path.dirname("a/b/c.fem"), "a/b")
check("basename", os.path.basename("a/b/c.fem"), "c.fem")
check("split", os.path.split("a/b/c.fem"), ("a/b", "c.fem"))
check("splitext", os.path.splitext("a/b/c.fem"), ("a/b/c", ".fem"))
check("splitext none", os.path.splitext("a/b/c"), ("a/b/c", ""))
check("splitext dotfile", os.path.splitext("a/b/.rc"), ("a/b/.rc", ""))
# A backslash path is what FEMM users type; every other command normalises it.
check("backslash", os.path.basename("a\\b\\c.fem"), "c.fem")

# --- failures are OSError, as a script written against CPython expects ---
try:
    os.mkdir(os.path.join(work, "no", "parent"))
    fails.append("mkdir with a missing parent did not raise")
except OSError:
    pass
except Exception as e:
    fails.append("mkdir raised %s, not OSError" % type(e).__name__)

try:
    os.stat(os.path.join(base, "definitely_absent_file.fem"))
    fails.append("stat of a missing file did not raise")
except OSError:
    pass

# --- the working directory is one piece of state, however it is set ---
os.makedirs(uni)
check("makedirs", os.path.isdir(uni), True)

os.chdir(uni)
check("os.chdir into a unicode dir", os.getcwd(), uni)
check("getcwd global agrees", getcwd(), os.getcwd())
setcurrentdirectory(base)
check("setcurrentdirectory is the same call", os.getcwd(), base)

# --- the point: os.mkdir's directory is the one mi_saveas writes into ---
newdocument(0)
mi_addnode(0, 0)
mi_addnode(1, 0)
f = os.path.join(uni, "x.fem")
mi_saveas(f)
check("mi_saveas into an os.mkdir dir", os.path.isfile(f), True)
check("listdir sees it", os.listdir(uni), ["x.fem"])
check("getsize matches stat", os.path.getsize(f), os.stat(f)[6])

# --- and Python's own open(), which reaches the disk through the VFS ---
# open() dispatches: a lone .fem/.fee/.feh/.fec path opens a DOCUMENT (see the
# init script), so use the file opener explicitly here.
fh = _builtin_open(f)
head = fh.read()[:8]
fh.close()                     # Windows will not rename a file that is open
check("open() reads what femm wrote", head, "[Format]")

txt = os.path.join(uni, "note.txt")
fh = _builtin_open(txt, "w")
fh.write("hello")
fh.close()
check("open() writes into a unicode dir", os.path.isfile(txt), True)
fh = _builtin_open(txt)
check("open() reads it back", fh.read(), "hello")
fh.close()
os.remove(txt)

# --- import out of a unicode directory, and text vs binary file modes ---
# The module body has to exceed the VFS reader's ~48-byte buffer. Text mode
# collapses CRLF on read, so a full buffer read comes back short and the reader
# used to take that for EOF -- truncating the module mid-statement. A shorter
# module passes either way, which is why this went unnoticed.
import sys

mod = os.path.join(uni, "umlautmod.py")
fh = _builtin_open(mod, "w")
body = "VALUE = 42\ndef greet():\n    return 'hello from a unicode dir'\n"
fh.write(body)
fh.close()

sys.path.append(uni)
try:
    import umlautmod
    check("import from a unicode dir", umlautmod.VALUE, 42)
    check("its function runs", umlautmod.greet(), "hello from a unicode dir")
except Exception as e:
    fails.append("import from a unicode dir: %s: %s" % (type(e).__name__, e))
sys.path.pop()

# Text mode does what CPython's does on Windows: \n is written as the platform
# line ending and read back as \n, so the string round-trips even though the
# byte count does not. Elsewhere the two are the same number.
fh = _builtin_open(mod)
check("text read round-trips", fh.read(), body)
fh.close()
grew = os.path.getsize(mod) - len(body)
check("text write uses platform endings", grew, 3 if sys.platform == "win32" else 0)
os.remove(mod)

# Binary mode is exact everywhere -- it was not, before: the Windows CRT
# translated in "wb" too, so b"\n" reached the disk as two bytes.
raw = os.path.join(uni, "raw.bin")
fh = _builtin_open(raw, "wb")
fh.write(b"\n\r\n\x00")
fh.close()
check("binary write is not translated", os.path.getsize(raw), 4)
fh = _builtin_open(raw, "rb")
check("binary read matches", fh.read(), b"\n\r\n\x00")
fh.close()
os.remove(raw)

# --- rename, and the renamed file still opens as a document ---
g = os.path.join(uni, "y.fem")
os.rename(f, g)
check("rename", os.listdir(uni), ["y.fem"])
try:
    opendocument(g)
except Exception as e:
    fails.append("opendocument after rename: %s" % e)

clean()
check("cleaned up", os.path.exists(work), False)

if fails:
    print("os module check: %d FAILED" % len(fails))
    for msg in fails:
        print("   " + msg)
else:
    print("os module check: os.*, the femm commands, open() and import agree, "
          "including on a unicode path; text and binary modes match CPython")
