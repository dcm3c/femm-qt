% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ActiveFEMM (C)2006 David Meeker, dmeeker@ieee.org

function ci_createradius(x,y,r)
callfemm(['ci_createradius(' , numc(x) , numc(y), num(r) , ')' ]);

