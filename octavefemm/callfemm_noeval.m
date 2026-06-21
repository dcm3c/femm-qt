% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% callfemm_noeval.m -- Send a command without parsing the result
% Fire-and-forget: does not read a response. Only used by closefemm
% where the server exits before responding.

function callfemm_noeval(x)
    global femmSockets femmActivePort

    if isempty(femmSockets) || isempty(femmActivePort)
        error('Not connected to FEMM. Call openfemm() first.');
    end

    portkey = ['p' num2str(femmActivePort)];
    if ~isfield(femmSockets, portkey)
        error('No FEMM connection on port %d.', femmActivePort);
    end

    conn = femmSockets.(portkey);
    cmd = uint8([x, char(10)]);
    conn.out.write(cmd, 0, length(cmd));
    conn.out.flush();
end
