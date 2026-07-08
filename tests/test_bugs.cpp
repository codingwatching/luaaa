// 历史缺陷回归测试。每个用例针对代码走查中曾定位的缺陷，用受控输入触发对应
// 代码路径，打印「实际 vs 预期」并标注结论。全部修复后应报告「0 个缺陷」。
// (B1/B5/B15 见 H4/M1/H3 修复；B2/B3 见 H1/H2 修复。)
// 每个用例用 lua pcall 隔离，避免 longjmp 影响后续用例。
#include "luaaa.hpp"

#include <string>
#include <array>
#include <utility>
#include <functional>
#include <cstdio>

using namespace luaaa;

//--------- B2: 传入 C++ 的 lua 回调是否可被多次调用 ---------
// 猜测：回调 get() 用 static funcId，调用后 luaL_unref，第二次调用失效。
static int callTwiceFunctor(std::function<int(int)> f) {
    int a = f(10);   // 期望 100
    int b = f(20);   // 期望 200
    return a + b;    // 期望 300
}
static int callTwiceFnPtr(int(*f)(int)) {
    int a = f(10);
    int b = f(20);
    return a + b;
}

//--------- B3: 同签名两个回调是否互相覆盖(共享 static 槽) ---------
static int callTwoCallbacks(std::function<int(int)> f, std::function<int(int)> g) {
    return f(1) * 1000 + g(1);   // 期望 f(1)=1, g(1)=2 -> 1002
}

//--------- B5: std::pair 的 get/put 是否对称 ---------
static std::pair<int,int> echoPair(std::pair<int,int> p) {
    return std::pair<int,int>(p.first + 1, p.second + 1);
}

//--------- B4: std::array 传入超长表是否安全 ---------
static int arraySum3(std::array<int,3> a) {
    return a[0] + a[1] + a[2];
}

//--------- B1/B15: module property ---------
static std::string g_wo = "";
static void  woSet(const std::string& s) { g_wo = s; }
static std::string roGet() { return std::string("readonly-value"); }

//========================= 执行辅助 =========================
static int g_bugs = 0;

// 运行一段 lua，返回是否成功；出错信息写入 err
static bool runLua(lua_State* L, const char* code, std::string& err) {
    if (luaL_dostring(L, code)) {
        err = lua_tostring(L, -1);
        lua_pop(L, 1);
        return false;
    }
    return true;
}

