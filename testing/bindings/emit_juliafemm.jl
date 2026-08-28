# emit_juliafemm.jl -- record what every juliafemm wrapper emits, driven by
# contract.tsv. No engine is started: the module's transport is replaced by a
# recorder via Core.eval, so nothing opens a socket.
#
#     julia emit_juliafemm.jl
#
# Writes emitted_juliafemm.tsv:  wrapper <TAB> pass <TAB> emission

const HERE = dirname(abspath(@__FILE__))
const EMIT = String[]

include(joinpath(HERE, "..", "..", "juliafemm", "juliafemm.jl"))
using .JuliaFEMM

# Replace the transport inside the module. Both entry points record and return
# an empty result; wrappers that post-process a reply will throw afterwards,
# which is caught below -- the emission has already happened.
Core.eval(JuliaFEMM, :(callfemm(cmd::AbstractString) =
    (push!(Main.EMIT, cmd); Any[])))
Core.eval(JuliaFEMM, :(callfemm_noeval(cmd::AbstractString) =
    (push!(Main.EMIT, cmd); nothing)))

function sentinels(kinds::AbstractString, which::AbstractString)
    args = Any[]
    for (i, k) in enumerate(kinds)
        if k == 's'
            push!(args, "s$i")
        elseif which == "int"
            push!(args, i)
        elseif which == "real"
            push!(args, -1.5 * i + 0.25)
        else
            push!(args, complex(float(i), -0.25 * i))
        end
    end
    args
end

function main()
    contract = joinpath(HERE, "contract.tsv")
    isfile(contract) || (println("no contract.tsv"); return 2)
    out = open(joinpath(HERE, "emitted_juliafemm.tsv"), "w")
    nok = nmiss = nerr = 0
    for line in eachline(contract)
        (isempty(line) || startswith(line, "#")) && continue
        parts = split(line, '\t')
        name = String(parts[1])
        kinds = length(parts) >= 3 ? String(parts[3]) : ""
        sym = Symbol(name)
        if !isdefined(JuliaFEMM, sym)
            println(out, "$name\tint\tMISSING")
            nmiss += 1
            continue
        end
        f = getfield(JuliaFEMM, sym)
        gotany = false
        for which in ("int", "real", "cplx")
            empty!(EMIT)
            try
                Base.invokelatest(f, sentinels(kinds, which)...)
            catch err
                if isempty(EMIT)
                    msg = replace(first(split(sprint(showerror, err), '\n')),
                                  '\t' => ' ')
                    println(out, "$name\t$which\tERROR:$msg")
                    nerr += 1
                    continue
                end
            end
            for e in EMIT
                println(out, "$name\t$which\t", replace(e, '\t' => ' '))
                gotany = true
            end
        end
        gotany && (nok += 1)
    end
    close(out)
    println("juliafemm: $nok wrappers recorded, $nmiss missing, ",
            "$nerr refused the contract")
    return 0
end

exit(main())
