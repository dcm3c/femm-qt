# The csv module (pylib/csv.py). Runs inside femm_headless:
#
#     build/bin/femm_headless -run testing/CsvTest.py
#
# Unlike datetime there is no upstream implementation to vendor and no upstream
# test suite to run: CPython's csv is a C extension, so every line of
# pylib/csv.py was written here and every expected value below was taken from
# CPython 3.13. The cases are the ones where a plausible implementation and a
# correct one differ -- a quote inside an unquoted field, a delimiter after a
# closing quote, whether a field containing the quotechar needs quoting when an
# escapechar is available. All four writer bugs this module ever had were in
# that last category, and all four were found by comparison rather than thought.
#
# It also pins two things that are NOT csv: newline="" on open() (patch 07,
# without which every CSV written on Windows gets doubled carriage returns),
# and the fact that MicroPython's bool is not an int subclass, which decides
# whether True is quoted under QUOTE_NONNUMERIC.
#
# Worth knowing when editing: this file also runs under CPython
# (`python testing/CsvTest.py`), and everything passes there except the two
# newline= cases, which assert a femm-qt-specific restriction. That makes it a
# cross-check as well as a regression test -- if a new assertion fails under
# CPython, the assertion is wrong, not pylib/csv.py.

import csv

fails = []


def check(label, got, want):
    if got != want:
        fails.append("%s: got %r, want %r" % (label, got, want))


class Out:
    def __init__(self):
        self.buf = []

    def write(self, s):
        self.buf.append(s)
        return len(s)

    def value(self):
        return "".join(self.buf)


def wrote(row, **kw):
    o = Out()
    csv.writer(o, **kw).writerow(row)
    return o.value()


def read(text, **kw):
    return list(csv.reader(iter([text]), **kw))


# --- constants and the registry ---------------------------------------------
check("QUOTE_* values",
      (csv.QUOTE_MINIMAL, csv.QUOTE_ALL, csv.QUOTE_NONNUMERIC, csv.QUOTE_NONE,
       csv.QUOTE_STRINGS, csv.QUOTE_NOTNULL), (0, 1, 2, 3, 4, 5))
check("default dialects", sorted(csv.list_dialects()),
      ["excel", "excel-tab", "unix"])
check("excel is the default", csv.get_dialect("excel").delimiter, ",")
check("excel terminator", csv.get_dialect("excel").lineterminator, "\r\n")
check("unix quotes everything", csv.get_dialect("unix").quoting, csv.QUOTE_ALL)
check("field_size_limit default", csv.field_size_limit(), 131072)

# --- reading ----------------------------------------------------------------
check("plain", read("a,b,c"), [["a", "b", "c"]])
check("trailing CRLF", read("a,b,c\r\n"), [["a", "b", "c"]])
# An empty string from the iterator is still a line, and a line with no fields
# is an empty row. Only an exhausted iterator ends the reader.
check("empty line is an empty row", read(""), [[]])
check("blank line is an empty row", read("\r\n"), [[]])
check("no lines at all", list(csv.reader(iter([]))), [])
check("empty fields", read("a,,c"), [["a", "", "c"]])
check("all empty", read(",,"), [["", "", ""]])
check("quoted", read('"a","b"'), [["a", "b"]])
check("delimiter inside quotes", read('"a,b",c'), [["a,b", "c"]])
check("doubled quote", read('"a""b",c'), [['a"b', "c"]])
check("quoted empties", read('"",""'), [["", ""]])
check("newline inside quotes", read('"a\nb",c'), [["a\nb", "c"]])
check("CRLF inside quotes", read('"a\r\nb",c'), [["a\r\nb", "c"]])
check("unterminated quote at EOF", read('a,"b'), [["a", "b"]])
# The cases where a guess would go wrong: text after a closing quote, and a
# bare quote in the middle of an unquoted field.
check("junk after closing quote", read('"a" b,c'), [["a b", "c"]])
check("quote inside unquoted field", read('a"b,c'), [['a"b', "c"]])
check("quote reopened", read('"a"b",c'), [['ab"', "c"]])
check("spaces are data", read("a,b ,  c"), [["a", "b ", "  c"]])
check("skipinitialspace", read("a, b,  c", skipinitialspace=True),
      [["a", "b", "c"]])
