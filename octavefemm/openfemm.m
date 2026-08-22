% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% openfemm.m -- Open connection to femm-qt via TCP
% Development version using TCP IPC instead of ActiveX/file-link.
%
% Usage:
%   openfemm()           -- launch GUI on default port (48015)
%   openfemm(0)          -- same as openfemm()
%   openfemm(1)          -- launch headless on default port
%   openfemm(0, 48016)   -- launch GUI on port 48016
%   openfemm(1, 48016)   -- launch headless on port 48016
%
% For multi-instance, use femmselect(port) to switch between instances.

function openfemm(varargin)
    global femmSockets femmActivePort

    if isempty(femmSockets)
        femmSockets = struct();
    end

    % openfemm uses Java sockets and ProcessBuilder to talk to femm-qt.
    % Both MATLAB and Octave can do this via Java interop, but Java must
    % be available. If not, give the user runtime-specific guidance.
    isOctave = (exist('OCTAVE_VERSION', 'builtin') ~= 0);
    if usejava('jvm') == 0
        if isOctave
            error(sprintf(['openfemm requires Java support in Octave.\n' ...
                'Install a JRE or JDK (e.g. https://adoptium.net) and set\n' ...
                'the JAVA_HOME environment variable to point at it. You\n' ...
                'may also need to install and load the Octave java package:\n' ...
                '  pkg install -forge java\n' ...
                '  pkg load java']));
        else
            error('openfemm requires Java. Restart MATLAB without the -nojvm option.');
        end
    end

    host = '127.0.0.1';

    headless = 0;
    port = 48015;
    if nargin >= 1
        headless = varargin{1};
    end
    if nargin >= 2
        port = varargin{2};
    end

    if headless
        % On Windows spawn the windowless build (femm_headlessw) so no console
        % window flashes; the console femm_headless is for terminal/batch use.
        % Other platforms have only femm_headless.
        if ispc
            exename = 'femm_headlessw';
        else
            exename = 'femm_headless';
        end
    else
        exename = 'femm_gui';
    end

    portkey = ['p' num2str(port)];

    % If we already have an active connection on this port, just reuse it
    if ~isempty(femmSockets) && isfield(femmSockets, portkey)
        femmActivePort = port;
        return
    end

    % Try to connect to an already-running instance. KEEP the socket on
    % success — DO NOT probe-and-close, because femm-qt's IPC server now
    % terminates the process on client disconnect (commit 0d1ddd2 in
    % tcp_server.cpp: closeClient -> exit(0)). The previous probe-and-close
    % pattern would kill the existing femm-qt instead of detecting it.
    %
    % javaObject(...) is used for instantiation instead of MATLAB's
    % java.X.Y(...) dot syntax because Octave's java package only
    % supports the function form. javaObject(...) works in both runtimes,
    % so the same code path serves both. Method calls (jsock.connect,
    % jsock.setTcpNoDelay, etc.) use identical syntax in both, no change
    % needed there.
    jsock = [];
    try
        jsock = javaObject('java.net.Socket');
        jsock.connect(javaObject('java.net.InetSocketAddress', host, port), 100);
    catch
        jsock = [];
    end

    if isempty(jsock)
        % Search FEMM_DIR, then locations relative to this script. In each
        % directory, try every platform's executable form; the first one that
        % exists wins, so we don't need to branch on the OS. The third form is
        % the macOS application bundle: the GUI builds as <exename>.app, a
        % directory whose actual executable is at Contents/MacOS/<exename>.
        % That path only exists on macOS, so it is harmlessly skipped elsewhere.
        searchdirs = {};
        femmdir = getenv('FEMM_DIR');
        if ~isempty(femmdir)
            searchdirs{end+1} = femmdir;
        end
        thisdir = fileparts(mfilename('fullpath'));
        searchdirs{end+1} = fullfile(thisdir, '..', 'build', 'bin');
        searchdirs{end+1} = thisdir;

        candidates = {};
        for i = 1:numel(searchdirs)
            d = searchdirs{i};
            candidates{end+1} = fullfile(d, [exename '.exe']);                       % Windows
            candidates{end+1} = fullfile(d, 'femm_gui.app', 'Contents', 'MacOS', exename);  % macOS: inside the bundle
            candidates{end+1} = fullfile(d, exename);                                % Linux / macOS CLI
            candidates{end+1} = fullfile(d, [exename '.app'], 'Contents', 'MacOS', exename);  % macOS .app
        end

        exe = '';
        for k = 1:length(candidates)
            if exist(candidates{k}, 'file')
                exe = candidates{k};
                break
            end
        end
        if isempty(exe)
            error(['Cannot find ' exename ' executable. ' ...
                   'Set the FEMM_DIR environment variable to the directory containing it.']);
        end

        if ispc
            % ProcessBuilder with redirectOutput to NUL — no shell, no
            % console window flash. Works in both MATLAB and Octave
            % (with a modern JDK; Octave 11.1.0 + MATLAB's Java 8 JRE
            % segfaults on pb.start(), but Adoptium Temurin 25+ works).
            pb = javaObject('java.lang.ProcessBuilder', ...
                {strrep(exe,'/','\'), '--port', num2str(port)});
            pb.directory(javaObject('java.io.File', fileparts(exe)));
            % Clear QT_PLUGIN_PATH so femm_gui uses its own Qt plugins,
            % not Octave's (which may be an incompatible Qt version).
            env = pb.environment();
            env.remove('QT_PLUGIN_PATH');
            nul = javaObject('java.io.File', 'NUL');
            pb.redirectOutput(nul);
            pb.redirectError(nul);
            pb.start();
        else
            % Quote the path: a macOS bundle's inner binary can sit under a
            % directory with spaces (e.g. /Users/me/My FEMM/...).
            system(['"' exe '" --port ' num2str(port) ' &']);
        end

        % Wait for TCP server. Try-and-keep, same reason as the
        % already-running check above. The first successful connect IS
        % our long-lived connection — don't close it and reopen.
        for attempt = 1:200
            pause(0.025);
            try
                jsock = javaObject('java.net.Socket');
                jsock.connect(javaObject('java.net.InetSocketAddress', host, port), 100);
                break
            catch
                jsock = [];
                if attempt == 200
                    error('femm-qt launched but TCP connection failed');
                end
            end
        end
    end

    % jsock is now the live connection (to either an existing instance
    % or the one we just launched). Wrap streams and store it.
    jsock.setTcpNoDelay(true);
    conn.socket = jsock;
    conn.out = jsock.getOutputStream();
    % Decode responses as UTF-8 (the wire encoding), not the JVM default
    % charset (Cp1252 on many Windows JVMs) — otherwise a non-ASCII filename
    % returned by e.g. mi_gettitle / getcurrentdirectory would be mangled.
    conn.reader = javaObject('java.io.BufferedReader', ...
        javaObject('java.io.InputStreamReader', jsock.getInputStream(), 'UTF-8'));

    % Store this connection and make it active
    femmSockets.(portkey) = conn;
    femmActivePort = port;

    callfemm(['setcurrentdirectory(' quote(strrep(pwd, '\', '/')) ')']);

end
