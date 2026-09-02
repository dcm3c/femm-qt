% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ho_getallnodes() - coordinates of every mesh node, in one call.
% Returns an Nx2 matrix: [x y]

function z = ho_getallnodes()
    raw = callfemm('ho_getallnodes()');
    z = reshape(raw, 2, [])';
