# Contributing to FEMM

Thank you for helping. FEMM is maintained by its author, David Meeker, who
reviews every change and decides what is accepted. A change may be declined
because it does not fit the project, even when it is correct; that is not a
judgment of the work.

## Questions and bug reports

Ask questions and report bugs on the FEMM user group,
[groups.io/g/femm](https://groups.io/g/femm/). Please say which version you are
running (Help > About shows it) and attach a model file that shows the problem
if you can.

## Pull requests

Pull requests are welcome. Before opening one:

* Run the regression suite, `bash testing/_verify_all.sh`, and say in the pull
  request what it reported. A change that moves numerical results should
  explain why the new results are right.
* Give every new source file an `SPDX-License-Identifier` line matching the
  directory it goes in (see Licensing, below).
* Match the style of the code around your change.
* Do not edit vendored third-party code: `micropython/micropython_embed/`
  (regenerated from upstream MicroPython by `micropython/regen.sh`),
  `micropython/ulab/`, `pylib/vendor/`, `third_party/`, `lua/liblua/`,
  `lua/luasocket-1.0/` and `provenance/`. Report problems in it instead.
  `tangle/` is maintained separately and imported as a whole; changes to it are
  made there, so describe what you need rather than patching it here.

**How accepted changes arrive.** The published repository is generated from
the maintainer's own tree, so pull requests are not merged on GitHub. An
accepted change is applied there, tested, credited to you in the commit and
in the release notes, and appears in the next published release; you are
also added to [AUTHORS](AUTHORS). The pull request is then closed with a
pointer to that release.

## Licensing of contributions

Which terms apply depends on where your change lands.

* **Everything outside `solvers/`** is released under the MIT License (see
  [LICENSE-MIT](LICENSE-MIT)). By contributing, you license your contribution
  under those same terms.
* **`solvers/`** is released under the GNU General Public License v3 or later,
  and its author also offers it under other terms by arrangement. To keep that
  possible, a contribution that changes anything in `solvers/` can be accepted
  only under the **Solver Contributor License Agreement**,
  [CLA-solvers.md](CLA-solvers.md). To agree to it, include this sentence in
  your pull request, under your own name:

  > I have read and agree to the FEMM Solver Contributor License Agreement,
  > version 1.0, for this contribution.

  **If your employer owns what you write**, as is usual for employees of
  companies, universities and laboratories, you cannot agree to it yourself.
  Your employer accepts the entity version instead,
  [CLA-solvers-entity.md](CLA-solvers-entity.md): it lists the people who may
  contribute on its behalf, a person authorized to bind the organization signs
  it, and it is e-mailed to dmeeker@ieee.org, countersigned and returned.
  Organizations with an agreement in place are listed in
  [CLA-ledger.md](CLA-ledger.md). Once you are designated in it, include this
  sentence in your pull request instead of the one above:

  > I am contributing on behalf of ORGANIZATION under its FEMM Solver
  > Contributor License Agreement, entity version 1.0.

  Work that is your own rather than your employer's, such as something written
  on your own time, goes under the individual version as usual.

  **If you are a US federal employee** and the change is a work of the US
  Government, say so in the pull request (Section 3(e) of the agreement).

  Your acceptance is recorded in the commit that applies your change, with
  your name, your GitHub username and the agreement version. Without an
  agreement a solver change cannot be accepted, but a description of the
  problem it fixes is always welcome on the user group.

  **An accepted solver contribution is always published here, under the GNU
  General Public License, before it is used under any other license.** Your
  work therefore always remains available to everyone on the GPL's terms.
