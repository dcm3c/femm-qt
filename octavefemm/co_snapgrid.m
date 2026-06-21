% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ActiveFEMM (C)2006 David Meeker, dmeeker@ieee.org

function co_snapgrid(flag)
callfemm(['co_gridsnap(' , quote(flag) , ')' ]);

