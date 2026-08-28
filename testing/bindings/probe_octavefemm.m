% probe_octavefemm.m -- derive the command contract by EXECUTING octavefemm.
%
% Every wrapper is called with integer sentinels while `callfemm` is shadowed by
% a recorder, so what comes out is the command each wrapper actually builds:
% its name, its arity, and -- from which positions came back quoted -- which
% arguments are strings. Nothing is parsed, so the contract cannot drift from
% the code the way a hand-written table would.
%
% octavefemm is the source used because it is the wire-token authority: it is
% the binding the other four were derived from, and the one whose .m files are
% closest to Original FEMM's.
%
%   matlab -batch "probe_octavefemm"      (or run under Octave)
%
% Writes probe_octavefemm.tsv:  name <TAB> arity <TAB> emitted-command

global EMITTED

here = fileparts(mfilename('fullpath'));
oct  = fullfile(here, '..', '..', 'octavefemm');
addpath(oct);
addpath(fullfile(here, 'stub_octavefemm'));   % AFTER oct, so the stub wins

% Transport and connection management are not wrappers; skip them. num/numc/
% quote/quotec/complex2str are the formatters under test, exercised through
% every other wrapper rather than directly.
INFRA = {'callfemm','callfemm_noeval','openfemm','closefemm','femmselect', ...
         'quote','quotec','rawquote','num','numc','complex2str'};

files = dir(fullfile(oct, '*.m'));
fid = fopen(fullfile(here, 'probe_octavefemm.tsv'), 'w');
nok = 0; nno = 0; nskip = 0;
for k = 1:numel(files)
    name = files(k).name(1:end-2);
    if any(strcmp(name, INFRA)), nskip = nskip + 1; continue, end
    try
        na = nargin(name);
    catch
        nskip = nskip + 1; continue
    end
    if na < 0, na = -na - 1; end        % varargin: probe the fixed args only
    args = num2cell(1:na);
    EMITTED = {};
    try
        feval(name, args{:});
    catch
        % Wrappers that post-process the reply will fail on the stub's empty
        % return. The emission happened first, which is all we are collecting.
    end
    if isempty(EMITTED)
        nno = nno + 1;
        fprintf(fid, '%s\t%d\tNOEMIT\n', name, na);
    else
        nok = nok + 1;
        for j = 1:numel(EMITTED)
            fprintf(fid, '%s\t%d\t%s\n', name, na, EMITTED{j});
        end
    end
end
fclose(fid);
fprintf('probe: %d wrappers emitted, %d silent, %d infrastructure skipped\n', ...
        nok, nno, nskip);
