% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ho_getallt() - temperature at every mesh node, in one call.
% Returns a column vector of N values (one value per node).

function z = ho_getallt()
    raw = callfemm('ho_getallt()');
    z = raw(:);
