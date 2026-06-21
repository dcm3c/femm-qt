// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_bind.h - pybind11-style facade over the MicroPython C API.
//
// This is the single header that hides the MicroPython C API. After this
// layer, every binding file uses m.def(name, [...]) to register functions.
//
// Pinned to MicroPython v1.28.0 (see micropython/MICROPYTHON_VERSION).
// The exact
// C API spellings used here (mp_obj_new_complex / mp_obj_get_complex,
// MP_DEFINE_CONST_OBJ_TYPE slot form, qstr_from_strn) were verified against
// that release. The const mp_obj_type_t for our callable is defined in
// py_bind_type.c (compiled as C, where the macro's designated initializers
// are standard) to keep this C++ glue portable across -std=c++17 toolchains.
#ifndef PY_BIND_H
#define PY_BIND_H

extern "C" {
#include "py/runtime.h"
#include "py/obj.h"
#include "py/qstr.h"
#include "py/gc.h"
}

#include "complex.h"

#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

// Maximum positional arity of any bound function. femm's widest signature
// (mi_addmaterial) is ~14 args; 24 leaves head-room. Exceeding it is a
// compile-time error in Module::def (never a silent truncation).
#define FEMM_MAX_ARGS 24

// The callable object's behaviour is split so the type table can live in C:
//   - femm_fn_call : implemented in py_bind.cpp (needs C++/std::function)
//   - femm_fn_type : defined in py_bind_type.c via MP_DEFINE_CONST_OBJ_TYPE
extern "C" {
mp_obj_t femm_fn_call(mp_obj_t self_in, size_t n_args, size_t n_kw, const mp_obj_t *args);
extern const mp_obj_type_t femm_fn_type;
}

