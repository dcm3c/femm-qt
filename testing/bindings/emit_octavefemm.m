% emit_octavefemm.m -- record what every octavefemm wrapper emits, driven by
% contract.tsv. Same recorder stub as the probe; the difference is that this
% pass uses the three sentinel sets every binding shares, so the results are
% comparable across languages.
%
%   matlab -batch "emit_octavefemm"       (or run under Octave)
%
% Writes emitted_octavefemm.tsv:  wrapper <TAB> pass <TAB> emission

global EMITTED

here = fileparts(mfilename('fullpath'));
addpath(fullfile(here, '..', '..', 'octavefemm'));
addpath(fullfile(here, 'stub_octavefemm'));      % AFTER, so the stub wins

fid = fopen(fullfile(here, 'contract.tsv'), 'r');
out = fopen(fullfile(here, 'emitted_octavefemm.tsv'), 'w');
nok = 0; nmiss = 0; nerr = 0;
passes = {'int', 'real', 'cplx'};

while true
    line = fgetl(fid);
    if ~ischar(line), break, end
    if isempty(line) || line(1) == '#', continue, end
    parts = strsplit(line, sprintf('\t'));
    name = parts{1};
    kinds = '';
    if numel(parts) >= 3, kinds = parts{3}; end
    if exist(name, 'file') ~= 2
        fprintf(out, '%s\tint\tMISSING\n', name);
        nmiss = nmiss + 1;
        continue
    end
    gotany = false;
    for p = 1:numel(passes)
        which = passes{p};
        args = cell(1, numel(kinds));
        for i = 1:numel(kinds)
            if kinds(i) == 's'
                args{i} = sprintf('s%d', i);
            elseif strcmp(which, 'int')
                args{i} = i;
            elseif strcmp(which, 'real')
                args{i} = -1.5 * i + 0.25;
            else
                args{i} = complex(i, -0.25 * i);
            end
        end
        EMITTED = {};
        try
            feval(name, args{:});
        catch err
            if isempty(EMITTED)
                msg = strrep(err.message, sprintf('\t'), ' ');
                nl = find(msg == sprintf('\n'), 1);
                if ~isempty(nl), msg = msg(1:nl-1); end
                fprintf(out, '%s\t%s\tERROR:%s\n', name, which, msg);
                nerr = nerr + 1;
                continue
            end
        end
        for j = 1:numel(EMITTED)
            fprintf(out, '%s\t%s\t%s\n', name, which, ...
                    strrep(EMITTED{j}, sprintf('\t'), ' '));
            gotany = true;
        end
    end
    if gotany, nok = nok + 1; end
end
fclose(fid); fclose(out);
fprintf('octavefemm: %d wrappers recorded, %d missing, %d refused the contract\n', ...
        nok, nmiss, nerr);
