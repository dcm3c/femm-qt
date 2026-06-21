% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ActiveFEMM (C)2006 David Meeker, dmeeker@ieee.org

function z=ho_getconductorproperties(pname)
z=callfemm(['ho_getconductorproperties(' , quote(pname) , ')' ]);

