% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% mo_getalla() — retrieve A (vector potential) at all element centroids.
% Returns an N×1 vector (complex for AC problems).
% ~50x faster than per-element mo_geta.

function z = mo_getalla()
    raw = callfemm('mo_getalla()');
    z = raw(:);
