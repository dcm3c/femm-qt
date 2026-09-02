% Layer 2 phasecurrent, octavefemm: sends COMPLEX arguments.
%
%   matlab     -batch "l2_phase_octave"
%   octave-cli --no-gui -q l2_phase_octave.m
%
% This is the direct regression test for the bug that started the binding work.
% num.m formatted a complex argument with num2str, producing MATLAB's own 1+2i,
% which the interpreter rejects with "invalid syntax for integer with base 10".
% Every current below is complex, so a regression fails here immediately rather
% than five months later.
%
% No local functions: MATLAB wants them last in a script, Octave wants them
% before use. An anonymous function satisfies both.

here = fileparts(mfilename('fullpath'));
addpath(fullfile(here, '..', '..', 'octavefemm'));
ex = fullfile(here, '..', '..', 'examples');
tmp = getenv('TEMP'); if isempty(tmp), tmp = 'C:/temp'; end

fmt = @(z) sprintf('%.10g %.10g', real(z), imag(z));
fwd = @(p) strrep(p, '\', '/');

names = {'A', 'B', 'C'};
curs = {complex(1000, 0), complex(-500, -866.0254038), complex(-500, 866.0254038)};

openfemm(1);
opendocument(fwd(fullfile(ex, 'three-phase-line.FEM')));
mi_saveas(fwd(fullfile(tmp, 'l2_phase_oct.fem')));
mi_probdef(60, 'inches', 'planar');
for k = 1:3
    mi_modifycircprop(names{k}, 1, curs{k});
end
mi_analyze();
mi_loadsolution();
fprintf('===  three-phase-line balanced 1000A 60Hz  ===\n');
for k = 1:3
    z = mo_getcircuitproperties(names{k});
    fprintf('  %-14s = %s | %s | %s\n', names{k}, fmt(z(1)), fmt(z(2)), fmt(z(3)));
end
mo_close();
fprintf('\nDone.\n');
closefemm();
