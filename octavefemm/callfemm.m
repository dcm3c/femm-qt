% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% callfemm.m -- Send a command to femm-qt via TCP and return result
% Uses the currently active connection (set by openfemm or femmselect).

function z = callfemm(x)
    global femmSockets femmActivePort

    if isempty(femmSockets) || isempty(femmActivePort)
        error('Not connected to FEMM. Call openfemm() first.');
    end

    portkey = ['p' num2str(femmActivePort)];
    if ~isfield(femmSockets, portkey)
        error('No FEMM connection on port %d. Call openfemm() first.', femmActivePort);
    end

    conn = femmSockets.(portkey);

    % Send command and read response, catching dead connections
    try
        cmd = uint8([char(x), char(10)]);
        conn.out.write(cmd, 0, length(cmd));
        conn.out.flush();

        % Read response (blocks until newline)
        response = char(conn.reader.readLine());
    catch ex
        % Connection is dead — clean up the stale socket
        try conn.socket.close(); catch; end
        femmSockets = rmfield(femmSockets, portkey);
        if isempty(fieldnames(femmSockets))
            femmActivePort = [];
        end
        error('callfemm:disconnected', 'FEMM connection lost (process may have closed). Call openfemm() to reconnect.');
    end

    % Check for error
    if length(response) >= 6 && strcmp(response(1:6), 'error:')
        error('FEMM returns:\n%s', response(7:end));
    end

    % Parse result
    if isempty(response)
        z = [];
        return
    end

    % Replace complex number notation and tab separators.
    % If eval fails (e.g. the response is a string like a filename
    % from mi_gettitle), return the raw response string instead.
    response = strrep(response, 'I', 'i');
    response = strrep(response, char(9), ',');
    try
        z = eval(['[' response ']']);
    catch
        z = response;
    end
end
