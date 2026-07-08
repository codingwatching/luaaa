// H1/H2 回调机制修复的专项测试。
//   std::function 回调: 多次调用 / 同签名多回调并存 / 可重入 / 存储后延迟调用 /
//                        返回值与栈平衡 / void 副作用。
//   函数指针回调:        单回调多次调用(H1)。
#include "luaaa.hpp"

#include <string>
#include <vector>
#include <functional>
#include <cstdio>

using namespace luaaa;

//--- std::function 回调目标 ---
static int callTwiceFunctor(std::function<int(int)> f) {
    return f(10) + f(20);                       // 期望 300
}
static int callTwoCallbacks(std::function<int(int)> f, std::function<int(int)> g) {
    return f(1) * 1000 + g(1);                  // 期望 1002
}
// 可重入：inner 回调在 outer 回调内部被调用
static int reentrant(std::function<int(int)> outer, std::function<int(int)> inner) {
    // outer 内部会通过 lua 再触发 inner；此处 C++ 侧直接嵌套调用两者
    int a = outer(inner(3));                    // inner(3), 再 outer(该结果)
    return a;
}
static void countingVoid(std::function<void(int)> f) {
    f(1); f(2); f(3);                           // 三次副作用
}

//--- 存储后延迟调用：把回调存起来，跨多次导出调用触发 ---
static std::vector<std::function<int(int)>> g_stored;
static void storeCallback(std::function<int(int)> f) { g_stored.push_back(f); }
static int  invokeStored(int which, int arg) { return g_stored[which](arg); }

//--- 栈平衡探针 ---
static lua_State* g_L = nullptr;
static int stackTop() { return lua_gettop(g_L); }
static int callN(std::function<int(int)> f, int n) {   // 调用回调 n 次
    int s = 0; for (int i = 0; i < n; ++i) s += f(i); return s;
}

//--- 函数指针回调(H1) ---
static int callTwiceFnPtr(int(*f)(int)) {
    return f(10) + f(20);                       // 期望 300
}

//--- 统计 ---
static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char* name) {
    if (ok) { ++g_pass; printf("  ok   %s\n", name); }
    else    { ++g_fail; printf("  FAIL %s\n", name); }
}
static bool luaOk(lua_State* L, const char* code) {
    bool ok = (luaL_dostring(L, code) == 0);
    if (!ok) { printf("    (err: %s)\n", lua_tostring(L, -1)); lua_pop(L, 1); }
    return ok;
}

int main() {
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    g_L = L;

    LuaModule G(L);
    G.fun("callTwiceFunctor", callTwiceFunctor);
    G.fun("callTwoCallbacks", callTwoCallbacks);
    G.fun("reentrant", reentrant);
    G.fun("countingVoid", countingVoid);
    G.fun("storeCallback", storeCallback);
    G.fun("invokeStored", invokeStored);
    G.fun("callN", callN);
    G.fun("stackTop", stackTop);
    G.fun("callTwiceFnPtr", callTwiceFnPtr);

    printf("== H1: std::function 回调多次调用 ==\n");
    check(luaOk(L, "assert(callTwiceFunctor(function(x) return x*10 end)==300)"),
          "回调被连调两次都生效 (f(10)+f(20)=300)");

    printf("== H2: 同签名多回调并存 ==\n");
    check(luaOk(L, "assert(callTwoCallbacks(function(x) return x end, function(x) return x*2 end)==1002)"),
          "两个同签名回调各自独立 (1002)");

    printf("== 可重入: 回调嵌套调用另一回调 ==\n");
    check(luaOk(L, "assert(reentrant(function(x) return x+100 end, function(x) return x*x end)==109)"),
          "inner(3)=9, outer(9)=109");

    printf("== 存储后延迟调用 ==\n");
    check(luaOk(L,
        "storeCallback(function(x) return x+1 end); storeCallback(function(x) return x*x end); "
        "assert(invokeStored(0,5)==6); assert(invokeStored(1,5)==25); "
        "assert(invokeStored(0,7)==8)"),
        "存两个回调, 跨多次调用反复触发正确");

    printf("== void 回调多次副作用 ==\n");
    check(luaOk(L,
        "local acc=0; countingVoid(function(x) acc=acc+x end); assert(acc==6)"),
        "countingVoid 触发 f(1)+f(2)+f(3), acc=6");

    printf("== 返回值与栈平衡 ==\n");
    check(luaOk(L,
        "local before=stackTop(); local s=callN(function(x) return x end, 50); "
        "local after=stackTop(); assert(s==(0+49)*50/2, 's='..s); assert(after==before, 'stack grew: '..before..'->'..after)"),
        "连调 50 次后结果正确且 lua 栈不增长");

    printf("== H1: 函数指针回调多次调用 ==\n");
    check(luaOk(L, "assert(callTwiceFnPtr(function(x) return x*10 end)==300)"),
          "函数指针回调连调两次都生效 (300)");

    printf("\n=== callbacks: %d passed, %d failed ===\n", g_pass, g_fail);
    g_stored.clear();
    lua_close(L);
    return g_fail > 0 ? 1 : 0;
}
