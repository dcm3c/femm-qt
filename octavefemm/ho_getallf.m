% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ho_getallf() - heat flux density F for every element, in one call.
% Returns an Nx2 matrix: [Fx Fy]

function z = ho_getallf()
    raw = callfemm('ho_getallf()');
    z = reshape(raw, 2, [])';
