// 第六轮评审修复回归测试:
//   N1  tuple 入参栈平衡: vector/list/map 元素为 tuple、tuple 嵌套 tuple(非末尾)
//   N2  预存全局表 / 默认 _G 模块 property; LuaJIT(5.1) 模块 property 不再失效
//   N3  const T* 指针参数 (pointee cv 归一化)
//   N4  超对齐 (alignas(16)) 类 placement 构造对齐
//   N6  返回 std::function / 函数指针可调用; lua_State* 回调参数个数平衡
//   N7  同一 state 两个不同 C++ 类型绑定同一 lua 名 -> 报错; 同类型重绑定 -> OK
//   N9  跨 state def 未绑定类 -> 报错; 不可默认构造类 def(无 obj) -> 报错
//   S1  返回对象 full userdata 化: T* 别名(可调方法/类型检查)、T 按值拥有拷贝、
//       T& 别名、nullptr->nil、未绑定类指针回退 lightuserdata
#include "luaaa.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <list>
#include <map>
#include <vector>
#include <tuple>
#include <functional>
using namespace luaaa;

static int P = 0, F = 0;
static void check(bool ok, const char* n) {
    if (ok) { ++P; printf("  ok   %s\n", n); }
    else    { ++F; printf("  FAIL %s\n", n); }
}

