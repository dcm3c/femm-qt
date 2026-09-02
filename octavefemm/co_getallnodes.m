% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% co_getallnodes() - coordinates of every mesh node, in one call.
% Returns an Nx2 matrix: [x y]

function z = co_getallnodes()
    raw = callfemm('co_getallnodes()');
    z = reshape(raw, 2, [])';
