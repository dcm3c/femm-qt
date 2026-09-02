// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 David Meeker

// Exercise CBigComplexLinProb's preconditioner under incomplete-
// factorization breakdown.
//
// The twin of tests/test_spars_pc.cpp, and it lives HERE rather than beside
// it because cspars.cpp is the GPL half of the tree: `grep -rl
// GPL-3.0-or-later` must keep matching solvers/ and nothing else.
//
// WHAT IS DIFFERENT ABOUT THE COMPLEX CASE. The real twin can lean on
// positive-definiteness: on its SPD path a nonpositive pivot is a
// contradiction, so breakdown announces itself early and unambiguously. A
// complex-symmetric matrix offers no such invariant -- no definiteness, no
// inertia, no ordering on the pivots at all. The only signal left is that a
// pivot has VANISHED, which for an unpivoted LDL^T can happen with nothing
// whatever wrong with the matrix (the real symmetric [[0,1],[1,0]] is the
// textbook case: perfectly nonsingular, dead on its first pivot). So this
// test has to establish two things the real one gets for free -- that the
// vanishing-pivot signal actually fires, and that the shift ladder still
// terminates in a guarantee.

#include "fknDlg.h"     // must precede spars.h: declares CString and CFknDlg
#include "spars.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

static int failures = 0;
static void check(bool ok, const char *what)
{
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

// Deterministic LCG, spelled out so the matrix is bit-identical everywhere.
struct Rng {
    unsigned int s;
    explicit Rng(unsigned int seed) : s(seed) {}
    unsigned int next() { s = s*1664525u + 1013904223u; return s; }
    double uniform() { return next() / 4294967296.0; }
    double decades(double lo, double hi)
    {
        double m = pow(10.0, lo + (hi-lo)*uniform());
        return (next() & 1u) ? m : -m;
    }
};

// A = L*D*L^T with unit lower-triangular sparse complex L and complex D --
// complex-SYMMETRIC (unconjugated), which is what the harmonic problem
// assembles and what cspars factors. Nothing here is definite: D's entries
// are scattered around the complex plane on purpose.
//
// `diagPhase` steers D's argument. At 0 the diagonal comes out roughly
// real; at PI/2 roughly imaginary, which is the physically interesting one
// -- an eddy-current row carries j*omega*sigma and is nearly pure
// imaginary.
static std::map<std::pair<int,int>, CComplex>
buildComplexSym(int n, unsigned int seed, double diagPhase, double spread)
{
    Rng rng(seed);

    std::vector<std::vector<std::pair<int,CComplex>>> Lcol(n);
    std::vector<CComplex> D(n);
    for (int j = 0; j < n; j++) {
        double mag = pow(10.0, -3.0 + 4.0*rng.uniform());
        double arg = diagPhase + spread*(rng.uniform() - 0.5);
        D[j] = CComplex(mag*cos(arg), mag*sin(arg));
        int nnz = 3 + (int)(rng.next() % 5u);
        for (int q = 0; q < nnz && j+1 < n; q++) {
            int r = j + 1 + (int)(rng.next() % (unsigned)(n - j - 1));
            Lcol[j].push_back({r, CComplex(rng.decades(-4.0, 1.0),
                                           rng.decades(-4.0, 1.0))});
        }
    }

    std::map<std::pair<int,int>, CComplex> A;
    for (int j = 0; j < n; j++) {
        std::vector<std::pair<int,CComplex>> col;
        col.push_back({j, CComplex(1,0)});
        for (auto &e : Lcol[j]) col.push_back(e);
        for (size_t a = 0; a < col.size(); a++)
            for (size_t c = 0; c < col.size(); c++) {
                int r = col[a].first, s = col[c].first;
                if (r > s) continue;
                A[{r,s}] += D[j] * col[a].second * col[c].second;
            }
    }
    return A;
}

static void load(CBigComplexLinProb &L,
                 const std::map<std::pair<int,int>,CComplex> &A)
{
    for (auto &kv : A) L.Put(kv.second, kv.first.first, kv.first.second);
}

int main()
{
    const int n = 400;
    const unsigned int seed = 20260901u;

    CFknDlg view(true);

    // 1. Breakdown is reachable, and the ladder ends in a guarantee.
    //
    //    NOTE ON THE FIXTURE, because it is not the random matrix the real
    //    twin's test uses and the reason matters. Sweeping 40 random
    //    complex-symmetric matrices of this size produced NOT ONE
    //    breakdown, where the real generator breaks readily. That is a
    //    property of the arithmetic, not luck: a real pivot vanishes
    //    whenever it changes sign, which a random walk does constantly,
    //    whereas a complex pivot has to bring TWO components to zero at
    //    once, which random data essentially never does. So the honest
    //    fixture is the deterministic one -- the textbook [[0,c],[c,0]]
    //    block, nonsingular yet fatal to an unpivoted LDL^T on its first
    //    pivot -- embedded in an otherwise healthy matrix.
    {
        auto A = buildComplexSym(n, seed, 0.6, 2.0);
        const CComplex c(0.7, -0.4);
        A[{0,0}] = CComplex(0,0);
        A[{0,1}] = c;
        A[{1,1}] = CComplex(0,0);

        CBigComplexLinProb L;
        L.TheView = &view;
        L.Create(n, n, n);
        load(L, A);
        L.Finalize();

        check(L.FactorPCOnce(0.0) == false,
              "unshifted ILDL breaks down on this complex-symmetric matrix");

        double guaranteed = L.DominanceShift();
        printf("  guaranteed shift = %.4g\n", guaranteed);
        check(guaranteed > 0, "the matrix is not already diagonally dominant");
        check(L.FactorPCOnce(guaranteed) == true,
              "the guaranteed (dominance) shift factors it");

        L.FactorPC();
        bool usable = true;
        for (int i = 0; i < n; i++)
            if (!std::isfinite(L.icD[i].re) || !std::isfinite(L.icD[i].im)
                || abs(L.icD[i]) == 0) usable = false;
        for (size_t q = 0; q < L.icVal.size(); q++)
            if (!std::isfinite(L.icVal[q].re) || !std::isfinite(L.icVal[q].im))
                usable = false;
        check(usable, "FactorPC's factor is finite with no vanished pivot");
    }

    // 1b. Weak dominance is NOT enough, on the textbook matrix itself.
    //
    //     [[0,1],[1,0]]: nonsingular, and dead on its first pivot. The alpha
    //     that merely equalizes |a| with the off-diagonal sum -- weak
    //     dominance -- drives the SECOND pivot exactly to zero here, so a
    //     DominanceShift computed that way would hand back a shift that does
    //     not work. Hence the dominance FACTOR. Kept as its own two-row case
    //     because in a larger matrix some unrelated row sets the maximum and
    //     the degeneracy never gets exercised.
    {
        CBigComplexLinProb L;
        L.TheView = &view;
        L.Create(2, 2, 2);
        L.Put(CComplex(0,0), 0, 0);
        L.Put(CComplex(1,0), 0, 1);
        L.Put(CComplex(0,0), 1, 1);
        L.Finalize();

        check(L.FactorPCOnce(0.0) == false, "[[0,1],[1,0]] breaks unshifted");
        check(L.FactorPCOnce(1.0) == false,
              "and the WEAK-dominance shift does not save it either");
        double guaranteed = L.DominanceShift();
        printf("  [[0,1],[1,0]] guaranteed shift = %.4g\n", guaranteed);
        check(guaranteed > 1.0, "the guaranteed shift exceeds the weak one");
        check(L.FactorPCOnce(guaranteed) == true,
              "the guaranteed shift factors it");
    }

    // 2. THE complex-specific property: the shift must preserve PHASE.
    //
    //    alpha*diag(M) multiplies each diagonal by the REAL factor (1+alpha),
    //    which moves its magnitude and leaves its argument alone. alpha*I --
    //    the recipe the textbooks give, and the one an earlier note in this
    //    project suggested -- would instead drag every diagonal toward the
    //    positive real axis. On an eddy-current row, whose diagonal is very
    //    nearly j*omega*sigma, that turns the operator into something the
    //    harmonic problem never posed.
    //
    //    Built so the diagonal is nearly pure imaginary, then shifted hard
    //    and checked: the factor's pivots must STILL be nearly pure
    //    imaginary. Under alpha*I at this shift they would be swamped by a
    //    real part.
    {
        // Built directly, not through L*D*L^T, so the diagonal's phase is
        // exactly what this asks for: PURELY imaginary, as an eddy-current
        // row's j*omega*sigma is. (Going through L*D*L^T would not do --
        // squaring complex L entries scatters the resulting diagonal's
        // argument all over the plane, which is what a first attempt at
        // this fixture did.)
        Rng rng(seed ^ 0x9e3779b9u);
        std::map<std::pair<int,int>, CComplex> A;
        for (int i = 0; i < n; i++) {
            A[{i,i}] = CComplex(0.0, pow(10.0, -1.0 + 2.0*rng.uniform()));
            for (int q = 0; q < 5; q++) {
                int j = i + 1 + (int)(rng.next() % 7u);
                if (j >= n) continue;
                A[{i,j}] += CComplex(rng.decades(-1.0, 0.7),
                                     rng.decades(-1.0, 0.7));
            }
        }

        CBigComplexLinProb L;
        L.TheView = &view;
        L.Create(n, n, n);
        load(L, A);
        L.Finalize();

        double worstDiag = 0;
        for (int i = 0; i < n; i++) {
            CComplex d = L.Get(i,i);
            double ratio = fabs(d.re) / (abs(d) > 0 ? abs(d) : 1);
            if (ratio > worstDiag) worstDiag = ratio;
        }
        printf("  |Re|/|.| of M's diagonal, worst = %.3e\n", worstDiag);
        check(worstDiag == 0.0, "the fixture's diagonal is exactly imaginary");

        // Shift far past anything the ladder would pick. The factor is then
        // essentially the shifted diagonal itself, so D's phase IS the
        // shift's answer, with no fill correction to argue about.
        check(L.FactorPCOnce(1e6) == true, "a huge shift factors it");
        double worstD = 0;
        for (int i = 0; i < n; i++) {
            double ratio = fabs(L.icD[i].re) / (abs(L.icD[i]) > 0 ? abs(L.icD[i]) : 1);
            if (ratio > worstD) worstD = ratio;
        }
        printf("  |Re|/|.| of D at shift 1e6, worst = %.3e\n", worstD);
        check(worstD < 1e-6,
              "alpha*diag(M) leaves the pivots' phase alone (alpha*I would not)");
    }

    // 3. Zero-diagonal rows: a pure Lagrange multiplier has neither
    //    magnitude nor phase for the shift to scale, so those rows shift on
    //    the row's 1-norm instead. Without that one such row costs the whole
    //    matrix its guaranteed rung.
    {
        auto A = buildComplexSym(n, seed, 0.6, 2.0);
        const int nCon = 8;
        for (int m = 0; m < nCon; m++) A[{n-1-m, n-1-m}] = CComplex(0,0);

        CBigComplexLinProb L;
        L.TheView = &view;
        L.Create(n, n, n);
        load(L, A);
        L.Finalize();

        int zeros = 0;
        for (int i = 0; i < n; i++) if (abs(L.Get(i,i)) == 0) zeros++;
        check(zeros == nCon, "the fixture really does have zero diagonals");

        double guaranteed = L.DominanceShift();
        printf("  saddle guaranteed shift = %.4g\n", guaranteed);
        check(guaranteed >= 0,
              "a zero diagonal does not forfeit the guaranteed shift");
        check(L.FactorPCOnce(guaranteed) == true,
              "the guaranteed shift factors the saddle matrix");

        L.FactorPCOnce(0.0);
        bool clean = true;
        for (int i = 0; i < nCon; i++)
            if (abs(L.Get(n-1-i, n-1-i)) != 0) clean = false;
        check(clean, "alpha == 0 leaves the zero diagonals untouched");
    }

    // 4. What actually matters: the harmonic solver reaches the right answer
    //    through a shifted preconditioner, measured against the ORIGINAL
    //    matrix rather than the shifted one.
    {
        // A well-scaled harmonic-shaped matrix -- imaginary diagonal,
        // moderate couplings -- with the textbook block dropped in, so
        // FactorPC is FORCED to shift before PBCG ever sees a factor.
        Rng gen(seed ^ 0x2545f491u);
        std::map<std::pair<int,int>, CComplex> A;
        for (int i = 0; i < n; i++) {
            A[{i,i}] = CComplex(0.0, 1.0 + gen.uniform());
            for (int q = 0; q < 4; q++) {
                int j = i + 1 + (int)(gen.next() % 5u);
                if (j >= n) continue;
                A[{i,j}] += CComplex(gen.uniform()-0.5, gen.uniform()-0.5);
            }
        }
        A[{0,0}] = CComplex(0,0);
        A[{0,1}] = CComplex(1,0);
        A[{1,1}] = CComplex(0,0);

        CBigComplexLinProb L;
        L.TheView = &view;
        L.Create(n, n, n);
        load(L, A);
        L.Finalize();
        check(L.FactorPCOnce(0.0) == false,
              "the solve fixture does require a shift");

        std::vector<CComplex> xExact(n);
        Rng rng(13572468u);
        for (int i = 0; i < n; i++)
            xExact[i] = CComplex(rng.uniform()-0.5, rng.uniform()-0.5);

        L.MultA(xExact.data(), L.b);
        std::vector<CComplex> b(L.b, L.b + n);

        L.Precision = 1e-8;
        check(L.PBCGSolve(0) == 1, "PBCGSolve converges with a shifted factor");

        std::vector<CComplex> r(n);
        L.MultA(L.V, r.data());
        double num = 0, den = 0;
        for (int i = 0; i < n; i++) {
            num += absq(b[i] - r[i]);
            den += absq(b[i]);
        }
        double rel = sqrt(num/den);
        printf("  relative residual |b - A*x| / |b| = %.3e\n", rel);
        check(rel < 1e-6, "the solution satisfies the ORIGINAL system");
    }

    printf(failures ? "test_cspars_pc: %d FAILURE(S)\n"
                    : "test_cspars_pc: all checks passed\n", failures);
    return failures ? 1 : 0;
}
