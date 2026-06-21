% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% mo_getallgapb(agename, npts) — retrieve B at all gap points in one call.
% agename: air gap element boundary name
% npts:    number of evaluation points (default 360)
% Returns an npts×2 matrix: [Br Bt] (complex for AC problems)

function z = mo_getallgapb(agename, npts)
    if nargin < 2
        raw = callfemm(['mo_getallgapb(' , quote(agename) , ')']);
    else
        raw = callfemm(['mo_getallgapb(' , quotec(agename) , num(npts) , ')']);
    end
    z = reshape(raw, 2, [])';
