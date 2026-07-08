// 针对本轮修复(除 H1/H2 外)的专项回归测试：
//   M2  std::function 存储改 placement new 后仍正常
//   L1  std::array 短表/正常/超长表边界
//   L3  property getter / 用户 __index 内部报错应向上传播(而非被吞)
#include "luaaa.hpp"

#include <array>
#include <string>
#include <functional>
#include <cstdio>

using namespace luaaa;

//--- L1: array 边界 ---
static int arraySum3(std::array<int,3> a) { return a[0] + a[1] + a[2]; }

//--- L3: 带一个会抛错的用户 __index 的类 ---
struct Widget { int x = 1; int getX() const { return x; } };

//--- M2b: holder __gc 会析构存储的 std::function ---
// 带静态存活计数：构造/拷贝 +1，析构 -1。按值捕获进 std::function，
// 若 holder userdata 没有 __gc，lua_close 后 alive 不会归零(资源泄漏)。
struct Tracker {
    static int alive;
    Tracker()               { ++alive; }
    Tracker(const Tracker&) { ++alive; }
    ~Tracker()              { --alive; }
};
int Tracker::alive = 0;

struct Gadget { int v = 0; };

static int throwingIndex(lua_State* s) {
    return luaL_error(s, "boom-from-index");
}

//--- 测试统计 ---
static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char* name) {
    if (ok) { ++g_pass; printf("  ok   %s\n", name); }
    else    { ++g_fail; printf("  FAIL %s\n", name); }
}
static bool luaOk(lua_State* L, const char* code) {         // 期望成功
    bool ok = (luaL_dostring(L, code) == 0);
    if (!ok) { printf("    (err: %s)\n", lua_tostring(L, -1)); lua_pop(L, 1); }
    return ok;
}
static bool luaErrHas(lua_State* L, const char* code, const char* needle) {  // 期望失败且含 needle
    if (luaL_dostring(L, code) == 0) return false;
    std::string e = lua_tostring(L, -1); lua_pop(L, 1);
    return e.find(needle) != std::string::npos;
}

int main() {
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    // 多个 std::function lambda(不同捕获) 验证 placement new 存储正确 (M2)
    int base = 100;
    LuaModule G(L);
    G.fun("arraySum3", arraySum3);
    G.fun("lamAdd",  std::function<int(int,int)>([](int a, int b){ return a + b; }));
    G.fun("lamCap",  std::function<int(int)>([base](int a){ return a + base; }));
    G.fun("lamStr",  std::function<std::string(std::string)>([](std::string s){ return s + "!"; }));

    LuaClass<Widget> w(L, "Widget");
    w.ctor();
    w.fun("getX", &Widget::getX);
    w.fun("__index", throwingIndex);   // 用户自定义 __index，内部抛错

    printf("== M2: std::function 存储(placement new) ==\n");
    check(luaOk(L, "assert(lamAdd(3,4)==7)"),           "lamAdd 无捕获 lambda");
    check(luaOk(L, "assert(lamCap(5)==105)"),           "lamCap 值捕获 lambda");
    check(luaOk(L, "assert(lamStr('hi')=='hi!')"),      "lamStr 返回 string lambda");

    printf("== L1: std::array 边界 ==\n");
    check(luaOk(L, "assert(arraySum3({10,20,30})==60)"), "正常 3 元素");
    check(luaOk(L, "assert(arraySum3({7,8})==15)"),      "短表(第3项零初始化)=7+8+0");
    check(luaOk(L, "assert(arraySum3({1,2,3,4,5})==6)"), "超长表只取前 3 项");
    check(luaOk(L, "arraySum3({7,8,9}); assert(arraySum3({1,1,1})==3)"), "连续调用后栈无残留错乱");

    printf("== L3: getter/__index 内部报错向上传播 ==\n");
    // 访问不存在的属性 -> 触发用户 __index -> 内部 luaL_error -> 应传播
    check(luaErrHas(L, "local o=Widget.new(); return o.nope", "boom-from-index"),
          "用户 __index 抛错被传播(不再被吞没)");
    // 正常成员仍可用
    check(luaOk(L, "local o=Widget.new(); assert(o:getX()==1)"), "正常成员不受影响");

    printf("== M2b: holder __gc 析构 std::function(不泄漏捕获) ==\n");
    {
        // 独立子 state：注册按值捕获 Tracker 的模块函数与类函数各一，
        // 覆盖 _registerModuleFunction 与 _registerClassFunction 两条存储路径。
        lua_State* L2 = luaL_newstate();
        luaL_openlibs(L2);
        {
            Tracker t;   // 基准存活对象
            LuaModule G2(L2);
            G2.fun("modCap", std::function<int(int)>([t](int a){ (void)t; return a; }));

            LuaClass<Gadget> g(L2, "Gadget");
            g.ctor();
            g.fun("clsCap", std::function<int(Gadget&)>([t](Gadget&){ (void)t; return 1; }));
        } // 局部 t 析构后，仍存活的 Tracker 拷贝都保存在两个 std::function 里
        check(Tracker::alive > 0, "注册后 std::function 捕获的 Tracker 存活");
        lua_close(L2);
        check(Tracker::alive == 0, "lua_close 后 holder __gc 已析构全部捕获");
    }

    printf("\n=== fixes: %d passed, %d failed ===\n", g_pass, g_fail);
    lua_close(L);
    return g_fail > 0 ? 1 : 0;
}
