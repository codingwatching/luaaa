// ============================================================================
//  luaaa example — "The Little Cat Shelter"  (full C++ std-lib build)
// ----------------------------------------------------------------------------
//  A tiny, runnable story that shows the pieces you use every day:
//    * bind a C++ class to Lua                       -> class Cat
//    * member functions, a static function, __tostring
//    * properties (cat.name / cat.age)
//    * pass a Lua array to C++                        -> Cat:eat{...}
//    * a module of free functions & constants         -> shelter.*
//    * hand a Lua function to C++ as a callback        -> shelter.adopt
//    * teach luaaa a brand-new type                    -> LuaStack<Color>
//    * subclass an exported class in pure Lua          -> see example.lua
//
//  Build & run:  bash example/build.sh        (or see README "Run the example")
// ============================================================================

#include <string>
#include <list>
#include <functional>
#include <iostream>
#include <fstream>
#include <sstream>

#include "../luaaa.hpp"

using namespace luaaa;

// ----------------------------------------------------------------------------
// 1) An ordinary C++ class. It knows nothing about Lua.
// ----------------------------------------------------------------------------
class Cat
{
public:
    Cat() : m_name("stray"), m_age(1) {}
    explicit Cat(const std::string& name) : m_name(name), m_age(1) {}
    ~Cat() { std::cout << "  (C++)  cat '" << m_name << "' left the shelter\n"; }

    const std::string& name() const     { return m_name; }
    void setName(const std::string& n)  { m_name = n; }

    int  age() const                    { return m_age; }
    void setAge(int a)                  { m_age = a; }

    // Takes a Lua array {"fish", "milk"} as a std::list — luaaa converts it.
    void eat(const std::list<std::string>& foods)
    {
        for (const auto& f : foods)
            std::cout << "  " << m_name << " eats " << f << '\n';
    }

    // A static member is exposed to Lua like a normal method.
    static void meow(const std::string& who)
    {
        std::cout << "  " << who << ": meow~\n";
    }

    // Bound as __tostring, so `print(cat)` and `tostring(cat)` just work.
    std::string toString() const
    {
        return m_name + " (" + std::to_string(m_age) + "y)";
    }

private:
    std::string m_name;
    int         m_age;
};

// ----------------------------------------------------------------------------
// 2) A brand-new type luaaa does not know yet: a collar color.
//    Teach luaaa how to move it across the stack by specializing LuaStack.
//    (GCC requires the specialization to live in namespace luaaa.)
//    On the Lua side a Color is just a table: { r=.., g=.., b=.. }
// ----------------------------------------------------------------------------
struct Color { int r, g, b; };

namespace luaaa {
    template<> struct LuaStack<Color>
    {
        static Color get(lua_State* L, int idx)
        {
            auto t = LuaStack<std::map<std::string, int>>::get(L, idx);
            return Color{ t["r"], t["g"], t["b"] };
        }
        static void put(lua_State* L, const Color& c)
        {
            std::map<std::string, int> t{ {"r", c.r}, {"g", c.g}, {"b", c.b} };
            LuaStack<decltype(t)>::put(L, t);
        }
    };
}

// ----------------------------------------------------------------------------
// 3) Free functions that make up the "shelter" module.
// ----------------------------------------------------------------------------

// Mixing two collar colors — shows a custom type flowing both ways.
static Color mixColors(const Color& a, const Color& b)
{
    return Color{ (a.r + b.r) / 2, (a.g + b.g) / 2, (a.b + b.b) / 2 };
}

// Accepts a Lua function as a callback. The std::function form is the one to
// prefer: it owns its own Lua reference, so it can be stored, copied and
// called many times, and it is re-entrant.
static void adopt(const std::string& catName,
                  std::function<void(const std::string&)> onDone)
{
    std::cout << "  filing adoption paperwork for " << catName << " ...\n";
    onDone(catName);   // call back into Lua
}

// ----------------------------------------------------------------------------
// 4) Wire the C++ world to Lua.
// ----------------------------------------------------------------------------
static void bindToLua(lua_State* L)
{
    // A class. Calls can be chained.
    LuaClass<Cat>(L, "Cat")
        .ctor<std::string>()                 // Lua:  Cat.new("Bingo")
        .fun("eat",  &Cat::eat)
        .fun("meow", &Cat::meow)             // static function, same syntax
        .fun("__tostring", &Cat::toString)
        .get("name", &Cat::name).set("name", &Cat::setName)  // read + write
        .get("age",  &Cat::age ).set("age",  &Cat::setAge);

    // A module: free functions and constants grouped under one Lua table.
    LuaModule(L, "shelter")
        .def("city",     "Catville")
        .def("capacity", 50)
        .fun("adopt", adopt)
        .fun("mix",   mixColors);
}

// ----------------------------------------------------------------------------
// 5) Boot Lua, bind, and run the story in example.lua.
// ----------------------------------------------------------------------------
int main()
{
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    bindToLua(L);

    std::ifstream file("example.lua");
    if (!file)
    {
        std::cerr << "cannot open example.lua (run from the example/ folder)\n";
        lua_close(L);
        return 1;
    }
    std::stringstream buf; buf << file.rdbuf();

    if (luaL_dostring(L, buf.str().c_str()) != 0)
    {
        std::cerr << "lua error: " << lua_tostring(L, -1) << '\n';
        lua_pop(L, 1);
    }

    lua_close(L);
    return 0;
}
