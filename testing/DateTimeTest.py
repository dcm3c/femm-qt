# The datetime module (pylib/datetime/). Runs inside femm_headless:
#
#     build/bin/femm_headless -run testing/DateTimeTest.py
#
# Two jobs. The first is to pin femm-qt's own half of the package -- strftime,
# strptime, isocalendar, ctime, utcnow -- against values taken from CPython
# 3.13, since none of it exists upstream and there is no other oracle. The
# second is to pin the handful of things that were BROKEN and are easy to break
# again: aware-datetime equality (which needed NotImplemented bound in _core's
# globals) and the local-vs-UTC conversions in the time module, which
# datetime.now() sits directly on top of. See pylib/README.md.
#
# It deliberately does not re-test upstream's arithmetic; that is what
# pylib/tests/test_datetime_upstream.py is for, and it is 521 tests.

import time

from datetime import MINYEAR, MAXYEAR, datetime, date, time as dtime, \
    timedelta, timezone

fails = []


def check(label, got, want):
    if got != want:
        fails.append("%s: got %r, want %r" % (label, got, want))


DT = datetime(2026, 8, 17, 14, 32, 5, 250000)          # a Monday
UTC = timezone.utc
IST = timezone(timedelta(hours=5, minutes=30), "IST")

# --- the module still is what upstream made it -------------------------------
check("MINYEAR/MAXYEAR", (MINYEAR, MAXYEAR), (1, 9999))
check("isoformat", DT.isoformat(), "2026-08-17T14:32:05.250000")
check("str", str(DT), "2026-08-17 14:32:05.250000")
check("subtraction", (DT - datetime(2026, 8, 1)).total_seconds(), 1434725.25)
check("addition", str(DT + timedelta(days=3, hours=2)), "2026-08-20 16:32:05.250000")
check("fromisoformat", datetime.fromisoformat("2026-08-17T14:32:05.250000"), DT)
# Upstream wants exactly 3 or 6 fractional digits and rejects a trailing Z --
# which is how every machine-written UTC timestamp ends.
check("fromisoformat short fraction",
      datetime.fromisoformat("2026-08-17T14:32:05.25"), DT)
check("fromisoformat one digit",
      datetime.fromisoformat("2026-08-17T14:32:05.2").microsecond, 200000)
check("fromisoformat Z", datetime.fromisoformat("2026-08-17T14:32:05Z"),
      datetime(2026, 8, 17, 14, 32, 5, tzinfo=UTC))
check("fromisoformat Z with a fraction",
      datetime.fromisoformat("2026-08-17T14:32:05.25Z"),
      datetime(2026, 8, 17, 14, 32, 5, 250000, tzinfo=UTC))
# timetuple must report the object's OWN fields; upstream converted an aware
# one into the machine's timezone instead.
check("timetuple naive", tuple(DT.timetuple()),
      (2026, 8, 17, 14, 32, 5, 0, 229, -1))
check("timetuple aware keeps its own offset", tuple(DT.replace(tzinfo=IST).timetuple()),
      (2026, 8, 17, 14, 32, 5, 0, 229, -1))
check("timetuple on a date", tuple(date(2026, 8, 17).timetuple()),
      (2026, 8, 17, 0, 0, 0, 0, 229, -1))
check("date/time split", (str(DT.date()), str(DT.time())),
      ("2026-08-17", "14:32:05.250000"))
check("weekday/isoweekday", (DT.weekday(), DT.isoweekday()), (0, 1))
check("timezone.utc is named", str(UTC), "UTC")

# Value semantics -- the reason this is in-process and not bridged.
check("hashable", hash(DT) == hash(datetime(2026, 8, 17, 14, 32, 5, 250000)), True)
check("dict key", {DT: "v"}[datetime(2026, 8, 17, 14, 32, 5, 250000)], "v")
check("sortable",
      [d.isoformat() for d in sorted([datetime(2026, 3, 1),
                                      datetime(2025, 1, 1),
                                      datetime(2026, 1, 1)])],
      ["2025-01-01T00:00:00", "2026-01-01T00:00:00", "2026-03-01T00:00:00"])

