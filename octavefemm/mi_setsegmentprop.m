% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ActiveFEMM (C)2006 David Meeker, dmeeker@ieee.org

function mi_setsegmentprop(pn,es,am,hi,gr)
callfemm(['mi_setsegmentprop(' , quotec(pn) , numc(es) , numc(am) , numc(hi) , num(gr) , ')']);

