#!/usr/bin/env python3
"""Build QT-THIRD-PARTY-NOTICES.txt from Qt's own SPDX SBOM, for the Qt
binaries a package actually ships.

    python packaging/gen-qt-notices.py QT_PREFIX OUTPUT --deployed WHAT [WHAT ...]

      QT_PREFIX  the Qt install the build used, with sbom/ in it (Qt 6.8+);
                 or "from-iss" to take it from the installer script's
                 Source line for Qt6Core.dll.
      OUTPUT     the file to write. It is a BUILD PRODUCT: the Windows
                 installer (pack.bat) and the macOS bundle (macos_build.sh)
                 each generate their own, from their own Qt. Nothing in the
                 repository is a stored copy.
      WHAT       what ships. A directory is walked (the macOS .app); a file
                 ending .iss is read for its Source: lines (the Windows
                 installer script, which names every Qt file it takes).

WHY THIS EXISTS. Qt's binary libraries statically incorporate a lot of other
people's code -- FreeType, HarfBuzz, PCRE2, zlib, libpng, libjpeg,
double-conversion, the Unicode character data, md4c and more. Our installer
and our .app are what put those Qt binaries on a user's disk, so we are the
ones distributing that object code, and FTL, libpng, zlib, IJG and the
various MIT/BSD terms each attach their notice requirement to whoever
distributes the binary form. Linux and FreeBSD builds link the distribution's
Qt and redistribute nothing of it, so they ship no such notice.

WHY PER PLATFORM, AND WHY FROM THE SHIPPED FILES. Which third-party code a Qt
library contains depends on the platform and the Qt release: the Windows Qt
bundles zlib and wintab, the macOS one uses the system zlib and adds forkfd
and the CoreFoundation event dispatcher, and every Qt release moves versions.
A single stored file, generated from the Windows Qt, credited code the Mac
did not ship and omitted code it did, one Qt release behind (macOS RC-1138
pass, Nov 2026). So the scope is taken from the files actually shipped:

  1. Each SBOM records which package produced each installed file
     (CONTAINS file). The shipped files' basenames select the packages
     shipped: Core, Gui, Widgets, Svg, DBus, and the individual plugins.
  2. From those, follow DEPENDS_ON / CONTAINS into bundled third-party code
     (qt-bundled-3rdparty-module, qt-3rdparty-sources). Never cross into
     another Qt binary (qt-module, qt-plugin, qt-tool): it is credited only if
     it ships itself. System libraries (system-3rdparty) are not ours to
     credit.
  3. Credit exactly the third-party packages reached.

Every SBOM in the install is read, so a module added to a package later is
picked up without editing this file. Shipped Qt files the SBOM does not know
are REPORTED, as are credited components with no licence text on disk.

LICENCE TEXTS. The SBOM carries full text only for its non-standard
LicenseRef-* ids; standard SPDX ids are references to the SPDX list. So this
embeds what the SBOM has, plus anything in packaging/license-texts/<id>.txt
or LICENSES/<id>.txt, and PRINTS WHAT IS STILL MISSING rather than inventing
it.
"""
import argparse
import collections
import glob
import io
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
TEXTS = os.path.join(HERE, "license-texts")

# SPDXID kinds, from Qt's SBOM generator.
THIRD_PARTY = ("-qt-bundled-3rdparty-module-", "-qt-3rdparty-sources-")
QT_BINARY = ("-qt-module-", "-qt-plugin-", "-qt-tool-", "-qt-app-")


def is_binary(path):
    """A shipped Qt binary, as named in an SBOM file entry: a .dll, .dylib or
    .so, or a framework's executable (QtCore.framework/Versions/A/QtCore).
    Only these select what ships -- a framework's Info.plist or a module's
    .prl appears under every module and would select modules that do not."""
    p = path.replace("\\", "/")
    base = p.rsplit("/", 1)[-1]
    if re.search(r"\.(dll|dylib|so(\.\d+)*)$", base, re.I):
        return True
    m = re.search(r"/([^/]+)\.framework/(?:Versions/[^/]+/)?([^/]+)$", p)
    return bool(m and m.group(1) == m.group(2))


def shipped_names(whats):
    """Lower-case basenames of the files a package ships."""
    names = set()
    for w in whats:
        if os.path.isdir(w):
            for root, dirs, files in os.walk(w):
                names.update(f.lower() for f in files)
        elif w.lower().endswith(".iss"):
            # Source: "C:\Qt\6.10.3\mingw_64\bin\Qt6Core.dll"; DestDir: ...
            for line in io.open(w, encoding="utf-8", errors="replace"):
                m = re.match(r'\s*Source:\s*"([^"]+)"', line)
                if m:
                    names.add(re.split(r"[\\/]", m.group(1))[-1].lower())
        else:
            sys.exit("--deployed: %s is neither a directory nor an .iss" % w)
    return names


