# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker and contributors
#
# csv -- the standard module, for femm-qt scripts.
#
# Written here rather than vendored, because there is nothing to vendor.
# CPython's `csv` LOOKS like pure Python -- Lib/csv.py is 500 lines with only
# `import re` at the top -- but every one of reader, writer, Dialect, Error and
# the QUOTE_* constants comes `from _csv`, which is a built-in C module. What
# Lib/csv.py itself contributes is the dialect presets, DictReader, DictWriter
# and Sniffer, all of them sitting on top of the C parser. So the engine is the
# part that had to be written, and it is written here from the format's rules
# rather than translated from anyone's source.
#
# It is in-process for the same reason datetime is (see pylib/README.md):
# a script that writes a table of results should not need CPython installed,
# and a row should not cost a network round trip. csv also works over PyBridge
# if you want the real thing -- `pybridge.import_module("csv")` -- but there is
# no reason to.
#
# The parser is a character state machine, and it is a faithful one: the states
# below are CPython's, because the awkward cases (a quote inside an unquoted
# field, a delimiter after a closing quote, a field spanning lines) are defined
# by nothing but what the reference implementation does with them. Every one of
# those cases is pinned against CPython in testing/CsvTest.py.
#
# Divergences from CPython, all of them forced and all of them documented:
#
#   * DictReader yields an OrderedDict, not a dict. MicroPython's dict does not
#     preserve insertion order -- a plain dict would scramble the columns, so
#     `list(row.keys())` would not match the header. OrderedDict is a real dict
#     subclass here (isinstance(row, dict) is True), so this is closer to
#     CPython 3.8+'s observable behaviour than a dict would be, not further.
#   * Sniffer is an independent heuristic, not CPython's. It agrees with
#     CPython on ordinary files; on a genuinely ambiguous one the two may pick
#     differently, because there is no right answer to agree about.
#   * No __class_getitem__ (csv.DictReader[str, str] type subscripting). That
#     needs the `types` module, which this interpreter has not got, and it is
#     annotation sugar with no runtime meaning.

__all__ = ["QUOTE_MINIMAL", "QUOTE_ALL", "QUOTE_NONNUMERIC", "QUOTE_NONE",
           "QUOTE_STRINGS", "QUOTE_NOTNULL",
           "Error", "Dialect", "excel", "excel_tab", "unix_dialect",
           "field_size_limit", "reader", "writer",
           "register_dialect", "get_dialect", "list_dialects",
           "unregister_dialect", "Sniffer", "DictReader", "DictWriter"]

from collections import OrderedDict

QUOTE_MINIMAL = 0
QUOTE_ALL = 1
QUOTE_NONNUMERIC = 2
QUOTE_NONE = 3
QUOTE_STRINGS = 4      # 3.12+: quote every string, leave other types bare
QUOTE_NOTNULL = 5      # 3.12+: quote everything that is not None


class Error(Exception):
    pass


_field_limit = 131072


def field_size_limit(new_limit=None):
    """Read, or set, the longest field the reader will accept.

    A guard against a runaway parse rather than a real limit: an unterminated
    quote turns the rest of the file into one field, and without this the
    symptom is silent memory growth instead of an error naming the line.
    """
    global _field_limit
    old = _field_limit
    if new_limit is not None:
        if not isinstance(new_limit, int):
            raise TypeError("limit must be an integer")
        _field_limit = new_limit
    return old


# ---------------------------------------------------------------------------
# Dialects
# ---------------------------------------------------------------------------

