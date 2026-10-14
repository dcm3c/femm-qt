# FEMM material libraries

This directory holds the material libraries shipped with femm-qt:

| file          | physics                         |
|---------------|---------------------------------|
| `matlib.dat`  | magnetics                       |
| `statlib.dat` | electrostatics                  |
| `heatlib.dat` | heat flow                       |
| `condlib.dat` | current flow                    |

The `.dat` format has no comment syntax, so this file is where their terms
are stated.

## License

The material libraries are released under the MIT License, the same terms
as the rest of femm-qt outside `solvers/`. See `LICENSE-MIT` in the
femm-qt root.

Copyright (C) 2026 David Meeker and contributors.

## What the license covers

The entries are drawn from three kinds of source:

* standard reference works on magnetic and material properties;
* curves and values fitted to manufacturers' published datasheets and
  product information;
* reports from national laboratories.

The property values themselves are measured or tabulated facts, which are
taken from those sources and are not claimed here. What the license covers
is the work done on them for this library: the fitting and conditioning of
the curves, the choice of materials and conditions,
and the way the entries are organised and named. No text, figures or tables
are reproduced from the sources; they remain under their owners' copyright.

The sources for every entry, and how each curve was fitted, are documented
in the provenance papers in `femm-qt/provenance/`. Each paper sits in its own
folder with the scripts and digitised inputs that produce its numbers;
`provenance/README.md` explains how to rerun them, and
`provenance/SOURCES.md` pins the exact third-party documents used. The
papers are licensed separately, under CC BY 4.0; the scripts and data there
are MIT, as each file or `provenance/README.md` states.

In an installed copy of FEMM the papers sit together in the
`docs/materials` folder, beside this file, under the file names below. In
the femm-qt source tree each is in its own folder under `provenance/`, as
shown.

| library        | provenance papers (source-tree path under `provenance/`) |
|----------------|-----------------------------------------------------------|
| `matlib.dat`   | `low-carbon/bhcurves.pdf`, `iron/ironcurves.pdf`, `silicon/sicurves.pdf`, `nickel-iron/nifecurves.pdf`, `cobalt-iron/cofecurves.pdf`, `stainless/sscurves.pdf`, `metglas/metglascurves.pdf`, `ceramic/ceramiccurves.pdf`, `magnets/AlnicoProperties.pdf`, `magnets/SinteredNdFeBProperties.pdf`, `magnets/BondedNdFeB.pdf`, `magnets/SmCoProperties.pdf` |
| `statlib.dat`  | `libraries/provenance/statlib_provenance.pdf` |
| `heatlib.dat`  | `libraries/provenance/heatlib_provenance.pdf` |
| `condlib.dat`  | `libraries/provenance/condlib_provenance.pdf` |

None of the sources they cite is used under special terms: no material is
reproduced by permission, and no entry rests on a private communication.

## Trade names

Material and product names such as Metglas, ARMCO and Carpenter are
trademarks of their respective owners. They are used here only to identify
the material an entry describes, and their use implies no endorsement by,
or affiliation with, those owners.

## No warranty for engineering use

The entries describe typical or nominal properties. Real material varies
from heat to heat and lot to lot, and with processing: an as-machined part
and the same grade annealed for magnetic properties can differ greatly,
which is why some grades appear in both conditions. Treat the values as a
starting point, and confirm them against the supplier's certified data or
your own measurements wherever a design depends on them. As the MIT License
states, the libraries are provided as is, without warranty of any kind.

## Your own libraries

femm-qt never edits these files in place. It reads them directly until you
first save a change in the material dialog; that save creates your own copy
in your settings folder, and every later edit goes there. When a newer
shipped library arrives, femm-qt offers it rather than overwriting your
copy. Anything you add to your copy is yours, under whatever terms you
choose.
