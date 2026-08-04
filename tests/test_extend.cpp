// 内置继承测试：
//   验证 luaaa:extend / luaaa:base 现为框架内置能力——
//   绑定任意 LuaClass 后无需粘贴任何 Lua helper 即可直接使用。
#include "luaaa.hpp"
#include <string>
#include <cstdio>
using namespace luaaa;

struct Animal {
    std::string n;
    Animal(const std::string& s) : n(s) {}
    std::string name() const { return n; }
    void speak() const { printf("  [C++ base speak] %s\n", n.c_str()); }
};

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char* name) {
    if (ok) { ++g_pass; printf("  ok   %s\n", name); }
    else    { ++g_fail; printf("  FAIL %s\n", name); }
}

static void bindAnimal(lua_State* L) {
    LuaClass<Animal> c(L, "Animal");
    c.ctor<std::string>();
    c.fun("name", &Animal::name);
    c.fun("speak", &Animal::speak);
}

int main() {
    printf("=== 内置 luaaa:extend / luaaa:base(无需粘贴 helper) ===\n");
    {
        lua_State* L = luaL_newstate(); luaL_openlibs(L);
        bindAnimal(L);   // 绑定即自动注入 extend/base

        // 内置能力已存在
        check(luaL_dostring(L, "assert(type(luaaa)=='table' and type(luaaa.extend)=='function' "
                               "and type(luaaa.base)=='function')") == 0,
              "luaaa.extend/base 已自动注入");

        const char* prog = R"(
          Dog = luaaa:extend(Animal, {kind="dog"})
          function Dog:bark() return self:name() .. " barks" end
          local d = Dog:new("Rex")
          assert(d:name()=="Rex", "inherited C++ method failed")
          assert(d:bark()=="Rex barks", "subclass method failed")
          assert(d.kind=="dog", "subclass field failed")
          assert(luaaa:base(d):name()=="Rex", "base C++ object via @ failed")
        )";
        check(luaL_dostring(L, prog) == 0, "继承/子类方法/字段/base");

        // obj 省略也可用
        check(luaL_dostring(L,
              "Cat=luaaa:extend(Animal); local c=Cat:new('Tom'); assert(c:name()=='Tom')") == 0,
              "extend 省略 obj 参数");

        // luaaa:base 对非表返回 nil
        check(luaL_dostring(L, "assert(luaaa:base(123)==nil)") == 0, "base(非表)==nil");

        lua_close(L);
    }

    printf("=== 尊重用户预定义的 luaaa.extend(不被覆盖) ===\n");
    {
        lua_State* L = luaL_newstate(); luaL_openlibs(L);
        // 用户先定义自己的 luaaa.extend
        check(luaL_dostring(L,
              "luaaa={}; function luaaa.marker() return 'user' end "
              "function luaaa:extend(b,o) o=o or {}; o.mine=true; return o end") == 0,
              "用户预定义 luaaa.extend");
        bindAnimal(L);   // 注入应尊重既有定义
        check(luaL_dostring(L, "assert(luaaa.marker()=='user')") == 0, "用户字段保留");
        check(luaL_dostring(L,
              "local t=luaaa:extend(Animal, {}); assert(t.mine==true)") == 0,
              "用户 extend 未被内置覆盖");
        // base 缺失则由内置补齐
        check(luaL_dostring(L, "assert(type(luaaa.base)=='function')") == 0,
              "缺失的 base 由内置补齐");
        lua_close(L);
    }

    printf("=== 多 state 各自独立注入 ===\n");
    {
        lua_State* A = luaL_newstate(); luaL_openlibs(A);
        lua_State* B = luaL_newstate(); luaL_openlibs(B);
        bindAnimal(A); bindAnimal(B);
        check(luaL_dostring(A, "assert(type(luaaa.extend)=='function')") == 0, "A 有 extend");
        check(luaL_dostring(B,
              "local D=luaaa:extend(Animal,{}); local d=D:new('Zoe'); assert(d:name()=='Zoe')") == 0,
              "B 独立使用 extend");
        lua_close(A); lua_close(B);
    }

    printf("\n=== extend: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
