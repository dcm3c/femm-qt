// Layer 2, scifemm: EOCondPropTest driven through the binding.
//
//   WScilex-cli -quit -f l2_condprop_scifemm.sce   (with FEMM_DIR set)
//
// A line-for-line translation of testing/EOCondPropTest.py, so the existing
// CondPropCompare.py checks it against the same Original FEMM golden.

HERE = get_absolute_file_path('l2_condprop_scifemm.sce');
exec(fullfile(HERE, '..', '..', 'scifemm', 'scifemm.sci'), -1);
TESTS = fullfile(HERE, '..');
TMP = getenv('TEMP', 'C:/temp');
BS = ascii(92);        // a backslash, without fighting the shell over escapes

function runcase(fee, tmp, cond, tag)
    BS = ascii(92);
    opendocument(strsubst(fee, BS, '/'));
    ei_saveas(strsubst(fullfile(tmp, 'l2_sci_temp.fee'), BS, '/'));
    ei_analyze();
    ei_loadsolution();
    mprintf('===  %s  ===\n', tag);
    z = eo_getconductorproperties(cond);
    mprintf('  %-14s = %.10g %.10g | %.10g %.10g\n', cond, ..
            real(z(1)), imag(z(1)), real(z(2)), imag(z(2)));
    eo_close();
endfunction

openfemm(1);
runcase(fullfile(TESTS, 'electrostatic1.fee'), TMP, '+10000V', 'electrostatic1 (axi)');
runcase(fullfile(TESTS, 'electrostatic2.fee'), TMP, '+10000V', 'electrostatic2 (planar)');
mprintf('\nDone.\n');
closefemm();
quit();
