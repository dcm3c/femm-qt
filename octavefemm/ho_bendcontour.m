% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ActiveFEMM (C)2006 David Meeker, dmeeker@ieee.org

function ho_bendcontour(tta,dtta)
callfemm(['ho_bendcontour(' , numc(tta) , num(dtta) , ')' ]);

