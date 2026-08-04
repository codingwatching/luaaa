// 容器转换回归测试。重点覆盖两个已修复缺陷：
//   Bug1: string 键关联容器读取【数字键】表时, lua_next 遍历中对数字键调用
//         lua_tostring 会破坏迭代 (报 "invalid key to 'next'")。
//   Bug2: std::multimap / std::unordered_multimap 的 get() 误用 operator[]
//         (这两个容器没有 operator[]), 作为函数参数时直接编译失败。
#include "luaaa.hpp"
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <cstdio>
using namespace luaaa;

// --- 各容器 echo (触发 get + put 两个方向的实例化) ---
static std::map<std::string,int>            echoStrMap(std::map<std::string,int> v)                     { return v; }
static std::map<int,std::string>            echoIntMap(std::map<int,std::string> v)                     { return v; }
static std::unordered_map<std::string,int>  echoUStrMap(std::unordered_map<std::string,int> v)          { return v; }
static std::multimap<std::string,int>       echoMMap(std::multimap<std::string,int> v)                  { return v; }
static std::unordered_multimap<std::string,int> echoUMMap(std::unordered_multimap<std::string,int> v)   { return v; }
static std::vector<std::vector<int>>        echoNested(std::vector<std::vector<int>> v)                 { return v; }
static std::set<std::string>                echoStrSet(std::set<std::string> v)                         { return v; }

static int g_pass = 0, g_fail = 0;
static void run(lua_State* L, const char* tag, const char* code) {
    printf("[%s]\n", tag);
    if (luaL_dostring(L, code)) {
        printf("  FAIL %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        ++g_fail;
    } else {
        printf("  ok\n");
        ++g_pass;
    }
}

int main() {
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    LuaModule G(L);
    G.fun("echoStrMap",  echoStrMap);
    G.fun("echoIntMap",  echoIntMap);
    G.fun("echoUStrMap", echoUStrMap);
    G.fun("echoMMap",    echoMMap);
    G.fun("echoUMMap",   echoUMMap);
    G.fun("echoNested",  echoNested);
    G.fun("echoStrSet",  echoStrSet);

    // Bug1: string 键 map 收到数字键的数组风格表
    run(L, "map<string,int> <- {10,20,30} (数字键→字符串键)",
        "local r = echoStrMap({10,20,30})\n"
        "local n=0; for k,v in pairs(r) do n=n+1 end\n"
        "assert(n==3, '期望3个键, 实得'..n)\n"
        "assert(r['1']==10 and r['2']==20 and r['3']==30, 'key 转换错误')\n");

    // string 键 map 混合字符串键仍正常
    run(L, "map<string,int> <- {x=1,y=2}",
        "local r = echoStrMap({x=1,y=2})\n"
        "assert(r.x==1 and r.y==2)\n");

    // int 键 map (数字键直接走整型路径)
    run(L, "map<int,string> <- {[1]='a',[2]='b'}",
        "local r = echoIntMap({[1]='a',[2]='b'})\n"
        "assert(r[1]=='a' and r[2]=='b')\n");

    // unordered_map 同样修复
    run(L, "unordered_map<string,int> <- {100,200}",
        "local r = echoUStrMap({100,200})\n"
        "assert(r['1']==100 and r['2']==200)\n");

    // Bug2: multimap 往返 (编译能过即证明 operator[] 缺陷已修)
    run(L, "multimap<string,int> 往返",
        "local r = echoMMap({a=1,b=2})\n"
        "assert(r.a==1 and r.b==2)\n");

    run(L, "unordered_multimap<string,int> 往返",
        "local r = echoUMMap({a=1,b=2})\n"
        "assert(r.a==1 and r.b==2)\n");

    // 嵌套容器
    run(L, "vector<vector<int>>",
        "local r = echoNested({{1,2},{3,4,5}})\n"
        "assert(#r==2 and #r[1]==2 and #r[2]==3 and r[2][3]==5)\n");

    // set<string> 元素在 value 槽, 数字元素 stringify 不影响 lua_next, 但仍验证正确性
    run(L, "set<string> <- {'a','b','c'}",
        "local r = echoStrSet({'a','b','c'})\n"
        "local n=0; for _ in pairs(r) do n=n+1 end\n"
        "assert(n==3)\n");

    printf("\n=== containers: %d passed, %d failed ===\n", g_pass, g_fail);
    lua_close(L);
    return g_fail ? 1 : 0;
}
