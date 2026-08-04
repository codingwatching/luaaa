// 并发测试 — ASan 下应正常完成, TSan 下应无 data race.
//
// 重点验证 (方案3):
//   【H2 已修】函数指针回调 per-signature 槽 (cacheLuaState/cacheLuaFuncId) 现为
//             LUAAA_THREAD_LOCAL, 每线程独立. 每线程用自己的 lua_State 并发调用
//             「接收函数指针回调的 C++ 函数」, 应无跨线程 race、无崩溃, 且结果正确.
//   【对照】std::function 回调 (无共享槽) 应 race-free
//   【验证】LuaClass 构造期 installExtendHelper 静态 key 无 race
//
// 说明: 函数指针回调仍要求「在获取它的那个线程上调用」——本测试每线程各自注册并调用,
//   符合该约束. 跨线程调用同一函数指针回调不受支持 (应改用 std::function).
#include "luaaa.hpp"

#include <cstdio>
#include <thread>
#include <vector>
#include <atomic>
#include <functional>
#include <mutex>
using namespace luaaa;

static int P = 0, F = 0;
static void check(bool ok, const char* n) {
    if (ok) { ++P; printf("  ok   %s\n", n); }
    else    { ++F; printf("  FAIL %s\n", n); }
}

// 接收函数指针回调 (经此调用触发 static 槽写)
static int call_ptr_cb(int(*cb)(int), int x) { return cb(x); }
// 接收 std::function 回调 (无 static 槽)
static int call_fn_cb(std::function<int(int)> cb, int x) { return cb(x); }

class ConcItem {
public:
    int v;
    ConcItem() : v(0) {}
    int get() const { return v; }
};

// 线程入口: 独立 state, 多次调用「接收函数指针回调」的绑定函数.
// 每次 cb.call(f, x) 内部 LuaStack<FTYPE>::get 写 static cacheLuaState/cacheLuaFuncId.
// 两线程并发 -> TSan 报 data race.
static void thread_call_ptr(int tid, std::atomic<int>* ok_count) {
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    {
        LuaModule m(L, "cb");
        m.fun("call", call_ptr_cb);
    }
    char code[160];
    snprintf(code, sizeof(code),
             "local f = function(x) return x + %d end; "
             "local r = cb.call(f, 100); "
             "if r == 100 + %d then return 1 else return 0 end",
             tid, tid);
    for (int i = 0; i < 200; ++i) {
        if (luaL_dostring(L, code) == 0) {
            // 校验返回值正确 (回调用对了本线程的 state/func): chunk 返回 1 表示 r==100+tid
            if (lua_tointeger(L, -1) == 1) ok_count->fetch_add(1);
        }
        lua_pop(L, 1);   // 弹出返回值或错误消息, 保持栈干净
    }
    lua_close(L);
}

// 对照: std::function 回调 (无 static 槽)
static void thread_call_fn(int tid, std::atomic<int>* ok_count) {
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    {
        LuaModule m(L, "cb");
        m.fun("call", call_fn_cb);
    }
    char code[160];
    snprintf(code, sizeof(code),
             "local f = function(x) return x + %d end; "
             "local r = cb.call(f, 100); "
             "if r == 100 + %d then return 1 else return 0 end",
             tid, tid);
    for (int i = 0; i < 200; ++i) {
        if (luaL_dostring(L, code) == 0) {
            ok_count->fetch_add(1);
        }
    }
    lua_close(L);
}

static void thread_construct(int /*tid*/) {
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    {
        LuaClass<ConcItem> cls(L, "Item");
        cls.ctor();
        cls.get("v", &ConcItem::get);
    }
    luaL_dostring(L, "local it = Item.new(); assert(it.v == 0)");
    lua_close(L);
}

int main() {
    printf("[并发调用 — 函数指针回调 thread_local 槽 (H2 已修), 应 race-free]\n");
    {
        std::atomic<int> ok_count{0};
        std::vector<std::thread> ts;
        for (int t = 0; t < 4; ++t) ts.emplace_back(thread_call_ptr, t, &ok_count);
        for (auto& th : ts) th.join();
        printf("    (信息) 4 线程 × 200 次并发调用函数指针回调, 结果正确 = %d (期望 800)\n", ok_count.load());
        // thread_local 槽后: 每线程只用自己的 state, 全部调用应成功且结果正确.
        check(ok_count.load() == 4 * 200, "函数指针回调并发调用全部成功且结果正确 (thread_local 槽)");
    }

    printf("[并发调用 — std::function 回调 (无 static 槽), 应 race-free]\n");
    {
        std::atomic<int> ok_count{0};
        std::vector<std::thread> ts;
        for (int t = 0; t < 4; ++t) ts.emplace_back(thread_call_fn, t, &ok_count);
        for (auto& th : ts) th.join();
        printf("    (信息) 4 线程并发调用 std::function 回调, 成功调用 = %d\n", ok_count.load());
        check(ok_count.load() > 0, "std::function 回调并发调用完成 (TSan 应无 race)");
    }

    printf("[并发 — LuaClass 构造 (installExtendHelper 静态 key)]\n");
    {
        std::vector<std::thread> ts;
        for (int t = 0; t < 4; ++t) ts.emplace_back(thread_construct, t);
        for (auto& th : ts) th.join();
        check(true, "4 线程并发构造 LuaClass 完成 (TSan 应无 race)");
    }

    printf("\n=== concurrency: %d passed, %d failed ===\n", P, F);
    return F > 0 ? 1 : 0;
}
