// 子测试: LUAAA_FEATURE_EXTEND=0 时的行为.
// 探查: 关闭 extend 后, 类绑定仍工作, 但 luaaa:extend/base helper 缺席.
#define LUAAA_FEATURE_EXTEND 0
#include "luaaa.hpp"
#include <cstdio>
#include <string>

static int P = 0, F = 0;
static void check(bool ok, const char* n) {
    if (ok) { ++P; printf("  ok   %s\n", n); }
    else    { ++F; printf("  FAIL %s\n", n); }
}

class E2 {
public:
    int v;
    E2() : v(0) {}
    int method(int x) { return x * 2; }
};

int main() {
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    {
        luaaa::LuaClass<E2> cls(L, "E2");
        cls.ctor();
        cls.fun("method", &E2::method);
    }
    printf("[EXTEND=0 — 类绑定基线]\n");
    check(luaL_dostring(L, "local e = E2.new(); assert(e:method(5) == 10)") == 0,
          "EXTEND=0 时类绑定与方法调用正常");
    if (luaL_dostring(L, "local e = E2.new(); assert(e:method(5) == 10)") != 0) {
        lua_pop(L, 1);
    }

    printf("[EXTEND=0 — luaaa 全局表缺席]\n");
    {
        // luaaa:extend 应不可用 (luaaa 全局未创建)
        luaL_dostring(L, "g_has_luaaa = (luaaa ~= nil)");
        lua_getglobal(L, "g_has_luaaa");
        bool has = lua_toboolean(L, -1); lua_pop(L, 1);
        printf("    (信息) luaaa 全局存在 = %d (期望 false)\n", (int)has);
        check(!has, "EXTEND=0 时 luaaa 全局表缺席");
    }

    lua_close(L);
    printf("\n=== features_no_extend: %d passed, %d failed ===\n", P, F);
    return F > 0 ? 1 : 0;
}
