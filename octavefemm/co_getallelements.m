% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% co_getallelements() - data for every mesh element, in one call.
% Returns an Nx7 matrix: [p1 p2 p3 ctr_x ctr_y area group]
% Node indices are 1-based.

function z = co_getallelements()
    raw = callfemm('co_getallelements()');
    z = reshape(raw, 7, [])';
