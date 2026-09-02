#!/usr/bin/env python3
"""Build QT-THIRD-PARTY-NOTICES.txt from Qt's own SPDX SBOM.

    python packaging/gen-qt-notices.py [QT_PREFIX] [OUTPUT]

      QT_PREFIX  a Qt install with sbom/ in it (Qt 6.8+ ships these).
                 Default: the CMAKE_PREFIX_PATH used by the build, else
                 C:/Qt/6.10.3/mingw_64.
      OUTPUT     default <repo>/QT-THIRD-PARTY-NOTICES.txt

WHY THIS EXISTS. Qt's binary libraries statically incorporate a lot of other
people's code -- FreeType, HarfBuzz, PCRE2, zlib, libpng, libjpeg,
double-conversion, the Unicode character data, md4c and more. Our installer is
what puts Qt6Gui.dll on a user's disk, so we are the ones distributing that
object code, and FTL, libpng, zlib, IJG and the various MIT/BSD terms each
attach their notice requirement to whoever distributes the binary form. There
is no redistributor exemption in a permissive licence -- "conveying Qt
unmodified" is an LGPLv3 s4 / GPLv3 s6 idea with no analogue here.

This was missed for a long time because of an accident of packaging: on macOS
those libraries arrive as separate dylibs that macdeployqt copies in, so
gen-third-party-notices.sh saw them. On Windows and Linux the same code is
compiled into the Qt libraries and nothing noticed. The obligation was always
identical; only the visibility differed. Hence a PLATFORM-INDEPENDENT file.

SCOPE. Only what we actually ship (see packaging/femm-qt.iss): Qt6Core,
Qt6Gui, Qt6Widgets, Qt6Svg, the qwindows platform plugin, the image-format
and icon-engine plugins, and the styles plugin. Qt's SBOM covers all of
qtbase, so entries belonging to modules we do not deploy -- Network, SQL,
DBus, Test, and the build-time bootstrap and CMake tooling -- are excluded by
prefix and REPORTED, so the exclusions are a visible decision rather than a
silent filter.

LICENCE TEXTS. The SBOM carries full text only for its non-standard
LicenseRef-* ids; standard SPDX ids are references to the SPDX list. So this
embeds what the SBOM has, plus anything in packaging/license-texts/<id>.txt,
and PRINTS WHAT IS STILL MISSING rather than inventing it -- the same habit
gen-third-party-notices.sh has of reporting gaps for a human to fill.
"""
import glob
import io
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
TEXTS = os.path.join(HERE, "license-texts")

# Component-name prefixes for the Qt modules we deploy. Qt names SBOM packages
# <Module>_Attribution_<thing>, or Bundled<Thing> for a vendored library
# compiled into the module.
SHIPPED = ("Bundled", "Core_Attribution_", "Gui_Attribution_",
           "FreetypePrivate_", "Svg_Attribution_",
           "QWindowsIntegrationPlugin_Attribution_")

# Present in the SBOM, deliberately NOT shipped by us. Listed so the reason is
# reviewable instead of implicit in a regex.
NOT_SHIPPED = {
    "Network_": "Qt6Network is not deployed",
    "QSQLiteDriverPlugin_": "no SQL driver plugin is deployed",
    "DBus_": "Qt6DBus is not deployed",
    "Test_": "Qt6Test is not deployed",
    "TestInternals": "Qt6Test internals are not deployed",
    "Platform_Attribution_": "build-time CMake tooling, not shipped",
    "Bootstrap_": "build-time bootstrap library, not shipped",
}


# The Qt REPOSITORIES we deploy something from. A Qt install carries an SBOM
# per repository -- 36 of them here -- and reading all of them pulls in
# components from qtmultimedia, qtquick3d, qtvirtualkeyboard and the rest,
# which would attribute code that is not in the installer. Prefix matching
# alone does not catch it: Bundled* and Gui_Attribution_* occur in several.
SHIPPED_MODULES = ("qtbase", "qtsvg")


