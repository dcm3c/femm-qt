// point_location.h -- shared tolerance for the point-in-triangle predicate.
//
// Each of the four postprocessors carries its own copy of InTriangleTest
// (they differ only in member names). They all test an edge with an
// orientation-consistent cross product whose endpoints are ordered by NODE
// INDEX, so that two triangles sharing an edge evaluate the identical
// expression with opposite reject conditions. That ordering is what makes them
// agree about a point lying on their common edge, and it must not be removed.
//
// What the ordering does NOT provide is an exact zero. For a query point lying
// on an edge the two products are equal as reals, but they are not the same
// pair of doubles, so each rounds separately and z is a small residual whose
// sign is arbitrary. Across a SHARED edge that is harmless -- both triangles
// compute the same z and exactly one of them claims the point. On an OUTER
// BOUNDARY edge there is no second triangle, and a wrong sign loses the point
// outright: measured on Windows before this tolerance existed, 23-35% of the
// midpoints of slanted boundary edges could not be found, in all four physics.
// Axis-parallel edges were exempt (both products contain a zero factor), which
// is why contours hugging r=0 never exposed it.
//
// A query point that IS a vertex used to be safe, because there both products
// come from the same pair of doubles and round identically. That stopped being
// true when a compiler contracted a*b - c*d into a fused multiply-add, which
// rounds only one of them: clang on arm64 does this by default, as does gcc
// with -ffp-contract=fast wherever FMA exists. On macOS it made point location
// fail at mesh nodes -- 100% of the time at a degree-1 node, which is how it
// was found (a temperature-drop line integral whose endpoint sat on a tangency).
//
// One tolerance covers both, because both residuals have the same bound. The
// comparison is only ever decided near zero, i.e. with the query point on or
// within about an edge length of the edge, and there each product is at most
// M^2 in magnitude, where M is the edge length. So each rounds to within
// 2^-53*M^2 ~ 1.1e-16*M^2, and the FMA residual -- which IS the second
// product's rounding error -- is bounded by the same thing. Further away, z
// grows like M times the distance and swamps any tolerance, so the bound does
// not need to hold there. Scaling by the edge keeps the tolerance
// dimensionally consistent with z, which is an area:
//
//     eps = kInTriangleTol * (dx*dx + dy*dy)
//
// 1e-12 clears the 1.1e-16 noise floor by about four orders of magnitude while
// admitting a point only within ~1e-12*M of the edge -- far below any
// geometric scale, and tighter than the mesher's own point location, which
// locates with a tolerance scaled at 1e-10 (EPS_SCALE in tangle.cpp).
//
// Do not replace this with a compiler flag. -ffp-contract=off restores the
// exact zero only where the two products are the same pair of doubles, i.e.
// only at vertices; it does nothing for the boundary-edge case, which is the
// larger defect and is present on every platform.

#ifndef POINT_LOCATION_H
#define POINT_LOCATION_H

static const double kInTriangleTol = 1.0e-12;

#endif  // POINT_LOCATION_H
