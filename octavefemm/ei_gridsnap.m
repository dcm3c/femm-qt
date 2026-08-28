% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ei_gridsnap("flag") -- the name the manual documents. octavefemm
% shipped no flag-taking wrapper for the editors at all, only
% ei_snapgridon/off, so a user following the documentation had
% nothing to call.

function ei_gridsnap(flag)
callfemm(['ei_gridsnap(' , quote(flag) , ')' ]);