check("trailing delimiter", read("a,b,"), [["a", "b", ""]])
check("leading delimiter", read(",a"), [["", "a"]])
check("tab dialect", read("a\tb", delimiter="\t"), [["a", "b"]])
check("semicolon dialect", read("a;b", delimiter=";"), [["a", "b"]])
check("single quote as quotechar", read("'a,b',c", quotechar="'"),
      [["a,b", "c"]])
check("escapechar", read("a\\,b,c", escapechar="\\"), [["a,b", "c"]])
check("QUOTE_NONE keeps quotes", read('"a",b', quoting=csv.QUOTE_NONE),
      [['"a"', "b"]])
check("multiple records",
      list(csv.reader(iter(["a,b\n", "c,d\n"]))), [["a", "b"], ["c", "d"]])
check("blank line between records",
      list(csv.reader(iter(["a,b\n", "\n", "c,d\n"]))),
      [["a", "b"], [], ["c", "d"]])
check("field spanning two lines",
      list(csv.reader(iter(['"a\n', 'b",c\n']))), [["a\nb", "c"]])

# A bare CR mid-line is the "opened without newline=''" symptom, and it must
# say so rather than silently producing a wrong row.
try:
    read("a,b\rc,d")
    fails.append("bare CR in unquoted field should have raised")
except csv.Error:
    pass

# strict only changes the malformed cases
check("strict is normal on good input", read("a,b", strict=True), [["a", "b"]])
try:
    read('"a"b",c', strict=True)
    fails.append("strict should reject text after a closing quote")
except csv.Error:
    pass

# QUOTE_NONNUMERIC converts UNQUOTED fields to float; quoted stay text.
check("nonnumeric reads numbers", read("1,2.5", quoting=csv.QUOTE_NONNUMERIC),
      [[1.0, 2.5]])
check("nonnumeric keeps quoted text", read('"a",1', quoting=csv.QUOTE_NONNUMERIC),
      [["a", 1.0]])
try:
    read("a,1", quoting=csv.QUOTE_NONNUMERIC)
    fails.append("nonnumeric should reject unquoted text")
except (csv.Error, ValueError):
    pass

# --- writing ----------------------------------------------------------------
check("plain write", wrote(["a", "b"]), "a,b\r\n")
check("delimiter forces quotes", wrote(["a,b", "c"]), '"a,b",c\r\n')
check("quote is doubled", wrote(['say "hi"']), '"say ""hi"""\r\n')
check("newline forces quotes", wrote(["a\nb"]), '"a\nb"\r\n')
check("CR forces quotes", wrote(["a\rb"]), '"a\rb"\r\n')
check("empty row", wrote([]), "\r\n")
# A lone empty field must be quoted or the line would read back as no fields.
check("single empty field is quoted", wrote([""]), '""\r\n')
check("two empty fields are not", wrote(["", ""]), ",\r\n")
check("None is an empty field", wrote([None, "x"]), ",x\r\n")
check("numbers are str()", wrote([1, 2.5]), "1,2.5\r\n")
check("QUOTE_ALL", wrote(["a", 1], quoting=csv.QUOTE_ALL), '"a","1"\r\n')
check("QUOTE_ALL quotes None", wrote([None], quoting=csv.QUOTE_ALL), '""\r\n')
check("QUOTE_NONNUMERIC", wrote(["a", 1, 2.5], quoting=csv.QUOTE_NONNUMERIC),
      '"a",1,2.5\r\n')
check("QUOTE_NONNUMERIC quotes None",
      wrote([None, 1], quoting=csv.QUOTE_NONNUMERIC), '"",1\r\n')
# bool is NOT an int subclass in MicroPython (it is in CPython), so this is a
# deliberate special case rather than something isinstance gives for free.
check("QUOTE_NONNUMERIC leaves bool bare",
      wrote([True, False], quoting=csv.QUOTE_NONNUMERIC), "True,False\r\n")
# complex is a number here as in CPython -- and FEMM returns phasors, so
# quoting them would write a file this same setting could not read back.
check("QUOTE_NONNUMERIC leaves complex bare",
      wrote([1 + 2j], quoting=csv.QUOTE_NONNUMERIC), "(1+2j)\r\n")
check("QUOTE_STRINGS", wrote(["a", 1, None], quoting=csv.QUOTE_STRINGS),
      '"a",1,\r\n')
check("QUOTE_NOTNULL", wrote(["a", 1, None], quoting=csv.QUOTE_NOTNULL),
      '"a","1",\r\n')
