% Layer 2, octavefemm: EOCondPropTest driven through the binding.
%
%   matlab -batch "l2_condprop_octave"   (with FEMM_DIR set to <build>/bin)
%
% A line-for-line translation of testing/EOCondPropTest.py, so the existing
% CondPropCompare.py checks the result against the same Original FEMM golden.

here = fileparts(mfilename('fullpath'));
addpath(fullfile(here, '..', '..', 'octavefemm'));
tests = fullfile(here, '..');
tmp = getenv('TEMP'); if isempty(tmp), tmp = 'C:/temp'; end

openfemm(1);                              % 1 = headless
c = onCleanup(@() closefemm());

runcase(fullfile(tests, 'electrostatic1.fee'), tmp, '+10000V', 'electrostatic1 (axi)');
runcase(fullfile(tests, 'electrostatic2.fee'), tmp, '+10000V', 'electrostatic2 (planar)');
fprintf('\nDone.\n');

function s = fmt(re, im)
    s = sprintf('%.10g %.10g', re, im);
end

function runcase(fee, tmp, cond, tag)
    opendocument(strrep(fee, '\', '/'));
    ei_saveas(strrep(fullfile(tmp, 'l2_oct_temp.fee'), '\', '/'));
    ei_analyze();
    ei_loadsolution();
    fprintf('===  %s  ===\n', tag);
    z = eo_getconductorproperties(cond);
    fprintf('  %-14s = %s | %s\n', cond, ...
            fmt(real(z(1)), imag(z(1))), fmt(real(z(2)), imag(z(2))));
    eo_close();
end
