% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% eo_getallnodes() - coordinates of every mesh node, in one call.
% Returns an Nx2 matrix: [x y]

function z = eo_getallnodes()
    raw = callfemm('eo_getallnodes()');
    z = reshape(raw, 2, [])';
