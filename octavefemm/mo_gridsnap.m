% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% mo_gridsnap("flag") -- the name the manual documents. The wrapper
% every binding shipped was mo_snapgrid, which appears nowhere in the
% manual, so a user following the documentation had nothing to call.

function mo_gridsnap(flag)
callfemm(['mo_gridsnap(' , quote(flag) , ')' ]);
