% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ho_gridsnap("flag") -- the name the manual documents. The wrapper
% every binding shipped was ho_snapgrid, which appears nowhere in the
% manual, so a user following the documentation had nothing to call.

function ho_gridsnap(flag)
callfemm(['ho_gridsnap(' , quote(flag) , ')' ]);
