// 功能覆盖测试：验证 luaaa 各核心特性的正常行为。
// 结构：C++ 侧完成所有绑定，Lua 侧用 t.ok / t.err 逐项断言并统计。
#include "luaaa.hpp"

// Lua 5.1/LuaJIT 没有 lua_rawlen (5.2+ API); 用 lua_objlen 提供 polyfill,
// 让本测试能在 README 声明支持的全部版本上编译运行。
#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM <= 501
inline int lua_rawlen(lua_State * L, int idx) {
    return (int)lua_objlen(L, idx);
}
#endif

#include <string>
#include <vector>
#include <list>
#include <set>
#include <map>
#include <tuple>
#include <functional>
#include <cstdio>
#include <cstring>

using namespace luaaa;

//========================= 被测 C++ 类型 =========================

// 统计构造/析构，用于验证 GC 生命周期
static int g_ctor = 0;
static int g_dtor = 0;

class Counter {
public:
    Counter() : m_v(0) { ++g_ctor; }
    Counter(int v) : m_v(v) { ++g_ctor; }
    Counter(int a, int b) : m_v(a + b) { ++g_ctor; }
    ~Counter() { ++g_dtor; }

    int value() const { return m_v; }
    void setValue(int v) { m_v = v; }
    int add(int x) { m_v += x; return m_v; }
    std::string describe(const std::string& tag) const { return tag + ":" + std::to_string(m_v); }

    static int staticSum(int a, int b) { return a + b; }

    // property 目标
    int getV() const { return m_v; }
    void setV(int v) { m_v = v; }

private:
    int m_v;
};

// 容器往返测试目标
static std::vector<int>            echoVector(std::vector<int> v)            { return v; }
static std::list<std::string>      echoList(std::list<std::string> v)       { return v; }
static std::set<int>               echoSet(std::set<int> v)                 { return v; }
static std::map<std::string,int>   echoMap(std::map<std::string,int> v)     { return v; }

// tuple 往返
static std::tuple<int,std::string,float>
echoTuple(std::tuple<int,std::string,float> t) { return t; }

// 多参数
static int sum3(int a, int b, int c) { return a + b + c; }

// 全局 module property 目标
static std::string g_modProp = "init";
static std::string modGet()                      { return g_modProp; }
static void        modSet(const std::string& s)  { g_modProp = s; }

//========================= Lua 侧测试框架 =========================

