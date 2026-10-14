# FEMM material curves with provenance

B-H curves for FEMM's magnetic materials library, each regressed from or
built on citable measurements, with the papers that document how; and the
current-flow, electrostatic and heat-flow material libraries with their
provenance documents. Every number in the libraries traces to a source
listed in the papers and in `SOURCES.md`, or the text says it has none.

Author: David Meeker.

## The papers

Each paper has its own folder, holding the paper, the scripts that produce
its results, its digitized inputs and, after a run, its tables, figures and
FEMM library files.

| Folder | Paper | Materials | FEMM library files |
|---|---|---|---|
| `low-carbon/` | `bhcurves` | Low-carbon steels, 1004-1030 and 1117, as hot rolled and annealed at 1300, 1400 and 1500 °F | `lowcarbon_fit.dat`, `lowcarbon_fit_materials.fem` |
| `iron/` | `ironcurves` | ARMCO pure iron and Telar 57 (AK Steel data) | carried in the low-carbon files |
| `silicon/` | `sicurves` | Nonoriented silicon steels, US Steel 1978 catalog | `silicon_fit.dat`, `silicon_fit_materials.fem` |
| `nickel-iron/` | `nifecurves` | 48% and 80% nickel-iron | `nife_fit.dat`, `nife_fit_materials.fem` |
| `stainless/` | `sscurves` | Ferritic and martensitic stainless (annealed), 17-4, 15-5 and 13-8 Mo PH | `ss_fit.dat`, `ss_fit_materials.fem`, `ph_fit.dat`, `ph_fit_materials.fem` |
| `cobalt-iron/` | `cofecurves` | 49Co-2V (three conditions) and 27Co cobalt-iron | `cofe_fit.dat`, `cofe_fit_materials.fem` |
| `metglas/` | `metglascurves` | Metglas 2605SA1 and 2605HB1M | `metglas_fit.dat`, `metglas_fit_materials.fem` |
| `ceramic/` | `ceramiccurves` | Ceramic (hard ferrite) magnets, measured and typical Y grades | `Magnets/Ceramic/Indox.fem`, `Magnets/Ceramic/Yferrite_typical.fem` |

`libraries/` holds the other material libraries and their provenance
documents (`libraries/provenance/`): current flow
(`condlib_provenance`, library `matlibs/condlib_catalog.dat`),
electrostatics (`statlib_provenance`, `matlibs/statlib_catalog.dat`) and
heat flow (`heatlib_provenance`, `matlibs/heatlib_fixed.dat`).

`magnets/` holds the provenance notes for the other permanent magnets in
FEMM's library, as published on femm.info: alnico (`AlnicoProperties`),
sintered NdFeB (`SinteredNdFeBProperties`), bonded NdFeB (`BondedNdFeB`)
and samarium cobalt (`SmCoProperties`). No script produces their numbers.
`magnets/alnico_scanned/` holds the digitized points behind the alnico
entries, read from the curves International Magnaproducts published on
magnetsim.com (no longer online): `<grade>.txt` as read (H in A/m,
negative; B in T) and `<grade>Shift.txt` shifted by the coercivity, the
form FEMM's library carries. `LNG40.nb` interpolates LNG40 between LNG37
and LNG44; `ProcessScannedCurve.nb` and `myMaterials.fem` are the working
files that turned the points into library entries.
`magnets/out/alnico_arnold_curves.csv` holds Arnold's typical cast-alnico
curves, read from the vector artwork of its brochure by
`magnets/digitize/dig_alnico.py`: the cross-check described in
`AlnicoProperties`.

## Using the curves in FEMM

FEMM reads only its own `matlib.dat`. Each `.dat` file here is one or two
library folders, written in the same format, ready to paste into
`matlib.dat`; each `.fem` file is a geometry-free problem containing the
same materials, which can be opened in FEMM and the materials copied from
it. Laminated entries carry their gauge as the lamination thickness and a
suggested stacking factor as `LamFill`; the B-H tables themselves are
solid-material curves (see `sicurves` and `metglascurves`).

## Rebuilding everything

