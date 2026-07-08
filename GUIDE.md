# luaaa — Advanced Guide

[English](GUIDE.md) | [中文](GUIDE.zh-CN.md) · Back to [README.md](README.md)

This guide is a reference and FAQ for people who already know the basics from the [README](README.md). Each section stands on its own — jump to what you need.

1. [Type conversion & `LuaStack`](#type-conversion--luastack)
2. [Teaching luaaa your own types](#teaching-luaaa-your-own-types)
3. [Constructors in depth](#constructors-in-depth)
4. [Overloaded functions & disambiguation](#overloaded-functions--disambiguation)
5. [Callbacks in depth](#callbacks-in-depth)
6. [Properties in depth](#properties-in-depth)
7. [`def`: constants, arrays & embedded instances](#def-constants-arrays--embedded-instances)
8. [Metamethods](#metamethods)
9. [Multiple `lua_State`s & the `TAG` parameter](#multiple-lua_states--the-tag-parameter)
10. [Embedded / no-stdlib builds](#embedded--no-stdlib-builds)
11. [Feature macros](#feature-macros)
12. [Object lifetime & GC ownership](#object-lifetime--gc-ownership)
13. [Compatibility notes](#compatibility-notes)
14. [Troubleshooting / FAQ](#troubleshooting--faq)

---

## Type conversion & `LuaStack`

Every value that crosses the C++ ⇄ Lua boundary goes through `LuaStack<T>`, which has two jobs: `get` (Lua → C++) and `put` (C++ → Lua). luaaa ships specializations for:

| Category | Types |
|---|---|
| Floating | `float`, `double`, `long double` |
| Boolean | `bool` |
| Integers | `int`, `long`, `long long`, `short`, `char` and their `unsigned` forms — and therefore every alias (`size_t`, `int64_t`, `uint32_t`, `ptrdiff_t`, …) |
| Strings | `const char*`, `char*`, `std::string` |
| Sequence containers | `std::array`, `vector`, `deque`, `list`, `forward_list` |
| Sets | `set`, `multiset`, `unordered_set`, `unordered_multiset` |
| Maps | `map`, `multimap`, `unordered_map`, `unordered_multimap` |
| Tuple-like | `std::pair`, `std::tuple` |
| Lua handle | `lua_State*` (receives the current state; consumes no argument) |
| Bound classes | `T`, `T&`, `const T&`, `T*` for any class you exported with `LuaClass` |

Containers map to Lua tables: sequences become array-style tables (`{1,2,3}`), maps become key/value tables (`{a=1}`), `pair`/`tuple` become positional tables (`{first, second}`).

> ⚠️ **Integer precision across Lua versions.** All integer types are converted through `lua_Integer`. On Lua 5.3/5.4 that's 64-bit, so `int`/`long`/`size_t` round-trip exactly. On **Lua 5.1/5.2 and LuaJIT** numbers are doubles, so any integer beyond 2⁵³ loses precision. Also, an `unsigned` 64-bit value ≥ 2⁶³ maps to a negative `lua_Integer` even on 5.3+. This is an inherent Lua limitation — keep large 64-bit integers as strings if you need them exact on older Lua.
>
> ⚠️ **Floating-point overflow is checked.** Every FP value is converted through `lua_Number`. If a value can't fit — a huge `long double` pushed into a `double`-typed `lua_Number`, or a `double`-valued Lua number read back into a `float` — luaaa raises a Lua error rather than silently producing `inf`. Precision loss *within* range (mantissa truncation, or any FP on Lua 5.1/5.2/LuaJIT) is inherent to Lua and is not reported.
>
> Non-arithmetic scalars (`enum`, raw structs) still have no specialization; add a `LuaStack` for those (next section).

## Teaching luaaa your own types

To pass a type luaaa doesn't know, specialize `LuaStack<YourType>`. **The specialization must live in `namespace luaaa`** (GCC requires it, and it's harmless elsewhere). You typically build on the specializations that already exist.

```cpp
struct Color { int r, g, b; };   // your type; Lua sees it as { r=.., g=.., b=.. }

namespace luaaa {
    template<> struct LuaStack<Color> {
        static Color get(lua_State* L, int idx) {
            auto t = LuaStack<std::map<std::string, int>>::get(L, idx);
            return Color{ t["r"], t["g"], t["b"] };
        }
        static void put(lua_State* L, const Color& c) {
            std::map<std::string, int> t{ {"r", c.r}, {"g", c.g}, {"b", c.b} };
            LuaStack<decltype(t)>::put(L, t);
        }
    };
}
```

Now any bound function taking or returning `Color` just works. This is exactly what `example/example.cpp` does for collar colors.

## Constructors in depth

A class needs at least one constructor. There are four flavors; all can coexist under different names, and all return a fresh Lua object.

```cpp
LuaClass<Cat> cat(L, "Cat");

// 1) placement constructor — object is built inside the Lua userdata,
//    ~Cat() runs when Lua collects it. Template args = C++ ctor params.
cat.ctor<std::string>("new");            // Cat.new("Tom") -> new Cat("Tom")

// 2) factory / spawner — a static or free function returns a heap object.
//    luaaa `delete`s it on GC by default.
cat.ctor("fromShelter", &Cat::adopt);    // Cat* adopt() { return new Cat(...); }

// 3) spawner + custom deleter — your deleter is called on GC.
cat.ctor("managed", &Cat::adopt, &Cat::release);   // void release(Cat*)

// 4) spawner + nullptr deleter — Lua NEVER destroys it. Use for singletons
//    or objects owned elsewhere.
cat.ctor("shared", &Cat::instance, nullptr);
```

A spawner is `TCLASS* (*)(ARGS...)`; a deleter is `R (*)(TCLASS*)`. Both may be static members or free functions.

**Name conflicts.** Two constructors with the same Lua name: the later one overwrites the earlier. When `LUAAA_CHECK_CONSTRUCTOR_NAME_CONFLICT` is `1` (the default), luaaa prints a warning to help you catch it.

## Overloaded functions & disambiguation

If a C++ name is overloaded, the compiler can't tell which one you mean from a bare `&Name`. Cast to the exact signature:

```cpp
bool  parse(const std::string&);
void  parse(int);

LuaModule(L, "m")
    .fun("parseStr", (bool(*)(const std::string&)) parse)
    .fun("parseInt", (void(*)(int))               parse);

struct Calc { int add(int,int); int add(int); };

LuaClass<Calc>(L, "Calc")
    .ctor()
    .fun("add2", (int(Calc::*)(int,int)) &Calc::add)
    .fun("add1", (int(Calc::*)(int))     &Calc::add);
```

Give each overload a distinct Lua name (Lua has no overloading).

## Callbacks in depth

A bound C++ function can receive a Lua function. Declare the parameter as either a `std::function` or a raw function pointer — the two behave very differently.

```cpp
void onEach(std::function<int(int)> cb);   // (a) preferred
void onEvent(int (*cb)(const char*));      // (b) raw pointer
```

**(a) `std::function` — recommended.** Each callback captures its own reference to the Lua function (guarded by a `shared_ptr`). Therefore it can be:
* stored and called many times,
* held alongside other callbacks of the same signature,
* re-entrant (a callback may trigger another).

The Lua reference is released when the callback object and all its copies are destroyed. **The callback must not outlive its `lua_State`** (the destructor calls `luaL_unref`).

**(b) raw function pointer — limited.** A non-capturing function pointer can't carry context, so luaaa parks the `(state, ref)` in `static` slots keyed by the *signature*. Consequences:
* only **one** callback of a given signature can be live at a time — registering another overwrites it;
* it is **not** re-entrant or thread-safe;
* the reference is not released until the state is closed (it leaks meanwhile).

Use the raw form only for a single, simple, long-lived callback (it's the only option in [embedded builds](#embedded--no-stdlib-builds)); otherwise prefer `std::function`.

## Properties in depth

`get`/`set` accept several shapes. `TCLASS` is the bound class.

**Getters** (return the property; must return non-void):
```cpp
cat.get("name", &Cat::name);                       // member:  P (Cat::*)() const
cat.get("total", &globalCount);                    // free:    P (*)()
cat.get("label", &describe);                       // free:    P (*)(const Cat&)
cat.get("w", [](Cat& c){ return c.weight(); });    // lambda:  []( [const] Cat& )->P
cat.get("k", []{ return 3.14; });                  // lambda:  []()->P
```

**Setters** (take the new value; return type ignored):
```cpp
cat.set("name", &Cat::setName);                    // member:  R (Cat::*)(P)
cat.set("flag", &setGlobalFlag);                   // free:    R (*)(P)
cat.set("w", &applyWeight);                        // free:    R (*)(Cat&, P)
cat.set("w", [](Cat& c, float v){ c.setW(v); });   // lambda:  [](Cat&, P)
cat.set("k", [](float v){ /*...*/ });              // lambda:  [](P)
```

**Access rules.** Getter only → read-only; writing raises `attempt to write Read-Only property '...'`. Setter only → write-only; reading raises `attempt to read Write-Only property '...'`. Reading an unknown property yields `nil`.

**Module properties** work the same way, but the getter/setter are free functions or lambdas with no `self` parameter (a module has no instance). A module getter must return non-void (enforced by `static_assert`).

## `def`: constants, arrays & embedded instances

`def` publishes read-only-ish data.

```cpp
// classes and modules: a scalar constant or a string
mod.def("version", 3);
mod.def("name", "shelter");

// modules only: a C array becomes a Lua array table
static const int primes[] = { 2, 3, 5, 7 };
mod.def("primes", primes, sizeof(primes)/sizeof(primes[0]));

// modules only: embed a live instance of a bound class into the module table.
// obj = nullptr default-constructs one; pass a deleter to control cleanup.
LuaClass<Cat> cat(L, "Cat"); cat.ctor();
mod.def("mascot", cat);                 // shelter.mascot is a Cat
mod.def("mascot", cat, existingCatPtr); // or embed an existing object
```

## Metamethods

You can bind Lua metamethods with `fun`. Most (`__tostring`, `__add`, `__len`, `__eq`, …) are registered as-is:

```cpp
cat.fun("__tostring", &Cat::toString);   // print(obj) / tostring(obj)
```

Three are special because luaaa uses them internally to implement properties and GC: `__index`, `__newindex`, `__gc`. When you bind one of these, luaaa keeps its own dispatcher installed and calls **yours as a fallback**, after built-in method and property lookup. So a custom `__index` handles only keys that aren't a bound method or property; a custom `__gc` runs in addition to the object's destructor.

Property/method lookup order for an instance read (`obj.key`): bound methods & constants → registered getter → your `__index` (function or table) → *(if a setter exists)* write-only error → `nil`.

## Multiple `lua_State`s & the `TAG` parameter

luaaa stores each type's Lua name in a **per-state registry**, so the same C++ type can be exported under **different names in different states**:

```cpp
LuaClass<Widget>(A, "Button").ctor().fun("get", &Widget::get);  // state A
LuaClass<Widget>(B, "Slider").ctor().fun("get", &Widget::get);  // state B
// Button.new():get() in A and Slider.new():get() in B both work.
```

Within a **single** state, a C++ type maps to exactly **one** Lua name. Binding the same type under a second name in the same state raises a conflict error. If you genuinely need two Lua views of one type in one state, wrap it in two distinct C++ types (e.g. trivial subclasses) — that is the reliable approach.

The template also takes an integer `TAG` — `LuaClass<T, TAG>` — that gives `<T, TAG>` its own registry slot and metatable. It's useful for keeping separate bindings from colliding, **but note**: instance-method `self` is always resolved through `LuaClass<T, 0>`, so binding *instance methods* on a non-zero `TAG` will fail to find `self` at call time. Treat `TAG` as an advanced escape hatch, not a general "same type twice in one state" solution.

## Embedded / no-stdlib builds

For microcontrollers and other freestanding targets, define this **before** including the header:

```cpp
#define LUAAA_WITHOUT_CPP_STDLIB 1
#include "luaaa.hpp"
```

Typically compiled with `-fno-exceptions -fno-rtti`. See `example/embedded.cpp` for a complete, runnable node.

**Not available** in this mode (they need the STL): `std::string`, `std::function`, all STL container conversions, `std::tuple`/`std::pair`, and lambda binding (lambdas rely on `std::function`).

**Use instead:**
* `const char*` for text (fixed buffers in your classes).
* C arrays via `def(name, array, length)` for tables.
* raw **function-pointer** callbacks (see [Callbacks](#callbacks-in-depth)).
* a `lua_State*` parameter on a method to read tables/arguments by hand:
  ```cpp
  void feedAll(lua_State* L) {
      if (lua_istable(L, -1)) {
          lua_pushnil(L);
          while (lua_next(L, -2)) {
              const char* who = LuaStack<const char*>::get(L, lua_gettop(L));
              /* ... */
              lua_pop(L, 1);
          }
      }
  }
  ```

**Provide `operator new`/`delete`.** luaaa uses placement `new` to build objects inside userdata; a freestanding runtime has no allocator. Provide your own (guard them out on hosted toolchains that already define them):
```cpp
#ifdef LUAAA_FREESTANDING
void* operator new(size_t, void* p) noexcept { return p; }
void  operator delete(void*, void*) noexcept {}
void* operator new(size_t n)            { return malloc(n); }
void  operator delete(void* p) noexcept { free(p); }
#endif
```

**RTTI.** With `-fno-rtti` luaaa can't read type names, so `not export`/conflict messages show `?` instead of the C++ type name. Everything still functions; only the diagnostics are terser.

## Feature macros

Define these **before** including `luaaa.hpp`.

| Macro | Default | Effect |
|---|---|---|
| `LUAAA_WITHOUT_CPP_STDLIB` | `0` | Drop all C++ std-lib use (embedded mode). |
| `LUAAA_FEATURE_PROPERTY` | `1` | Enable `get`/`set` properties (the `__index`/`__newindex` machinery). |
| `LUAAA_FEATURE_EXTEND` | `1` | Auto-inject `luaaa:extend` / `luaaa:base` for Lua-side inheritance. |
| `LUAAA_CHECK_CONSTRUCTOR_NAME_CONFLICT` | `1` | Warn when two constructors share a Lua name. |
| `LUAAA_DEBUG` | `0` | Enable the `LUAAA_DUMP(L)` stack-dump helper. |

## Object lifetime & GC ownership

Who calls the destructor depends on how the object entered Lua:

| Creation | On GC |
|---|---|
| `ctor<Args...>()` (placement) | object lives in the userdata; `~T()` runs |
| `ctor(name, spawner)` | `delete` the returned pointer (no-op if `T` isn't destructible) |
| `ctor(name, spawner, deleter)` | your `deleter(T*)` runs |
| `ctor(name, spawner, nullptr)` | never destroyed by Lua (borrowed / singleton) |
| returned as `T*`/`T&` from a bound function | pushed as a light reference; Lua does **not** own or collect it |

Returning a pointer/reference to a C++ object gives Lua a handle it can pass back into other bound functions, but Lua won't manage its lifetime — keep the C++ object alive yourself.

## Compatibility notes

* **Lua versions.** 5.1, 5.2, 5.3, 5.4 and LuaJIT. For 5.1/LuaJIT, luaaa supplies the few 5.2+ helpers it needs (`luaL_setfuncs`, `lua_rawgetp`, …).
* **Module registry.** On Lua > 5.1 without `LUA_COMPAT_MODULE`, modules are created as plain global tables (the modern style); otherwise the legacy `luaL_openlib` path is used. This is automatic.
* **C++ standard.** C++11 is the floor. C++14+ selects a faster `std::tuple` conversion path; a C++11 fallback is included, so tuples work either way.

## Troubleshooting / FAQ

**`cpp class 'X' not export`** — luaaa was asked to convert a type it doesn't have bound/specialized. Causes: (1) you called a method before `LuaClass<X>` was constructed in *that* state; (2) the bound signature uses a scalar with no specialization (e.g. an `enum` or a raw struct) — add a `LuaStack` for it; (3) you're using the object in a different `lua_State` than the one it was bound in.

**`C++ class '...' bind to conflict lua name`** — you bound the same C++ type under two names in one state. Use one name per state, separate states, or distinct wrapper types (see [Multiple states](#multiple-lua_states--the-tag-parameter)).

**A static method call `Class.method()` fails with "nil value"** — static/free functions bound via `fun` are invoked like methods: call `instance:method(args)`. The instance is the skipped `self`.

**`attempt to read Write-Only` / `write Read-Only property`** — the property has only a setter (write-only) or only a getter (read-only). Register the missing accessor if you need both.

**A lambda won't bind** — in embedded mode lambdas are unavailable (they need `std::function`); use a free function pointer. In normal mode, make sure the lambda is non-generic (no `auto` params) so its signature can be deduced.

**My callback crashes after a while** — a raw function-pointer callback is single-slot per signature and not re-entrant; a `std::function` callback must not outlive its `lua_State`. See [Callbacks](#callbacks-in-depth).
