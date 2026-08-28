// Layer 2 phasecurrent, scifemm: sends COMPLEX arguments.
// See l2_phase_reference.py for why this scenario exists.

HERE = get_absolute_file_path('l2_phase_scifemm.sce');
exec(fullfile(HERE, '..', '..', 'scifemm', 'scifemm.sci'), -1);
EX = fullfile(HERE, '..', '..', 'examples');
TMP = getenv('TEMP', 'C:/temp');
BS = ascii(92);

function s = fmt(z)
    s = msprintf('%.10g %.10g', real(z), imag(z));
endfunction

names = ['A', 'B', 'C'];
curs = [complex(1000, 0), complex(-500, -866.0254038), complex(-500, 866.0254038)];

openfemm(1);
opendocument(strsubst(fullfile(EX, 'three-phase-line.FEM'), BS, '/'));
mi_saveas(strsubst(fullfile(TMP, 'l2_phase_sci.fem'), BS, '/'));
mi_probdef(60, 'inches', 'planar');
for k = 1:3
    mi_modifycircprop(names(k), 1, curs(k));
end
mi_analyze();
mi_loadsolution();
mprintf('===  three-phase-line balanced 1000A 60Hz  ===\n');
for k = 1:3
    z = mo_getcircuitproperties(names(k));
    mprintf('  %-14s = %s | %s | %s\n', names(k), fmt(z(1)), fmt(z(2)), fmt(z(3)));
end
mo_close();
mprintf('\nDone.\n');
closefemm();
quit();
