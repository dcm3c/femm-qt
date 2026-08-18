# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker and contributors
#
# datetime -- the standard module, for femm-qt scripts.
#
# Unlike matplotlib (see python/matplotlib/), this is NOT forwarded to the
# CPython worker. datetime is integer arithmetic over a calendar: there is
# nothing to gain by sending it across a socket, and three things to lose --
# it would need CPython installed just to timestamp a filename, every .year
# would cost a round trip, and the results would be proxies rather than the
# hashable, sortable values that scripts put in dicts and sort lists by.
#
# So it runs in-process. `_core.py` is micropython-lib's pure-Python datetime,
# copied VERBATIM -- do not edit it; re-vendoring must stay a plain file copy
# (README.md says where from). Everything femm-qt adds lives in this file, and
# almost all of it is additive: the only two things replaced rather than added
# are timezone.utc's name and datetime.timetuple (see its docstring).
#
# What is added, and why each is here rather than upstream:
#   strftime()    -- upstream has none, and formatting a timestamp into an
#                    output filename is the single commonest use of this module
#   strptime()    -- the inverse, for reading timestamps back out of logs
#   isocalendar() -- ISO week numbering, which %G/%V/%U/%W also need
#   ctime()       -- three lines once strftime exists
#   fromisoformat() -- widened to accept a trailing Z and 1-6 fractional
#                    digits, both of which upstream rejects outright
#   utcnow(), utcfromtimestamp() -- deprecated in CPython 3.12 but present, and
#                    scripts ported from desktop Python still call them
#
# Known divergence: f"{dt:%Y}" does NOT work. MicroPython's str.format never
# calls __format__ on the object, so a non-numeric format spec is rejected by
# the interpreter before anything here could see it. Use dt.strftime("%Y").
#
# Locale: %a/%A/%b/%B/%p/%c/%x/%X are the C locale (English), always. The
# interpreter has no locale database, and a fixed answer is better than one
# that silently varies with the machine -- see main.cpp, which pins LC_NUMERIC
# for the same reason.
import time as _t

from . import _core
from ._core import *
from ._core import _date, _dbm, _dim, _o2ymd

# MicroPython has no NotImplemented singleton, and _core's timezone.__eq__
# returns it for a non-timezone operand. That made `aware == aware` raise
# NameError, because datetime.__eq__ opens with `self._tz == None` -- so every
# equality test on a timezone-aware datetime failed, dict lookups included.
# Binding it to False in _core's globals gives the answer the protocol would
# have arrived at anyway: a timezone is not equal to a thing that is not one.
_core.NotImplemented = False

_DAYS = ("Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday")
_MONTHS = ("January", "February", "March", "April", "May", "June",
           "July", "August", "September", "October", "November", "December")


# ---------------------------------------------------------------------------
# ISO week numbering
# ---------------------------------------------------------------------------

def _isocal(o):
    """(ISO year, ISO week, ISO weekday) for proleptic ordinal `o`.

    A week belongs to the year containing its Thursday, which is the whole of
    the ISO rule: find this week's Thursday, take its calendar year, and count
    weeks from that year's first Thursday.
    """
    wd = (o + 6) % 7 + 1                    # Mon=1 .. Sun=7
    thu = o - wd + 4
    y = _o2ymd(thu)[0]
    return y, (thu - _date(y, 1, 1)) // 7 + 1, wd


def _date_isocalendar(self):
    return _isocal(self._ord)


def _datetime_isocalendar(self):
    return _isocal(self._d)


# ---------------------------------------------------------------------------
# strftime
# ---------------------------------------------------------------------------

def _offstr(off, colon):
    """A UTC offset as %z renders it; "" when naive."""
    if off is None:
        return ""
    s = round(off.total_seconds())
    sign = "-" if s < 0 else "+"
    s = abs(s)
    us = off.microseconds if s or off.microseconds else 0
    hh, s = divmod(s, 3600)
    mm, ss = divmod(s, 60)
    sep = ":" if colon else ""
    r = "%s%02d%s%02d" % (sign, hh, sep, mm)
    if ss or us:
        r += "%s%02d" % (sep, ss)
        if us:
            r += ".%06d" % us
    return r


