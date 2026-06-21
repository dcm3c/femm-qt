% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% closefemm.m -- Close the currently active FEMM connection
%
% Usage:
%   closefemm()         -- close current instance
%   closefemm(port)     -- close instance on specific port

function closefemm(varargin)
    global femmSockets femmActivePort

    if nargin >= 1
        port = varargin{1};
    else
        port = femmActivePort;
    end

    if isempty(port)
        return
    end

    portkey = ['p' num2str(port)];

    try
        callfemm_noeval('quit()');
    catch
    end

    % Close Java socket
    if ~isempty(femmSockets) && isfield(femmSockets, portkey)
        try
            femmSockets.(portkey).socket.close();
        catch
        end
        femmSockets = rmfield(femmSockets, portkey);
    end

    % Wait briefly for femm-qt to fully exit. Without this, an immediate
    % subsequent openfemm() races against the dying process — the listen
    % socket can stay kernel-bound for a few ms after exit(0), so the
    % openfemm connect probe succeeds against a moribund instance and
    % then gets RST when it tries to send the first command. mathfemm's
    % CloseFEMM has the equivalent Pause[0.5] for the same reason.
    pause(0.1);

    if femmActivePort == port
        femmActivePort = [];
    end
end
