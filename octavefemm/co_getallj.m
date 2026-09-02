% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% co_getallj() - current density J for every element, in one call.
% Returns an Nx2 matrix: [Jx Jy]
% Complex at a nonzero frequency.

function z = co_getallj()
    raw = callfemm('co_getallj()');
    z = reshape(raw, 2, [])';