class Dialect:
    """The formatting rules, and their validation.

    CPython validates by round-tripping the dialect through the C reader's
    constructor; there is no C here, so validation is explicit -- and it has to
    exist, because the failure it prevents (a two-character delimiter, say) does
    not show up until the parse silently does the wrong thing.
    """
    _name = ""
    _valid = False
    delimiter = None
    quotechar = None
    escapechar = None
    doublequote = None
    skipinitialspace = None
    lineterminator = None
    quoting = None
    strict = False

    def __init__(self):
        if self.__class__ != Dialect:
            self._valid = True
        self._validate()

    def _validate(self):
        if not isinstance(self.delimiter, str) or len(self.delimiter) != 1:
            raise TypeError('"delimiter" must be a 1-character string')
        if self.quotechar is None:
            if self.quoting != QUOTE_NONE:
                raise TypeError("quotechar must be set if quoting enabled")
        elif not isinstance(self.quotechar, str) or len(self.quotechar) != 1:
            raise TypeError('"quotechar" must be a 1-character string')
        if self.escapechar is not None and (
                not isinstance(self.escapechar, str) or len(self.escapechar) != 1):
            raise TypeError('"escapechar" must be a 1-character string')
        if self.quoting not in (QUOTE_MINIMAL, QUOTE_ALL, QUOTE_NONNUMERIC,
                                QUOTE_NONE, QUOTE_STRINGS, QUOTE_NOTNULL):
            raise TypeError("bad 'quoting' value")
        if not isinstance(self.lineterminator, str):
            raise TypeError('"lineterminator" must be a string')
        self.doublequote = bool(self.doublequote)
        self.skipinitialspace = bool(self.skipinitialspace)
        self.strict = bool(self.strict)


class excel(Dialect):
    """The dialect Excel reads and writes, and the default everywhere."""
    delimiter = ","
    quotechar = '"'
    doublequote = True
    skipinitialspace = False
    lineterminator = "\r\n"
    quoting = QUOTE_MINIMAL


class excel_tab(excel):
    """excel, with tabs."""
    delimiter = "\t"


class unix_dialect(Dialect):
    """Quote everything, and end lines the way Unix does."""
    delimiter = ","
    quotechar = '"'
    doublequote = True
    skipinitialspace = False
    lineterminator = "\n"
    quoting = QUOTE_ALL


_dialects = {"excel": excel, "excel-tab": excel_tab, "unix": unix_dialect}

_ATTRS = ("delimiter", "quotechar", "escapechar", "doublequote",
          "skipinitialspace", "lineterminator", "quoting", "strict")


class _Params(Dialect):
    """One dialect's rules, resolved from a name/class/instance plus kwargs.

    A fresh object every time rather than the registered class itself, so that
    per-call fmtparams cannot leak back into a registered dialect and change it
    for everyone else.
    """

    def __init__(self, dialect, kwargs):
        if dialect is None:
            dialect = excel
        if isinstance(dialect, str):
            dialect = get_dialect(dialect)
        for a in _ATTRS:
            setattr(self, a, getattr(dialect, a, None))
        for k in kwargs:
            if k not in _ATTRS:
                raise TypeError('"%s" is an invalid keyword argument for this '
                                "function" % k)
            setattr(self, k, kwargs[k])
        # A dialect given as an object may leave these unset; fill from excel
        # so a partial dialect behaves like a partial dialect, not a broken one.
        for a in _ATTRS:
            if getattr(self, a) is None and a not in ("quotechar", "escapechar"):
                setattr(self, a, getattr(excel, a))
        self._validate()


def register_dialect(name, dialect=None, **fmtparams):
    if not isinstance(name, str):
        raise TypeError("dialect name must be a string")
    _dialects[name] = _Params(dialect, fmtparams)


def unregister_dialect(name):
    if name not in _dialects:
        raise Error('unknown dialect: "%s"' % name)
    del _dialects[name]


def get_dialect(name):
    try:
        return _dialects[name]
    except KeyError:
        raise Error('unknown dialect: "%s"' % name)


def list_dialects():
    return list(_dialects.keys())


# ---------------------------------------------------------------------------
# reader
# ---------------------------------------------------------------------------

# Parser states. Names are CPython's, so that anyone comparing this with
# _csv.c is reading the same machine.
(_START_RECORD, _START_FIELD, _ESCAPED_CHAR, _IN_FIELD, _IN_QUOTED_FIELD,
 _ESCAPE_IN_QUOTED_FIELD, _QUOTE_IN_QUOTED_FIELD, _EAT_CRNL,
 _AFTER_ESCAPED_CRNL) = range(9)

_EOL = None      # the sentinel processed at the end of each input string


