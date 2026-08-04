// 子测试: LUAAA_FEATURE_PROPERTY=0 时的行为.
// 探查: 关闭 property 后, luaaa.hpp:1492-1496 把 metatable 设为自身的 __index.
//   若用户同时注册 fun("__index", f), luaL_setfuncs 会用该函数覆盖 metatable.__index,
//   导致: (a) 所有方法访问都走该 __index 函数 (而非自索引 metatable);
//         (b) 方法访问返回 __index 的返回值 (破坏方法查找).
//   这是 PROPERTY=0 + fun("__index") 的交互问题.
#define LUAAA_FEATURE_PROPERTY 0
#include "luaaa.hpp"
#include <cstdio>
#include <string>

static int P = 0, F = 0;
static void check(bool ok, const char* n) {
    if (ok) { ++P; printf("  ok   %s\n", n); }
    else    { ++F; printf("  FAIL %s\n", n); }
}

static int g_idx_calls = 0;
static int customIndex(lua_State* L) {
    ++g_idx_calls;
    lua_pushinteger(L, 42);
    return 1;
}

class P2 {
public:
    int v;
    P2() : v(0) {}
    int method(int x) { return x; }
};

int main() {
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    {
        luaaa::LuaClass<P2> cls(L, "P2");
        cls.ctor();
        cls.fun("method", &P2::method);
        cls.fun("__index", customIndex);  // 用户 __index
    }
    printf("[PROPERTY=0 + fun('__index') 交互]\n");
    {
        // 在 PROPERTY=0 下: luaL_setfuncs 注册的 __index 函数覆盖了
        // luaaa.hpp:1494 的 "__index = metatable 自身". 于是 metatable.__index = customIndex.
        // 访问任何属性都会调用 customIndex 返回 42.
        bool method_ok = (luaL_dostring(L, "g_r = P2.new().method") == 0);
        if (!method_ok) { lua_pop(L, 1); }
        lua_getglobal(L, "g_r");
        bool is_42 = lua_isnumber(L, -1) && lua_tointeger(L, -1) == 42;
        lua_pop(L, 1);
        printf("    (信息) P2.new().method -> customIndex_calls=%d is_42=%d\n",
               g_idx_calls, (int)is_42);
        // 当前行为: 方法访问被 __index 函数接管 (破坏方法查找). 文档化此交互问题.
        check(is_42, "PROPERTY=0 + fun('__index'): 方法访问被 __index 函数接管 (交互问题, 文档化)");
    }

    printf("[PROPERTY=0 无 __index — 方法应正常]\n");
    {
        lua_State* L2 = luaL_newstate(); luaL_openlibs(L2);
        {
            luaaa::LuaClass<P2> cls(L2, "P2b");
            cls.ctor();
            cls.fun("method", &P2::method);
            // 不注册 __index
        }
        bool ok = (luaL_dostring(L2, "local w=P2b.new(); assert(w:method(5)==5)") == 0);
        check(ok, "PROPERTY=0 且无 __index 时方法访问正常");
        if (!ok) { lua_pop(L2, 1); }
        lua_close(L2);
    }

    lua_close(L);
    printf("\n=== features_no_property: %d passed, %d failed ===\n", P, F);
    return F > 0 ? 1 : 0;
}
