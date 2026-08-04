// ============================================================================
//  luaaa example — "The Feeder Node"  (embedded / no C++ std-lib build)
// ----------------------------------------------------------------------------
//  The same shelter world, but on a microcontroller: no std::string, no
//  std::function, no STL containers, no exceptions, no RTTI.  This file shows
//  the embedded dialect of luaaa, enabled by:
//
//        #define LUAAA_WITHOUT_CPP_STDLIB 1
//
//  What changes vs. the full build:
//    * text is `const char *`, not std::string
//    * arrays are C arrays exported with def(name, arr, length)
//    * a class method may take `lua_State*` to read a Lua table by hand
//    * callbacks use raw function pointers (one live callback per signature)
//    * you provide operator new / delete (freestanding runtimes have none)
//
//  Build & run:  bash example/build.sh        (or see GUIDE "Embedded / no-stdlib")
// ============================================================================

#define LUAAA_WITHOUT_CPP_STDLIB 1

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "../luaaa.hpp"

using namespace luaaa;

#if defined(_MSC_VER)
#   define strncpy strncpy_s
#endif

// ----------------------------------------------------------------------------
// A true freestanding target ships no operator new/delete, so you provide them
// (luaaa uses placement new to construct objects inside Lua userdata).  On a
// hosted toolchain the C++ runtime already defines these, so we only compile
// them for a real firmware build:  add -DLUAAA_FREESTANDING there.
// ----------------------------------------------------------------------------
#ifdef LUAAA_FREESTANDING
void* operator new(size_t, void* p) noexcept { return p; }   // placement new
void  operator delete(void*, void*) noexcept {}
void* operator new(size_t n)              { return malloc(n); }
void  operator delete(void* p) noexcept   { free(p); }
void* operator new[](size_t n)            { return malloc(n); }
void  operator delete[](void* p) noexcept { free(p); }
#endif

// ----------------------------------------------------------------------------
// A device class. Fixed buffers, C strings — no heap-heavy members.
// ----------------------------------------------------------------------------
class Feeder
{
public:
    Feeder()                 : m_portions(0) { strncpy(m_id, "F-000", sizeof(m_id)); }
    explicit Feeder(const char* id) : m_portions(0)
    {
        strncpy(m_id, id, sizeof(m_id));
        m_id[sizeof(m_id) - 1] = '\0';
    }
    ~Feeder() { printf("  (C++)  feeder %s powered off\n", m_id); }

    const char* id() const           { return m_id; }

    int  portions() const            { return m_portions; }
    void setPortions(int p)          { m_portions = p; }

    void dispense()
    {
        if (m_portions > 0) { --m_portions; printf("  %s: click! one portion served\n", m_id); }
        else                { printf("  %s: hopper empty\n", m_id); }
    }

    // Read a Lua array {"Bingo","Felix"} by hand through the raw lua_State.
    // Taking `lua_State*` as a parameter is how you touch Lua directly when
    // the STL container converters are not available.
    void feedAll(lua_State* L)
    {
        if (lua_istable(L, -1))
        {
            lua_pushnil(L);
            while (lua_next(L, -2) != 0)
            {
                const char* who = LuaStack<const char*>::get(L, lua_gettop(L));
                printf("  %s: feeding %s\n", m_id, who);
                lua_pop(L, 1);
            }
        }
    }

    static void beep(const char* id) { printf("  %s: beep beep\n", id); }

private:
    char m_id[16];
    int  m_portions;
};

// ----------------------------------------------------------------------------
// A raw function-pointer callback: the firmware ticks the schedule and asks
// Lua how many cats were fed each hour.  With no std::function available this
// is the callback form to use (only one live callback per signature).
// ----------------------------------------------------------------------------
static void runSchedule(int (*tick)(int hour))
{
    if (!tick) return;
    const int hours[] = { 8, 12, 18 };
    for (int i = 0; i < 3; ++i)
    {
        int fed = tick(hours[i]);
        printf("  scheduler: %d:00 -> %d cats fed\n", hours[i], fed);
    }
}

// ----------------------------------------------------------------------------
// Wire it up.  The API is the same shape as the full build.
// ----------------------------------------------------------------------------
static void bindToLua(lua_State* L)
{
    LuaClass<Feeder>(L, "Feeder")
        .ctor<const char*>()             // Lua:  Feeder.new("F-01")
        .fun("dispense", &Feeder::dispense)
        .fun("feedAll",  &Feeder::feedAll)
        .fun("beep",     &Feeder::beep)  // static
        .get("portions", &Feeder::portions).set("portions", &Feeder::setPortions);

    static const int hours[] = { 8, 12, 18 };

    LuaModule(L, "device")
        .def("firmware", "1.0.3")
        .def("hours", hours, sizeof(hours) / sizeof(hours[0]))  // C array -> Lua table
        .fun("runSchedule", runSchedule);                       // raw callback
}

// ----------------------------------------------------------------------------
int main()
{
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    bindToLua(L);

    if (luaL_dofile(L, "embedded.lua") != 0)
    {
        printf("lua error: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
    }

    lua_close(L);
    return 0;
}