// 执行一段 Lua 代码, 返回是否成功 (断言失败/抛错 => false)
static bool lua_ok(lua_State* L, const char* code) {
    if (luaL_dostring(L, code) != 0) {
        printf("    (lua err) %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    return true;
}

// 探测「受保护 C 绑定调用是否抛错」(绑定期 luaL_error 需 pcall 承接)
static bool bindRaises(lua_State* L, lua_CFunction fn) {
    lua_pushcfunction(L, fn);
    return lua_pcall(L, 0, 0, 0) != 0;
}

// ===================== 被测类型与函数 =====================
struct Point {
    static int s_alive;
    int x, y;
    Point(int a = 0, int b = 0) : x(a), y(b) { ++s_alive; }
    Point(const Point& o) : x(o.x), y(o.y) { ++s_alive; }
    ~Point() { --s_alive; }
    int sum() const { return x + y; }
    int getX() const { return x; }
    void setX(int v) { x = v; }
};
int Point::s_alive = 0;

// ---- N1: tuple 嵌套容器 ----
static int sumVecTuple(std::vector<std::tuple<int, int>> v) {
    int s = 0; for (auto& t : v) s += std::get<0>(t) + std::get<1>(t); return s;
}
static int sumListTuple(std::list<std::tuple<int, int>> v) {
    int s = 0; for (auto& t : v) s += std::get<0>(t) + std::get<1>(t); return s;
}
static int sumMapTuple(std::map<std::string, std::tuple<int, int>> m) {
    int s = 0; for (auto& kv : m) s += std::get<0>(kv.second) + std::get<1>(kv.second); return s;
}
static int nestedTuple(std::tuple<int, std::tuple<int, int>, int> t) {
    return std::get<0>(t) + std::get<0>(std::get<1>(t)) + std::get<1>(std::get<1>(t)) + std::get<2>(t);
}
static std::tuple<int, int> echoTuple(std::tuple<int, int> t) { return t; }

// ---- N2: 模块 property ----
static int g_preVal = 5;
static int getPre() { return g_preVal; }
static void setPre(int v) { g_preVal = v; }
static int g_gVal = 5;
static int getG() { return g_gVal; }
static void setG(int v) { g_gVal = v; }

// ---- N3: const T* 参数 ----
static int takeConstPtr(const Point* p) { return p ? p->x : -999; }

// ---- N4: 超对齐类型 ----
struct alignas(16) Aligned16 {
    double v;
    Aligned16(double x) : v(x) {}
    double get() const { return v; }
};

// ---- N6: 回调返回 / lua_State* 回调参数 ----
static std::function<int(int)> makeAdder(int k) { return [k](int x) { return x + k; }; }
static int mulImpl(int a, int b) { return a * b; }
static int (*getMul())(int, int) { return &mulImpl; }
static int feedStateCb(std::function<void(lua_State*, int)> cb, lua_State* L) {
    cb(L, 42); cb(L, 43);
    return 1;
}

// ---- N7: 同名冲突类型 ----
struct TypeA { int v; TypeA() : v(1) {} int get() const { return v; } };
struct TypeB { int v; TypeB() : v(2) {} int get() const { return v; } };
static int bindTypeB_SameName(lua_State* L) {
    LuaClass<TypeB> cls(L, "DupName");   // TypeA 已占用 "DupName" -> 应报错
    return 0;
}
static int bindTypeA_Again(lua_State* L) {
    LuaClass<TypeA> cls(L, "DupName");   // 同类型同名重绑定 -> 允许
    cls.ctor();
    cls.fun("get", &TypeA::get);
    return 0;
}

// ---- N9: def 校验 ----
struct NoDef { int v; NoDef(int x) : v(x) {} int get() const { return v; } };
struct TypeC { int v; TypeC() : v(4) {} };
static LuaClass<TypeC>* g_clsForeign = nullptr;   // 仅在另一个 state 中绑定
static int defForeignClass(lua_State* L) {
    // 该类未在 *当前* state 绑定 -> 应报错 (以前静默挂 nil metatable)
    LuaModule(L, "x").def("o", *g_clsForeign, (const TypeC*)nullptr);
    return 0;
}
static int defNoDef(lua_State* L) {
    LuaClass<NoDef> cls(L, "NoDef");
    LuaModule(L, "y").def("sing", cls);   // 无 obj 且不可默认构造 -> 应报错
    return 0;
}
static NoDef g_nd(9);
static int defNoDefWithObj(lua_State* L) {
    LuaClass<NoDef> cls(L, "NoDef");      // 同类型同名重绑定 -> 允许
    cls.fun("get", &NoDef::get);
    LuaModule(L, "y2").def("sing", cls, &g_nd);   // 显式 obj -> 正常
    return 0;
}

// ---- S1: 返回对象 ----
static Point g_pt(7, 8);
static Point*       ptrPoint()  { return &g_pt; }
static Point*       nullPoint() { return nullptr; }
static Point        valPoint(int x) { return Point(x, x + 1); }
static Point&       refPoint()  { return g_pt; }
static const Point& crefPoint() { return g_pt; }
static bool         samePoint(Point* a, Point* b) { return a == b; }
struct Unbound { int u; Unbound(int x) : u(x) {} };
static Unbound g_ub(5);
static Unbound* makeUnbound() { return &g_ub; }          // 未绑定类 -> lightuserdata 回退
static int      readUnbound(Unbound* p) { return p ? p->u : -1; }
struct Other { int z; Other() : z(3) {} int get() const { return z; } };

int main() {
    lua_State* L = luaL_newstate(); luaL_openlibs(L);

    LuaModule(L, "m")
        .fun("vt", sumVecTuple)
        .fun("lt", sumListTuple)
        .fun("mt", sumMapTuple)
        .fun("nt", nestedTuple)
        .fun("et", echoTuple)
        .fun("cptr", takeConstPtr)
        .fun("adder", makeAdder)
        .fun("getmul", getMul)
        .fun("feed", feedStateCb)
        .fun("ptr", ptrPoint)
        .fun("null", nullPoint)
        .fun("val", valPoint)
        .fun("ref", refPoint)
        .fun("cref", crefPoint)
        .fun("same", samePoint)
        .fun("mkunb", makeUnbound)
        .fun("readunb", readUnbound);

    {
        LuaClass<Point> cls(L, "Point");
        cls.ctor<int, int>("new");
        cls.fun("sum", &Point::sum).fun("getX", &Point::getX).fun("setX", &Point::setX);
    }
    // def 指针对象必须在类绑定之后 (绑定时未绑定的指针只能回退 lightuserdata)
    LuaModule(L, "m").def("gpt", &g_pt);
    {
        LuaClass<Aligned16> cls(L, "Aligned16");
        cls.ctor<double>("new");
        cls.fun("get", &Aligned16::get);
    }
    {
        LuaClass<Other> cls(L, "Other");
        cls.ctor();
        cls.fun("get", &Other::get);
    }

    // ===== N1: tuple 嵌套容器 (修复前: invalid key to 'next') =====
    printf("[N1: vector<tuple> 参数 — 修复前必报 invalid key to 'next']\n");
    check(lua_ok(L, "assert(m.vt({{1,2},{3,4},{5,6}}) == 21)"),
          "vector<tuple<int,int>> 6 元素求和 21");
    printf("[N1: list<tuple> / map<string,tuple> 参数]\n");
    check(lua_ok(L, "assert(m.lt({{1,1},{2,2},{3,3}}) == 12)"),
          "list<tuple<int,int>> 求和 12");
    check(lua_ok(L, "assert(m.mt({a={1,2}, b={3,4}}) == 10)"),
          "map<string,tuple<int,int>> 求和 10");
    printf("[N1: tuple 嵌套 tuple (非末尾位置)]\n");
    check(lua_ok(L, "assert(m.nt({10,{20,30},40}) == 100)"),
          "tuple<int, tuple<int,int>, int> 求和 100");
    printf("[N1: 顶层 tuple 往返 (回归保护)]\n");
    check(lua_ok(L, "local t = m.et({7,8}); assert(t[1] == 7 and t[2] == 8)"),
          "顶层 tuple 往返不变");
    printf("[N1: 短表容忍 (缺失元素保持默认值, 与 std::array 一致)]\n");
    check(lua_ok(L, "assert(m.vt({{1,2},{3}}) == 6)"),
          "短表元素 {3} -> (3,0), 总和 6");

    // ===== N2: 预存全局表的模块 property =====
    printf("[N2: Lua 侧预建表 PreM = {} 后再绑定 — property 应生效]\n");
    check(lua_ok(L, "PreM = {}"), "预建 PreM 表");
    {
        LuaModule(L, "PreM").get("prop", &getPre).set("prop", &setPre);
    }
    check(lua_ok(L, "assert(PreM.prop == 5)"), "预存表模块 property 读取 (getter 触发)");
    check(lua_ok(L, "PreM.prop = 17; assert(PreM.prop == 17)"), "预存表模块 property 写入");
    check(g_preVal == 17, "预存表 setter 真实写回 C++ 侧");

    // ===== N2: 默认 _G 模块 property (修复前静默失效) =====
    printf("[N2: 默认 LuaModule(L) 即 _G — property 应生效]\n");
    {
        LuaModule(L).get("gprop", &getG).set("gprop", &setG);
    }
    check(lua_ok(L, "assert(gprop == 5)"), "_G 模块 property 读取 (修复前为 nil)");
    check(lua_ok(L, "gprop = 9; assert(gprop == 9)"), "_G 模块 property 写入");
    check(g_gVal == 9, "_G setter 真实写回 C++ 侧");
    printf("[N2: _G 挂 metatable 后普通全局语义不受影响]\n");
    check(lua_ok(L, "assert(undefined_global_xyz == nil); brand_new_global = 123; assert(brand_new_global == 123)"),
          "未定义全局读 nil / 普通全局赋值正常");

    // ===== N3: const T* 参数 =====
    printf("[N3: const Point* 参数 (修复前误报 cpp pointer expected)]\n");
    check(lua_ok(L, "assert(m.cptr(Point.new(11, 22)) == 11)"),
          "const Point* 接收绑定对象");

    // ===== N4: 超对齐类型构造 =====
    printf("[N4: alignas(16) 类 placement 构造 (UBSan 对齐检查)]\n");
    check(lua_ok(L, "assert(Aligned16.new(3.5):get() == 3.5)"),
          "alignas(16) 类构造与方法调用");

    // ===== N6: 返回回调可调用 =====
    printf("[N6: 返回 std::function — 修复前 put 非 static/无 upvalue, 不可用]\n");
    check(lua_ok(L, "local a = m.adder(40); assert(a(2) == 42)"),
          "返回的 std::function 可从 Lua 调用");
    printf("[N6: 返回函数指针]\n");
    check(lua_ok(L, "assert(m.getmul()(3, 4) == 12)"),
          "返回的函数指针可从 Lua 调用");
    printf("[N6: lua_State* 回调参数个数平衡]\n");
    check(lua_ok(L, "g_cnt = 0; m.feed(function(h, v) assert(v == 42 or v == 43); g_cnt = g_cnt + 1 end); assert(g_cnt == 2)"),
          "带 lua_State* 形参的回调两次调用无栈错乱");

    // ===== N7: 两类型同名冲突 =====
    printf("[N7: 同一 state 两类型绑定同一 lua 名 — 应拒绝]\n");
    {
        LuaClass<TypeA> clsA(L, "DupName");
        clsA.ctor();
        clsA.fun("get", &TypeA::get);
    }
    check(bindRaises(L, bindTypeB_SameName), "TypeB 抢注 'DupName' 报错 (修复前静默覆盖+类型混淆)");
    check(lua_ok(L, "assert(DupName.new():get() == 1)"), "冲突被拒后原 metatable 未被破坏");
    check(!bindRaises(L, bindTypeA_Again), "同类型同名重绑定允许");
    check(lua_ok(L, "assert(DupName.new():get() == 1)"), "重绑定后对象仍正常");

    // ===== N9: def 校验 =====
    printf("[N9: 跨 state def 未绑定类 — 应报错]\n");
    {
        lua_State* L1 = luaL_newstate(); luaL_openlibs(L1);
        g_clsForeign = new LuaClass<TypeC>(L1, "TypeC");
        check(bindRaises(L, defForeignClass), "def 未在此 state 绑定的类报错 (修复前静默 nil metatable)");
        lua_close(L1);
    }
    printf("[N9: 不可默认构造类 def 无 obj — 应报错]\n");
    check(bindRaises(L, defNoDef), "def 空单例报错 (修复前注册空对象, 访问时才报错)");
    check(!bindRaises(L, defNoDefWithObj), "def 显式 obj 正常");
    check(lua_ok(L, "assert(y2.sing:get() == 9)"), "显式 obj 单例方法调用");

    // ===== S1: 返回对象 full userdata =====
    printf("[S1: 返回 T* — 非拥有别名, 方法可调, 身份一致]\n");
    check(lua_ok(L, "local p = m.ptr(); assert(type(p) == 'userdata'); assert(p:getX() == 7)"),
          "返回指针可调方法 (修复前是 lightuserdata, 无 metatable)");
    check(lua_ok(L, "assert(m.same(m.ptr(), m.ptr()))"), "两次返回同一对象指针相等");
    printf("[S1: 指针别名 — 经一个别名修改, 另一个别名可见]\n");
    check(lua_ok(L, "local p = m.ptr(); p:setX(70); assert(m.ref():getX() == 70)"),
          "T*/T& 别名共享同一 C++ 对象");
    printf("[S1: 返回 T (按值) — 拥有拷贝, GC 析构]\n");
    {
        // 先全量 GC 再取基线 (前面用例构造的对象尚未回收, 会一并计入基线)
        lua_ok(L, "collectgarbage(); collectgarbage()");
        const int alive0 = Point::s_alive;
        check(lua_ok(L, "local v = m.val(1); assert(v:getX() == 1 and v:sum() == 3); v:setX(50); assert(v:getX() == 50)"),
              "按值返回的对象方法/修改正常 (拥有拷贝)");
        check(lua_ok(L, "assert(m.ptr():getX() == 70)"), "按值拷贝不影响原对象");
        check(lua_ok(L, "for i=1,3 do local t = m.val(i) end; collectgarbage(); collectgarbage()"),
              "按值对象 GC 回收");
        check(Point::s_alive == alive0, "GC 后存活计数回到基线 (无泄漏)");
    }
    printf("[S1: 返回 const T& — 只读访问]\n");
    check(lua_ok(L, "assert(m.cref():getX() == 70)"), "const T& 返回可读");
    printf("[S1: 返回 nullptr — 应为 nil]\n");
    check(lua_ok(L, "assert(m.null() == nil)"), "nullptr -> nil (跨版本一致)");
    printf("[S1: 类型检查 — 错类型对象被拒绝]\n");
    check(lua_ok(L, "local ok = pcall(function() return m.cptr(Other.new()) end); assert(not ok)"),
          "Other 对象传给 const Point* 报错");
    printf("[S1: 未绑定类指针 — 回退 lightuserdata, 往返可用]\n");
    check(lua_ok(L, "assert(m.readunb(m.mkunb()) == 5)"), "未绑定类指针 lightuserdata 往返");
    printf("[S1: def 指针值 — 带类型 userdata, 方法可调]\n");
    check(lua_ok(L, "assert(m.gpt:getX() == 70)"), "def 的指针对象方法可调");

    lua_close(L);
    printf("\n=== review3: %d passed, %d failed ===\n", P, F);
    return F > 0 ? 1 : 0;
}
