% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% mo_getallgapa(agename, npts) — retrieve A at all gap points in one call.
% agename: air gap element boundary name
% npts:    number of evaluation points (default 360)
% Returns an npts×1 vector (complex for AC problems).

function z = mo_getallgapa(agename, npts)
    if nargin < 2
        raw = callfemm(['mo_getallgapa(' , quote(agename) , ')']);
    else
        raw = callfemm(['mo_getallgapa(' , quotec(agename) , num(npts) , ')']);
    end
    z = raw(:);
