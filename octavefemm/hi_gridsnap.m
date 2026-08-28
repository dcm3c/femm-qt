% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% hi_gridsnap("flag") -- the name the manual documents. octavefemm
% shipped no flag-taking wrapper for the editors at all, only
% hi_snapgridon/off, so a user following the documentation had
% nothing to call.

function hi_gridsnap(flag)
callfemm(['hi_gridsnap(' , quote(flag) , ')' ]);
