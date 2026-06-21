% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% mo_getallelements() — retrieve all mesh element data in one call.
% Returns an N×7 matrix: [p1 p2 p3 ctr_x ctr_y area group]
% Node indices are 1-based.  ~50x faster than per-element mo_getelement.

function z = mo_getallelements()
    raw = callfemm('mo_getallelements()');
    z = reshape(raw, 7, [])';