class reader:
    """Iterate the rows of a CSV source, each row a list of strings.

    The source is any iterable of strings -- an open file, a list, a generator.
    A file should be opened with newline="" so that a newline inside a quoted
    field reaches the parser instead of being translated away.
    """

    def __init__(self, csvfile, dialect=None, **fmtparams):
        self._it = iter(csvfile)
        self.dialect = _Params(dialect, fmtparams)
        self.line_num = 0
        self._state = _START_RECORD
        self._fields = []
        self._field = []
        self._numeric = False

    def __iter__(self):
        return self

    # -- field assembly ---------------------------------------------------

    def _add(self, c):
        if len(self._field) >= _field_limit:
            raise Error("field larger than field limit (%d)" % _field_limit)
        self._field.append(c)

    def _save(self):
        s = "".join(self._field)
        self._field = []
        if self._numeric:
            # QUOTE_NONNUMERIC: an UNQUOTED field is a number. A quoted one is
            # text, which is what makes the round trip lossless.
            self._numeric = False
            try:
                s = float(s)
            except ValueError:
                raise Error("could not convert string to float: %r" % s)
        self._fields.append(s)

    # -- the state machine ------------------------------------------------

    def _process(self, c):
        d = self.dialect
        st = self._state

        if st == _START_RECORD:
            if c == _EOL:
                return
            if c == "\n" or c == "\r":
                self._state = _EAT_CRNL
                return
            self._state = _START_FIELD
            st = _START_FIELD
            # fall through and process this character as the field's first

        if st == _START_FIELD:
            if c == "\n" or c == "\r" or c == _EOL:
                self._save()
                self._state = _START_RECORD if c == _EOL else _EAT_CRNL
            elif c == d.quotechar and d.quoting != QUOTE_NONE:
                self._state = _IN_QUOTED_FIELD
            elif c == d.escapechar:
                self._state = _ESCAPED_CHAR
            elif c == " " and d.skipinitialspace:
                pass
            elif c == d.delimiter:
                self._save()
            else:
                if d.quoting == QUOTE_NONNUMERIC:
                    self._numeric = True
                self._add(c)
                self._state = _IN_FIELD

        elif st == _ESCAPED_CHAR:
            if c == "\n" or c == "\r":
                self._add(c)
                self._state = _AFTER_ESCAPED_CRNL
            elif c == _EOL:
                self._add("\n")
                self._state = _IN_FIELD
            else:
                self._add(c)
                self._state = _IN_FIELD

        elif st == _AFTER_ESCAPED_CRNL or st == _IN_FIELD:
            # An escaped newline continues the same unquoted field, so the two
            # states behave identically from here.
            if c == "\n" or c == "\r" or c == _EOL:
                self._save()
                self._state = _START_RECORD if c == _EOL else _EAT_CRNL
            elif c == d.escapechar:
                self._state = _ESCAPED_CHAR
            elif c == d.delimiter:
                self._save()
                self._state = _START_FIELD
            else:
                self._add(c)
                self._state = _IN_FIELD

        elif st == _IN_QUOTED_FIELD:
            if c == _EOL:
                pass                      # the field continues on the next line
            elif c == d.escapechar:
                self._state = _ESCAPE_IN_QUOTED_FIELD
            elif c == d.quotechar and d.quoting != QUOTE_NONE:
                self._state = _QUOTE_IN_QUOTED_FIELD if d.doublequote else _IN_FIELD
            else:
                self._add(c)

        elif st == _ESCAPE_IN_QUOTED_FIELD:
            self._add("\n" if c == _EOL else c)
            self._state = _IN_QUOTED_FIELD

        elif st == _QUOTE_IN_QUOTED_FIELD:
            # Just past a quotechar inside a quoted field: doubled means a
            # literal quote, otherwise the field has ended.
            if d.quoting != QUOTE_NONE and c == d.quotechar:
                self._add(c)
                self._state = _IN_QUOTED_FIELD
            elif c == d.delimiter:
                self._save()
                self._state = _START_FIELD
            elif c == "\n" or c == "\r" or c == _EOL:
                self._save()
                self._state = _START_RECORD if c == _EOL else _EAT_CRNL
            elif not d.strict:
                self._add(c)
                self._state = _IN_FIELD
            else:
                raise Error("'%s' expected after '%s'" % (d.delimiter, d.quotechar))

        elif st == _EAT_CRNL:
            if c == "\n" or c == "\r":
                pass
            elif c == _EOL:
                self._state = _START_RECORD
            else:
                raise Error("new-line character seen in unquoted field - do you "
                            "need to open the file with newline=''?")

    def __next__(self):
        self._fields = []
        if self._state == _START_RECORD:
            self._field = []
        while True:
            try:
                line = next(self._it)
            except StopIteration:
                # A file that stops mid-quote still owes us its last field;
                # anything else at EOF is genuinely the end.
                if self._field or self._state == _IN_QUOTED_FIELD:
                    if self._state == _IN_QUOTED_FIELD and self.dialect.strict:
                        raise Error("unexpected end of data")
                    self._save()
                    self._state = _START_RECORD
                    return self._fields
                raise
            self.line_num += 1
            if not isinstance(line, str):
                raise Error("iterator should return strings, not %s "
                            "(the file should be opened in text mode)"
                            % type(line).__name__)
            if "\0" in line:
                raise Error("line contains NUL")
            for ch in line:
                self._process(ch)
            self._process(_EOL)
            if self._state == _START_RECORD:
                return self._fields