check("QUOTE_NONE escapes", wrote(["a,b"], quoting=csv.QUOTE_NONE,
                                  escapechar="\\"), "a\\,b\r\n")
check("QUOTE_NONE escapes the quotechar",
      wrote(['say "hi"'], quoting=csv.QUOTE_NONE, escapechar="\\"),
      'say \\"hi\\"\r\n')
try:
    wrote(["a,b"], quoting=csv.QUOTE_NONE)
    fails.append("QUOTE_NONE with no escapechar should have raised")
except csv.Error:
    pass
try:
    wrote([""], quoting=csv.QUOTE_NONE, escapechar="\\")
    fails.append("QUOTE_NONE single empty field should have raised")
except csv.Error:
    pass

# The four that a "decide, then escape" writer gets wrong: with an escapechar
# available the special character is ESCAPED and the field stays bare.
check("escapechar means no quotes needed",
      wrote(["back\\slash"], escapechar="\\"), "back\\\\slash\r\n")
check("no doublequote: quote is escaped, field stays bare",
      wrote(['say "hi"'], doublequote=False, escapechar="\\"),
      'say \\"hi\\"\r\n')
check("but a delimiter still quotes",
      wrote(["a,b"], escapechar="\\"), '"a,b"\r\n')
try:
    wrote(['a"b'], doublequote=False)
    fails.append("no doublequote and no escapechar should have raised")
except csv.Error:
    pass

check("lineterminator", wrote(["a"], lineterminator="\n"), "a\n")
check("tab delimiter", wrote(["a", "b"], delimiter="\t"), "a\tb\r\n")
o = Out()
csv.writer(o).writerows([["a", "b"], ["c,d"]])
check("writerows", o.value(), 'a,b\r\n"c,d"\r\n')
o = Out()
csv.writer(o, "unix").writerow(["a", 1])
check("named dialect", o.value(), '"a","1"\n')

# --- round trip, which is what a user actually depends on -------------------
for label, kw in (("default", {}), ("all", {"quoting": csv.QUOTE_ALL}),
                  ("tab", {"delimiter": "\t"}), ("lf", {"lineterminator": "\n"}),
                  ("esc", {"escapechar": "\\"}),
                  ("nodouble", {"doublequote": False, "escapechar": "\\"})):
    for row in (["a", "b"], ["a,b", "c"], ['q"x', "y"], ["nl\nhere"],
                ["", "x"], ["  pad  "], ["back\\slash"], ["a\tb"]):
        back = read(wrote(row, **kw), **kw)
        if back != [row]:
            fails.append("round trip %s %r -> %r" % (label, row, back))

# --- the dict wrappers ------------------------------------------------------
SRC = ["name,x,y\n", "coil,1,2\n", "magnet,3,4\n"]
rows = list(csv.DictReader(iter(SRC)))
check("DictReader rows", len(rows), 2)
check("DictReader values", [r["name"] for r in rows], ["coil", "magnet"])
# MicroPython's dict does not preserve insertion order, so DictReader yields an
# OrderedDict; without it the columns would come back shuffled.
check("DictReader keeps column order", list(rows[0].keys()), ["name", "x", "y"])
check("DictReader row is a dict", isinstance(rows[0], dict), True)
check("DictReader fieldnames", csv.DictReader(iter(SRC)).fieldnames,
      ["name", "x", "y"])
check("DictReader short row",
      list(csv.DictReader(iter(["a,b,c\n", "1,2\n"]), restval="?"))[0]["c"], "?")
check("DictReader long row",
      list(csv.DictReader(iter(["a,b\n", "1,2,3,4\n"]), restkey="rest"))[0]["rest"],
      ["3", "4"])
check("DictReader given fieldnames",
      list(csv.DictReader(iter(["1,2\n"]), fieldnames=["p", "q"]))[0]["p"], "1")
check("DictReader skips blank lines",
      len(list(csv.DictReader(iter(["a\n", "\n", "1\n"])))), 1)

o = Out()
dw = csv.DictWriter(o, ["b", "a"])
dw.writeheader()
dw.writerow({"a": 1, "b": 2})
check("DictWriter honours field order", o.value(), "b,a\r\n2,1\r\n")
o = Out()
csv.DictWriter(o, ["a", "b"], restval="-").writerow({"a": 1})
check("DictWriter restval", o.value(), "1,-\r\n")
o = Out()
csv.DictWriter(o, ["a"], extrasaction="ignore").writerow({"a": 1, "zz": 2})
check("DictWriter ignores extras", o.value(), "1\r\n")
try:
    csv.DictWriter(Out(), ["a"]).writerow({"a": 1, "zz": 2})
    fails.append("DictWriter should reject unknown keys")
