% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% eo_getallv() - voltage at every mesh node, in one call.
% Returns a column vector of N values (one value per node).

function z = eo_getallv()
    raw = callfemm('eo_getallv()');
    z = raw(:);