namespace femm_py {

// ---------------------------------------------------------------------------
// Traits
// ---------------------------------------------------------------------------
template<class T> struct is_optional : std::false_type {};
template<class U> struct is_optional<std::optional<U>> : std::true_type {};

// Number of leading *required* args = total minus the run of trailing optionals.
// (In the femm API all optional parameters are trailing.)
template<class... X>
constexpr size_t min_required() {
    constexpr size_t N = sizeof...(X);
    if constexpr (N == 0) {
        return 0;
    } else {
        const bool opt[N] = { is_optional<X>::value... };
        size_t m = N;
        while (m > 0 && opt[m - 1]) --m;
        return m;
    }
}

// Convert a numeric object to a real double. A complex whose imaginary part is
// negligible -- roundoff relative to the magnitude -- is accepted and reduced to
// its real part, so a value that is mathematically real but happens to be typed
// complex (it fell out of complex arithmetic) still flows into the double/int/
// float-typed bindings; this mirrors original FEMM's all-complex number model.
// A complex with a genuinely non-negligible imaginary part RAISES rather than
// silently dropping it -- a complex coordinate is almost always a mistake, and a
// loud error beats a node placed at the wrong location. The test is relative
// (so it doesn't choke on roundoff at any scale) and division-free (so 0+0j is
// accepted); 1e-12 matches CComplex::Chop()'s default sense of "numerically 0".
static inline double femm_as_real(mp_obj_t o) {
    if (mp_obj_is_type(o, &mp_type_complex)) {
        mp_float_t re, im;
        mp_obj_complex_get(o, &re, &im);
        mp_float_t are = re < 0 ? -re : re;
        mp_float_t aim = im < 0 ? -im : im;
        if (aim > (mp_float_t)1e-12 * (are + aim))
            mp_raise_msg(&mp_type_TypeError,
                MP_ERROR_TEXT("can't use a complex value with a non-negligible "
                              "imaginary part where a real number is required"));
        return (double)re;
    }
    return (double)mp_obj_get_float(o);
}

// ---------------------------------------------------------------------------
// Type casters: static mp_obj_t cast(T) and static T load(mp_obj_t)
// ---------------------------------------------------------------------------
template<typename T> struct caster;

template<> struct caster<double> {
    static mp_obj_t cast(double v)   { return mp_obj_new_float((mp_float_t)v); }
    static double   load(mp_obj_t o) { return femm_as_real(o); }
};

template<> struct caster<float> {
    static mp_obj_t cast(float v)    { return mp_obj_new_float((mp_float_t)v); }
    static float    load(mp_obj_t o) { return (float)femm_as_real(o); }
};

// Integer casters accept float values with integer magnitude (e.g. 1.0 -> 1) because the
// wire clients were written against that contract, so e.g. a client sending
// `1.0` for an int argument must still work.  Non-integer floats (3.5) are
// rejected with TypeError rather than silently truncated.
template<> struct caster<int> {
    static mp_obj_t cast(int v)      { return mp_obj_new_int(v); }
    static int      load(mp_obj_t o) {
        if (mp_obj_is_float(o) || mp_obj_is_type(o, &mp_type_complex)) {
            double f = femm_as_real(o);
            int i = (int)f;
            if (f != (double)i)
                mp_raise_msg(&mp_type_TypeError,
                    MP_ERROR_TEXT("can't convert non-integer float to int"));
            return i;
        }
        return (int)mp_obj_get_int(o);
    }
};

template<> struct caster<long> {
    static mp_obj_t cast(long v)     { return mp_obj_new_int((mp_int_t)v); }
    static long     load(mp_obj_t o) {
        if (mp_obj_is_float(o) || mp_obj_is_type(o, &mp_type_complex)) {
            double f = femm_as_real(o);
            long i = (long)f;
            if (f != (double)i)
                mp_raise_msg(&mp_type_TypeError,
                    MP_ERROR_TEXT("can't convert non-integer float to int"));
            return i;
        }
        return (long)mp_obj_get_int(o);
    }
};

template<> struct caster<bool> {
    static mp_obj_t cast(bool v)     { return mp_obj_new_bool(v); }
    static bool     load(mp_obj_t o) { return mp_obj_is_true(o); }
};

// Identity passthrough: a binding can declare an argument as mp_obj_t to accept
// a value whose type is decided at runtime (e.g. mi_setblockprop's magdir,
// which is either a number or a direction-function string, or the modify*
// "value" argument whose type depends on the property index).
template<> struct caster<mp_obj_t> {
    static mp_obj_t cast(mp_obj_t v) { return v; }
    static mp_obj_t load(mp_obj_t o) { return o; }
};

template<> struct caster<std::string> {
    static mp_obj_t cast(const std::string &v) {
        return mp_obj_new_str(v.data(), v.size());
    }
    static std::string load(mp_obj_t o) {
        size_t n = 0;
        const char *s = mp_obj_str_get_data(o, &n);
        return std::string(s, n);
    }
};

// CComplex <-> Python complex. load() also promotes int/float to (re, 0),
// matching the old toComplex(sol::object) helper. mp_obj_get_complex already
// accepts int, float and complex, so no manual type dispatch is needed.
template<> struct caster<CComplex> {
    static mp_obj_t cast(const CComplex &v) {
        return mp_obj_new_complex((mp_float_t)v.re, (mp_float_t)v.im);
    }
    static CComplex load(mp_obj_t o) {
        mp_float_t re = 0, im = 0;
        mp_obj_get_complex(o, &re, &im);
        return CComplex((double)re, (double)im);
    }
};

// std::optional<T> : None <-> nullopt, else delegate to caster<T>.
template<typename T> struct caster<std::optional<T>> {
    static mp_obj_t cast(const std::optional<T> &v) {
        return v ? caster<T>::cast(*v) : mp_const_none;
    }
    static std::optional<T> load(mp_obj_t o) {
        if (o == mp_const_none) return std::nullopt;
        return std::optional<T>(caster<T>::load(o));
    }
};

// std::tuple<Ts...> -> Python tuple (the replacement for sol::variadic_results).
template<typename... Ts> struct caster<std::tuple<Ts...>> {
    template<size_t... I>
    static mp_obj_t cast_(const std::tuple<Ts...> &t, std::index_sequence<I...>) {
        mp_obj_t items[sizeof...(Ts) ? sizeof...(Ts) : 1] = {
            caster<std::decay_t<Ts>>::cast(std::get<I>(t))...
        };
        return mp_obj_new_tuple(sizeof...(Ts), items);
    }
    static mp_obj_t cast(const std::tuple<Ts...> &t) {
        return cast_(t, std::index_sequence_for<Ts...>{});
    }
};

// std::vector<T> <-> Python list.
template<typename T> struct caster<std::vector<T>> {
    static mp_obj_t cast(const std::vector<T> &v) {
        std::vector<mp_obj_t> items;
        items.reserve(v.size());
        for (const auto &e : v)
            items.push_back(caster<T>::cast(e));
        return mp_obj_new_list(items.size(), items.data());
    }
    static std::vector<T> load(mp_obj_t o) {
        size_t n = 0;
        mp_obj_t *items = nullptr;
        mp_obj_get_array(o, &n, &items);
        std::vector<T> out;
        out.reserve(n);
        for (size_t i = 0; i < n; i++)
            out.push_back(caster<T>::load(items[i]));
        return out;
    }
};

// ---------------------------------------------------------------------------
// Function traits: pull return type and (decayed) argument tuple from any
// callable - capturing lambda, function pointer, or std::function.
// ---------------------------------------------------------------------------
template<typename F> struct fn_traits : fn_traits<decltype(&std::decay_t<F>::operator())> {};

template<typename R, typename C, typename... A>
struct fn_traits<R (C::*)(A...) const> {
    using ret  = R;
    using args = std::tuple<std::decay_t<A>...>;
    static constexpr size_t n     = sizeof...(A);
    static constexpr size_t min_n = min_required<std::decay_t<A>...>();
};

template<typename R, typename C, typename... A>
struct fn_traits<R (C::*)(A...)> {
    using ret  = R;
    using args = std::tuple<std::decay_t<A>...>;
    static constexpr size_t n     = sizeof...(A);
    static constexpr size_t min_n = min_required<std::decay_t<A>...>();
};

template<typename R, typename... A>
struct fn_traits<R (*)(A...)> {
    using ret  = R;
    using args = std::tuple<std::decay_t<A>...>;
    static constexpr size_t n     = sizeof...(A);
    static constexpr size_t min_n = min_required<std::decay_t<A>...>();
};

// ---------------------------------------------------------------------------
// Dispatcher: load each arg via its caster, call f, cast the result back.
// `a` always has fn_traits<F>::n entries (missing optionals are mp_const_none,
// padded by femm_fn_call).
// ---------------------------------------------------------------------------
template<typename F, size_t... I>
mp_obj_t invoke_(F &f, const mp_obj_t *a, std::index_sequence<I...>) {
    using Traits = fn_traits<std::decay_t<F>>;
    using Args   = typename Traits::args;
    using Ret    = typename Traits::ret;
    if constexpr (std::is_void_v<Ret>) {
        (void)a;
        f(caster<std::tuple_element_t<I, Args>>::load(a[I])...);
        return mp_const_none;
    } else {
        return caster<std::decay_t<Ret>>::cast(
            f(caster<std::tuple_element_t<I, Args>>::load(a[I])...));
    }
}

// ---------------------------------------------------------------------------
// Callable MicroPython object owning a std::function (so capturing lambdas
// work). The type table (femm_fn_type) and the call entry point
// (femm_fn_call) are declared at file scope above.
// ---------------------------------------------------------------------------
struct FemmFn {
    mp_obj_base_t base;
    std::function<mp_obj_t(const mp_obj_t *)> *impl;  // receives max_n padded args
    // Varargs alternative (def_raw): receives (n_args, args) unpadded. Exactly
    // one of impl / raw_impl is non-null. m_new_obj does not zero memory, so
    // both registration paths must set both fields explicitly.
    std::function<mp_obj_t(size_t, const mp_obj_t *)> *raw_impl;
    size_t min_n;
    size_t max_n;
    const char *name;   // for error messages
};

// ---------------------------------------------------------------------------
// Module: wraps a globals dict and exposes def().
// ---------------------------------------------------------------------------
struct Module {
    mp_obj_dict_t *globals = nullptr;  // set during init_py()

