// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 David Meeker

// main.cpp — CLI entry point for fkn magnetics solver
// Replaces the MFC dialog-based launcher with a simple console program.

#include "StdAfx.h"
#ifdef _OPENMP
#include <omp.h>
#endif
#include "fkn.h"
#include "fknDlg.h"
#include "complex.h"
#include "spars.h"
#include "mesh.h"
#include "femmedoccore.h"
// Tangle meshing — in-memory mesh transfer (no file I/O)
#include "tangle_mesh.h"
#include "utf8_io.h"

// fkn-local glue: mesh the .fem and populate the doc in one call. Defined in
// load_tangle_mesh.cpp. Not part of the tangle interface (the doc type is
// solver-specific); a signature drift here surfaces as a link error.
int tangle_mesh_and_load(const std::string& inputBase, CFemmeDocCore& Doc);

// MagDirFctn formula evaluator.
#include "mag_dir_eval.h"

int main(int argc, char* argv[])
{
    std::vector<std::string> args = femm_utf8_args(argc, argv);
#ifdef _OPENMP
    // Cap threads at 4 by default — diminishing returns beyond that,
    // and users may prefer spare cores for other solver instances.
    // Respects OMP_NUM_THREADS if set lower.
    int max_t = omp_get_max_threads();
    if (max_t > 4) omp_set_num_threads(4);
    fprintf(stderr, "OpenMP: %d threads\n", omp_get_max_threads());
#endif

    CFknDlg theView;
    CFemmeDocCore Doc;
    char PathName[4096];
    char outstr[1024];

    if ((int)args.size() < 2) {
        fprintf(stderr, "Usage: fkn <filename>\n");
        fprintf(stderr, "  filename: path to .fem file (without extension)\n");
        return 1;
    }
    snprintf(PathName, sizeof(PathName), "%s", args[1].c_str());

    Doc.PathName = PathName;
    Doc.TheView = &theView;

    if (Doc.OnOpenDocument() != TRUE) {
        fprintf(stderr, "Problem loading .fem file\n");
        return 7;
    }

    // A block label with no material assigned ("<None>") flattens to file index 0,
    // i.e. BlockType -1 here; the mesher reads index 0 as a hole and voids the
    // region -- an empty-mesh crash if it is the only one, else a silently dropped
    // region. Always an error: a default block label only covers regions with no
    // label at all, not a label left without a material. libfemm rejects it too.
    for (int i = 0; i < Doc.NumBlockLabels; i++)
        if (Doc.labellist[i].BlockType < 0) {
            fprintf(stderr, "A block label has no material assigned.\n");
            return 10;
        }

    // Mesh the geometry and load directly into solver arrays (no file I/O)
    // For incremental permeability, fall back to LoadMesh() which reads the previous solution
    if ((Doc.PrevSoln.GetLength() > 0) && (Doc.PrevType > 0)) {
        if (Doc.LoadMesh() != TRUE) {
            fprintf(stderr, "Problem loading mesh from previous solution\n");
            return 2;
        }
    } else {
        fprintf(stderr, "Meshing...\n");
        int meshret = tangle_mesh_and_load(PathName, Doc);
        if (meshret != TRUE) {
            // Distinct exit codes per failure category (tangle also prints the
            // specific reason to stderr): 10 no material, 11 no file, 12 parse,
            // 13 meshing, 2 generic mesh-load.
            if (meshret == -2)                 return 10;
            if (meshret == TANGLE_ERR_NO_FILE) { fprintf(stderr, "Input file not found\n"); return 11; }
            if (meshret == TANGLE_ERR_PARSE)   { fprintf(stderr, "Could not parse input file\n"); return 12; }
            if (meshret == TANGLE_ERR_MESH)    { fprintf(stderr, "Meshing failed\n"); return 13; }
            fprintf(stderr, "Meshing or mesh loading failed\n");
            return 2;
        }
    }

    // Axisymmetric geometry must lie at r >= 0. A region drawn left of the r = 0
    // axis otherwise meshes and "solves" silently with negative element volumes
    // (each carries a 2*pi*r weight). Checking the meshed nodes also catches an
    // arc whose belly dips below the axis even though its endpoints do not, since
    // meshing lands nodes on the arc. Tolerance matches the solver's on-axis test
    // (c[LengthUnits]*1e-6). The GUI/headless front ends guard this too, but a
    // solver invoked directly from the command line would not otherwise.
    if (Doc.ProblemType == 1) {
        double c[] = {2.54, 0.1, 1., 100., 0.00254, 1.e-04};
        double tol = c[Doc.LengthUnits] * 1.e-6;
        double minR = 0.0;
        for (int i = 0; i < Doc.NumNodes; i++)
            if (Doc.meshnode[i].x < minR) minR = Doc.meshnode[i].x;
        if (minR < -tol) {
            fprintf(stderr, "Axisymmetric geometry extends to r = %g; move it onto "
                    "or to the right of the r = 0 axis.\n", minR / c[Doc.LengthUnits]);
            return 9;
        }
    }

    // Print problem name
    char *ProbName = PathName;
    for (int i = 0; i < (int)strlen(PathName); i++)
        if (PathName[i] == '\\' || PathName[i] == '/')
            ProbName = PathName + i + 1;
    fprintf(stderr, "%s - fkern\n", ProbName);

    // Cuthill-McKee renumbering is now done by tangle at mesh time.

    fprintf(stderr, "Problem Statistics:\n%i nodes\n%i elements\nPrecision: %3.2e\n",
            Doc.NumNodes, Doc.NumEls, Doc.Precision);

    if (Doc.Frequency == 0) {
        CBigLinProb L;
        L.TheView = &theView;
        L.Precision = Doc.Precision;

        if (L.Create(Doc.NumNodesPlus, Doc.BandWidth) == FALSE) {
            fprintf(stderr, "Couldn't allocate enough space for matrices\n");
            return 4;
        }

        if (Doc.ProblemType == FALSE) {
            if (Doc.Static2D(L) == FALSE) {
                fprintf(stderr, "Couldn't solve the problem\n");
                return 5;
            }
            theView.m_prog1.SetPos(100); theView.m_prog2.SetPos(100); theView.UpdateWindow();
            fprintf(stderr, "\nStatic 2-D problem solved\n");
        } else {
            if (Doc.StaticAxisymmetric(L) == FALSE) {
                fprintf(stderr, "Couldn't solve the problem\n");
                return 5;
            }
            theView.m_prog1.SetPos(100); theView.m_prog2.SetPos(100); theView.UpdateWindow();
            fprintf(stderr, "\nStatic axisymmetric problem solved\n");
        }

        if (Doc.WriteStatic2D(L) == FALSE) {
            fprintf(stderr, "Couldn't write results to disk\n");
            return 6;
        }
        fprintf(stderr, "Results written to disk\n");

    } else {
        CBigComplexLinProb L;
        L.TheView = &theView;
        L.Precision = Doc.Precision;

        if (L.Create(Doc.NumNodes + Doc.NumCircProps, Doc.BandWidth, Doc.NumNodes) == FALSE) {
            fprintf(stderr, "Couldn't allocate enough space for matrices\n");
            return 4;
        }

        if (Doc.ProblemType == FALSE) {
            if (Doc.Harmonic2D(L) == FALSE) {
                fprintf(stderr, "Couldn't solve the problem\n");
                return 5;
            }
            theView.m_prog1.SetPos(100); theView.m_prog2.SetPos(100); theView.UpdateWindow();
            fprintf(stderr, "\nHarmonic 2-D problem solved\n");
        } else {
            if (Doc.HarmonicAxisymmetric(L) == FALSE) {
                fprintf(stderr, "Couldn't solve the problem\n");
                return 5;
            }
            theView.m_prog1.SetPos(100); theView.m_prog2.SetPos(100); theView.UpdateWindow();
            fprintf(stderr, "\nHarmonic axisymmetric problem solved\n");
        }

        if (Doc.WriteHarmonic2D(L) == FALSE) {
            fprintf(stderr, "Couldn't write results to disk\n");
            return 6;
        }
        fprintf(stderr, "Results written to disk\n");
    }

    Doc.CleanUp();
    return 0;
}
