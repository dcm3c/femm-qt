% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% mo_getallnodes() — retrieve all mesh node coordinates in one call.
% Returns an N×2 matrix: [x y]
% ~50x faster than per-node mo_getnode.

function z = mo_getallnodes()
    raw = callfemm('mo_getallnodes()');
    z = reshape(raw, 2, [])';