except ValueError:
    pass
try:
    csv.DictWriter(Out(), ["a"], extrasaction="bogus")
    fails.append("bad extrasaction should have raised")
except ValueError:
    pass

# --- dialect registration and validation ------------------------------------
csv.register_dialect("femm-pipe", delimiter="|", quoting=csv.QUOTE_ALL)
check("registered dialect is listed", "femm-pipe" in csv.list_dialects(), True)
check("registered dialect reads", read("a|b", dialect="femm-pipe"),
      [["a", "b"]])
o = Out()
csv.writer(o, "femm-pipe").writerow(["a"])
check("registered dialect writes", o.value(), '"a"\n' if False else '"a"\r\n')
# fmtparams must not leak back into the registered dialect
read("a|b", dialect="femm-pipe", delimiter=";")
check("dialect not mutated by fmtparams",
      csv.get_dialect("femm-pipe").delimiter, "|")
csv.unregister_dialect("femm-pipe")
check("unregistered", "femm-pipe" not in csv.list_dialects(), True)

for bad, exc in ((lambda: csv.get_dialect("nope"), csv.Error),
                 (lambda: csv.unregister_dialect("nope"), csv.Error),
                 (lambda: csv.reader(iter([""]), delimiter="ab"), TypeError),
                 (lambda: csv.reader(iter([""]), delimiter=1), TypeError),
                 (lambda: csv.reader(iter([""]), quoting=99), TypeError),
                 (lambda: csv.reader(iter([""]), nosuchparam=1), TypeError),
                 (lambda: csv.writer(object()), TypeError)):
    try:
        bad()
        fails.append("expected %s from a bad dialect" % exc.__name__)
    except exc:
        pass

old = csv.field_size_limit(10)
try:
    read("x" * 50)
    fails.append("field_size_limit was not enforced")
except csv.Error:
    pass
csv.field_size_limit(old)
check("field_size_limit restored", csv.field_size_limit(), 131072)

# --- Sniffer ----------------------------------------------------------------
sn = csv.Sniffer()
check("sniff comma", sn.sniff("a,b,c\n1,2,3\n").delimiter, ",")
check("sniff semicolon", sn.sniff("a;b;c\n1;2;3\n").delimiter, ";")
check("sniff tab", sn.sniff("a\tb\n1\t2\n").delimiter, "\t")
check("sniff quoted", sn.sniff('"a";"b"\r\n"c";"d"\r\n').delimiter, ";")
check("sniff sees doubled quotes",
      sn.sniff('"a""b",c\n"d",e\n').doublequote, True)
check("has_header yes", sn.has_header("name,x\ncoil,1\nmagnet,2\n"), True)
check("has_header no", sn.has_header("1,2\n3,4\n5,6\n"), False)

# --- newline="" (patch 07), without which every Windows CSV is malformed -----
# The writer emits \r\n; text mode then translates the \n to \r\n, so each
# record ends \r\r\n and Excel shows a blank row between every pair.
import os

tmp = "_csvtest_tmp.csv"
try:
    f = open(tmp, "w", newline="")
    csv.writer(f).writerow(["a", "b"])
    f.close()
    f = open(tmp, "rb")
    raw = f.read()
    f.close()
    check("newline='' writes exactly CRLF", raw, b"a,b\r\n")

    f = open(tmp, "w")
    csv.writer(f).writerow(["a", "b"])
    f.close()
    f = open(tmp, "rb")
    raw = f.read()
    f.close()
    # Not a bug being pinned as correct -- this is CPython's behaviour too, and
    # the reason newline='' has to exist.
    check("without newline='' the CR doubles (as in CPython)",
          raw in (b"a,b\r\r\n", b"a,b\r\n"), True)

    for bad in ("\n", "\r\n", "x"):
        try:
            open(tmp, "r", newline=bad).close()
            fails.append("newline=%r should have raised" % bad)
        except ValueError:
            pass
finally:
    try:
        os.remove(tmp)
    except Exception:
        pass

if fails:
    print("csv check: %d FAILED" % len(fails))
    for msg in fails:
        print("   " + msg)
else:
    print("csv check: reader, writer, dicts, dialects and Sniffer match "
          "CPython, and newline='' writes a file Excel reads")