def load(prefix, shipped):
    """Read every SBOM in the install. Returns the credited packages, the
    extracted licence texts, the shipped Qt files found, and the count."""
    files = sorted(glob.glob(os.path.join(prefix, "sbom", "*.spdx.json")))
    if not files:
        sys.exit("no SBOM in %s/sbom -- Qt 6.8+ ships these; check the prefix"
                 % prefix)
    credited, texts, roots_found = {}, {}, collections.OrderedDict()
    for f in files:
        repo = os.path.basename(f).split("-")[0]
        d = json.load(io.open(f, encoding="utf-8"))
        pk = {p["SPDXID"]: p for p in d.get("packages", [])}
        fname = {x["SPDXID"]: x["fileName"] for x in d.get("files", [])}
        edges = collections.defaultdict(list)
        roots = set()
        for r in d.get("relationships", []):
            a, t, b = r["spdxElementId"], r["relationshipType"], r["relatedSpdxElement"]
            if t == "CONTAINS" and b in fname and a in pk and is_binary(fname[b]):
                base = re.split(r"[\\/]", fname[b])[-1].lower()
                if base in shipped:
                    roots.add(a)
                    roots_found.setdefault(base, (pk[a]["name"], repo))
            if t in ("DEPENDS_ON", "CONTAINS") and b in pk:
                edges[a].append(b)
        if not roots:
            continue
        for e in d.get("hasExtractedLicensingInfos", []):
            if e.get("licenseId") and e.get("extractedText"):
                texts.setdefault(e["licenseId"], e["extractedText"])
        seen, todo = set(roots), list(roots)
        while todo:
            sid = todo.pop()
            for nxt in edges[sid]:
                if nxt in seen:
                    continue
                seen.add(nxt)
                if any(k in nxt for k in QT_BINARY):
                    continue            # another Qt binary: credited only if shipped
                if any(k in nxt for k in THIRD_PARTY):
                    p = pk[nxt]
                    p["_module"] = repo
                    credited.setdefault(p["name"], p)
                    todo.append(nxt)    # its own attributions
    return credited, texts, roots_found, len(files)


