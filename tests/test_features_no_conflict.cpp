// 子测试: LUAAA_CHECK_CONSTRUCTOR_NAME_CONFLICT=0 + ctor(spawner,deleter) 漏检.
// 探查: 关闭冲突检测后, 同类型同 state 可绑不同名 (per-state registry 仍拦截,
//   但 ctor 名冲突检查被宏关闭). 同时验证 luaaa.hpp:1638 spawner+deleter overload
//   本就不调冲突检查 (无论宏开关).
#define LUAAA_CHECK_CONSTRUCTOR_NAME_CONFLICT 0
#include "luaaa.hpp"
#include <cstdio>
#include <string>

static int P = 0, F = 0;
static void check(bool ok, const char* n) {
    if (ok) { ++P; printf("  ok   %s\n", n); }
    else    { ++F; printf("  FAIL %s\n", n); }
}

class C2 {
public:
    int v;
    C2() : v(0) {}
};
static C2* makeC2() { return new C2(); }
static void delC2(C2* p) { delete p; }

int main() {
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    {
        luaaa::LuaClass<C2> cls(L, "C2");
        cls.ctor();
        // 两个同名 ctor (spawner+deleter) — 本就漏检 (luaaa.hpp:1638)
        cls.ctor("dup", makeC2, delC2);
        cls.ctor("dup", makeC2, delC2);
    }
    printf("[CONFLICT=0 — 重复同名 ctor 不报错]\n");
    // 由于宏关闭, 上述注册都应成功 (编译期无错, 运行期无 conflict 错误)
    // 调用最后一个 dup — 后者覆盖
    bool ok = (luaL_dostring(L, "local c = C2.dup(); assert(c ~= nil)") == 0);
    check(ok, "CONFLICT=0 时重复同名 ctor 注册成功且可调用");
    if (!ok) { lua_pop(L, 1); }

    lua_close(L);
    printf("\n=== features_no_conflict: %d passed, %d failed ===\n", P, F);
    return F > 0 ? 1 : 0;
}
