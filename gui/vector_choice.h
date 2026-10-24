// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// vector_choice.h — one numbering for "which vector plot", from outside the widget
#pragma once

// A vector-plot choice arrives numbered 0 = off, 1.. = a plot: from the
// Preferences dialogs and from the *_showvectorplot script commands. The
// results widgets keep it differently, as m_showVectors plus a 0-based
// m_vectorType that picks one of two fields (B/H, D/E, F/G, J/E).
//
// FEMM 4.2's current-flow and AC-magnetics lists alternate those two fields:
// Re(J), Re(E), Im(J), Im(E), Re+Im(J), Re+Im(E). femm-qt draws either field
// at the phase on the widget's slider instead, which covers Re and Im, so odd
// choices mean the first field and even ones the second. For the two-entry
// lists (electrostatics, heat, DC magnetics) that is simply choice - 1.
inline int vectorFieldOf(int choice) { return (choice - 1) % 2; }