# ---------------------------------------------------------------------------
# writer
# ---------------------------------------------------------------------------

class writer:
    """Write rows to any object with a write() method."""

    def __init__(self, csvfile, dialect=None, **fmtparams):
        if not hasattr(csvfile, "write"):
            raise TypeError("argument 1 must have a 'write' method")
        self._write = csvfile.write
        self.dialect = _Params(dialect, fmtparams)

    def _render(self, field, single):
        """One field, escaped and quoted exactly as the dialect asks.

        Quoting is decided WHILE escaping, not before it, and that ordering is
        the whole subtlety. A field containing the quotechar does not
        automatically need quotes: if doublequote is off and an escapechar is
        set, the quote is escaped and the field stays bare. Deciding first and
        escaping afterwards gets those cases wrong in a way that still round-
        trips through this same module -- so only a comparison against CPython
        catches it, which is what testing/CsvTest.py is for.
        """
        d = self.dialect
        q = d.quoting

        # What the TYPE alone says. Two wrinkles, both found by comparison
        # rather than by thought:
        #   bool is listed explicitly because MicroPython's bool is NOT an int
        #     subclass -- isinstance(True, int) is False here and True in
        #     CPython -- so without it a boolean would come out quoted.
        #   complex is a number here as it is in CPython, and it matters more:
        #     half of what FEMM returns is a phasor, and quoting those under
        #     QUOTE_NONNUMERIC writes a file the same setting cannot read back.
        if q == QUOTE_ALL:
            quoted = True
        elif q == QUOTE_NONNUMERIC:
            quoted = not isinstance(field, (int, float, complex, bool))
        elif q == QUOTE_STRINGS:
            quoted = isinstance(field, str)
        elif q == QUOTE_NOTNULL:
            quoted = field is not None
        else:
            quoted = False

        if field is None:
            s = ""                      # None is an empty field, never "None"
        elif isinstance(field, str):
            s = field
        else:
            s = str(field)

        out = []
        for ch in s:
            special = (ch == d.delimiter or ch == d.quotechar or
                       ch == d.escapechar or ch in d.lineterminator)
            if special:
                want_escape = False
                if q == QUOTE_NONE:
                    want_escape = True
                elif ch == d.quotechar:
                    if d.doublequote:
                        out.append(d.quotechar)     # doubled, and so quoted
                    else:
                        want_escape = True
                elif ch == d.escapechar:
                    want_escape = True
                if not want_escape and q != QUOTE_NONE:
                    quoted = True
                if want_escape:
                    if d.escapechar is None:
                        raise Error("need to escape, but no escapechar is set")
                    out.append(d.escapechar)
            out.append(ch)
        body = "".join(out)

        if single and body == "":
            # A lone empty field would be written as an empty line, which reads
            # back as a row of NO fields. Quotes are the only way to say "one
            # field, and it is empty" -- so where quoting is forbidden, this
            # cannot be represented at all.
            if q == QUOTE_NONE:
                raise Error("single empty field record must be quoted")
            quoted = True

        if quoted:
            return d.quotechar + body + d.quotechar
        return body

    def writerow(self, row):
        try:
            fields = list(row)
        except TypeError:
            raise Error("iterable expected, not %s" % type(row).__name__)
        only = len(fields) == 1
        line = self.dialect.delimiter.join(
            [self._render(f, only) for f in fields])
        return self._write(line + self.dialect.lineterminator)

    def writerows(self, rows):
        for row in rows:
            self.writerow(row)


