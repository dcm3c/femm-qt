% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% mo_getallb() — retrieve B field at all element centroids in one call.
% Returns an N×2 matrix: [B1 B2] (complex for AC problems)
% ~50x faster than per-element mo_getb.

function z = mo_getallb()
    raw = callfemm('mo_getallb()');
    z = reshape(raw, 2, [])';