Requirements: Python 3.10 or later with the packages in `requirements.txt`,
and a LaTeX installation providing `pdflatex` (TeX Live or MiKTeX) for the
papers.

    pip install -r requirements.txt
    python run_all.py

`run_all.py` runs every step, each inside its paper's folder, and then
compiles the eight papers and the seven provenance documents, three LaTeX
passes each. Logs go to `logs/`. Options: `--no-pdf`, `--wiki` (also write
the DokuWiki and Markdown versions of each paper), and `--only <folder>`.
The whole run takes a few minutes; the silicon regression is the longest
step.

`buildsource.bat` (Windows, needs `bash` and `git` on the path) zips
everything committed at HEAD into `output/provenance_<date>_<commit>.zip`;
uncommitted changes and the files `.gitignore` excludes are left out.

To run one script by hand, run it from its folder with `common`,
`cobalt-iron` and `nickel-iron` on the import path, for example on Windows

    cd silicon
    set PYTHONPATH=..\common;..\cobalt-iron;..\nickel-iron
    python bh_fit_si.py

The papers depend on one another in a few places: the low-carbon library
files carry the iron entries and read the iron folder's tables, the iron
figure reads the low-carbon fit, and the nickel-iron, stainless and Metglas
builds import the cobalt-iron and nickel-iron modules. `run_all.py` runs
the steps in an order that respects this.

One step exits non-zero by design: `silicon/check_ordering_si.py` reports
two documented ordering defects of the silicon family (Type 2-S at
0.024 in, below 0.7 T; see `sicurves`). `run_all.py` expects that.

Several fits accept environment variables that select variants examined in
the papers (for example `AKULOV_SIG`, `SIG_T`, `LRS0`, `LEVEL_SIG` and `TAG`
for `low-carbon/bh_fit.py` and `silicon/bh_fit_si.py`); each script's
docstring lists its own. With no variables set, every script reproduces
the adopted results.

## Layout

    common/             modules shared by several papers
    <paper folder>/     the paper (.tex, .pdf), its scripts, and:
      out/              digitized inputs (*_raw.csv and the source tables)
                        and, after a run, fit parameters and figures
      digitize/         the digitizers (see below)
    libraries/          the current-flow, electrostatic and heat-flow
                        libraries, their sources and provenance documents
    SOURCES.md          the register of every source document, with
                        retrieval notes and checksums

## Licensing

- **Code** is under the MIT License (`LICENSE`). Every script carries an
  `SPDX-License-Identifier: MIT` line; so do `run_all.py` and
  `buildsource.bat`. The Mathematica notebooks (`*.nb`) are code too, and
  MIT, though they carry no header.
- **Documents** are under the Creative Commons Attribution 4.0 International
  License (CC BY 4.0, https://creativecommons.org/licenses/by/4.0/). Each
  `.tex` source carries an `SPDX-License-Identifier: CC-BY-4.0` line and
  states the licence at its end; the PDFs built from them, and the figures
  the magnet notes include, are covered with them.
- **Data files** that cannot carry a header of their own are under the MIT
  License: the CSV files, the alnico point files (`*.txt`), the
  spreadsheets (`*.xlsx`) and the FEMM library files (`*.dat`, `*.fem`).

In short, everything here is MIT or CC BY 4.0, as labeled in the files
themselves, and the file types above say which for the files that cannot be
labeled.

## What is not included

The package starts from digitized data. The scans, vendor bulletins and
book pages that the curves were read from are not redistributed: most are
copyrighted, and together they are several hundred megabytes. `SOURCES.md`
identifies each one, with its checksum where it was held as a file, and the
papers cite them. The digitizers in each `digitize/` folder are included so
the reading of each chart can be inspected, but they need those originals
to run. The Mathematica notebook that holds the digitized Dexter ceramic
curves is not included either; its output, `ceramic/Magnets/Ceramic/Indox.fem`,
is, and `ceramic/cer_check.py` carries the digitized points themselves.
`ceramic/Magnets/Ceramic/ceramic.nb` generates the typical Y-grade entries.

FEMM 4.2's own material libraries are left out of this package, along with
every comparison with them: the papers and provenance documents establish
where each value in the new libraries comes from, not how they differ from
the old ones.
