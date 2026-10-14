# SPDX-License-Identifier: MIT
"""inventory.py -- every digitized AK Steel curve, with the Js that belongs to it.

Js is a COMPOSITION property, not a heat-treatment one, so all Pure Iron
anneals share 2.15 T (stated in the bulletin) and all Telar bars share
2.142 T (v10 dilution anchor at C 0.02 / Mn 0.425 -- the bulletin's 2.15 T is
quoted jointly for both products and is a family figure, while Telar's own
tabulated B at 24 kA/m runs 0.03-0.05 T below Pure Iron's).

Names are deliberately verbose: this set exists to be READ in the FEMM
material dialog, where the only thing distinguishing two entries is the name.
"""

JS_PURE_FE = 2.150
JS_TELAR = 2.142

# (block name, raw csv, Js)
RAW = []

for dia in ("4mm", "8mm", "10mm", "12mm"):
    RAW.append(("Telar57 %s annealed 815C (meas)" % dia,
                "../iron/out/telar_%s_annealed_raw.csv" % dia, JS_TELAR))
    RAW.append(("Telar57 %s as received (meas)" % dia,
                "../iron/out/telar_%s_asreceived_raw.csv" % dia, JS_TELAR))

for sheet, lab in (("cr2mm", "2mm cold rolled"), ("hr3mm", "3mm hot rolled")):
    for tag, nm in (("assheared", "as sheared"), ("700C", "700C"),
                    ("820C", "820C"), ("950C", "950C"), ("1150C", "1150C")):
        RAW.append(("PureIron %s %s (meas)" % (lab, nm),
                    "../iron/out/pi_%s_%s_raw.csv" % (sheet, tag), JS_PURE_FE))

RAW.append(("PureIron Fig7 annealed (meas)",
            "../iron/out/pi_fig7_annealed_raw.csv", JS_PURE_FE))

# High-induction intrinsic curves (dig_highfield.py): Telar Fig 5 and
# Pure Iron Fig 8. These carry the measurement through the anisotropy
# field and are spliced into the curated entries by emit_measured.py.
RAW.append(("Telar57 Fig5 high induction (meas)",
            "../iron/out/telar_fig5_high_raw.csv", JS_TELAR))
RAW.append(("PureIron Fig8 high induction (meas)",
            "../iron/out/pi_fig8_high_raw.csv", JS_PURE_FE))

# The two curated entries that also go in the .dat library. Built from the
# emitted tables rather than re-derived, so the .fem and the .dat cannot
# disagree about what ships.
CURATED = [
    ("ARMCO Pure Iron (AK Steel data)", "../iron/femm_ArmcoPureIron.csv"),
    ("ARMCO Telar 57 (AK Steel data, 3-bar mean)", "../iron/femm_Telar57.csv"),
]
