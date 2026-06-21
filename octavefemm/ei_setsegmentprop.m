% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ActiveFEMM (C)2006 David Meeker, dmeeker@ieee.org

function ei_setsegmentprop(pn,es,am,hi,gr, inconductor)
callfemm(['ei_setsegmentprop(' , quotec(pn) , numc(es) , numc(am) , numc(hi) , numc(gr) , quote(inconductor) , ')']);