# --- aware comparison: raised NameError before NotImplemented was bound ------
A1 = datetime(2026, 1, 1, tzinfo=UTC)
A2 = datetime(2026, 1, 1, tzinfo=UTC)
check("aware == aware", A1 == A2, True)
check("aware != naive", A1 == datetime(2026, 1, 1), False)
check("aware in a dict", {A1: "v"}.get(A2), "v")
check("timezone vs non-timezone", UTC == 5, False)
check("timezone vs None", UTC == None, False)      # noqa: E711 -- the point
check("aware sorts", datetime(2026, 1, 1, tzinfo=IST) <
      datetime(2026, 1, 1, tzinfo=UTC), True)

# --- strftime ----------------------------------------------------------------
check("strftime date", DT.strftime("%Y-%m-%d"), "2026-08-17")
check("strftime time", DT.strftime("%H:%M:%S.%f"), "14:32:05.250000")
check("strftime 12-hour", DT.strftime("%I:%M %p"), "02:32 PM")
check("strftime midnight is 12 AM",
      datetime(2026, 1, 1).strftime("%I %p"), "12 AM")
check("strftime noon is 12 PM",
      datetime(2026, 1, 1, 12).strftime("%I %p"), "12 PM")
check("strftime names", DT.strftime("%a %A %b %B"), "Mon Monday Aug August")
check("strftime yearday/weekday", DT.strftime("%j %u %w"), "229 1 1")
check("strftime week numbers", DT.strftime("%U %W %G-W%V"), "33 33 2026-W34")
check("strftime C locale", DT.strftime("%c"), "Mon Aug 17 14:32:05 2026")
check("strftime %x %X", DT.strftime("%x %X"), "08/17/26 14:32:05")
check("strftime literal percent", DT.strftime("100%% of %Y"), "100% of 2026")
check("strftime unknown passes through", DT.strftime("%Q"), "%Q")
check("strftime naive %z %Z", DT.strftime("[%z][%Z]"), "[][]")
check("strftime aware %z %Z",
      DT.replace(tzinfo=IST).strftime("[%z][%Z]"), "[+0530][IST]")
check("strftime negative offset",
      DT.replace(tzinfo=timezone(-timedelta(hours=5))).strftime("%z"), "-0500")
check("strftime on a date", date(2026, 8, 17).strftime("%Y-%m-%d %a"),
      "2026-08-17 Mon")
check("strftime on a time", dtime(14, 32, 5, 7).strftime("%H:%M:%S.%f %Y"),
      "14:32:05.000007 1900")
check("ctime", DT.ctime(), "Mon Aug 17 14:32:05 2026")
# Year 1 and year 9999 exercise the ordinal maths at both ends.
check("strftime MINYEAR", date(1, 1, 1).strftime("%Y-%m-%d %a"), "0001-01-01 Mon")
check("strftime MAXYEAR", date(9999, 12, 31).strftime("%Y-%m-%d %a"),
      "9999-12-31 Fri")

# --- isocalendar: the week belongs to the year holding its Thursday ----------
check("isocalendar plain", tuple(DT.isocalendar()), (2026, 34, 1))
check("isocalendar year start", tuple(date(2026, 1, 1).isocalendar()), (2026, 1, 4))
check("isocalendar week 53 of the year before",
      tuple(date(2021, 1, 3).isocalendar()), (2020, 53, 7))
check("isocalendar first week of the year after",
      tuple(date(2024, 12, 30).isocalendar()), (2025, 1, 1))
check("isocalendar on a date and a datetime agree",
      tuple(DT.date().isocalendar()), tuple(DT.isocalendar()))

# --- strptime ----------------------------------------------------------------
check("strptime date", datetime.strptime("2026-08-17", "%Y-%m-%d"),
      datetime(2026, 8, 17))
check("strptime datetime",
      datetime.strptime("2026-08-17 14:32:05", "%Y-%m-%d %H:%M:%S"),
      datetime(2026, 8, 17, 14, 32, 5))
check("strptime two-digit year pivot",
      (datetime.strptime("68", "%y").year, datetime.strptime("69", "%y").year),
      (2068, 1969))
check("strptime month name", datetime.strptime("Aug 17 2026", "%b %d %Y"),
      datetime(2026, 8, 17))
check("strptime full month", datetime.strptime("August 17, 2026", "%B %d, %Y"),
      datetime(2026, 8, 17))
check("strptime weekday is parsed and ignored",
      datetime.strptime("Friday 17 August 2026", "%A %d %B %Y"),
      datetime(2026, 8, 17))
check("strptime fractional seconds pad right",
      datetime.strptime("14:32:05.25", "%H:%M:%S.%f").microsecond, 250000)