# ---------------------------------------------------------------------------
# The dict wrappers
# ---------------------------------------------------------------------------

class DictReader:
    """reader, but each row is a mapping keyed by the header.

    Yields an OrderedDict rather than a dict: MicroPython's dict does not keep
    insertion order, and a scrambled row is worse than an unfamiliar type. It
    is a dict subclass, so isinstance(row, dict) holds.
    """

    def __init__(self, f, fieldnames=None, restkey=None, restval=None,
                 dialect=None, **fmtparams):
        self._fieldnames = list(fieldnames) if fieldnames is not None else None
        self.restkey = restkey
        self.restval = restval
        self.reader = reader(f, dialect, **fmtparams)
        self.dialect = dialect
        self.line_num = 0

    def __iter__(self):
        return self

    @property
    def fieldnames(self):
        if self._fieldnames is None:
            try:
                self._fieldnames = next(self.reader)
            except StopIteration:
                pass
            self.line_num = self.reader.line_num
        return self._fieldnames

    @fieldnames.setter
    def fieldnames(self, value):
        self._fieldnames = list(value) if value is not None else None

    def __next__(self):
        if self.line_num == 0:
            self.fieldnames                     # consume the header row
        row = next(self.reader)
        self.line_num = self.reader.line_num
        while row == []:                        # blank lines are not records
            row = next(self.reader)
            self.line_num = self.reader.line_num

        names = self.fieldnames
        d = OrderedDict()
        n = len(names)
        for i in range(n):
            d[names[i]] = row[i] if i < len(row) else self.restval
        if len(row) > n:
            d[self.restkey] = row[n:]
        return d


class DictWriter:
    """writer, taking a mapping per row and a fixed column order."""

    def __init__(self, f, fieldnames, restval="", extrasaction="raise",
                 dialect=None, **fmtparams):
        self.fieldnames = fieldnames
        self.restval = restval
        if extrasaction not in ("raise", "ignore"):
            raise ValueError("extrasaction (%s) must be 'raise' or 'ignore'"
                             % extrasaction)
        self.extrasaction = extrasaction
        self.writer = writer(f, dialect, **fmtparams)

    def writeheader(self):
        # A dict of name->name, so the header goes through exactly the same
        # quoting path as the data. CPython does this too.
        header = OrderedDict()
        for k in self.fieldnames:
            header[k] = k
        return self.writerow(header)

    def _to_row(self, rowdict):
        if self.extrasaction == "raise":
            extra = [k for k in rowdict if k not in self.fieldnames]
            if extra:
                raise ValueError("dict contains fields not in fieldnames: "
                                 + ", ".join([repr(x) for x in extra]))
        return [rowdict.get(key, self.restval) for key in self.fieldnames]

    def writerow(self, rowdict):
        return self.writer.writerow(self._to_row(rowdict))

    def writerows(self, rowdicts):
        for rowdict in rowdicts:
            self.writerow(rowdict)


# ---------------------------------------------------------------------------
# Sniffer
# ---------------------------------------------------------------------------

