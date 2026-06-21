% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% mo_getallgapharmonics(agename) — retrieve all gap harmonic coefficients.
% Returns an nn×6 matrix: [acc acs brc brs btc bts]
% (complex for AC problems)

function z = mo_getallgapharmonics(agename)
    raw = callfemm(['mo_getallgapharmonics(' , quote(agename) , ')']);
    z = reshape(raw, 6, [])';
