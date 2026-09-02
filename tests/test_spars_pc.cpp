// Exercise CBigLinProb's preconditioner under incomplete-factorization
// breakdown.
//
// WHY THIS EXISTS. The ILDL preconditioner drops fill, so for a general SPD
// matrix the incomplete Schur complement can go indefinite even though the
// matrix itself is perfectly definite -- Kershaw's 1978 observation. When
// that happened the old code patched the offending pivot in place and
// carried on; the L column already divided by the pre-patch value was then
// inconsistent with D, and the error compounded column over column. On the
// case that exposed it (a saturated PM motor warm-started across a sliding
// band) the pivots ran -1.9e-3, -2.0, -43, -5.4e8, -4.2e18 ... to overflow
// within ten columns, and PCG got a NaN where it wanted a preconditioner:
// "Couldn't solve the problem" on a well-posed model. FactorPC now refactors
// under a growing diagonal shift instead.
//
// The matrix here is synthetic on purpose. It is built as A = L*D*L^T with a
// positive D, which makes it SPD by construction and independent of any
// model file, while the several decades of magnitude in L guarantee the
// threshold rule discards enough to trigger the breakdown. The generator is
// a fixed-seed LCG written out in full, so the matrix is bit-identical on
// every platform.

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

// Deterministic LCG (Numerical Recipes' constants), spelled out so the
// matrix does not depend on any library's rand().
struct Rng {
    unsigned int s;
    explicit Rng(unsigned int seed) : s(seed) {}
    unsigned int next() { s = s*1664525u + 1013904223u; return s; }
    double uniform() { return next() / 4294967296.0; }          // [0,1)
    double decades(double lo, double hi)                        // +/- 10^U(lo,hi)
    {
        double m = pow(10.0, lo + (hi-lo)*uniform());
        return (next() & 1u) ? m : -m;
    }
};

// A = L*D*L^T with unit lower-triangular sparse L and positive diagonal D.
// Returns the upper triangle, keyed by (row, col) with row <= col.
static std::map<std::pair<int,int>, double> buildSPD(int n, unsigned int seed)
{
    Rng rng(seed);

    // L, by column: column j holds the implicit unit diagonal plus a handful
    // of entries at rows below it.
    std::vector<std::vector<std::pair<int,double>>> Lcol(n);
    std::vector<double> D(n);
    for (int j = 0; j < n; j++) {
        D[j] = pow(10.0, -3.0 + 4.0*rng.uniform());
        int nnz = 3 + (int)(rng.next() % 5u);           // 3..7 sub-diagonal entries
        for (int q = 0; q < nnz && j+1 < n; q++) {
            int r = j + 1 + (int)(rng.next() % (unsigned)(n - j - 1));
            Lcol[j].push_back({r, rng.decades(-4.0, 1.0)});
        }
    }

    // A = sum_j D[j] * l_j * l_j^T, l_j being column j of L including its
    // unit diagonal. Accumulated into the upper triangle only.
    std::map<std::pair<int,int>, double> A;
    for (int j = 0; j < n; j++) {
        std::vector<std::pair<int,double>> col;
        col.push_back({j, 1.0});
        for (auto &e : Lcol[j]) col.push_back(e);
        for (size_t a = 0; a < col.size(); a++)
            for (size_t c = 0; c < col.size(); c++) {
                int r = col[a].first, s = col[c].first;
                if (r > s) continue;                     // upper triangle only
                A[{r,s}] += D[j] * col[a].second * col[c].second;
            }
    }
    return A;
}

static void load(CBigLinProb &L, const std::map<std::pair<int,int>,double> &A)
{
    for (auto &kv : A) L.Put(kv.second, kv.first.first, kv.first.second);
}

// The shape prob1big assembles when a problem carries circuit constraints:
//
//     [  K    C^T ]     K SPD (the field), -E a small NEGATIVE diagonal on
//     [  C   -E   ]     the constraint rows -- "quasi-definite", (+,...,+,-).
//
// QMRSolve exists for exactly this, and wants a symmetric preconditioner,
// NOT a definite one: D has to be allowed to carry the sign. This is the
// case that makes the shift alpha*diag(M) rather than the textbook alpha*I
// -- see the assertions in main().
static std::map<std::pair<int,int>, double>
buildQuasiDefinite(int nField, int nCon, unsigned int seed)
{
    auto A = buildSPD(nField, seed);
    Rng rng(seed ^ 0x5bf03635u);
    for (int m = 0; m < nCon; m++) {
        int row = nField + m;
        for (int q = 0; q < 6; q++) {
            int j = (int)(rng.next() % (unsigned)nField);
            A[{j, row}] += rng.decades(-2.0, 0.5);      // C^T, upper triangle
        }
        A[{row, row}] = -pow(10.0, -6.0 + 2.0*rng.uniform());   // -E
    }
    return A;
}

