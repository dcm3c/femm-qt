# Layer 2 phasecurrent, juliafemm: sends COMPLEX arguments.
# See l2_phase_reference.py for why this scenario exists.

using Printf
const HERE = dirname(abspath(@__FILE__))
include(joinpath(HERE, "..", "..", "juliafemm", "juliafemm.jl"))
using .JuliaFEMM

const EX = abspath(joinpath(HERE, "..", "..", "examples"))
# tempdir() is the platform's own temp directory: honours TMPDIR on POSIX and TEMP on Windows, so no per-platform branch is needed.
const TMP = tempdir()
const PHASES = (("A", complex(1000.0, 0.0)),
                ("B", complex(-500.0, -866.0254038)),
                ("C", complex(-500.0, 866.0254038)))

fmt(z) = @sprintf("%.10g %.10g", real(z), imag(z))

openfemm(headless=true)
try
    opendocument(replace(joinpath(EX, "three-phase-line.FEM"), "\\" => "/"))
    mi_saveas(replace(joinpath(TMP, "l2_phase_jl.fem"), "\\" => "/"))
    mi_probdef(60, "inches", "planar")
    for (name, cur) in PHASES
        mi_modifycircprop(name, 1, cur)
    end
    mi_analyze(); mi_loadsolution()
    println("===  three-phase-line balanced 1000A 60Hz  ===")
    for (name, _) in PHASES
        z = mo_getcircuitproperties(name)
        @printf("  %-14s = %s | %s | %s
", name, fmt(z[1]), fmt(z[2]), fmt(z[3]))
    end
    mo_close()
    println("")
    println("Done.")
finally
    closefemm()
end
