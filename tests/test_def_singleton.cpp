// Bug3 回归: LuaModule::def(name, class, obj) 传显式对象时, 不应再强制要求
// 类型可默认构造 (以前 else 分支的 `new TCLASS` 被无条件编译, 无默认构造的类直接
// 编译失败)。此测试的类【故意】没有默认构造函数, 能编译并运行即证明修复。
#include "luaaa.hpp"
#include <string>
#include <cstdio>
using namespace luaaa;

struct Config {
    std::string name;
    int version;
    // 只有带参构造, 没有默认构造函数
    Config(const std::string& n, int v) : name(n), version(v) {}
    std::string getName() const { return name; }
    int getVersion() const { return version; }
};

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char* name) {
    if (ok) { ++g_pass; printf("  ok   %s\n", name); }
    else    { ++g_fail; printf("  FAIL %s\n", name); }
}

int main() {
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    LuaClass<Config> cfg(L, "Config");
    cfg.fun("getName", &Config::getName);
    cfg.fun("getVersion", &Config::getVersion);

    static Config theConfig("prod", 5);   // 应用侧持有的单例
    LuaModule sys(L, "sys");
    sys.def("config", cfg, &theConfig);    // 显式对象, 无 deleter

    check(luaL_dostring(L,
        "assert(sys.config:getName()=='prod', 'name')\n"
        "assert(sys.config:getVersion()==5, 'version')\n") == 0,
        "无默认构造类可作为单例 def 并从 Lua 调用");
    if (g_fail) printf("  lua-err: %s\n", lua_tostring(L, -1));

    printf("\n=== def_singleton: %d passed, %d failed ===\n", g_pass, g_fail);
    lua_close(L);
    return g_fail ? 1 : 0;
}
