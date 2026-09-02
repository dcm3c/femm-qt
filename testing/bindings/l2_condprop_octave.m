% Layer 2, octavefemm: EOCondPropTest driven through the binding.
%
%   matlab     -batch "l2_condprop_octave"
%   octave-cli --no-gui -q l2_condprop_octave.m
%
% with FEMM_DIR set to <build>/bin. A line-for-line translation of
% testing/EOCondPropTest.py, so the existing CondPropCompare.py checks the
% result against the same Original FEMM golden.
%
% ONE file serves both hosts, which needs care: MATLAB requires a script's local
% functions to come LAST, Octave requires them to be defined BEFORE use. Those
% are mutually exclusive, so this file has no local functions at all -- helpers
% are anonymous functions and the per-case body is inlined in a loop. Both forms
% are portable; a local function is not.

here = fileparts(mfilename('fullpath'));
addpath(fullfile(here, '..', '..', 'octavefemm'));
tests = fullfile(here, '..');
tmp = getenv('TEMP'); if isempty(tmp), tmp = 'C:/temp'; end

fmt = @(z) sprintf('%.10g %.10g', real(z), imag(z));
fwd = @(p) strrep(p, '\', '/');

cases = {{'electrostatic1.fee', 'electrostatic1 (axi)'}, ...
         {'electrostatic2.fee', 'electrostatic2 (planar)'}};
cond = '+10000V';

openfemm(1);                              % 1 = headless
for k = 1:numel(cases)
    fee = cases{k}{1};
    tag = cases{k}{2};
    opendocument(fwd(fullfile(tests, fee)));
    ei_saveas(fwd(fullfile(tmp, 'l2_oct_temp.fee')));
    ei_analyze();
    ei_loadsolution();
    fprintf('===  %s  ===\n', tag);
    z = eo_getconductorproperties(cond);
    fprintf('  %-14s = %s | %s\n', cond, fmt(z(1)), fmt(z(2)));
    eo_close();
end
fprintf('\nDone.\n');
closefemm();