def _strftime(fmt, Y, M, D, hh, mi, ss, us, off, tzn):
    """The formatting engine, shared by date, time and datetime.

    Written as a scanner rather than a table of lambdas so that an unknown
    directive can be passed through untouched (CPython's behaviour on most
    platforms) instead of raising.
    """
    o = _date(Y, M, D)
    wd = (o + 6) % 7                        # Mon=0 .. Sun=6
    yday = _dbm(Y, M) + D
    out = []
    i, n = 0, len(fmt)
    while i < n:
        c = fmt[i]
        if c != "%" or i + 1 >= n:
            out.append(c)
            i += 1
            continue
        d = fmt[i + 1]
        i += 2
        if d == "a":
            out.append(_DAYS[wd][:3])
        elif d == "A":
            out.append(_DAYS[wd])
        elif d == "b":
            out.append(_MONTHS[M - 1][:3])
        elif d == "B":
            out.append(_MONTHS[M - 1])
        elif d == "c":
            out.append("%s %s %2d %02d:%02d:%02d %04d"
                       % (_DAYS[wd][:3], _MONTHS[M - 1][:3], D, hh, mi, ss, Y))
        elif d == "d":
            out.append("%02d" % D)
        elif d == "f":
            out.append("%06d" % us)
        elif d == "G":
            out.append("%04d" % _isocal(o)[0])
        elif d == "H":
            out.append("%02d" % hh)
        elif d == "I":
            out.append("%02d" % (hh % 12 or 12))
        elif d == "j":
            out.append("%03d" % yday)
        elif d == "m":
            out.append("%02d" % M)
        elif d == "M":
            out.append("%02d" % mi)
        elif d == "p":
            out.append("PM" if hh >= 12 else "AM")
        elif d == "S":
            out.append("%02d" % ss)
        elif d == "u":
            out.append("%d" % (wd + 1))
        elif d == "U":
            # Week of the year, Sunday first; days before the first Sunday
            # are week 0. %W is the same count taken from Monday.
            out.append("%02d" % ((yday + 6 - (wd + 1) % 7) // 7))
        elif d == "V":
            out.append("%02d" % _isocal(o)[1])
        elif d == "w":
            out.append("%d" % ((wd + 1) % 7))
        elif d == "W":
            out.append("%02d" % ((yday + 6 - wd) // 7))
        elif d == "x":
            out.append("%02d/%02d/%02d" % (M, D, Y % 100))
        elif d == "X":
            out.append("%02d:%02d:%02d" % (hh, mi, ss))
        elif d == "y":
            out.append("%02d" % (Y % 100))
        elif d == "Y":
            out.append("%04d" % Y)
        elif d == "z":
            out.append(_offstr(off, False))
        elif d == "Z":
            out.append(tzn or "")
        elif d == "%":
            out.append("%")
        else:
            out.append("%" + d)             # unknown: leave it alone
    return "".join(out)


def _date_strftime(self, fmt):
    Y, M, D = self.tuple()
    return _strftime(fmt, Y, M, D, 0, 0, 0, 0, None, None)


def _time_strftime(self, fmt):
    # A bare time has no date, and CPython dates it 1900-01-01 so that %Y and
    # friends still produce something rather than raising.
    hh, mi, ss, us, tz, _fold = self.tuple()
    return _strftime(fmt, 1900, 1, 1, hh, mi, ss, us,
                     self.utcoffset(), self.tzname())


def _datetime_strftime(self, fmt):
    Y, M, D, hh, mi, ss, us, tz, _fold = self.tuple()
    return _strftime(fmt, Y, M, D, hh, mi, ss, us,
                     self.utcoffset(), self.tzname())


def _datetime_timetuple(self):
    """The object's OWN fields, which is what CPython's timetuple() returns.

    The one method here that replaces an upstream one rather than adding to it.
    Upstream converts an aware datetime through _t.localtime(), i.e. into the
    machine's timezone rather than leaving it in its own -- so a UTC-aware value
    came back in local time. That was invisible while _t.localtime() was itself
    UTC (see patches/06); fixing the clock is what surfaced it. Shape matches
    _core's own date.timetuple(): CPython's 9 fields, tm_wday from Monday.
    """
    Y, M, D, hh, mi, ss, us, tz, fold = self.tuple()
    dst = self.dst()
    return (Y, M, D, hh, mi, ss, self.weekday(), _dbm(Y, M) + D,
            -1 if dst is None else (1 if dst else 0))


def _date_ctime(self):
    return _date_strftime(self, "%c")


def _datetime_ctime(self):
    return _datetime_strftime(self, "%c")


# ---------------------------------------------------------------------------
# strptime
# ---------------------------------------------------------------------------

def _digits(s, i, maxn):
    """Read up to maxn digits at s[i:]; return (value, ndigits, newpos)."""
    j = i
    while j < len(s) and j - i < maxn and "0" <= s[j] <= "9":
        j += 1
    if j == i:
        raise ValueError
    return int(s[i:j]), j - i, j


def _name(s, i, names, abbr):
    """Match one of `names` (or its 3-letter abbreviation) case-blind."""
    low = s[i:].lower()
    if abbr:
        for k, nm in enumerate(names):
            if low.startswith(nm[:3].lower()):
                return k, i + 3
        raise ValueError
    # Longest first, so "June" is not shadowed by a prefix match on "Jun".
    best = -1
    for k, nm in enumerate(names):
        if low.startswith(nm.lower()) and (best < 0 or len(nm) > len(names[best])):
            best = k
    if best < 0:
        raise ValueError
    return best, i + len(names[best])


def _strptime(s, fmt):
    Y, M, D, hh, mi, ss, us = 1900, 1, 1, 0, 0, 0, 0
    yday = None
    pm = None
    tz = None
    # Only the month/day directives count here, not %Y/%y: "%Y %j" must still
    # resolve the day of the year, and would not if the year suppressed it.
    have_md = False
    i = j = 0
    ls, lf = len(s), len(fmt)
    while j < lf:
        c = fmt[j]
        if c != "%":
            if c in " \t\n\r":
                # Whitespace in the format matches any run of it in the data,
                # including none -- the same latitude CPython allows.
                j += 1
                while i < ls and s[i] in " \t\n\r":
                    i += 1
                continue
            if i >= ls or s[i] != c:
                raise ValueError
            i += 1
            j += 1
            continue
        if j + 1 >= lf:
            raise ValueError("stray %% in format string")
        d = fmt[j + 1]
        j += 2
        if d == "%":
            if i >= ls or s[i] != "%":
                raise ValueError
            i += 1
        elif d == "Y":
            Y, _, i = _digits(s, i, 4)
        elif d == "y":
            v, _, i = _digits(s, i, 2)
            Y = v + (2000 if v < 69 else 1900)   # CPython's POSIX pivot
        elif d == "m":
            M, _, i = _digits(s, i, 2)
            have_md = True
        elif d == "d":
            D, _, i = _digits(s, i, 2)
            have_md = True
        elif d in "HI":
            hh, _, i = _digits(s, i, 2)
        elif d == "M":
            mi, _, i = _digits(s, i, 2)
        elif d == "S":
            ss, _, i = _digits(s, i, 2)
        elif d == "f":
            v, k, i = _digits(s, i, 6)
            us = v * 10 ** (6 - k)           # "25" means 250000, not 25
        elif d == "j":
            yday, _, i = _digits(s, i, 3)
        elif d == "p":
            k, i = _name(s, i, ("AM", "PM"), False)
            pm = bool(k)
        elif d in "bB":
            k, i = _name(s, i, _MONTHS, d == "b")
            M = k + 1
            have_md = True
        elif d in "aA":
            # Parsed and discarded, as CPython does: the weekday is implied by
            # the date, and honouring a contradictory one would be worse.
            _, i = _name(s, i, _DAYS, d == "a")
        elif d == "z":
            if i < ls and s[i] in "zZ":
                tz = timezone.utc
                i += 1
            else:
                if i >= ls or s[i] not in "+-":
                    raise ValueError
                sign = -1 if s[i] == "-" else 1
                i += 1
                v, k, i = _digits(s, i, 2)
                secs = v * 3600
                if i < ls and s[i] == ":":
                    i += 1
                v, k, i = _digits(s, i, 2)
                secs += v * 60
                if i < ls and s[i] == ":":
                    i += 1
                    v, k, i = _digits(s, i, 2)
                    secs += v
                micro = 0
                if i < ls and s[i] == ".":
                    i += 1
                    v, k, i = _digits(s, i, 6)
                    micro = v * 10 ** (6 - k)
                # The sign goes on the components, not on the timedelta:
                # upstream defines __mul__ but no __rmul__, so `sign * td`
                # would depend on MICROPY_PY_REVERSE_SPECIAL_METHODS.
                tz = timezone(timedelta(seconds=sign * secs,
                                        microseconds=sign * micro))
        elif d == "Z":
            k = i
            while k < ls and (s[k].isalpha() or s[k] in "+-/_"):
                k += 1
            if k == i:
                raise ValueError
            i = k                            # recorded nowhere: no tz database
        else:
            raise ValueError("unsupported directive '%%%s'" % d)
    if i != ls:
        raise ValueError("unconverted data remains: %s" % s[i:])
    if yday is not None and not have_md:
        M = 1
        while M < 12 and yday > _dim(Y, M):
            yday -= _dim(Y, M)
            M += 1
        D = yday
    if pm is not None:
        hh = hh % 12 + (12 if pm else 0)
    return Y, M, D, hh, mi, ss, us, tz


def _datetime_strptime(cls, s, fmt):
    try:
        Y, M, D, hh, mi, ss, us, tz = _strptime(s, fmt)
    except ValueError as e:
        # Re-raised with the inputs attached: the inner failures know what
        # they could not read, but not what the user actually passed.
        msg = str(e)
        raise ValueError("time data %r does not match format %r%s"
                         % (s, fmt, " (%s)" % msg if msg else ""))
    return cls(Y, M, D, hh, mi, ss, us, tz)


# ---------------------------------------------------------------------------
# fromisoformat, widened to what CPython 3.11 accepts
# ---------------------------------------------------------------------------

def _iso_widen(s):
    """Rewrite the two ISO-8601 spellings upstream's parser turns away.

    A trailing Z is the one that matters: it is how essentially every machine
    -written UTC timestamp ends, and upstream raises a bare ValueError on it.
    """
    if s and s[-1] in "zZ":
        s = s[:-1] + "+00:00"
    dot = s.find(".")
    if dot >= 0:
        end = dot + 1
        while end < len(s) and "0" <= s[end] <= "9":
            end += 1
        n = end - dot - 1
        # Upstream wants exactly 3 or 6 fractional digits; CPython takes 1 to 6
        # and zero-fills, so ".25" is 250000 us rather than an error.
        if 0 < n < 6 and n != 3:
            s = s[:end] + "0" * (6 - n) + s[end:]
    return s


_dt_fromiso = datetime.fromisoformat
_tm_fromiso = time.fromisoformat


def _datetime_fromisoformat(cls, s):
    return _dt_fromiso(_iso_widen(s))


def _time_fromisoformat(cls, s):
    return _tm_fromiso(_iso_widen(s))


# ---------------------------------------------------------------------------
# UTC constructors
# ---------------------------------------------------------------------------

def _utcnow(cls):
    return cls.utcfromtimestamp(_t.time())


def _utcfromtimestamp(cls, ts):
    # Deliberately naive, matching CPython: the value reads as UTC but carries
    # no tzinfo. datetime.now(timezone.utc) is the one to prefer, and is what
    # the manual recommends.
    return cls.fromtimestamp(ts, timezone.utc).replace(tzinfo=None)


date.strftime = _date_strftime
date.isocalendar = _date_isocalendar
date.ctime = _date_ctime
time.strftime = _time_strftime
time.fromisoformat = classmethod(_time_fromisoformat)
datetime.strftime = _datetime_strftime
datetime.fromisoformat = classmethod(_datetime_fromisoformat)
datetime.isocalendar = _datetime_isocalendar
datetime.timetuple = _datetime_timetuple
datetime.ctime = _datetime_ctime
datetime.strptime = classmethod(_datetime_strptime)
datetime.utcnow = classmethod(_utcnow)
datetime.utcfromtimestamp = classmethod(_utcfromtimestamp)

# Upstream builds UTC unnamed, so str(timezone.utc) comes out "UTC+00:00" and
# %Z with it renders the same. CPython names it, and scripts print it. EPOCH is
# rebuilt on the named one for the same reason -- it is the only other place
# the unnamed instance was stored.
timezone.utc = timezone(timedelta(0), "UTC")
datetime.EPOCH = datetime(1970, 1, 1, tzinfo=timezone.utc)

__all__ = ("MINYEAR", "MAXYEAR", "timedelta", "tzinfo", "timezone",
           "date", "time", "datetime")