// The same thing with E == 0: a pure Lagrange multiplier, whose row reaches
// the factorization with a diagonal of exactly ZERO. prob1big regularizes
// its CURRENT-constraint rows to -1e-6 precisely to avoid this, but a FLUX
// constraint whose CircuitMatrix carries no self term is not regularized and
// does arrive this way -- confirmed by running testing/myTestProblem with
// [CircuitMatrix] = 0.
static std::map<std::pair<int,int>, double>
buildSaddle(int nField, int nCon, unsigned int seed)
{
    auto A = buildQuasiDefinite(nField, nCon, seed);
    for (int m = 0; m < nCon; m++) A[{nField+m, nField+m}] = 0.0;
    return A;
}

int main()
{
    const int n = 400;
    const unsigned int seed = 20260831u;

    CFknDlg view(true);     // quiet: no progress bars on stderr

    auto A = buildSPD(n, seed);

    // 1. The breakdown is real and this matrix reaches it. Guarding this
    //    matters more than it looks: if a future change to the dropping rule
    //    makes the unshifted factorization succeed here, the rest of the test
    //    would pass while testing nothing.
    {
        CBigLinProb L;
        L.TheView = &view;
        L.Create(n, n);
        load(L, A);
        L.Finalize();
        check(L.FactorPCOnce(0.0) == false,
              "unshifted ILDL breaks down on this SPD matrix");

        // The rung the ladder ends on. Diagonal dominance makes this an
        // H-matrix, for which an incomplete factorization exists with
        // positive pivots whatever is dropped -- so this must succeed for
        // ANY matrix reaching it, not merely for this one.
        double guaranteed = L.DominanceShift();
        printf("  guaranteed shift = %.4g\n", guaranteed);
        check(guaranteed > 0, "the matrix is not already diagonally dominant");
        check(L.FactorPCOnce(guaranteed) == true,
              "the guaranteed (dominance) shift factors it");
    }

    // 2. FactorPC picks a shift and hands back a usable factor: finite
    //    throughout, and with a strictly positive D, which is what PCG needs
    //    of a preconditioner.
    {
        CBigLinProb L;
        L.TheView = &view;
        L.Create(n, n);
        load(L, A);
        L.Finalize();
        L.FactorPC();

        bool finite = true, posD = true;
        for (int i = 0; i < n; i++) {
            if (!std::isfinite(L.icD[i])) finite = false;
            if (!(L.icD[i] > 0)) posD = false;
        }
        for (size_t q = 0; q < L.icVal.size(); q++)
            if (!std::isfinite(L.icVal[q])) finite = false;
        check(finite, "FactorPC's factor is finite everywhere");
        check(posD,   "FactorPC's D stays positive (SPD preconditioner)");
    }

    // 3. What actually matters: PCG solves against it. The preconditioner
    //    only steers the iteration, so a shifted factor must still reach the
    //    true solution -- checked here against the residual of the ORIGINAL
    //    matrix, not the shifted one.
    {
        CBigLinProb L;
        L.TheView = &view;
        L.Create(n, n);
        load(L, A);

        std::vector<double> xExact(n);
        Rng rng(1234567u);
        for (int i = 0; i < n; i++) xExact[i] = rng.uniform() - 0.5;

        L.Finalize();
        L.MultA(xExact.data(), L.b);            // b = A*xExact
        std::vector<double> b(L.b, L.b + n);

        L.Precision = 1e-8;
        int ok = L.PCGSolve(0);
        check(ok == 1, "PCGSolve converges with the shifted preconditioner");

        std::vector<double> r(n);
        L.MultA(L.V, r.data());
        double num = 0, den = 0;
        for (int i = 0; i < n; i++) {
            double d = b[i] - r[i];
            num += d*d;
            den += b[i]*b[i];
        }
        double rel = sqrt(num/den);
        printf("  relative residual |b - A*x| / |b| = %.3e\n", rel);
        check(rel < 1e-6, "the solution satisfies the ORIGINAL system");
    }

    // 4. The indefinite path: circuit constraints + QMR. Nothing here may
    //    force the preconditioner definite.
    {
        const int nField = 300, nCon = 12;
        auto Q = buildQuasiDefinite(nField, nCon, seed);

        CBigLinProb L;
        L.TheView = &view;
        L.Create(nField + nCon, nField + nCon);
        load(L, Q);
        L.Finalize();

        // The guaranteed rung has to be AVAILABLE here. It is computed from
        // |diagonal|, so negative diagonals do not disqualify a matrix from
        // it -- if this ever returns -1 for a merely indefinite matrix, the
        // ladder silently loses its backstop and ends at Jacobi.
        double guaranteed = L.DominanceShift();
        printf("  quasi-definite guaranteed shift = %.4g\n", guaranteed);
        check(guaranteed >= 0,
              "the dominance shift is available despite negative diagonals");

        // THE point of shifting by alpha*diag(M) and not the textbook
        // alpha*I: diag scales each diagonal by (1+alpha), preserving its
        // sign, so a quasi-definite matrix stays quasi-definite however far
        // the ladder climbs. alpha*I would march the small negative
        // constraint diagonals up through zero and out the positive side --
        // at the shift this matrix asks for, 2e6 against diagonals of 1e-6,
        // it would definitise them completely -- handing QMR a preconditioner
        // of the wrong inertia. Caught here as: even at the largest shift the
        // ladder can choose, D must still go negative somewhere.
        //
        // (Note what is NOT claimed: that D's inertia MATCHES the matrix's.
        // Dropping fill can flip an individual pivot, and both this code and
        // its predecessor leave 2 of the 12 constraint rows non-negative.
        // QMR wants a symmetric preconditioner, not a definite or an
        // inertia-faithful one.)
        check(L.FactorPCOnce(guaranteed) == true,
              "the guaranteed shift factors the quasi-definite matrix");
        int negAtShift = 0;
        for (int i = 0; i < nField + nCon; i++) if (L.icD[i] < 0) negAtShift++;
        printf("  D has %d negative pivots at the guaranteed shift\n", negAtShift);
        check(negAtShift > 0,
              "even the largest shift leaves the preconditioner indefinite");

        L.FactorPC();
        int neg = 0;
        for (int i = 0; i < nField + nCon; i++) if (L.icD[i] < 0) neg++;
        printf("  D has %d negative pivots as FactorPC leaves it\n", neg);
        check(neg > 0, "D is allowed to go negative on the indefinite path");

        // And it solves. QMR, not PCG, because of the constraint rows.
        std::vector<double> xExact(nField + nCon);
        Rng rng(7654321u);
        for (int i = 0; i < nField + nCon; i++) xExact[i] = rng.uniform() - 0.5;
        L.MultA(xExact.data(), L.b);
        std::vector<double> b(L.b, L.b + nField + nCon);

        L.Precision = 1e-8;
        check(L.QMRSolve(0) == 1, "QMRSolve converges on the quasi-definite system");

        std::vector<double> r(nField + nCon);
        L.MultA(L.V, r.data());
        double num = 0, den = 0;
        for (int i = 0; i < nField + nCon; i++) {
            double d = b[i] - r[i];
            num += d*d;
            den += b[i]*b[i];
        }
        printf("  relative residual |b - A*x| / |b| = %.3e\n", sqrt(num/den));
        check(sqrt(num/den) < 1e-6, "the constrained solution is correct");
    }

    // 5. Zero-diagonal saddle rows -- a pure Lagrange multiplier. No
    //    multiple of a zero diagonal strengthens anything, so these rows
    //    shift on the row's 1-norm instead; without that the whole matrix
    //    loses the guaranteed rung on account of a handful of rows, and the
    //    ladder ends at Jacobi.
    {
        const int nField = 300, nCon = 12, N = nField + nCon;
        auto S = buildSaddle(nField, nCon, seed);

        CBigLinProb L;
        L.TheView = &view;
        L.Create(N, N);
        load(L, S);
        L.Finalize();

        int zeros = 0;
        for (int i = 0; i < N; i++) if (L.Get(i,i) == 0) zeros++;
        check(zeros == nCon, "the fixture really does have zero diagonals");

        double guaranteed = L.DominanceShift();
        printf("  saddle guaranteed shift = %.4g\n", guaranteed);
        check(guaranteed >= 0,
              "a zero diagonal does not forfeit the guaranteed shift");
        check(L.FactorPCOnce(guaranteed) == true,
              "the guaranteed shift factors the saddle matrix");

        // alpha == 0 must still leave those diagonals at exactly zero --
        // the shift may not perturb a matrix nobody asked to perturb.
        L.FactorPCOnce(0.0);
        bool clean = true;
        for (int i = nField; i < N; i++) if (L.Get(i,i) != 0) clean = false;
        check(clean, "alpha == 0 leaves the zero diagonals untouched");

        L.FactorPC();
        bool finite = true;
        for (int i = 0; i < N; i++) if (!std::isfinite(L.icD[i]) || L.icD[i] == 0)
            finite = false;
        for (size_t q = 0; q < L.icVal.size(); q++)
            if (!std::isfinite(L.icVal[q])) finite = false;
        check(finite, "no zero or non-finite pivot survives into the factor");

        std::vector<double> xExact(N);
        Rng rng(2468013u);
        for (int i = 0; i < N; i++) xExact[i] = rng.uniform() - 0.5;
        L.MultA(xExact.data(), L.b);
        std::vector<double> b(L.b, L.b + N);

        L.Precision = 1e-8;
        check(L.QMRSolve(0) == 1, "QMRSolve converges on the saddle system");

        std::vector<double> r(N);
        L.MultA(L.V, r.data());
        double num = 0, den = 0;
        for (int i = 0; i < N; i++) { double d = b[i]-r[i]; num += d*d; den += b[i]*b[i]; }
        printf("  relative residual |b - A*x| / |b| = %.3e\n", sqrt(num/den));
        check(sqrt(num/den) < 1e-6, "the saddle solution is correct");
    }

    printf(failures ? "test_spars_pc: %d FAILURE(S)\n" : "test_spars_pc: all checks passed\n",
           failures);
    return failures ? 1 : 0;
}