def licence_text(spdx, extracted):
    """Full text for an SPDX id, or None if we do not have one to hand.

    Two local sources, in order: the SBOM's own extracted text (which Qt
    provides only for its non-standard LicenseRef-* ids), then a file named
    for the SPDX id. LICENSES/ already holds SPDX TEMPLATES carrying
    "<year> <copyright holders>" rather than a specific holder, which is
    exactly what is wanted here -- the permission notice is the operative
    text and is identical for every MIT component, while each component's
    actual copyright line comes from the SBOM and is printed above it.

    Nothing is synthesised. An id with no text on disk is reported, not
    guessed at, because this is legally operative wording.
    """
    if spdx in extracted:
        return extracted[spdx]

    # SPDX expressions, not just ids: "IJG AND BSD-3-Clause",
    # "FTL OR GPL-2.0-only", "MIT AND MIT-open-group". Each operand is a
    # separate licence with its own text, so split and emit every one --
    # a file named after the whole expression would never exist, and an
    # AND expression genuinely requires both notices.
    parts, missing = [], []
    for atom in re.split(r"\s+(?:AND|OR|WITH)\s+", spdx):
        atom = atom.strip("() ")
        if not atom:
            continue
        if atom in extracted:
            parts.append((atom, extracted[atom]))
            continue
        # GPL-2.0-or-later and GPL-2.0-only (likewise LGPL, GPL-3.0) are one
        # licence text: "or later" is a grant made in each source file's
        # header, not different wording. So an -or-later id uses the -only
        # file rather than a second copy of the same text.
        names = [atom]
        if atom.endswith("-or-later"):
            names.append(atom[:-len("-or-later")] + "-only")
        found = None
        for name in names:
            for d in (TEXTS, os.path.join(REPO, "LICENSES")):
                p = os.path.join(d, name + ".txt")
                if os.path.isfile(p):
                    found = p
                    break
            if found:
                break
        if found:
            parts.append((atom, io.open(found, encoding="utf-8",
                                        errors="replace").read()))
        else:
            missing.append(atom)
    if missing or not parts:
        return None
    if len(parts) == 1:
        return parts[0][1]
    # Label each, so an OR expression does not read as one run-on licence.
    return "\n\n".join("--- %s ---\n\n%s" % (n, t.rstrip()) for n, t in parts)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("qt_prefix")
    ap.add_argument("output")
    ap.add_argument("--deployed", nargs="+", required=True, metavar="WHAT")
    a = ap.parse_args(argv[1:])

    shipped = shipped_names(a.deployed)
    if a.qt_prefix == "from-iss":
        # The installer script names the Qt it deploys from; read it there, so
        # the notice and the installer cannot disagree about which Qt.
        a.qt_prefix = None
        for w in a.deployed:
            if w.lower().endswith(".iss"):
                for line in io.open(w, encoding="utf-8", errors="replace"):
                    m = re.match(r'\s*Source:\s*"([^"]+)[\\/]bin[\\/]Qt6Core\.dll"', line, re.I)
                    if m:
                        a.qt_prefix = m.group(1).replace("\\", "/")
                        break
        if not a.qt_prefix:
            sys.exit("from-iss: no Source line ending bin\\Qt6Core.dll in the .iss")
    credited, extracted, roots, nfiles = load(a.qt_prefix, shipped)
    if not roots:
        sys.exit("none of the shipped files is a Qt binary this Qt's SBOM "
                 "knows -- wrong QT_PREFIX, or wrong --deployed?")
    keep = [credited[k] for k in sorted(credited)]

    qtbins = sorted(set(name for name, repo in roots.values()))
    out = []
    w = out.append
    w("=" * 70)
    w("THIRD-PARTY NOTICES  --  code compiled into the Qt libraries femm-qt ships")
    w("=" * 70)
    w("")
    w("Generated by packaging/gen-qt-notices.py from Qt's own SPDX SBOM, for")
    w("the Qt files in this package.")
    w("Qt install: %s   (%d SBOM documents read)" % (a.qt_prefix, nfiles))
    w("")
    w("This package ships these Qt libraries and plugins:")
    for base in sorted(roots):
        w("  %-34s (%s)" % (base, roots[base][0]))
    w("")
    w("Those binaries statically incorporate the components below. Distributing")
    w("them means distributing this code, so each is listed with its version,")
    w("licence and copyright.")
    w("")
    w("Qt ITSELF is not covered here -- see NOTICE-Qt.txt (LGPL-3.0).")
    w("A macOS .app built against Homebrew's Qt also bundles Homebrew libraries;")
    w("those are credited in its THIRD-PARTY-NOTICES.txt. Release builds use the")
    w("official Qt and bundle no such libraries.")
    w("")
    w("Components (%d):" % len(keep))
    w("")
    for p in keep:
        w("  %-46s %-14s %s" % (p["name"][:46],
                                (p.get("versionInfo") or "")[:14],
                                p.get("licenseConcluded") or
                                p.get("licenseDeclared") or "?"))
    w("")

    missing = {}
    for p in keep:
        spdx = p.get("licenseConcluded") or p.get("licenseDeclared") or "?"
        w("")
        w("-" * 70)
        w("%s   (Qt repository: %s)" % (p["name"], p["_module"]))
        w("-" * 70)
        if p.get("versionInfo"):
            w("Version:   %s" % p["versionInfo"])
        w("License:   %s" % spdx)
        cp = (p.get("copyrightText") or "").strip()
        if cp and cp != "NOASSERTION":
            w("Copyright: %s" % cp.replace("\n", "\n           "))
        for ref in p.get("externalRefs", []):
            if ref.get("referenceType") in ("purl", "url"):
                w("Source:    %s" % ref.get("referenceLocator"))
                break
        text = licence_text(spdx, extracted)
        if text:
            w("")
            w(text.rstrip())
        else:
            missing.setdefault(spdx, []).append(p["name"])
            w("")
            w("*** Full licence text not embedded: no text for %s ***" % spdx)
            w("*** Drop it in packaging/license-texts/%s.txt and re-run. ***"
              % spdx)
    w("")

    d = os.path.dirname(os.path.abspath(a.output))
    if not os.path.isdir(d):
        os.makedirs(d)
    io.open(a.output, "w", encoding="utf-8", newline="\n").write("\n".join(out) + "\n")

    print("wrote %s" % a.output)
    print("  %d Qt files shipped -> %s" % (len(roots), ", ".join(qtbins)))
    print("  %d third-party components credited, from %d SBOM documents"
          % (len(keep), nfiles))
    unknown = sorted(n for n in shipped
                     if re.match(r"^(qt6?\w+\.dll|libq\w+\.dylib|qt\w+)$", n)
                     and n not in roots)
    if unknown:
        print("  shipped Qt-looking files the SBOM does not know (NOT credited):")
        for n in unknown:
            print("    " + n)
    if missing:
        print("")
        print("  %d licence id(s) have NO embedded text -- add to "
              "packaging/license-texts/:" % len(missing))
        for spdx, names in sorted(missing.items()):
            print("    %-46s (%d component(s))" % (spdx[:46], len(names)))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
