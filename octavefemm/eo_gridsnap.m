% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% eo_gridsnap("flag") -- the name the manual documents. The wrapper
% every binding shipped was eo_snapgrid, which appears nowhere in the
% manual, so a user following the documentation had nothing to call.

function eo_gridsnap(flag)
callfemm(['eo_gridsnap(' , quote(flag) , ')' ]);