int main() {
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    LuaModule G(L);
    G.fun("callTwiceFunctor", callTwiceFunctor);
    G.fun("callTwiceFnPtr", callTwiceFnPtr);
    G.fun("callTwoCallbacks", callTwoCallbacks);
    G.fun("echoPair", echoPair);
    G.fun("arraySum3", arraySum3);

    LuaModule M(L, "MOD");
    M.set("wo", woSet);      // 只写属性
    M.get("ro", roGet);      // 只读属性

    std::string err;

    printf("========== BUG 触发测试 ==========\n\n");

    // ---- B2a: functor 回调二次调用 ----
    printf("[B2a] std::function 回调二次调用\n");
    if (runLua(L, "R = callTwiceFunctor(function(x) return x*10 end)", err)) {
        lua_getglobal(L, "R"); long r = (long)lua_tointeger(L, -1); lua_pop(L, 1);
        printf("      结果 R=%ld  (期望 300)\n", r);
        if (r != 300) { printf("      >>> BUG CONFIRMED: 回调第二次调用未生效\n"); ++g_bugs; }
        else printf("      OK\n");
    } else {
        printf("      >>> BUG CONFIRMED: 回调二次调用直接报错: %s\n", err.c_str()); ++g_bugs;
    }
    printf("\n");

    // ---- B2b: 函数指针回调二次调用 ----
    printf("[B2b] 函数指针回调二次调用\n");
    if (runLua(L, "R = callTwiceFnPtr(function(x) return x*10 end)", err)) {
        lua_getglobal(L, "R"); long r = (long)lua_tointeger(L, -1); lua_pop(L, 1);
        printf("      结果 R=%ld  (期望 300)\n", r);
        if (r != 300) { printf("      >>> BUG CONFIRMED: 回调第二次调用未生效\n"); ++g_bugs; }
        else printf("      OK\n");
    } else {
        printf("      >>> BUG CONFIRMED: %s\n", err.c_str()); ++g_bugs;
    }
    printf("\n");

    // ---- B3: 同签名两个回调 ----
    printf("[B3] 同签名两个回调是否互相覆盖\n");
    if (runLua(L, "R = callTwoCallbacks(function(x) return x end, function(x) return x*2 end)", err)) {
        lua_getglobal(L, "R"); long r = (long)lua_tointeger(L, -1); lua_pop(L, 1);
        printf("      结果 R=%ld  (期望 1002: f(1)=1, g(1)=2)\n", r);
        if (r != 1002) { printf("      >>> BUG CONFIRMED: 两个回调共享 static 槽相互覆盖\n"); ++g_bugs; }
        else printf("      OK\n");
    } else {
        printf("      >>> BUG (error): %s\n", err.c_str()); ++g_bugs;
    }
    printf("\n");

    // ---- B5: pair 往返 ----
    printf("[B5] std::pair get/put 对称性\n");
    if (runLua(L, "local p = echoPair({10,20}); RA = p[1]; RB = p[2]", err)) {
        lua_getglobal(L, "RA"); long a = (long)lua_tointeger(L, -1); lua_pop(L, 1);
        lua_getglobal(L, "RB"); long b = (long)lua_tointeger(L, -1); lua_pop(L, 1);
        printf("      结果 {%ld,%ld}  (期望 {11,21})\n", a, b);
        if (a != 11 || b != 21) { printf("      >>> BUG CONFIRMED: pair get 未从 table 读取元素\n"); ++g_bugs; }
        else printf("      OK\n");
    } else {
        printf("      >>> BUG CONFIRMED: pair 传参报错: %s\n", err.c_str()); ++g_bugs;
    }
    printf("\n");

    // ---- B4: array 超长输入 ----
    printf("[B4] std::array<int,3> 传入 5 元素表\n");
    if (runLua(L, "R = arraySum3({1,2,3,4,5}); R2 = arraySum3({7,8,9})", err)) {
        lua_getglobal(L, "R");  long r  = (long)lua_tointeger(L, -1); lua_pop(L, 1);
        lua_getglobal(L, "R2"); long r2 = (long)lua_tointeger(L, -1); lua_pop(L, 1);
        printf("      arraySum3({1..5})=%ld (期望前3项和=6), 随后 arraySum3({7,8,9})=%ld (期望24)\n", r, r2);
        if (r2 != 24) { printf("      >>> BUG: 超长输入破坏了后续调用的栈平衡\n"); ++g_bugs; }
        else printf("      未观察到崩溃/后续错乱(但内部栈可能残留, 见 ASan)\n");
    } else {
        printf("      >>> 报错: %s\n", err.c_str()); ++g_bugs;
    }
    printf("\n");

    // ---- B15: module property getter 是否生效 ----
    printf("[B15] module property getter\n");
    if (runLua(L, "R = MOD.ro", err)) {
        lua_getglobal(L, "R");
        const char* s = lua_isstring(L, -1) ? lua_tostring(L, -1) : "<nil>";
        printf("      MOD.ro = %s  (期望 'readonly-value')\n", s);
        if (!lua_isstring(L, -1) || std::string(s) != "readonly-value") {
            printf("      >>> BUG CONFIRMED: module getter 未触发(注册到 table 却从 metatable 查找)\n"); ++g_bugs;
        } else printf("      OK\n");
        lua_pop(L, 1);
    } else {
        printf("      >>> 报错: %s\n", err.c_str()); ++g_bugs;
    }
    printf("\n");

    // ---- B1/M1: 读取 module 只写属性应正确报 write-only 错误(且不因格式串缺陷崩溃) ----
    printf("[B1/M1] 读取 module 只写属性应报 write-only 错误\n");
    if (runLua(L,
        "local ok,e = pcall(function() return MOD.wo end); WO_OK = ok; WO_ERR = tostring(e)", err)) {
        lua_getglobal(L, "WO_OK"); int ok = lua_toboolean(L, -1); lua_pop(L, 1);
        lua_getglobal(L, "WO_ERR"); const char* e = lua_tostring(L, -1);
        printf("      读取 MOD.wo: pcall ok=%d, err=%s\n", ok, e ? e : "<nil>");
        lua_pop(L, 1);
        if (ok) { printf("      >>> write-only 保护未生效(读取成功)\n"); ++g_bugs; }
        else    { printf("      OK: 正确报错且未崩溃\n"); }
    } else {
        printf("      >>> 读取过程异常(可能崩溃/格式串 UB): %s\n", err.c_str()); ++g_bugs;
    }
    printf("\n");

    lua_close(L);

    printf("========== 结论: 触发/确认了 %d 个缺陷 ==========\n", g_bugs);
    return 0;   // 该程序用于观测, 不以退出码判定
}