check("strptime 12-hour clock",
      (datetime.strptime("11:05 PM", "%I:%M %p").hour,
       datetime.strptime("12:00 AM", "%I:%M %p").hour), (23, 0))
# %j has to survive %Y; the two used to cancel each other out.
check("strptime day of year", datetime.strptime("2026 060", "%Y %j"),
      datetime(2026, 3, 1))
check("strptime day of year in a leap year",
      datetime.strptime("2024 060", "%Y %j"), datetime(2024, 2, 29))
check("strptime offset", datetime.strptime("2026-08-17T00:00:00+0530",
                                           "%Y-%m-%dT%H:%M:%S%z").utcoffset(),
      timedelta(hours=5, minutes=30))
check("strptime offset with colon",
      datetime.strptime("2026-08-17T00:00:00-05:30",
                        "%Y-%m-%dT%H:%M:%S%z").utcoffset(),
      -timedelta(hours=5, minutes=30))
check("strptime Z", datetime.strptime("2026-08-17T00:00:00Z",
                                      "%Y-%m-%dT%H:%M:%S%z").utcoffset(),
      timedelta(0))
check("strptime whitespace is elastic",
      datetime.strptime("2026-08-17   00:00:00", "%Y-%m-%d %H:%M:%S"),
      datetime(2026, 8, 17))
check("round trip", datetime.strptime(DT.strftime("%Y-%m-%dT%H:%M:%S.%f"),
                                      "%Y-%m-%dT%H:%M:%S.%f"), DT)

for bad, fmt in (("2026-08-17", "%Y/%m/%d"),        # separator mismatch
                 ("2026-08", "%Y-%m-%d"),           # input ends early
                 ("2026-08-17x", "%Y-%m-%d"),       # unconverted data remains
                 ("not a date", "%Y-%m-%d"),        # no digits at all
                 ("2026-13-01", "%Y-%m-%d"),        # month out of range
                 ("2026-02-30", "%Y-%m-%d"),        # day out of range
                 ("Smarch 1 2026", "%B %d %Y")):    # not a month name
    try:
        datetime.strptime(bad, fmt)
        fails.append("strptime(%r, %r) should have raised" % (bad, fmt))
    except ValueError:
        pass

# --- the clock: local is local, UTC is UTC ----------------------------------
# The whole point of shipping datetime is now(), and now() is only as right as
# the time module underneath it. Before patch 06 every one of these was off by
# the UTC offset -- invisible in Greenwich, four hours wrong here.
now = datetime.now()
check("now is naive", now.tzinfo, None)
check("now agrees with time.localtime()",
      (now.year, now.month, now.day, now.hour), time.localtime()[:4])
check("now(utc) agrees with time.gmtime()",
      datetime.now(UTC).timetuple()[:4], time.gmtime()[:4])
# gmtime and localtime must differ exactly by the offset, and must not be the
# same function wearing two names -- which is how upstream ships them.
offset = (datetime(*time.gmtime()[:6]) - datetime(*time.localtime()[:6]))
check("gmtime - localtime is a whole number of minutes",
      offset.total_seconds() % 60, 0.0)
check("localtime(t) round-trips through mktime",
      time.mktime(time.localtime(1786968397)), 1786968397)
check("localtime(t) is local, not UTC",
      time.localtime(1786968397)[:6], tuple(datetime.fromtimestamp(1786968397).timetuple()[:6]))
check("gmtime(t) is UTC",
      time.gmtime(0)[:6], (1970, 1, 1, 0, 0, 0))
# Pre-epoch needs a signed timestamp in timeutils; it used to report year 53052.
check("gmtime before the epoch", time.gmtime(-86400)[:6], (1969, 12, 31, 0, 0, 0))
check("fromtimestamp at the epoch is representable",
      datetime.fromtimestamp(0).year in (1969, 1970), True)
check("timestamp round trip", datetime.fromtimestamp(1786968397).timestamp(),
      1786968397)
check("utcnow is naive and matches now(utc)",
      (datetime.utcnow().tzinfo,
       abs((datetime.utcnow() -
            datetime.now(UTC).replace(tzinfo=None)).total_seconds()) < 2),
      (None, True))
check("utcfromtimestamp", str(datetime.utcfromtimestamp(0)), "1970-01-01 00:00:00")

if fails:
    print("datetime check: %d FAILED" % len(fails))
    for msg in fails:
        print("   " + msg)
else:
    print("datetime check: strftime/strptime/isocalendar match CPython, aware "
          "comparison works, and the clock reports local as local")
