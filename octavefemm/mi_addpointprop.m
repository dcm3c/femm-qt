% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ActiveFEMM (C)2006 David Meeker, dmeeker@ieee.org

function mi_addpointprop(pname,ap,jp)
callfemm(['mi_addpointprop(' , quotec(pname) , numc(ap), num(jp), ')']);

