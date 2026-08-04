// 容器转换边界测试：覆盖未测容器类型与边界情形。
//
// 覆盖未测分支:
//   - std::array<int,N> 定长往返 (短表/长表/空表)
//   - std::deque / std::forward_list / std::multiset / std::unordered_multiset echo
//   - 空表 {} -> 空容器往返
//   - 非表传参 (echo_vec(123)) 应报错 "required table not found"
//   - 深层嵌套 vector<list<map<string,set<int>>>> 往返
//   - map 数字键 + 字符串键混合表
//
// M6 已修复: std::forward_list 特化改用 insert_after (保序), 现可正常往返。
// L10 已修复: 容器 luaL_argcheck 参数位置改为 idx (不再硬编码 1)。
#include "luaaa.hpp"

#include <cstdio>
#include <string>
#include <vector>
#include <list>
#include <set>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <deque>
#include <forward_list>
#include <array>
using namespace luaaa;

static int P = 0, F = 0;
static void check(bool ok, const char* n) {
    if (ok) { ++P; printf("  ok   %s\n", n); }
    else    { ++F; printf("  FAIL %s\n", n); }
}

static bool run(lua_State* L, const char* tag, const char* code) {
    printf("[%s]\n", tag);
    if (luaL_dostring(L, code)) {
        printf("  FAIL (lua-err: %s)\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        ++F;
        return false;
    }
    printf("  ok\n");
    ++P;
    return true;
}

static bool raises(lua_State* L, const char* code) {
    char buf[512];
    snprintf(buf, sizeof(buf), "g_ok = pcall(function() %s end)", code);
    if (luaL_dostring(L, buf) != 0) { lua_pop(L, 1); return true; }
    lua_getglobal(L, "g_ok");
    bool ok = lua_isboolean(L, -1) && lua_toboolean(L, -1);
    lua_pop(L, 1);
    return !ok;
}

// ---- 被测 echo 函数 (触发 get + put 双向实例化) ----
static std::array<int,3>               echoArray(std::array<int,3> v)               { return v; }
static std::deque<int>                 echoDeque(std::deque<int> v)                  { return v; }
static std::forward_list<int>          echoFList(std::forward_list<int> v)           { return v; }
static std::multiset<int>              echoMSet(std::multiset<int> v)                { return v; }
static std::unordered_multiset<int>    echoUMSet(std::unordered_multiset<int> v)     { return v; }
static std::vector<int>                echoVec(std::vector<int> v)                   { return v; }
static std::vector<std::list<std::map<std::string, std::set<int>>>>
                                       echoDeep(std::vector<std::list<std::map<std::string, std::set<int>>>> v) { return v; }
static std::map<std::string,int>       echoMap(std::map<std::string,int> v)          { return v; }

int main() {
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    LuaModule G(L);
    G.fun("echoArray", echoArray);
    G.fun("echoDeque", echoDeque);
    G.fun("echoFList", echoFList);
    G.fun("echoMSet", echoMSet);
    G.fun("echoUMSet", echoUMSet);
    G.fun("echoVec", echoVec);
    G.fun("echoDeep", echoDeep);
    G.fun("echoMap", echoMap);

    // ===== std::array 定长往返 =====
    run(L, "std::array<int,3> 正好 3 元素",
        "local r = echoArray({10,20,30})\n"
        "assert(#r == 3 and r[1]==10 and r[2]==20 and r[3]==30, 'array 3 elem')\n");

    run(L, "std::array<int,3> 短表 (2 元素, 尾部补零)",
        "local r = echoArray({7,8})\n"
        "assert(#r == 3 and r[1]==7 and r[2]==8 and r[3]==0, 'array short tail zero')\n");

    run(L, "std::array<int,3> 空表 (全零)",
        "local r = echoArray({})\n"
        "assert(#r == 3 and r[1]==0 and r[2]==0 and r[3]==0, 'array empty all zero')\n");

    // ===== std::deque =====
    run(L, "std::deque<int> 往返",
        "local r = echoDeque({1,2,3,4})\n"
        "assert(#r == 4 and r[1]==1 and r[4]==4, 'deque roundtrip')\n");

    // ===== std::forward_list (M6 已修复: insert_after 保序) =====
    run(L, "std::forward_list<int> 往返 (保序)",
        "local r = echoFList({5,6,7})\n"
        "assert(#r == 3 and r[1]==5 and r[2]==6 and r[3]==7, 'flist 保序 roundtrip')\n");

    // ===== std::multiset (允许重复) =====
    run(L, "std::multiset<int> 允许重复",
        "local r = echoMSet({3,1,2,2,1})\n"
        "local n=0; for _ in pairs(r) do n=n+1 end\n"
        "assert(n==5, 'multiset 保留重复, 期望5 实得'..n)\n");

    // ===== std::unordered_multiset =====
    run(L, "std::unordered_multiset<int> 允许重复",
        "local r = echoUMSet({9,9,8})\n"
        "local n=0; for _ in pairs(r) do n=n+1 end\n"
        "assert(n==3, 'unordered_multiset 保留重复')\n");

    // ===== 空表 -> 空容器 =====
    run(L, "空表 {} -> 空 vector",
        "local r = echoVec({})\n"
        "assert(#r == 0, 'empty vec')\n");

    // ===== 非表传参应报错 =====
    printf("[非表传参应报错 — echo_vec(123)]\n");
    {
        bool r = raises(L, "echoVec(123)");
        printf("    (信息) echoVec(123) raises=%d (期望 true)\n", (int)r);
        check(r, "echo_vec(123) 非表传参应报错 (required table not found)");
    }
    printf("[非表传参 — echo_vec(nil)]\n");
    {
        bool r = raises(L, "echoVec(nil)");
        printf("    (信息) echoVec(nil) raises=%d (期望 true)\n", (int)r);
        check(r, "echo_vec(nil) 非表传参应报错");
    }
    printf("[非表传参 — echo_vec('str')]\n");
    {
        bool r = raises(L, "echoVec('hello')");
        printf("    (信息) echoVec('hello') raises=%d (期望 true)\n", (int)r);
        check(r, "echo_vec('hello') 字符串非表应报错");
    }

    // ===== 深层嵌套 vector<list<map<string,set<int>>>> =====
    run(L, "深层嵌套 vector<list<map<string,set<int>>>>",
        "local r = echoDeep({\n"
        "  { { x = {1,2,3} } },\n"             // vec[1] = list{ map{ x=set{1,2,3} } }
        "  { { y = {4} }, { z = {} } }\n"       // vec[2] = list{ map{y=set{4}}, map{z=set{}} }
        "})\n"
        "assert(#r == 2, 'outer vec len 2, got '..#r)\n"
        "assert(#r[1] == 1, 'list[1] len 1')\n"
        "local nx=0; for _ in pairs(r[1][1].x) do nx=nx+1 end\n"
        "assert(nx==3, 'set x has 3, got '..nx)\n"
        "assert(#r[2] == 2, 'list[2] len 2')\n"
        "local ny=0; for _ in pairs(r[2][1].y) do ny=ny+1 end\n"
        "assert(ny==1, 'set y has 1')\n"
        "local nz=0; for _ in pairs(r[2][2].z) do nz=nz+1 end\n"
        "assert(nz==0, 'set z empty')\n");

    // ===== map 数字键 + 字符串键混合 =====
    run(L, "map<string,int> 混合数组+哈希键",
        "local r = echoMap({10,20, x=99})\n"
        "assert(r['1']==10 and r['2']==20 and r.x==99, 'mixed keys')\n");

    lua_close(L);
    printf("\n=== containers_edge: %d passed, %d failed ===\n", P, F);
    return F > 0 ? 1 : 0;
}
