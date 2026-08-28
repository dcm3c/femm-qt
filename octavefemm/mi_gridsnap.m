% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% mi_gridsnap("flag") -- the name the manual documents. octavefemm
% shipped no flag-taking wrapper for the editors at all, only
% mi_snapgridon/off, so a user following the documentation had
% nothing to call.

function mi_gridsnap(flag)
callfemm(['mi_gridsnap(' , quote(flag) , ')' ]);
