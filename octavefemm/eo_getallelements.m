% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% eo_getallelements() — retrieve all mesh element data in one call.
% Returns an N×7 matrix: [p1 p2 p3 ctr_x ctr_y area group]
% Node indices are 1-based.
%
% Uses the straccum/strappend/strfinish accumulator to build the result
% on the C++ heap (no interpreter heap pressure) in one IPC round-trip.

function z = eo_getallelements()
    n = callfemm('eo_numelements()');
    callfemm(['straccum(' num2str(n*70) ')']);
    callfemm(['for k in range(1,int(eo_numelements())+1): strappend(*eo_getelement(k))']);
    raw = callfemm('strfinish()');
    z = reshape(raw, 7, [])';
