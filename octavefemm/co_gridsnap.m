% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% co_gridsnap("flag") -- the name the manual documents. The wrapper
% every binding shipped was co_snapgrid, which appears nowhere in the
% manual, so a user following the documentation had nothing to call.

function co_gridsnap(flag)
callfemm(['co_gridsnap(' , quote(flag) , ')' ]);
