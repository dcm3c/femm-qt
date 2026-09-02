% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ActiveFEMM (C)2006 David Meeker, dmeeker@ieee.org

function eo_selectconductor(name)
callfemm(['eo_selectconductor(' , quote(name) , ')' ]);
