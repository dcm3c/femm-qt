"""Check that pylib's licensing claims are still true.

Run under CPython, pointing at a micropython-lib checkout:

    python femm-qt/pylib/tests/license_audit.py <path-to-micropython-lib>

It asserts the three things pylib/README.md promises:

  1. every vendored file is byte-identical to upstream (nothing here is a
     MODIFIED third-party file, which is what keeps the licensing simple);
  2. no vendored file carries a femm-qt copyright;
  3. every directory holding upstream code also holds the MIT notice, so a
     stray copy of one directory is still a compliant copy.

Not part of _verify_all.sh: it needs the upstream tree, which a normal build
does not have. Run it after a re-vendor, and before a release.
"""
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
PYLIB = HERE.parent

# vendored file -> where it came from in micropython-lib
VENDORED = {
    "vendor/contextlib.py": "python-stdlib/contextlib/contextlib.py",
    "vendor/copy.py": "python-stdlib/copy/copy.py",
    "vendor/fnmatch.py": "python-stdlib/fnmatch/fnmatch.py",
    "vendor/functools.py": "python-stdlib/functools/functools.py",
    "vendor/traceback.py": "python-stdlib/traceback/traceback.py",
    "vendor/types.py": "python-stdlib/types/types.py",
    "vendor/abc.py": "python-stdlib/abc/abc.py",
    "vendor/argparse.py": "python-stdlib/argparse/argparse.py",
    "vendor/base64.py": "python-stdlib/base64/base64.py",
    "vendor/bisect.py": "python-stdlib/bisect/bisect.py",
    "vendor/heapq.py": "python-stdlib/heapq/heapq.py",
    "vendor/inspect.py": "python-stdlib/inspect/inspect.py",
    "vendor/pathlib.py": "python-stdlib/pathlib/pathlib.py",
    "vendor/pickle.py": "python-stdlib/pickle/pickle.py",
    "vendor/pprint.py": "python-stdlib/pprint/pprint.py",
    "vendor/stat.py": "python-stdlib/stat/stat.py",
    # string is a package upstream, so both of its files come across.
    "vendor/string/__init__.py": "python-stdlib/string/string/__init__.py",
    "vendor/string/templatelib.py": "python-stdlib/string/string/templatelib.py",
    # NOT python-stdlib: contextlib imports it and fails without it.
    "vendor/ucontextlib.py": "micropython/ucontextlib/ucontextlib.py",
    "datetime/_core.py": "python-stdlib/datetime/datetime.py",
    "itertools/_core.py": "python-stdlib/itertools/itertools.py",
    "operator/_core.py": "python-stdlib/operator/operator.py",
    "shutil/_core.py": "python-stdlib/shutil/shutil.py",
    "tempfile/_core.py": "python-stdlib/tempfile/tempfile.py",
    "logging/_core.py": "python-stdlib/logging/logging.py",
    "tests/test_datetime_upstream.py": "python-stdlib/datetime/test_datetime.py",
}

NOTICE = "LICENSE.micropython-lib"


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    up = pathlib.Path(argv[1])
    if not (up / "python-stdlib").is_dir():
        print("not a micropython-lib checkout: %s" % up)
        return 2

    problems = []

    # 1. verbatim
    for ours, theirs in VENDORED.items():
        a = (PYLIB / ours).read_bytes().replace(b"\r\n", b"\n")
        src = up / theirs
        if not src.exists():
            problems.append("%s: upstream %s is gone (moved or renamed?)"
                            % (ours, theirs))
            continue
        b = src.read_bytes().replace(b"\r\n", b"\n")
        if a != b:
            problems.append("%s DIFFERS from upstream %s -- it is now a "
                            "MODIFIED third-party file, which the licensing "
                            "notes do not cover" % (ours, theirs))

    # 2. no femm-qt claim on someone else's file
    for ours in VENDORED:
        text = (PYLIB / ours).read_text(encoding="utf-8", errors="replace")
        if "Meeker" in text or "SPDX-License-Identifier" in text:
            problems.append("%s carries a femm-qt copyright header" % ours)

    # 3. the notice travels with the code
    dirs = sorted(set(pathlib.Path(o).parent.as_posix() for o in VENDORED))
    for d in dirs:
        if not (PYLIB / d / NOTICE).exists():
            problems.append("%s/ holds upstream code but no %s" % (d, NOTICE))

    # ...and the notice really is upstream's text, not a paraphrase
    lic = (up / "LICENSE").read_text(encoding="utf-8", errors="replace")
    lines = lic.replace("\r\n", "\n").splitlines()
    try:
        i = lines.index("The MIT License (MIT)")
    except ValueError:
        problems.append("upstream LICENSE no longer starts the MIT block where "
                        "expected -- check it by hand")
        i = None
    if i is not None:
        block = "\n".join(lines[i:i + 20])
        for d in dirs:
            got = (PYLIB / d / NOTICE).read_text(encoding="utf-8",
                                                 errors="replace")
            if block not in got.replace("\r\n", "\n"):
                problems.append("%s/%s does not contain upstream's MIT text "
                                "verbatim" % (d, NOTICE))

    if problems:
        print("license audit: %d PROBLEM(S)" % len(problems))
        for p in problems:
            print("  " + p)
        return 1
    print("license audit: %d vendored files byte-identical to upstream, "
          "none claiming femm-qt copyright, %d directories each carrying the "
          "MIT notice verbatim" % (len(VENDORED), len(dirs)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