    template<typename F>
    void def(const char *name, F &&f) {
        using Traits = fn_traits<std::decay_t<F>>;
        static_assert(Traits::n <= FEMM_MAX_ARGS,
                      "bound function exceeds FEMM_MAX_ARGS; raise the cap");

        FemmFn *obj = m_new_obj(FemmFn);
        obj->base.type = &femm_fn_type;
        obj->raw_impl = nullptr;
        obj->min_n = Traits::min_n;
        obj->max_n = Traits::n;

        size_t len = std::strlen(name);
        char *nm = (char *)m_malloc(len + 1);
        std::memcpy(nm, name, len + 1);
        obj->name = nm;

        obj->impl = new std::function<mp_obj_t(const mp_obj_t *)>(
            [fn = std::forward<F>(f)](const mp_obj_t *a) mutable {
                return invoke_(fn, a, std::make_index_sequence<Traits::n>{});
            });

        qstr key = qstr_from_strn(name, len);
        mp_obj_dict_store(MP_OBJ_FROM_PTR(globals),
                          MP_OBJ_NEW_QSTR(key), MP_OBJ_FROM_PTR(obj));
    }

    template<typename F>
    void def(const std::string &name, F &&f) { def(name.c_str(), std::forward<F>(f)); }

    // Register a raw varargs function: it receives (n_args, args) unmodified,
    // with no arity padding. For signatures the typed def() cannot express -
    // e.g. mixed types whose position depends on which optional args are
    // present, like plot(x, "xlabel", y1, "lbl1", y2, ...). n_args is still
    // bounds-checked against [min_n, max_n]; the function validates structure.
    template<typename F>
    void def_raw(const char *name, size_t min_n, size_t max_n, F &&f) {
        FemmFn *obj = m_new_obj(FemmFn);
        obj->base.type = &femm_fn_type;
        obj->impl = nullptr;
        obj->min_n = min_n;
        obj->max_n = max_n;

        size_t len = std::strlen(name);
        char *nm = (char *)m_malloc(len + 1);
        std::memcpy(nm, name, len + 1);
        obj->name = nm;

        obj->raw_impl = new std::function<mp_obj_t(size_t, const mp_obj_t *)>(
            std::forward<F>(f));

        qstr key = qstr_from_strn(name, len);
        mp_obj_dict_store(MP_OBJ_FROM_PTR(globals),
                          MP_OBJ_NEW_QSTR(key), MP_OBJ_FROM_PTR(obj));
    }

    // Register `newname` as an alias of an already-registered `oldname`.
    // Stores the same callable under both keys. `oldname` must already exist.
    void alias(const std::string &newname, const std::string &oldname) {
        mp_obj_t val = mp_obj_dict_get(
            MP_OBJ_FROM_PTR(globals),
            MP_OBJ_NEW_QSTR(qstr_from_strn(oldname.data(), oldname.size())));
        mp_obj_dict_store(
            MP_OBJ_FROM_PTR(globals),
            MP_OBJ_NEW_QSTR(qstr_from_strn(newname.data(), newname.size())), val);
    }
};

// Single global instance, defined in py_bind.cpp.
extern Module femm_module;

}  // namespace femm_py

#endif  // PY_BIND_H