class Sniffer:
    """Guess a file's dialect, and whether its first row is a header.

    An independent heuristic rather than a port of CPython's: it agrees on
    ordinary files, and on an ambiguous one the two may differ, because there
    is no correct answer for them to agree on. Guessing is a convenience --
    when the format is known, name it and skip this entirely.
    """

    preferred = [",", "\t", ";", " ", ":"]

    def sniff(self, sample, delimiters=None):
        quotechar, doublequote, delim = self._guess_quote_and_delimiter(
            sample, delimiters)
        if not delim:
            delim, skipinitialspace = self._guess_delimiter(sample, delimiters)
            quotechar, doublequote = '"', False
        else:
            skipinitialspace = False
        if not delim:
            raise Error("Could not determine delimiter")

        class dialect(Dialect):
            _name = "sniffed"
            lineterminator = "\r\n"
            quoting = QUOTE_MINIMAL

        dialect.delimiter = delim
        dialect.quotechar = quotechar or '"'
        dialect.doublequote = doublequote
        dialect.skipinitialspace = skipinitialspace
        return dialect

    def _guess_quote_and_delimiter(self, data, delimiters):
        """Look for a quoted field, and read the delimiter off its edges.

        A quoted field is the strongest evidence there is: whatever sits
        immediately outside the quotes is the delimiter, with no counting
        needed. Done by scanning rather than by regex, because the patterns
        CPython uses lean on features this `re` does not have.
        """
        counts = {}      # candidate delimiter -> how often it framed a quote
        quotes = {}      # candidate quotechar -> how often it appeared
        dq = 0
        for qc in ('"', "'"):
            i = 0
            n = len(data)
            while True:
                start = data.find(qc, i)
                if start < 0:
                    break
                end = data.find(qc, start + 1)
                if end < 0:
                    break
                # A doubled quote is an escaped one; step over it and keep the
                # field open, which is also the evidence for doublequote.
                while end + 1 < n and data[end + 1] == qc:
                    dq += 1
                    nxt = data.find(qc, end + 2)
                    if nxt < 0:
                        break
                    end = nxt
                before = data[start - 1] if start > 0 else ""
                after = data[end + 1] if end + 1 < n else ""
                for ch in (before, after):
                    if ch and ch not in ("\r", "\n", qc):
                        if delimiters is None or ch in delimiters:
                            counts[ch] = counts.get(ch, 0) + 1
                            quotes[qc] = quotes.get(qc, 0) + 1
                i = end + 1
        if not counts:
            return "", False, ""
        best = ""
        for ch in counts:
            if best == "" or counts[ch] > counts[best]:
                best = ch
        qc = ""
        for q in quotes:
            if qc == "" or quotes[q] > quotes[qc]:
                qc = q
        return qc, dq > 0, best

    def _guess_delimiter(self, data, delimiters):
        """No quotes anywhere: take the character that appears equally often
        on every line, preferring the ones people actually use."""
        lines = [ln for ln in data.split("\n") if ln.strip()]
        if not lines:
            return "", False
        candidates = delimiters if delimiters else \
            self.preferred + ["|", "^"]
        best, best_count = "", 0
        for ch in candidates:
            counts = [ln.count(ch) for ln in lines]
            if counts[0] == 0:
                continue
            if len(set(counts)) != 1:
                continue                     # not consistent across lines
            if counts[0] > best_count:
                best, best_count = ch, counts[0]
        if not best:
            return "", False
        # Trailing space after the delimiter is common and harmless to allow.
        skipinitialspace = False
        for ln in lines:
            if best + " " in ln:
                skipinitialspace = True
                break
        return best, skipinitialspace

    def has_header(self, sample):
        """True if row 0 looks unlike the rows beneath it.

        The test is type and length: a column whose data rows are consistently
        numeric, or consistently one width, votes header if row 0 breaks the
        pattern. Ties go to "no header", since inventing one is the worse error.
        """
        rdr = reader(sample.split("\n"), self.sniff(sample))
        rows = [r for r in rdr if r]
        if len(rows) < 2:
            return False
        header, body = rows[0], rows[1:]
        ncols = len(header)
        votes = 0
        for col in range(ncols):
            values = [r[col] for r in body if col < len(r)]
            if not values:
                continue
            numeric = True
            for v in values:
                try:
                    float(v)
                except ValueError:
                    numeric = False
                    break
            if numeric:
                try:
                    float(header[col])
                    votes -= 1          # header looks like data
                except ValueError:
                    votes += 1
                continue
            widths = set([len(v) for v in values])
            if len(widths) == 1:
                w = values[0]
                votes += 1 if len(header[col]) != len(w) else -1
        return votes > 0