def load(prefix):
    """{name: package} for the SBOMs of shipped modules, plus extracted texts."""
    pkgs, texts = {}, {}
    files = [f for f in sorted(glob.glob(os.path.join(prefix, "sbom",
                                                      "*.spdx.json")))
             if os.path.basename(f).split("-")[0] in SHIPPED_MODULES]
    if not files:
        sys.exit("no SBOM for %s in %s/sbom -- Qt 6.8+ ships these; check the "
                 "prefix" % ("/".join(SHIPPED_MODULES), prefix))
    for f in files:
        mod = os.path.basename(f).split("-")[0]
        d = json.load(io.open(f, encoding="utf-8"))
        for e in d.get("hasExtractedLicensingInfos", []):
            if e.get("licenseId") and e.get("extractedText"):
                texts.setdefault(e["licenseId"], e["extractedText"])
        for p in d.get("packages", []):
            p["_module"] = mod
            pkgs.setdefault(p["name"], p)
    return pkgs, texts, len(files)


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
        for d in (TEXTS, os.path.join(REPO, "LICENSES")):
            p = os.path.join(d, atom + ".txt")
            if os.path.isfile(p):
                parts.append(
                    (atom, io.open(p, encoding="utf-8",
                                   errors="replace").read()))
                break
        else:
            missing.append(atom)
    if missing or not parts:
        return None
    if len(parts) == 1:
        return parts[0][1]
    # Label each, so an OR expression does not read as one run-on licence.
    return "\n\n".join("--- %s ---\n\n%s" % (n, t.rstrip()) for n, t in parts)


def main(argv):
    prefix = argv[1] if len(argv) > 1 else os.environ.get(
        "QT_PREFIX", "C:/Qt/6.10.3/mingw_64")
    out_path = argv[2] if len(argv) > 2 else os.path.join(
        REPO, "QT-THIRD-PARTY-NOTICES.txt")

    pkgs, extracted, nfiles = load(prefix)

    keep, dropped = [], {}
    for name, p in sorted(pkgs.items()):
        if any(name.startswith(k) for k in SHIPPED):
            keep.append(p)
            continue
        for pre, why in NOT_SHIPPED.items():
            if name.startswith(pre):
                dropped.setdefault(why, []).append(name)
                break

    qtver = os.path.basename(prefix.rstrip("/\\"))
    out = []
    w = out.append
    w("=" * 70)
    w("THIRD-PARTY NOTICES  --  code compiled into the Qt libraries femm-qt ships")
    w("=" * 70)
    w("")
    w("Generated by packaging/gen-qt-notices.py from Qt's own SPDX SBOM.")
    w("Qt install: %s   (%d SBOM documents read)" % (prefix, nfiles))
    w("")
    w("femm-qt ships Qt6Core, Qt6Gui, Qt6Widgets and Qt6Svg, together with the")
    w("platform, image-format, icon-engine and style plugins. Those binaries")
    w("statically incorporate the components below. Distributing them means")
    w("distributing this code, so each is listed with its version, licence and")
    w("copyright.")
    w("")
    w("Qt ITSELF is not covered here -- see NOTICE-Qt.txt (LGPL-3.0).")
    w("Additional libraries bundled only in the macOS .app are covered by")
    w("THIRD-PARTY-NOTICES.txt, produced by gen-third-party-notices.sh.")
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
        w("%s   (Qt module: %s)" % (p["name"], p["_module"]))
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

    io.open(out_path, "w", encoding="utf-8").write("\n".join(out) + "\n")

    print("wrote %s" % out_path)
    print("  %d components shipped, from %d SBOM documents" % (len(keep), nfiles))
    for why, names in sorted(dropped.items()):
        print("  excluded %2d: %s" % (len(names), why))
    if missing:
        print("")
        print("  %d licence id(s) have NO embedded text -- add to "
              "packaging/license-texts/:" % len(missing))
        for spdx, names in sorted(missing.items()):
            print("    %-46s (%d component(s))" % (spdx[:46], len(names)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
