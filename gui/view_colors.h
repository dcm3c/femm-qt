// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// view_colors.h — Centralized color table for FEMM drawing
// Stores all UI + density + greyscale colors, persists via QSettings.
#ifndef VIEW_COLORS_H
#define VIEW_COLORS_H

#include <QColor>
#include <QSettings>
#include <array>

// All color indices matching the original FEMM clist[] array
enum ColorIndex {
    CI_Selected = 0,
    CI_Background,
    CI_Mesh,
    CI_Block,
    CI_Line,
    CI_Grid,
    CI_Node,
    CI_Region,
    CI_Text,
    CI_RealFluxLine,
    CI_ImagFluxLine,
    CI_MaskLine,
    CI_Name,
    CI_RealVector,
    CI_ImagVector,
    CI_Color00, CI_Color01, CI_Color02, CI_Color03, CI_Color04,
    CI_Color05, CI_Color06, CI_Color07, CI_Color08, CI_Color09,
    CI_Color10, CI_Color11, CI_Color12, CI_Color13, CI_Color14,
    CI_Color15, CI_Color16, CI_Color17, CI_Color18, CI_Color19,
    CI_Grey00, CI_Grey01, CI_Grey02, CI_Grey03, CI_Grey04,
    CI_Grey05, CI_Grey06, CI_Grey07, CI_Grey08, CI_Grey09,
    CI_Grey10, CI_Grey11, CI_Grey12, CI_Grey13, CI_Grey14,
    CI_Grey15, CI_Grey16, CI_Grey17, CI_Grey18, CI_Grey19,
    CI_COUNT  // = 55
};

// Human-readable names for the color dropdown
inline const char *colorName(int idx) {
    static const char *names[] = {
        "Selected", "Background", "Mesh", "Block Labels", "Lines/Arcs",
        "Grid", "Nodes", "Region", "Text",
        "Real Flux Lines", "Imag Flux Lines", "Mask Lines",
        "Names", "Real Vectors", "Imag Vectors",
        "Density 00 (high)", "Density 01", "Density 02", "Density 03", "Density 04",
        "Density 05", "Density 06", "Density 07", "Density 08", "Density 09",
        "Density 10", "Density 11", "Density 12", "Density 13", "Density 14",
        "Density 15", "Density 16", "Density 17", "Density 18", "Density 19 (low)",
        "Grey 00 (dark)", "Grey 01", "Grey 02", "Grey 03", "Grey 04",
        "Grey 05", "Grey 06", "Grey 07", "Grey 08", "Grey 09",
        "Grey 10", "Grey 11", "Grey 12", "Grey 13", "Grey 14",
        "Grey 15", "Grey 16", "Grey 17", "Grey 18", "Grey 19 (light)"
    };
    if (idx >= 0 && idx < CI_COUNT) return names[idx];
    return "?";
}

class ViewColors {
public:
    std::array<QColor, CI_COUNT> c;

    ViewColors() { resetDefaults(); }

    void resetDefaults();
    void save() const;
    void load();

    // Convenience accessors
    QColor selected() const { return c[CI_Selected]; }
    QColor background() const { return c[CI_Background]; }
    QColor mesh() const { return c[CI_Mesh]; }
    QColor blockLabel() const { return c[CI_Block]; }
    QColor line() const { return c[CI_Line]; }
    QColor grid() const { return c[CI_Grid]; }
    QColor node() const { return c[CI_Node]; }
    QColor name() const { return c[CI_Name]; }
    QColor realFluxLine() const { return c[CI_RealFluxLine]; }
    QColor imagFluxLine() const { return c[CI_ImagFluxLine]; }
    QColor realVector() const { return c[CI_RealVector]; }
    QColor imagVector() const { return c[CI_ImagVector]; }
    QColor densityColor(int level) const {
        if (level < 0) level = 0;
        if (level > 19) level = 19;
        return c[CI_Color00 + level];
    }
    QColor greyColor(int level) const {
        if (level < 0) level = 0;
        if (level > 19) level = 19;
        return c[CI_Grey00 + level];
    }
};

// Global instance
ViewColors &viewColors();

#endif // VIEW_COLORS_H
