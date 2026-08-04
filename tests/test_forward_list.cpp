// std::forward_list 往返专项测试 (M6 修复回归)。
//
// 历史: luaaa.hpp 的 std::forward_list 特化曾误用 push_back (forward_list 无此方法,
//   只有 push_front/insert_after), 导致其作为函数参数时直接编译失败。
//   M6 修复: 改用 insert_after(before_begin()) 单遍保序插入。
//
// 本测试验证修复后 forward_list 可正常编译且往返保序。
#include "luaaa.hpp"

#include <cstdio>
#include <forward_list>
#include <string>
using namespace luaaa;

static int P = 0, F = 0;
static void check(bool ok, const char* n) {
    if (ok) { ++P; printf("  ok   %s\n", n); }
    else    { ++F; printf("  FAIL %s\n", n); }
}

// 触发 LuaStack<std::forward_list>::get + put 双向实例化
static std::forward_list<int>         echoInt   (std::forward_list<int> v)         { return v; }
static std::forward_list<std::string> echoString(std::forward_list<std::string> v) { return v; }

int main() {
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    LuaModule(L, "fl")
        .fun("echoInt", echoInt)
        .fun("echoString", echoString);

    printf("[std::forward_list<int> 往返保序]\n");
    {
        if (luaL_dostring(L,
            "local r = fl.echoInt({10,20,30,40})\n"
            "assert(#r == 4, 'len')\n"
            "assert(r[1]==10 and r[2]==20 and r[3]==30 and r[4]==40, 'order preserved')\n") != 0) {
            printf("  FAIL (lua-err: %s)\n", lua_tostring(L, -1));
            lua_pop(L, 1);
            ++F;
        } else {
            printf("  ok\n");
            ++P;
        }
    }

    printf("[std::forward_list<std::string> 往返保序]\n");
    {
        if (luaL_dostring(L,
            "local r = fl.echoString({'a','b','c'})\n"
            "assert(#r == 3 and r[1]=='a' and r[2]=='b' and r[3]=='c', 'string flist order')\n") != 0) {
            printf("  FAIL (lua-err: %s)\n", lua_tostring(L, -1));
            lua_pop(L, 1);
            ++F;
        } else {
            printf("  ok\n");
            ++P;
        }
    }

    printf("[std::forward_list 空表往返]\n");
    {
        if (luaL_dostring(L,
            "local r = fl.echoInt({})\n"
            "assert(#r == 0, 'empty')\n") != 0) {
            printf("  FAIL (lua-err: %s)\n", lua_tostring(L, -1));
            lua_pop(L, 1);
            ++F;
        } else {
            printf("  ok\n");
            ++P;
        }
    }

    // 单元素 (验证 before_begin + 单次 insert_after 边界)
    printf("[std::forward_list 单元素]\n");
    {
        if (luaL_dostring(L,
            "local r = fl.echoInt({99})\n"
            "assert(#r == 1 and r[1]==99, 'single elem')\n") != 0) {
            printf("  FAIL (lua-err: %s)\n", lua_tostring(L, -1));
            lua_pop(L, 1);
            ++F;
        } else {
            printf("  ok\n");
            ++P;
        }
    }

    lua_close(L);
    printf("\n=== forward_list: %d passed, %d failed ===\n", P, F);
    return F > 0 ? 1 : 0;
}
