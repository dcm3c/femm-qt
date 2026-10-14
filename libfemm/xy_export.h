// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// xy_export.h — the one definition of how FEMM writes X-Y plot data
//
// Two paths reach this file: the Export... button on a rendered plot window,
// and *_makeplot called with a filename. They used to write different things
// (comma-separated with a header row vs. tab-separated with none); they now
// share writeCsv, so a file is the same whichever way it was produced.
//
// The plot-quantity name tables live here too, rather than in the GUI, for the
// same reason: the column headings in the file have to agree with the labels on
// the plot, and *_makeplot has no access to a GUI list. The index into each
// table IS the plotType argument of GetLineValues, so a table and the switch in
// the matching <physics>_results_doc.cpp must be edited together.
#ifndef XY_EXPORT_H
#define XY_EXPORT_H

#include "utf8_io.h"

#include <cstdio>
#include <string>
#include <vector>

namespace xyexport {

// Strip the parenthesised gloss that pads the GUI combo:
//   "|B|      (Magnitude of flux density)" -> "|B|"
inline std::string shortName(const std::string &s)
{
    const size_t paren = s.find('(');
    if (paren == std::string::npos || paren == 0) return s;
    size_t end = paren;
    while (end > 0 && (s[end - 1] == ' ' || s[end - 1] == '\t')) end--;
    return s.substr(0, end);
}

// Labels for the value columns of a quantity called `name`. One column is a
// plain real value; three are the magnitude, real and imaginary parts of a
// phasor. Anything else falls back to the bare name repeated.
inline std::vector<std::string> columnLabels(const std::string &name, int ncols)
{
    std::vector<std::string> out;
    if (ncols == 3) {
        out.push_back("|" + name + "|");
        out.push_back("Re(" + name + ")");
        out.push_back("Im(" + name + ")");
    } else {
        for (int i = 0; i < ncols; i++) out.push_back(name);
    }
    return out;
}

// Significant digits for exported values. %.14g is the house convention for a
// number a user reads or pastes elsewhere -- the same format the script
// console's print override uses (py_init_script.cpp). Not %.17g: that tier is
// for file round-trip, and it prints noise digits no field solution carries.
// Not %.8e either, which the Export... button used to emit; 'g' keeps
// mid-range values out of exponential notation, which reads better in a
// spreadsheet and matches every other number the program shows.
#define XY_EXPORT_FMT "%.14g"

// One CSV field, quoted per RFC 4180 when it has to be: a field holding a
// comma, a double quote or a line break is wrapped in double quotes, with each
// embedded quote doubled. Anything else is written bare -- whitespace alone
// needs no quoting. Labels such as "H, A/m" are why this exists: written bare,
// they split into two columns and the header no longer lines up with the data.
inline std::string csvField(const std::string &s)
{
    if (s.find_first_of(",\"\r\n") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += '"';
    return out;
}

// Write the data as comma-separated values: a header row naming the columns,
// then one row per point holding the x value followed by one column per curve.
// Ragged columns are tolerated -- a curve shorter than the x vector simply
// stops contributing.
//
// The separator is a bare comma, with no space after it. Under RFC 4180 a
// space after the comma belongs to the next field, and a quote only opens a
// quoted field as that field's first character, so ", \"H, A/m\"" would still
// split on the inner comma in Excel and in Python's csv module.
inline bool writeCsv(const std::string &path,
                     const std::string &xLabel,
                     const std::vector<std::string> &colLabels,
                     const std::vector<double> &x,
                     const std::vector<std::vector<double>> &cols)
{
    if (x.empty() || cols.empty()) return false;
    FILE *fp = femm_fopen(path, "wt");
    if (!fp) return false;

    fprintf(fp, "%s", csvField(xLabel).c_str());
    for (size_t c = 0; c < cols.size(); c++)
        fprintf(fp, ",%s", c < colLabels.size() ? csvField(colLabels[c]).c_str() : "");
    fprintf(fp, "\n");

    for (size_t i = 0; i < x.size(); i++) {
        fprintf(fp, XY_EXPORT_FMT, x[i]);
        for (const auto &col : cols)
            if (i < col.size()) fprintf(fp, "," XY_EXPORT_FMT, col[i]);
        fprintf(fp, "\n");
    }
    fclose(fp);
    return true;
}

// The x axis of a contour plot is arc length from the start of the contour.
inline const char *contourAxisLabel() { return "Position"; }

// ---------------------------------------------------------------------------
// Plot-quantity names. Index == the plotType argument of GetLineValues.
// ---------------------------------------------------------------------------

inline const std::vector<std::string> &magPlotNames()
{
    static const std::vector<std::string> n = {
        "Potential",
        "|B|      (Magnitude of flux density)",
        "B . n   (Normal flux density)",
        "B . t   (Tangential flux density)",
        "|H|      (Magnitude of field intensity)",
        "H . n   (Normal field intensity)",
        "H . t   (Tangential field intensity)",
        "J_eddy     (Eddy current density)",
        "Js+J_eddy  (Total current density)",
    };
    return n;
}

// Air gap element plots (ResultsDoc::GetGapValues) offer the contour list
// without the two current densities, which GetGapValues does not compute.
inline const std::vector<std::string> &magGapPlotNames()
{
    static const std::vector<std::string> n(magPlotNames().begin(),
                                            magPlotNames().end() - 2);
    return n;
}

// An air gap plot is sampled around the gap, so its x axis is an angle.
inline const char *gapAxisLabel() { return "Angle, Degrees"; }

inline const std::vector<std::string> &elecPlotNames()
{
    static const std::vector<std::string> n = {
        "V (voltage)", "|D|", "D.n", "D.t", "|E|", "E.n", "E.t",
    };
    return n;
}

inline const std::vector<std::string> &heatPlotNames()
{
    static const std::vector<std::string> n = {
        "Temperature", "|F| (flux magnitude)", "F.n", "F.t",
        "|G| (gradient)", "G.n", "G.t",
    };
    return n;
}

inline const std::vector<std::string> &currPlotNames()
{
    static const std::vector<std::string> n = {
        "V (voltage)", "|J| (current density)", "J.n", "J.t",
        "|E| (electric field)", "E.n", "E.t",
        "|Jc| (conduction)", "Jc.n", "Jc.t",
        "|Jd| (displacement)", "Jd.n", "Jd.t",
    };
    return n;
}

} // namespace xyexport

#endif // XY_EXPORT_H