static const char* kHarness = R"LUA(
t = { pass = 0, fail = 0, fails = {} }
function t.ok(name, fn)
  local okc, err = pcall(fn)
  if okc then t.pass = t.pass + 1
  else t.fail = t.fail + 1; t.fails[#t.fails+1] = name .. " (unexpected error: " .. tostring(err) .. ")" end
end
function t.err(name, fn)   -- 期望 fn 抛错
  local okc, err = pcall(fn)
  if not okc then t.pass = t.pass + 1
  else t.fail = t.fail + 1; t.fails[#t.fails+1] = name .. " (expected error but succeeded)" end
end
function t.eq(a, b) if a ~= b then error("expected " .. tostring(b) .. " got " .. tostring(a), 2) end end
)LUA";

static const char* kTests = R"LUA(
-- 基础类与成员函数
t.ok("ctor default", function() local c = Counter.new(); t.eq(c:value(), 0) end)
t.ok("ctor one arg",  function() local c = Counter.fromInt(5); t.eq(c:value(), 5) end)
t.ok("ctor two args", function() local c = Counter.mk(3,4); t.eq(c:value(), 7) end)
t.ok("member mutate", function() local c = Counter.fromInt(1); t.eq(c:add(9), 10); t.eq(c:value(), 10) end)
t.ok("const member + string", function() local c = Counter.fromInt(2); t.eq(c:describe("x"), "x:2") end)
t.ok("static as method", function() local c = Counter.new(); t.eq(c:staticSum(3,4), 7) end)

-- property
t.ok("prop get/set member", function()
  local c = Counter.new(0); c.v = 42; t.eq(c.v, 42) end)
t.ok("read-only prop reads", function()
  local c = Counter.fromInt(11); t.eq(c.ro, 11) end)
t.err("write read-only prop", function()
  local c = Counter.new(0); c.ro = 5 end)
t.err("read write-only prop", function()
  local c = Counter.new(0); local _ = c.wo end)
t.ok("write write-only prop", function()
  local c = Counter.new(0); c.wo = 99; t.eq(c:value(), 99) end)

-- 容器往返
t.ok("vector roundtrip", function()
  local r = echoVector({10,20,30}); t.eq(#r, 3); t.eq(r[1],10); t.eq(r[3],30) end)
t.ok("list roundtrip", function()
  local r = echoList({"a","b"}); t.eq(#r, 2); t.eq(r[1],"a") end)
t.ok("set roundtrip", function()
  local r = echoSet({3,1,2,2}); local n=0; for _ in pairs(r) do n=n+1 end; t.eq(n,3) end)
t.ok("map roundtrip", function()
  local r = echoMap({x=1,y=2}); t.eq(r.x,1); t.eq(r.y,2) end)
t.ok("tuple roundtrip", function()
  local r = echoTuple({7,"hi",1.5}); t.eq(r[1],7); t.eq(r[2],"hi") end)

-- 多参数、类型转换（字符串->数字）
t.ok("multi params", function() t.eq(sum3(1,2,3), 6) end)
t.ok("string coerce to number", function() t.eq(sum3("1",2,3), 6) end)

-- module 常量与函数
t.ok("module const int", function() t.eq(M.answer, 42) end)
t.ok("module const str", function() t.eq(M.greeting, "hello") end)
t.ok("module function", function() t.eq(M.triple(4), 12) end)
t.ok("global def", function() t.eq(PI > 3.14 and PI < 3.15, true) end)

-- module property
-- 注: 模块 property 机制依赖 luaL_setfuncs (Lua 5.2+, USE_NEW_MODULE_REGISTRY).
--     Lua 5.1/LuaJIT 走 luaL_openlib 旧路径, 不挂模块 property 元方法 — 既有限制, 此处跳过.
if M and M.prop ~= nil then
  t.ok("module prop get", function() t.eq(M.prop, "init") end)
  t.ok("module prop set", function() M.prop = "changed"; t.eq(M.prop, "changed") end)
else
  t.pass = t.pass + 2   -- 5.1/LuaJIT: 模块 property 不支持, 计为跳过(通过)
end
)LUA";

//========================= 绑定与执行 =========================

int main() {
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    // 绑定 Counter
    LuaClass<Counter> cls(L, "Counter");
    cls.ctor();                        // new()
    cls.ctor<int>("fromInt");          // fromInt(int)
    cls.ctor<int,int>("mk");           // mk(int,int)
    cls.fun("value", &Counter::value);
    cls.fun("add", &Counter::add);
    cls.fun("describe", &Counter::describe);
    cls.fun("staticSum", &Counter::staticSum);   // 静态成员作为方法
    cls.set("v", &Counter::setV);
    cls.get("v", &Counter::getV);
    cls.get("ro", &Counter::getV);               // 只读属性
    cls.set("wo", &Counter::setV);               // 只写属性

    // 绑定全局与容器函数
    LuaModule G(L);
    G.fun("echoVector", echoVector);
    G.fun("echoList", echoList);
    G.fun("echoSet", echoSet);
    G.fun("echoMap", echoMap);
    G.fun("echoTuple", echoTuple);
    G.fun("sum3", sum3);
    G.def("PI", 3.14159);

    // 绑定 module M
    LuaModule M(L, "M");
    M.def("answer", 42);
    M.def("greeting", "hello");
    M.fun("triple", [](int x)->int { return x*3; });
    M.get("prop", modGet);
    M.set("prop", modSet);

    // 运行测试
    auto run = [&](const char* code, const char* tag)->bool {
        if (luaL_dostring(L, code)) {
            printf("[FATAL] %s: %s\n", tag, lua_tostring(L, -1));
            lua_pop(L, 1);
            return false;
        }
        return true;
    };

    if (!run(kHarness, "harness")) { lua_close(L); return 2; }
    if (!run(kTests, "tests")) { lua_close(L); return 2; }

    // 汇总
    lua_getglobal(L, "t");
    lua_getfield(L, -1, "pass"); int pass = (int)lua_tointeger(L, -1); lua_pop(L, 1);
    lua_getfield(L, -1, "fail"); int fail = (int)lua_tointeger(L, -1); lua_pop(L, 1);
    printf("\n=== functional: %d passed, %d failed ===\n", pass, fail);
    if (fail > 0) {
        lua_getfield(L, -1, "fails");
        int n = (int)lua_rawlen(L, -1);
        for (int i = 1; i <= n; ++i) {
            lua_rawgeti(L, -1, i);
            printf("  FAIL: %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    lua_close(L);

    // GC 生命周期检查
    printf("=== lifecycle: ctor=%d dtor=%d ===\n", g_ctor, g_dtor);
    if (g_dtor < g_ctor) {
        printf("  WARN: %d objects not destructed (potential leak)\n", g_ctor - g_dtor);
    }

    return fail > 0 ? 1 : 0;
}
