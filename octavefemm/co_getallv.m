% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% co_getallv() - voltage at every mesh node, in one call.
% Returns a column vector of N values (one value per node).
% Complex at a nonzero frequency.

function z = co_getallv()
    raw = callfemm('co_getallv()');
    z = raw(:);
