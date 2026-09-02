% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% eo_getalld() - electric displacement D for every element, in one call.
% Returns an Nx2 matrix: [Dx Dy]

function z = eo_getalld()
    raw = callfemm('eo_getalld()');
    z = reshape(raw, 2, [])';
