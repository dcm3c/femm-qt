% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ci_gridsnap("flag") -- the name the manual documents. octavefemm
% shipped no flag-taking wrapper for the editors at all, only
% ci_snapgridon/off, so a user following the documentation had
% nothing to call.

function ci_gridsnap(flag)
callfemm(['ci_gridsnap(' , quote(flag) , ')' ]);
