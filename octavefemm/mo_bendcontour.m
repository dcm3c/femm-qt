% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ActiveFEMM (C)2006 David Meeker, dmeeker@ieee.org

function mo_bendcontour(tta,dtta)
callfemm(['mo_bendcontour(' , numc(tta) , num(dtta) , ')' ]);

