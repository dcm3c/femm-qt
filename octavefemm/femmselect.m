% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% femmselect.m -- Switch which FEMM instance receives commands
%
% Usage:
%   femmselect(48015)   -- direct commands to instance on port 48015
%   femmselect(48016)   -- direct commands to instance on port 48016

function femmselect(port)
    global femmSockets femmActivePort

    if isempty(femmSockets)
        error('No FEMM connections open. Call openfemm() first.');
    end

    portkey = ['p' num2str(port)];
    if ~isfield(femmSockets, portkey)
        error('No FEMM connection on port %d. Call openfemm() with that port first.', port);
    end

    femmActivePort = port;
end
