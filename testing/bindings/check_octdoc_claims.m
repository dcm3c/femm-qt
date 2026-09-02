% Check the claims the OctaveFEMM manual's introduction makes, by running them.
%
%   matlab -batch "check_octdoc_claims"
%
% The manual predates femm-qt and has never been re-verified against it. Each
% claim below is quoted from octavefemm/manual/octavefemm.tex.
addpath(fullfile(fileparts(mfilename('fullpath')), '..', '..', 'octavefemm'));

fails = {};
function_handle_unused = 1;

openfemm(1);            % 1 = headless, so no window appears during the check
newdocument(0);         % magnetics

% ---------------------------------------------------------------------------
% CLAIM: "Functions in Lua that have no arguments require a set of empty
% parentheses ... In Octave or Matlab, no parentheses should be used"
% ---------------------------------------------------------------------------
ok = true;
try
    mi_clearselected;           % no parentheses
catch err
    ok = false;
end
fprintf('  %-58s %s\n', 'no-parens call form', char(84*ok + 70*(1-ok)));
if ~ok, fails{end+1} = 'no-parens form'; end

% ---------------------------------------------------------------------------
% CLAIM: a point may be given "as a single two-element array", and to repeat an
% operation over many points you pass PARALLEL VECTORS -- one of x and one of
% y -- because a single n-by-2 array is read as one point, Matlab storing an
% array column-first.
%
% The manual used to claim the opposite: that an array of xy pairs performed
% the operation on every element. It does not, and this check is what
% established that. So the check now pins the corrected behaviour in both
% directions -- the vector form must add three nodes AND the pair-array form
% must add one, since a change that "fixed" the latter would silently
% invalidate the paragraph.
% ---------------------------------------------------------------------------
tmp = [tempname '.fem'];
mi_addnode([0 0]);                       % point as a 2-element array   -> 1
mi_addnode([1 1 0],[0 1 1]);             % parallel vectors             -> 3
mi_addnode([5 5; 6 5; 6 6]);             % one array of pairs           -> 1
mi_saveas(tmp);
txt = fileread(tmp);
tok = regexp(txt, '\[NumPoints\]\s*=\s*(\d+)', 'tokens', 'once');
n = str2double(tok{1});
fprintf('  %-58s %s   (NumPoints = %d, want 5)\n', ...
        'point-as-array, parallel vectors, pair-array', ...
        char(84*(n==5) + 70*(n~=5)), n);
if n ~= 5, fails{end+1} = sprintf('array forms gave %d nodes, want 5', n); end

% ---------------------------------------------------------------------------
% CLAIM: "All strings are enclosed in single quotes, rather than double quotes"
% Still sound advice? A double-quoted MATLAB literal is a string object, not a
% char array, so check whether it survives the wire.
% ---------------------------------------------------------------------------
dq = 'untested';
try
    mi_addmaterial("DblQuoted", 1, 1);
    dq = 'accepted';
catch err
    dq = 'rejected';
end
fprintf('  %-58s %s\n', 'double-quoted string argument', dq);

% ---------------------------------------------------------------------------
% CLAIM: openfemm "starts up a FEMM process" -- and the manual does not
% mention that it can now be started headless, which is what this script did.
% ---------------------------------------------------------------------------
fprintf('  %-58s %s\n', 'openfemm(1) headless start (now documented)', 'T');

closefemm;
delete(tmp);

if isempty(fails)
    fprintf('octavefemm intro claims: behavioural ones all still hold\n');
else
    fprintf('octavefemm intro claims: %d FAILED\n', numel(fails));
    for k = 1:numel(fails)
        fprintf('   %s\n', fails{k});
    end
end
