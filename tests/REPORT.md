# luaaa 系统性测试与潜在 Bug 排查报告

- **被测对象**：`luaaa.hpp`（v1.5，header-only）
- **测试范围**：7 个原有测试文件 + 9 个新增测试文件（见下）
- **测试环境**：macOS 15（arm64），`g++` / `clang++`，`-std=c++11`，`-fsanitize=address,undefined` / `-fsanitize=thread`
- **Lua 版本矩阵**：LuaJIT 2.1（5.1 API）、Lua 5.4.8、Lua 5.5.0
- **方法**：C++ 侧完成绑定 + Lua 侧 `pcall` 断言；对「正确行为」断言，FAIL 即为 bug 实证。全程 ASan/UBSan/TSan 监控。
- **运行**：`bash tests/run.sh all`（ASan+UBSan 全版本矩阵）；`SAN_MODE=tsan CXX=clang++ bash tests/run.sh all`（TSan）。详细日志见 `tests/results.txt`、`tests/tsan_report.txt`。

---

## 零、修复验证更新（最新）

本轮报告原发现的全部 **高危 3 项 + 中危 4 项** 及 **低危 L1/L2/L9** 已实施修复并重新验证。**LuaJIT/5.4/5.5 × ASan+UBSan 全矩阵 456 断言全部通过（0 FAIL、0 编译失败）；TSan 0 data race。** 修复细节见各缺陷条目末尾「✅ 修复与验证」。

| 编号 | 问题 | 状态 |
|---|---|---|
| H1 | 类 `__index` 返回值丢失 | ✅ 已修复（`nresults: 0→1`），`IdxBug.new().nonexistent`→999 |
| H2 | 构造函数抛 C++ 异常致宿主崩溃 | ✅ 已修复（调用边界 `try/catch → TranslateCppException → lua_error`），子进程退出码 0 |
| H3 | 函数指针回调线程不安全 | ✅ 已修复（`static`→`thread_local`），TSan 0 race |
| M1 | 整数 get 对非数字字符串静默返回 0 | ✅ 已修复（改用 `lua_tointegerx` 校验），`echo_ll('hello')`/`echo_char('A')` 报错 |
| M2 | 整数 put 无溢出检测 | ✅ 已修复（与浮点对称的 `lua_Integer` 范围检查），`echo_ull(ULLONG_MAX)` 报错 |
| M3 | `__index`/`__newindex` 拒绝非字符串 key | ✅ 已修复（`luaL_checkstring`→`IndexKeyToString`），`obj[{}]` 返回 nil |
| M4 | `T*` 传 nil 静默返回 nullptr | ✅ 已修复（补 `luaL_argcheck`），`ptr_addr(nil)` 报错 |
| L1/L2 | bool/char/`const char*` 强转不一致 | ✅ 已修复（统一为 **严格 Lua 类型语义**，见 L1 详述） |
| L9 | `test_functional.cpp` 用 5.2+ API（LuaJIT 编译失败） | ✅ 已修复（补 `lua_rawlen` polyfill） |

**核心结论**：常规功能路径与所有原报问题在 3 个版本下全部正常，ASan/UBSan/TSan 无报错。类型转换三特化（bool/int/`const char*`）已**统一为严格 Lua 类型语义**：bool 仅接受 Lua `boolean`（严格派，拒绝 number/string 的隐式强转以暴露脚本侧类型错误），int 对齐 Lua 算术（`true+1` 报错→`int(true)` 报错），`const char*` 等价 Lua `tostring()`。

---

## 零-续、第二轮扩展测试与新发现（最新）

在首轮 16 个测试文件基础上，新增 **8 个测试文件**（共 80 项新断言），覆盖此前未测的分支：运算符元方法、重载覆盖、`lua_State*` 注入、`lua_CFunction` 注册、`std::tuple` 返回、浮点边界、容器边界、非构造函数异常、字符串 NUL/UTF-8、数组 `def`、跨 state registry、forward_list 保序。**发现 4 类新缺陷（1 高危 + 2 中危 + 1 低危）**，全部已实证并修复。

| 编号 | 问题 | 严重度 | 状态 |
|---|---|---|---|
| **H4** | **成员函数抛 C++ 异常致进程崩溃（SIGABRT）** | 高危 | ✅ 已修复（`LuaInvokeInstanceMemberImpl` 补 try/catch，子进程退出码 0） |
| M5 | 浮点 get 对非数字字符串静默返回 0.0（与已修 M1 整数路径不对称） | 中危 | ✅ 已修复（改用 `lua_tonumberx` 校验 isnum，`echo_f('hello')` 报错） |
| M6 | `std::forward_list` 特化误用 `push_back`，作为函数参数直接编译失败 | 中危 | ✅ 已修复（改用 `insert_after(before_begin())` 保序插入） |
| L10 | 容器 `luaL_argcheck(..., 1, ...)` 硬编码参数位置（15 处诊断消息错位） | 低危 | ✅ 已修复（改为 `idx`，共 15 处） |

**矩阵结果**（`CXX=clang++ bash tests/run.sh all`，3 版本 × 26 文件）：
- **总断言 735 通过，0 失败，0 编译失败**（含新增 `test_forward_list.cpp`）
- **TSan 0 data race**（`SAN_MODE=tsan` 并发测试全绿，无回归）
- 修复细节见各缺陷条目末尾「✅ 修复与验证」

### 【高危 H4】成员函数抛 C++ 异常致进程崩溃（H2 修复的遗漏路径）

- **复现**：`tests/test_exception.cpp` → `[成员函数抛 std::runtime_error]` 子进程退出码 `-6`（SIGABRT）
- **涉及代码**：`luaaa.hpp:1090-1093`（`LuaInvokeInstanceMemberImpl`）
```cpp
template<...>
TRET LuaInvokeInstanceMemberImpl(lua_State* state, void* calleePtr, indices<Ns...>)
{
    return (LuaStack<TCLASS>::get(state, 1).**(FTYPE*)(calleePtr))(LuaStack<ARGS>::get(state, Ns + 2)...);
    // ← 无 try/catch! 异常穿过 extern "C" 边界 -> std::terminate -> SIGABRT
}
```
- **根因**：H2 修复（`TranslateCppException`）只覆盖了**构造函数路径**（`PlacementConstructorCaller::InvokeImpl`）和**非成员函数路径**（`LuaInvokeImpl:977` 有 try/catch），**漏掉了成员函数路径**。`LuaInvokeInstanceMemberImpl`（`luaaa.hpp:1090`）调用成员函数时无任何异常保护。
- **实测**（5.5）：绑定 `cls.fun("calc", &Boom::throwRuntime)`（const 成员函数），Lua 调用 `Boom.new():calc(-1)` 触发 `throw std::runtime_error` → 子进程退出码 `-6`（SIGABRT），stderr：`libc++abi: terminating due to uncaught exception`。即使 `pcall` 包裹仍崩溃（C++ 异常先于 pcall 穿过边界）。
- **对照**：**非成员函数**（自由函数/static）路径正常——`LuaInvokeImpl:977` 有 try/catch，`echo_div(0)` 抛 `std::logic_error` 被 `TranslateCppException` 翻译为 `lua_error: function call failed: zero not allowed`，pcall 成功捕获。两组形成鲜明对照，定位精确到成员 vs 非成员调用边界。
- **版本差异**：LuaJIT 下**不崩溃**（退出码 0）——LuaJIT 内置 C++ 异常交互捕获了异常（与 H2 同）。但标准 Lua 5.4/5.5 崩溃。
- **影响**：任何绑定的成员方法（const/非 const）若可能抛异常（很常见：参数校验、资源获取失败），会**杀死整个宿主进程**而非向 Lua 报错。比 H2 影响面更广——构造函数抛异常相对少见，但成员方法抛异常是日常场景。
- **建议**：在 `LuaInvokeInstanceMemberImpl`（`luaaa.hpp:1090`）包 `try/catch → TranslateCppException(state, "member function call")`，与 `LuaInvokeImpl` 对称。一行修复。

#### ✅ 修复与验证（H4）
在 `LuaInvokeInstanceMemberImpl`（`luaaa.hpp:1090`）的成员函数调用外包 `#if LUAAA_HAS_EXCEPTIONS try #endif { ... } #if LUAAA_HAS_EXCEPTIONS catch (...) { TranslateCppException(state, "member function call"); } #endif`，与非成员路径 `LuaInvokeImpl`（`luaaa.hpp:977`）**完全对称**。守卫方式一致（`-fno-exceptions` 下编译为空，embedded 模式零影响）。参数提取的 `luaL_error`（longjmp，非 C++ 异常）不会被 catch 捕获，与非成员路径行为一致。**验证**：`test_exception.cpp` 成员函数抛 `std::runtime_error` 子进程退出码从 `-6`(SIGABRT) → **0**（正常退出），pcall 捕获到 `luaaa: member function call failed: negative input`；`throw 42`（非 std::exception）也被兜底 catch 翻译为 lua_error；自由函数对照组保持通过。`test_exception` 从 4/3 → **7/0 全通过**。3 个版本 + TSan 无回归。

### 【中危 M5】浮点 get 对非数字字符串静默返回 0.0（与已修 M1 不对称）

- **复现**：`tests/test_float_edge.cpp` → `FAIL echo_f('hello') 应报错`
- **涉及代码**：`luaaa.hpp:526`（浮点 `get`）
```cpp
if (lua_isnumber(L, idx) || lua_isstring(L, idx)) {   // ← lua_isstring 对任意字符串为真
    const lua_Number v = lua_tonumber(L, idx);        // ← 非数字字符串返回 0
    ...                                                // ← 无 isnum 校验
}
```
- **根因**：浮点 `get` 分支条件用 `lua_isstring(L, idx)`（任意字符串都为真），与**已修复的整数 M1 路径**（`luaaa.hpp:619` 用 `lua_type==LUA_TSTRING && lua_isnumber` + `lua_tointegerx` 校验）**不对称**。整数 `echo_ll('hello')` 已报错，但浮点 `echo_f('hello')` 静默返回 `0.0`。
- **实测**（5.5）：`echo_f('hello')` → `0.0`（`raises=0`），应报错。`echo_f('3.14')` 数字字符串仍正常（回归保护通过）。
- **影响**：把字符串误传给浮点参数时，得到静默的 0.0 而非清晰的类型错误，掩盖调用方 bug。与整数路径行为不一致。
- **建议**：浮点 `get` 改为与整数对称：`lua_type==LUA_TNUMBER || (LUA_TSTRING && lua_isnumber)`，并用 `lua_tonumberx(L,idx,&isnum)` 校验。

#### ✅ 修复与验证（M5）
浮点特化 `get`（`luaaa.hpp:524`）改为与整数特化（M1）完全对称的模式：用 `lua_type(L,idx)` 判定 `LUA_TNUMBER || (LUA_TSTRING && lua_isnumber)`，并用 `lua_tonumberx(L,idx,&isnum)` 校验转换成功，`isnum==0` 则 `luaL_error("bad number (not a valid floating-point value)")`。原有溢出检查（`lv > hi || lv < -hi`）保持不变。**验证**：`echo_f('hello')` 从静默返回 `0.0` → **报错**（`raises=1`）；数字字符串 `echo_f('3.14')`、负零 `-0.0`、subnormal `1e-45`、NaN、`+inf` 溢出、近 FLT_MAX `3.4e38` 不误报等回归项全保持通过。`test_float_edge` 从 11/1 → **12/0 全通过**。3 个版本无回归。

### 【中危 M6】`std::forward_list` 特化误用 `push_back`，编译失败

- **复现**：修复前 `tests/test_forward_list_fail.cpp`（故意隔离，不在 run.sh 清单）→ 编译错误
- **涉及代码**：`luaaa.hpp:3376`
```cpp
template<typename K, typename ...ARGS>
struct LuaStack<std::forward_list<K, ARGS...>> {
    inline static Container get(lua_State * L, int idx) {
        ...
        while (0 != lua_next(L, idx)) {
            result.push_back(...);   // ← 错! forward_list 无 push_back, 只有 push_front
```
- **根因**：`std::forward_list`（单向链表）没有 `push_back` 方法。特化照抄了 vector/deque/list 的模板但忘了 forward_list 的 API 差异。
- **实测**：任何接受 `std::forward_list<T>` 参数的绑定函数都无法编译：`error: no member named 'push_back' in 'std::forward_list<int>'`。
- **影响**：`std::forward_list` 完全不可用。README 声明支持的容器列表中 forward_list 形同虚设。
- **建议**：改用 `push_front`（注意顺序会反转），或先收集到临时 vector 再 `reverse` 构造。其余容器（array/deque/multiset/unordered_multiset）测试全通过。

#### ✅ 修复与验证（M6）
`LuaStack<std::forward_list>::get`（`luaaa.hpp:3364`）改用 `insert_after(before_begin(), ...)` 单遍保序插入：从 `before_begin()` 起，每次 `it = result.insert_after(it, value)` 返回指向新元素的迭代器，下一次插入在其后，保持 Lua 数组原序（1,2,3）。`push_front` 会反转顺序故不采用。**验证**：新增 `test_forward_list.cpp`（4 断言：int/string 往返保序、空表、单元素）+ `test_containers_edge` 的 forward_list 从「跳过」改为实测往返保序，全通过。原 `test_forward_list_fail.cpp`（预期编译失败）重命名为 `test_forward_list.cpp` 并纳入 run.sh，现正常编译运行。3 个版本无回归。

### 【低危 L10】容器 `luaL_argcheck` 硬编码参数位置为 1

- **涉及代码**：`luaaa.hpp:3235,3274,3306,3338,3370,3402,3434,3466,3499,3532,3572,3611,3649,3687`（共 14 处）
- **现状**：所有容器 `get` 的 `luaL_argcheck(L, ..., 1, "required table not found")` 硬编码 `narg=1`。当容器参数位于第 N 位时，错误消息报 `bad argument #1` 而非实际位置。
- **影响**：仅诊断消息错位，不影响转换正确性（非表传参仍正确报错，`test_containers_edge` 验证 `echoVec(123/nil/'str')` 均报错）。
- **建议**：低优先级，将 `1` 改为实际 `idx`。

#### ✅ 修复与验证（L10）
全部 15 处容器 `get`（含 tuple `load_tuple_from_lua_table`）的 `luaL_argcheck(L, lua_istable(L, idx), 1, ...)` 改为 `..., idx, ...`（`luaaa.hpp:3262,3301,3333,3365,3397,3433,3465,3497,3530,3563,3603,3642,3680,3718,3751`）。`idx` 即 `get(L, idx)` 的真实栈位置。**验证**：非表传参仍正确报错（`test_containers_edge` 的 `echoVec(123/nil/'str')` 断言保持通过），错误消息现显示真实参数位置而非恒为 `#1`。3 个版本无回归。

### 第二轮新增正向覆盖（无缺陷）

| 测试文件 | 断言 | 覆盖 |
|---|---|---|
| test_overload | 8/8 ✓ | 同名 fun 覆盖（后者胜出）、lua_CFunction 类/模块侧、lua_State* 注入、tuple 单表返回 |
| test_metamethod | 18/18 ✓ | 类侧 __add/__eq/__lt/__le/__len/__call/__concat/__tostring 运算符全生效；模块侧 __add 不生效（实证既定限制） |
| test_containers_edge | 13/13 ✓ | array/deque/**forward_list**/multiset/unordered_multiset、空表、非表报错、深层嵌套、混合键 |
| test_strings | 14/14 ✓ | UTF-8 往返、嵌入 NUL 截断（既定限制）、tostring 强转回归、nil/table 报错 |
| test_static_const | 8/8 ✓ | 数组 def、类 def 常量（实例级非静态）、重复绑定、跨 state registry、deleter 析构 |
| test_forward_list | 4/4 ✓ | M6 修复回归：forward_list int/string 往返保序、空表、单元素边界 |

---

## 零-续三、第三轮深度路径排查与修复（最新）

在两轮 25 文件基础上，针对此前未测的分支（property 异常路径、GC/deleter 异常路径、整数窄化、`lua_State*` 返回值、ctor 名冲突）做深度排查。**发现并修复 2 个高危崩溃 bug（H5/H6），记录 3 个既定行为（B7/B8/B9）**。

| 编号 | 问题 | 严重度 | 状态 |
|---|---|---|---|
| **H5** | **属性 getter/setter 抛 C++ 异常致进程崩溃（SIGABRT）** | 高危 | ✅ 已修复（getter/setter Invoke 补 try/catch，16 处） |
| **H6** | **GC 期间 deleter/析构函数抛 C++ 异常致进程崩溃** | 高危 | ✅ 已修复（f__objgc 补 try/catch，吞没异常——__gc 契约） |
| B7 | 整数 get 窄化静默回绕（short/char 超 range，与浮点溢出检查不对称） | 低危 | 🟡 记录现状（用户决定不改） |
| B8 | `lua_State*` 参数占用栈位（违反 GUIDE「consumes no argument」契约） | 低危 | ✅ 已修复（make_arg_indices 跳过 lua_State* 槽位，f(lua_State*,int) 可写 f(21)） |
| B9 | 构造函数名冲突仅 printf 不报错 + spawner+deleter overload 漏调冲突检查 | 低危 | 🟡 记录现状 |

**矩阵结果**（`CXX=clang++ bash tests/run.sh all`，3 版本 × 29 文件）：
- **总断言 786 通过，0 失败，0 编译失败**
- **TSan 0 data race**

### 【高危 H5】属性 getter/setter 抛 C++ 异常致进程崩溃

- **复现**：`tests/test_property_except.cpp` → getter 抛 `std::runtime_error` 子进程退出码 `-6`（SIGABRT）
- **涉及代码**：getter/setter 的 `Invoke` lambda（类侧 12 处：`luaaa.hpp:2262/2283/2305/2327/2345/2362/2383/2405/2426/2453/2470/2487`；模块侧 4 处：`3118/3139/3160/3195`）直接 `(*(FTYPE*)(calleePtr))(...)` **无 try/catch**。
- **根因**：H4 修复（`TranslateCppException`）只覆盖了 `LuaInvokeInstanceMemberImpl`（普通成员函数，`luaaa.hpp:1090`）和非成员函数（`LuaInvokeImpl:977`），**漏掉了 property getter/setter 路径**——后者在各自的 `Invoke` lambda 内直接内联调用，不走集中调用器。抛异常的 getter/setter（校验失败、资源获取失败——常见场景）穿过 `extern "C"` 边界 → `std::terminate` → SIGABRT。
- **版本差异**：LuaJIT 下不崩溃（内置 C++ 异常交互）；标准 Lua 5.4/5.5 崩溃。
- **影响**：比 H4 更隐蔽——property 访问（`obj.x` / `M.v`）是日常操作，若其底层 getter/setter 可能抛异常，会杀死宿主进程。

#### ✅ 修复与验证（H5）
在全部 16 处 getter/setter `Invoke` lambda 的函数调用外包 `#if LUAAA_HAS_EXCEPTIONS try #endif { ... } #if LUAAA_HAS_EXCEPTIONS catch (...) { TranslateCppException(state, "property getter"/"property setter"); } #endif`，与 H4 成员函数路径对称。**验证**：`test_property_except.cpp` 类/模块 getter/setter 抛异常子进程退出码从 `-6`(SIGABRT) → **0**；正常 getter/setter 路径回归全通过。`test_property_except` 从 2/4 → **6/0 全通过**。3 个版本无回归。

### 【高危 H6】GC 期间 deleter/析构函数抛 C++ 异常致进程崩溃

- **复现**：`tests/test_gc_except.cpp` → 单例 deleter 抛异常 / 析构函数抛异常，`lua_close`/`collectgarbage` 触发 `__gc` → 子进程退出码 `-6`（SIGABRT）
- **涉及代码**：`f__objgc`（`luaaa.hpp:1585-1587`）调用 `(uData->dtor)(uData)` **无 try/catch**。
- **根因**：Lua 要求 `__gc` 不得传播异常——`lua_pcall` 不捕获 C++ 异常，且 `__gc` 内 `lua_error` 本身行为未定义（Lua 手册）。用户 deleter（如资源释放失败抛异常）或析构函数（`noexcept(false)` 抛异常）穿过 `extern "C"` 边界 → terminate。
- **影响**：deleter/析构抛异常是资源管理中的真实场景（释放失败、状态不一致），会杀死宿主进程而非优雅退出。

#### ✅ 修复与验证（H6）
在 `f__objgc`（`luaaa.hpp:1585`）的 `(uData->dtor)(uData)` 调用外包 try/catch。与 H5 不同，**吞没异常而非翻译为 lua_error**——Lua 手册明确 `__gc` 内 `lua_error` 行为未定义，故仅 `catch (...) { /* suppressed */ }`。**验证**：`test_gc_except.cpp` 单例 deleter/析构抛异常子进程退出码从 `-6` → **0**；正常 deleter/析构回归（`g_normal_deleted==1`、GC 平衡）全通过。`test_gc_except` 从 3/2 → **5/0 全通过**。3 个版本无回归。

### 【记录现状 B7/B9 + B8 已修复】

- **B7 整数 get 窄化**：`static_cast<T>(v)`（`luaaa.hpp:638`）对 short/char 无范围检查。`echo_short(99999)` 静默回绕为 -31073；`echo_ull(-1)` 静默回绕为 ULLONG_MAX。与浮点路径（有溢出报错）不对称。`test_numeric_edge2.cpp` 断言当前回绕值作为行为基线。
- **B8 `lua_State*` 参数占位**（✅ 已修复，详见下「✅ 修复与验证」）：原实现 `LuaStack<lua_State*>::get` 注入当前 L（设计正确），但参数索引 `Ns+1+skip` 对每个参数递增使 `lua_State*` 也占一位，违反 GUIDE 的「consumes no argument」契约。经深入架构评估排除两个替代方案后（见下），采用方案 3：保留注入语义 + 新增 `make_arg_indices` 生成器跳过 `lua_State*` 槽位。
  - **`put`（返回方向，刻意为空，非缺陷）**：`lua_State*` 是宿主句柄，非可序列化的 Lua 值，无法 push 回栈。绑定函数不应返回 `lua_State*`。
  - **返回路径副作用（版本相关，非预期用法）**：若函数声明返回 `lua_State*`，`LuaStackReturn` 的 `lua_settop(L,0)` + 空 put + `return 1` 使返回值取 base 处值——Lua 5.5 是函数闭包自身（`r==getL` 为 true，非垃圾）；LuaJIT 是不稳定 number。属误用，非有意义 state。
  - **架构评估（为何不用其他方案）**：
    - 方案 1（删特化走通用 `LuaStack<T*>` lightuserdata 路径）：虽能解锁「跨 VM」传值，但破坏 `feedAll(lua_State* L)` 的自动注入便利性（`example/embedded.cpp` 文档化用法），且 lightuserdata 无类型标记，误传指针即 UB。
    - 方案 2（注入优先 + 栈上是 lightuserdata 则读取）：引入**不可检测的 `void*`→`lua_State*` 类型混淆**——Lua 无 API 验证 lightuserdata 是否为合法 state，误传会致 UB，比当前类型安全的注入更危险，已排除。
    - **方案 3（采纳）**：保留注入的类型安全性，仅修索引让 `lua_State*` 不消耗栈位。

#### ✅ 修复与验证（B8）
新增 `is_lua_state<T>` / `is_lua_state_decayed<T>` trait（识别 `lua_State*` 及其 cv/引用变体）与 `make_arg_indices<ARGS...>` 编译期索引生成器（紧接 `make_indices` 之后）：遍历参数列表，`lua_State*` 位 emit 当前序号（被 `get` 忽略）但不递增序号，真实参数位 emit 序号并递增。三处调用点（`LuaInvoke`、`LuaInvokeInstanceMember`、`PlacementConstructorCaller::Invoke`）从 `make_indices<sizeof...(ARGS)>` 改为 `make_arg_indices<ARGS...>`，Impl 函数的 `Ns + base` 表达式不变。纯编译期计算，运行时零开销。**验证**：`test_numeric_edge2.cpp` B8 章节——`compute(21)==42`（`lua_State*` 不消耗栈位，21 直达 `int`）、`mixed(10,20)==30`（中间 `lua_State*` 不偏移后续 `int`）、单参 `touch()` 无需传值、返回路径副作用（版本相关）全通过；`example/embedded.cpp` 的 `feedAll(lua_State* L)`（原始文档化用法）仍正常工作。**已知限制（非本次修复目标）**：多 `lua_State*` 参数（如 `f(vm1, vm2, int)`）仍都折叠为当前 L——这是 `LuaStack<lua_State*>::get` 的固有语义（恒返回当前 L）。跨 VM 传值需 lightuserdata 类型标记（Lua 平台不提供，强行 `void*`→`lua_State*` 转换是方案 2 已排除的不可检测混淆）。
- **B9 ctor 名冲突**：`luaaa_check_constructor_name_conflict`（`luaaa.hpp:234-246`）仅 printf 不 luaL_error；spawner+deleter overload（`1861/1952`）漏调此检查。同名 ctor 后者覆盖前者。`test_numeric_edge2.cpp` 断言后者胜出行为。

### 第三轮新增正向覆盖

| 测试文件 | 断言 | 覆盖 |
|---|---|---|
| test_property_except | 6/6 ✓ | H5 修复：类/模块 getter/setter 抛异常不崩溃 + 正常路径回归 |
| test_gc_except | 5/5 ✓ | H6 修复：deleter/析构抛异常 GC 不崩溃 + 正常 GC 回归 |
| test_numeric_edge2 | 8/8 ✓ | B7 整数窄化/无符号负值/char 语义、B8 `lua_State*` 注入不占栈位+三参混合+返回副作用、B9 ctor 冲突（B8 修复验证） |

---

## 零-续四、第六轮评审：嵌套容器 tuple、模块 property 补挂、返回对象 full userdata 化（最新）

在前五轮（H1-H6、M1-M6、B7-B9、L1-L10 全部修复）基础上，对 `luaaa.hpp` 全量 4090 行做新一轮逐行评审，**新确认 8 类缺陷 + 1 项架构增强（S1）**，全部修复并回归。新增 `tests/test_review3.cpp`（39 断言）。

**矩阵结果**（`bash tests/run.sh all`，LuaJIT/5.4/5.5 × 30 文件，ASan+UBSan）：**总断言 909 通过，0 失败，0 编译失败**；`SAN_MODE=tsan` 9/9 通过 0 race；c++14 抽查（tuple 快路径）3 版本全通过；example（c++14）与 embedded（`-fno-exceptions -fno-rtti` no-stdlib）编译运行通过。

| 编号 | 问题 | 严重度 | 状态 |
|---|---|---|---|
| N1 | tuple 入参栈失衡：嵌套容器（vector/list/map 元素为 tuple、tuple 嵌套 tuple 非末尾）必报 `invalid key to 'next'` | 中高 | ✅ 修复（get 改 `lua_rawgeti` 下标读取，与 put 对称；C++14/C++11 两路径） |
| N2 | 默认 `_G` 模块 / 预存全局表的模块 property 静默失效；LuaJIT(5.1) 模块 property 整体失效 | 中高 | ✅ 修复（新增 `_ensureMetaTable`，注册时补挂；5.1 `luaL_openlib` 路径同补并清栈残留） |
| N3 | `const T*` 参数误报 `cpp pointer expected`（pointee 未做 cv 归一化） | 中 | ✅ 修复（`std::remove_cv`） |
| N4 | `alignof(TCLASS) > 8` 时 placement 构造地址错位（UB） | 中 | ✅ 修复（按 `alignof` 对齐 + 预留 padding） |
| N5 | PUC Lua 5.1 编译失败：`lua_tonumberx`/`lua_tointegerx` 未 polyfill（LuaJIT 自带故矩阵未暴露） | 中 | ✅ 修复（新增 `LUAAA_tonumberx`/`LUAAA_tointegerx` 封装，5.1 分支带钳位，避免与 LuaJIT 符号冲突） |
| N6 | 回调特化 `put` 非 `static` 且用 `lua_pushcfunction`（无 upvalue）→ 返回回调编译失败/调用报错；`lua_State*` 回调参数破坏 pcall 实参个数 | 低 | ✅ 修复（补 `static` + holder userdata 携带可调用体；`lua_State*::put` 改推 lightuserdata 保实参平衡） |
| N7 | 同一 state 两个不同 C++ 类型绑定同一 lua 名：metatable 被覆盖 + 类型混淆 UB | 低 | ✅ 修复（metatable 内打类型标记 `&s_typeKey`，异类重用即报错；同类重绑定仍允许） |
| N8 | `f__objgc` 在 metatable 被剥掉/替换时可能在 `__gc` 内抛错（lua_close 期间 → panic/abort） | 低 | ✅ 修复（`lua_touserdata` 替代 `luaL_checkudata`、`lua_getmetatable` 返回值检查） |
| N9 | `LuaModule::def(name, class, obj)` 不校验类是否已在该 state 绑定；不可默认构造时注册空对象 | 低 | ✅ 修复（绑定校验 argcheck；空对象注册期 `luaL_error`） |
| N10/N11 | placement ctor `free_func` 未初始化；若干显式 include（`cstdio/cstdlib/cstdint/cstring`） | 低 | ✅ 修复 |
| **S1** | **架构增强**：返回 `T*`/`T&` 由无类型 lightuserdata 升级为带类 metatable 的 full userdata（非拥有别名，方法可调、回传有类型检查，`nullptr`→`nil`）；按值返回 `T` 由编译失败升级为拥有拷贝（GC 析构）；未绑定类指针仍回退 lightuserdata（兼容透明句柄模式） | — | ✅ 实现（`LuaStack<T>::put` 双重载 + `luaaa_detail::put_ref` 引用派发，保证 `const std::string&` 等仍走原特化） |

**关键实证（修复前行为推导，修复后全部转绿）**：
- N1：`m.vt({{1,2},{3,4},{5,6}})`（`vector<tuple<int,int>>` 参数）修复前必报 `invalid key to 'next'`——tuple 的 `lua_next` 循环最后一次成功的 key 未弹出，外层容器循环把它当游标 key。顶层 tuple 测试从未暴露是因为残留恰被 `LuaStackReturn` 的 `lua_settop(L,0)` 清理。修复后 21 通过；`map<string,tuple>`、tuple 嵌套 tuple（非末尾）同绿。
- N2：`LuaModule(L)`（默认 `_G`）`.get/.set` 注册的 property 修复前读写静默无效（`_initMetaTable` 只在全局表不存在时调用，而 `_G` 恒存在）；脚本侧预建 `PreM = {}` 同病。修复后 getter/setter 正常触发，且 `_G` 挂 metatable 后普通全局语义不变（未定义读 nil、普通赋值正常）。LuaJIT 上模块 property 由「跳过」变为实测通过。
- N7：`TypeB` 抢注 `TypeA` 已占用的名字，修复前静默覆盖 metatable 且 `luaL_checkudata` 按名放行 → 跨类型 reinterpret UB；修复后绑定即报错，原 metatable 不受破坏；同类型同名重绑定（追加方法）仍允许。
- S1：`m.ptr()` 返回 `Point*` 修复前是 lightuserdata（`p:getX()` 报 attempt to index）；修复后 `type(p)=='userdata'`、方法可调、`m.same(m.ptr(), m.ptr())` 指针相等（别名语义）、`val()` 按值对象 GC 后存活计数回基线（无泄漏）、`Other` 传给 `const Point*` 报错（类型检查生效）。

**已知边界（记录现状）**：
- 预存表已挂**异质** metatable（宿主或其他库安装）时 luaaa 不覆盖，模块 property 在那里保持不可用（GUIDE 已注明）。
- 返回 `T&`/`const T&` 为非拥有别名，C++ 对象须活得比 Lua 引用久；`const T&` 别名请勿经 Lua 修改真正 const 的对象（GUIDE 已注明）。
- `LuaStack<T>::get` 假定正栈索引（当前全部调用点均满足）；负索引调用属未支持用法。

---

## 一、执行摘要

本轮系统测试在 **3 个 Lua 版本 × 2 种 sanitizer 模式**下运行，共执行约 **540 项断言**，发现并实证了 **10 类潜在问题**，按严重度分级：

| 严重度 | 数量 | 概述 |
|---|---|---|
| 高危 (H) | 3 | 类 `__index` 返回值丢失；构造函数抛 C++ 异常致宿主崩溃；函数指针回调线程不安全（TSan 实证 data race）|
| 中危 (M) | 4 | 整数 get 对非法/浮点字符串静默返回 0；整数 put 无溢出检测（与浮点特化不对称）；`__index` 拒绝非字符串 key；`T*` 传 nil 静默返回 nullptr |
| 低危 (L) | 3 | bool/char/const char* 类型强转行为不一致；`PROPERTY=0` + `fun("__index")` 交互致方法访问失效；测试文件 `test_functional.cpp` 用了 5.2+ API（LuaJIT 编译失败）|

**核心结论**：常规功能路径（类导出、GC 生命周期、容器/tuple 往返、继承、std::function 回调）在 3 个版本下全部正常，ASan/UBSan 无内存错误。问题集中在 **元方法（`__index`/`__newindex`）类与模块不对称**、**数值类型转换的错误输入处理**、**C++ 异常与 Lua 错误的桥接缺失**，以及**函数指针回调的进程级静态槽**（已知 H2 限制，本轮用 TSan 给出确凿实证）。

---

## 二、跨版本测试矩阵

ASan+UBSan 下，每个 Lua 版本 × 16 个测试文件：

| 测试文件 | LuaJIT(5.1) | Lua 5.4 | Lua 5.5 | 说明 |
|---|---|---|---|---|
| test_functional | 24/24 ✓（跳过2项模块prop）| 24/24 ✓ | 24/24 ✓ | L9 修复后可在 LuaJIT 编译；模块 property 在 5.1 跳过 |
| test_fixes | 11/11 ✓ | 11/11 ✓ | 11/11 ✓ | |
| test_callbacks | 7/7 ✓ | 7/7 ✓ | 7/7 ✓ | |
| test_bugs | 0 缺陷 ✓ | 0 缺陷 ✓ | 0 缺陷 ✓ | |
| test_edge | 14/14 ✓ | 14/14 ✓ | 14/14 ✓ | 多 state registry 正常 |
| test_extend | 10/10 ✓ | 10/10 ✓ | 10/10 ✓ | |
| test_numeric | 12/12 ✓ | 12/12 ✓ | 12/12 ✓ | |
| **test_cast**（新）| 全 ✓ | 全 ✓ | 全 ✓ | M1/M2/M4 修复后按版本断言 |
| **test_property**（新）| 全 ✓ | 全 ✓ | 全 ✓ | H1/M3 修复 |
| **test_lifetime**（新）| 15/15 ✓ | 15/15 ✓ | 15/15 ✓ | H2 修复：子进程退出码 0 |
| **test_callbacks_ptr**（新）| 5/5 ✓ | 5/5 ✓ | 5/5 ✓ | H3 限制实证 |
| **test_features**（新）| 4/4 ✓ | 4/4 ✓ | 4/4 ✓ | |
| test_features_no_property（新）| 2/2 ✓ | 2/2 ✓ | 2/2 ✓ | |
| test_features_no_extend（新）| 2/2 ✓ | 2/2 ✓ | 2/2 ✓ | |
| test_features_no_conflict（新）| 1/1 ✓ | 1/1 ✓ | 1/1 ✓ | |
| test_concurrency（新）| 3/3 ✓ | 3/3 ✓ | 3/3 ✓ | 功能断言 |

**ASan+UBSan 矩阵（修复后）**：LuaJIT/5.4/5.5 × 16 文件 = **420 断言全通过，0 FAIL，0 编译失败。**

**TSan 矩阵**（`test_concurrency`，Lua 5.5，修复后）：**0 处 data race**（修复前为 2 处，定位 `luaaa.hpp:918`，见 H3）。

> 注：FAIL = 测试断言「正确行为」失败 = 当前实现有 bug，是该问题的实证。

---

## 三、缺陷清单（按严重程度）

### 【高危 H1】类的用户 `__index` 返回值被丢弃

- **复现**：`bash tests/run.sh all` → `[5.5] test_property` → `FAIL 类 __index 返回值应能取到`
- **涉及代码**：`luaaa.hpp:1387-1388`
```cpp
lua_insert(state, 1);
if (lua_pcall(state, lua_gettop(state) - 1, 0, 0) != 0) { lua_error(state); }  // ← nresults=0
```
- **根因**：`f_internal_index` 调用用户注册的 `!__index` 函数时，`lua_pcall` 的 `nresults`（期望返回值数）传 **0**。用户 `__index` 的返回值被丢弃；随后 `return 1` 返回的是栈顶（nil）。
- **不对称**：**模块**版 `f_internal_index`（`luaaa.hpp:2388`）正确传 `nresults=1`。类与模块行为不一致。
- **实测**：`IdxBug.new().nonexistent` 返回 `nil`（`is_number=0 val=0`），而对照 `M.nonexistent` 正确返回 `777`（`calls=1`）。
- **影响**：任何「类上注册自定义 `__index` 并期望返回值」的用法都失效。模块能用、类不能用，是隐蔽的功能缺失。
- **建议**：把 `luaaa.hpp:1388` 的 `nresults` 从 `0` 改为 `1`（与模块版对齐）。

### 【高危 H2】构造函数抛 C++ 异常导致宿主进程崩溃（`std::terminate`）

- **复现**：`tests/test_lifetime.cpp` → `[构造函数抛异常]` 子进程退出码 `134`（SIGABRT）
- **涉及代码**：`luaaa.hpp:1529-1545`（`PlacementConstructorCaller::Invoke` → `f_new`），整个边界无 `try/catch`
- **根因**：绑定的 C++ 构造函数若抛异常，异常穿过 luaaa 的 C 边界（无 `try{...}catch(...){ lua_error(...); }` 翻译）。标准 Lua（5.4/5.5）下 C++ 异常穿过 `extern "C"` 边界 → `std::terminate` → 进程 SIGABRT。
- **实测**（5.5）：子进程退出码 `-6`（SIGABRT），stderr：`libc++abi: terminating due to uncaught exception of type std::runtime_error: ctor throws`。
- **版本差异**：**LuaJIT 下不崩溃**（退出码 0）——LuaJIT 内置 C++ 异常交互，捕获了异常。但标准 Lua 5.4/5.5 崩溃。
- **影响**：绑定的 C++ 类型若构造函数可能抛（很常见：内存不足、参数校验失败），会**杀死整个宿主进程**，而非向 Lua 报错让 `pcall` 捕获。这是最严重的一类问题——脚本错误变成进程级宕机。
- **建议**：在 luaaa 的函数/构造调用边界包 `try/catch`，把 C++ 异常翻译为 `lua_error`。`-fno-exceptions`（embedded 模式）下可跳过，但默认构建应有此保护。

#### ✅ 修复与验证（H2）
新增 `LUAAA_HAS_EXCEPTIONS` 检测宏（基于 `__cpp_exceptions`）与 `[[noreturn]] TranslateCppException(L, ctx)`，在 `LuaInvokeImpl`（所有成员/非成员/静态函数调用）与 `PlacementConstructorCaller::InvokeImpl`（placement 构造）两个集中边界包 `try/catch`。`-fno-exceptions` 下守卫编译为空，embedded 模式零影响。**验证**：`test_lifetime.cpp` 子进程退出码从 `-6`(SIGABRT) → **0**（正常退出），`pcall` 捕获到 `luaaa: constructor failed: ctor throws`；3 个版本全通过。

### 【高危 H3】函数指针回调的进程级静态槽——线程不安全（TSan 实证 data race）

- **复现**：`SAN_MODE=tsan CXX=clang++ bash tests/run.sh all` → stderr `WARNING: ThreadSanitizer: data race`（见 `tests/tsan_report.txt`）
- **涉及代码**：`luaaa.hpp:918`（展开自 `IMPLEMENT_CALLBACK_INVOKER` 宏，`luaaa.hpp:655-736`）
```cpp
static lua_State * cacheLuaState = nullptr;   // 进程级静态 (luaaa.hpp:917 附近)
static int cacheLuaFuncId = 0;                 // 进程级静态
...
cacheLuaState = L;                             // ← TSan 报: Write of size 8
cacheLuaFuncId = luaL_ref(L, LUA_REGISTRYINDEX);  // ← TSan 报: Write of size 4
```
- **TSan 实证**：4 线程各自独立 `lua_State` 并发调用「接收函数指针回调」的绑定函数，触发 2 处 data race：
  - `Write of size 8 at 0x...990000`（`cacheLuaState = L`）by thread T1 vs T3
  - `Write of size 4 at 0x...990008`（`cacheLuaFuncId = ...`）by thread T4 vs T2
  - 调用栈：`LuaStack<int (*)(int)>::get` → `LuaInvoke` → `NonMemberFunctionCaller::Invoke`
- **根因**：函数指针回调（`RET(*)(ARGS...)`）的特化用 per-signature **进程级 static** 存 `lua_State*` 与 ref id。同一签名的回调共用这一对变量。并发写无同步 → data race；逻辑上也会互相踩踏（H2 已知限制，注释 `luaaa.hpp:650-654` 已说明）。
- **影响**：多线程/多 state 场景下用函数指针回调，行为未定义（结果错乱或崩溃）。TSan 下触发后还观察到 SEGV。
- **建议**：这是 C++ 函数指针无法携带上下文的语言限制（注释已承认）。建议在文档显著位置警告「函数指针回调非线程安全」，并引导用户用 `std::function`（其特化用捕获 lambda + `shared_ptr`，无 static 槽，TSan 实测无 race）。

---

### 【中危 M1】整数 `get` 对非数字/浮点格式字符串静默返回 0

- **复现**：`test_cast` → `FAIL echo_ll('hello')`、`FAIL echo_ll('1e30')`
- **涉及代码**：`luaaa.hpp:550-561`
```cpp
if (lua_isnumber(L, idx) || lua_isstring(L, idx)) {   // ← lua_isstring 对任意字符串为真
    return static_cast<T>(lua_tointeger(L, idx));     // ← 字符串非整数时 lua_tointeger 返回 0
}
```
- **根因**：分支条件用 `lua_isstring(L, idx)`（任意字符串都为真），而非 `lua_isnumber`。当字符串不能解析为整数时，`lua_tointeger` 返回 0——**静默地**把错误输入变成 0。
- **实测**：
  - `echo_ll("hello")` → `0`（应报错）
  - `echo_ll("1e30")` → `0`（Lua 5.3+，浮点格式字符串 `lua_tointeger` 返回 0）
  - `echo_char("A")` → `0`（`char` 走同一特化；"A" 不是数字 → 0，而非 ASCII 65）
- **影响**：把字符串误传给整数参数时，得到静默的 0 而非清晰的类型错误，掩盖调用方 bug。`char` 类型尤其反直觉（用户期望 `'A'`→65）。
- **建议**：分支条件改为仅在 `lua_isnumber` 为真时转换；字符串先尝试数值解析，失败则 `luaL_argcheck` 报错。对 `char` 可考虑单独特化以支持字符语义。

#### ✅ 修复与验证（M1）
整数特化 `get` 改为 `lua_type==LUA_TNUMBER || (LUA_TSTRING && lua_isnumber)`，并用 `lua_tointegerx(L,idx,&isnum)` 校验转换成功，失败则 `luaL_error("bad number (not an integer)")`。**验证**：`echo_ll('hello')`、`echo_char('A')` 在 5.4/5.5 报错（之前返回 0）；正常数字字符串 `echo_ll('42')==42` 仍可转换（回归保护）。LuaJIT/5.1 上 `'1e30'` 截断为 LLONG_MAX（`isnum=1`）属 Lua 固有行为，测试按版本断言。

### 【中危 M2】整数 `put`（C++→Lua）无溢出检测，与浮点特化不对称

- **复现**：`test_cast` → `echo_ull(ULLONG_MAX)` 静默截断
- **涉及代码**：`luaaa.hpp:563-566`（整数 `put`）vs `luaaa.hpp:505-513`（浮点 `put` 有溢出报错）
```cpp
// 整数 put (无溢出检测):
inline static void put(lua_State * L, const T & t) {
    lua_pushinteger(L, static_cast<lua_Integer>(t));   // ← 超范围静默回绕
}
```
- **实测**：
  - Lua 5.5：`echo_ull(18446744073709551615)`（ULLONG_MAX）→ `raises=0`，静默接受
  - LuaJIT：`echo_ull(ULLONG_MAX)` → 返回 `9.22337e+18`（=`LLONG_MAX`，64 位无符号被回绕成有符号）
- **不对称**：浮点特化的 `put`（`luaaa.hpp:508-510`）在 `t > hi` 时 `luaL_error`；整数特化没有等价检查。`test_numeric.cpp` 已测浮点溢出报错，但整数方向未测。
- **影响**：大无符号值（如哈希、ID）经 luaaa 往返后变成负数或截断值，无任何提示。
- **建议**：整数 `put` 增加与 `lua_Integer` 范围对比的溢出检查（注意 Lua 5.1/LuaJIT 的 `lua_Integer` 仅 32/64 位且数字为 double，需版本感知）。

#### ✅ 修复与验证（M2）
整数 `put` 现在与浮点特化对称：在 `long double` 域比较 `t` 与 `[lua_Integer::min, lua_Integer::max]`，超范围则 `luaL_error("number overflow: integer value out of lua_Integer range")`。**验证**：`echo_ull(ULLONG_MAX)` 在 5.4/5.5 报溢出（`raises=1`）；`echo_ll(-2^31)` 等在范围内值仍正常（回归保护）。LuaJIT 上字面量先被解析为 double 再 `get` 截断，C++ 收到 LLONG_MAX（在范围内）不触发 `put` 报错——属 Lua 固有，测试按版本断言。

### 【中危 M3】`__index`/`__newindex` 拒绝非字符串 key（违反 Lua 元方法契约）

- **复现**：`test_property` → `FAIL Widget[{}] table key 应允许访问`
- **涉及代码**：`luaaa.hpp:1355`（类 `f_internal_index`）、`luaaa.hpp:1418`（类 `f_internal_newindex`）、`luaaa.hpp:2349/2420`（模块对应）
```cpp
const char* key = luaL_checkstring(state, 2);   // ← table key 在此抛硬错误
```
- **根因**：`luaL_checkstring` 对无法转字符串的类型（table、function）抛出 `bad argument` 错误。但 Lua 的 `__index` 契约允许任意类型 key（数字、table 等），未知 key 应返回 nil。
- **实测**：
  - `Widget.new()[{}]`（table key）→ `raises=1`（抛错，违反契约）
  - `Widget.new()[123]`（数字 key）→ 在 5.5 下**通过**（`luaL_checkstring` 把数字强转为字符串 "123"，但这本身也是一种静默强转）
- **影响**：Lua 侧 `obj[some_table]` 这类合法用法在 luaaa 绑定对象上会抛错；与原生 Lua table 行为不一致。
- **建议**：用 `luaL_tolstring` 或先判断 `lua_type`；非字符串 key 应走「未知 key → nil」路径，而非硬报错。

#### ✅ 修复与验证（M3）
新增 `IndexKeyToString(state, idx)` 辅助（用 `lua_tostring`，数字可强转、table/function 返回 nullptr），替换类与模块共 4 处 `__index`/`__newindex` 的 `luaL_checkstring`。key 无法强转时：`__index` 返回 nil、`__newindex` 直接返回（不报错）。**验证**：`Widget.new()[{}]` 从 `raises=1`（抛错）→ `raises=0`（返回 nil，符合 `__index` 契约）；正常字符串/数字 key 访问不受影响（回归保护）。

### 【中危 M4】`T*` 参数传 nil 静默返回 nullptr（缺 `luaL_argcheck`）

- **复现**：`test_cast` → `ptr_addr(nil)` 静默返回 0
- **涉及代码**：`luaaa.hpp:435-460`（`LuaStack<T*>::get`）
```cpp
inline static T * get(lua_State * state, int idx) {
    if (lua_islightuserdata(state, idx)) { ... }
    else if (lua_isuserdata(state, idx)) { ... }
    return nullptr;   // ← nil/其他类型走这里, 无 argcheck
}
```
- **不对称**：对象值特化 `LuaStack<T>::get`（`luaaa.hpp:397`）有 `luaL_argcheck`；指针特化没有。
- **实测**：`ptr_addr(nil)` → `raises=0`，C++ 函数收到 `nullptr`。若 C++ 函数解引用该指针，直接段错误。
- **影响**：Lua 脚本传 `nil`（或错误类型）给 `T*` 参数时，错误以「C++ 侧空指针解引用崩溃」的形式爆发，而非清晰的 Lua 参数错误。安全隐患。
- **建议**：`LuaStack<T*>::get` 末尾加 `luaL_argcheck(state, <got non-null>, idx, "...");`。

#### ✅ 修复与验证（M4）
`LuaStack<T*>::get` 在既非 lightuserdata 也非已绑定 userdata 的分支末尾补 `luaL_argcheck(state, false, 1, "cpp pointer ... expected")`（no-stdlib 分支用纯字面量消息）。**验证**：`ptr_addr(nil)` 从 `raises=0`（静默返回 nullptr）→ `raises=1`（报 `cpp pointer ... expected`），C++ 侧不再可能收到 nullptr 解引用崩溃；正常的 lightuserdata/绑定对象传递不受影响。

---

### 【低危 L1】bool 类型严格拒绝 number/string 强转（与其它数值特化不一致）

- **复现**：`test_cast` → `echo_bool(1)`/`echo_bool(0)`/`echo_bool('true')` 均抛错
- **涉及代码**：`luaaa.hpp:519-523`
```cpp
luaL_checktype(L, idx, LUA_TBOOLEAN);   // ← 严格, 不接受 number/string
```
- **现状**：bool 特化严格（传 `1` 报错），而整型/`const char*` 特化接受字符串强转，`const char*` 甚至接受 bool/number 强转。三个特化的「宽松度」各不相同，行为不可预期。
- **影响**：用户需记住「bool 严格、整数松、字符串更松」，易踩坑。属于一致性/可用性问题，非崩溃 bug。
- **建议**：统一强转策略（要么都严格，要么都宽松），并在文档说明。

#### ✅ 修复与验证（L1/L2 —— 统一为严格 Lua 类型语义）
> 决策历程：曾尝试对齐 **JavaScript** 的 `Boolean()`/`Number()`/`String()`（`0`/`""`→falsy、`true`→1）。复审后否决：本库是 **Lua** 绑定，脚本作者的心智模型是 Lua（`if 0 then` 为真），强行套用 JS 规则会让 `bool(0)=false` 在 Lua 作者眼里变成 surprise。**正确基准是宿主语言 Lua 本身的类型规则**，不是另一种脚本语言的模仿。

最终采用 **严格 Lua 类型语义**，每个特化对应一个 Lua 原生操作的「类型接受集合」，规则务实明确、可在 Lua 手册中找到出处：
- **`bool`**（`LuaStack<bool>::get`）：保留 `luaL_checktype(L, idx, LUA_TBOOLEAN)` 严格匹配——**只接受 Lua `boolean`（true/false）**，number/string/nil 一律报错。采用严格派而非 Lua truthiness（`if x then`）的宽松派：宽松虽符合 Lua 气质，但「`0` 是真、`""` 是真」违反一般直觉，严格规则能让脚本侧的类型错误立刻暴露，更务实。
- **`int`/整数特化**：**移除** 曾加的 `LUA_TBOOLEAN` 分支——对齐 Lua 算术（Lua 里 `1 + true` 报 `attempt to perform arithmetic on a boolean value`），故 `int(true)`/`int(false)` 也报错。数字 / 数字字符串维持可转换。
- **`const char*`**：**维持现状**——它本就等于 Lua 的 `tostring()`（`true`→`"true"`、`123`→`"123"`、`nil`→`"nil"`），是 Lua 原生行为，非 bug。

| Lua 传入 | bool（严格 boolean） | int（Lua 算术） | const char*（Lua tostring） |
|---|---|---|---|
| `true`/`false` | ✅ 原值 | ❌报错（`true+1` 也报错） | ✅ "true"/"false" |
| `1`/`0`/`42` | ❌报错 | ✅ 原值 | ✅ "1"/"0"/"42" |
| `""`/`"x"`/`"42"` | ❌报错 | ✅42（数字串）/ ❌报错（非数字） | ✅ 原串 |
| `nil` | ❌报错 | ❌报错 | ❌报错（tostring 外） |

**设计要点（非对称是有意为之）**：`bool(1)=报错` 但 `int(1)=1`——正映射 Lua 里 `if 1 then` 合法但 `true + 1` 报错。三特化各按对应 Lua 操作的接受集合，不再有「bool 严、int 中、str 松」的人为割裂，而是「每个类型严格按其 Lua 原生操作的输入要求」。

**验证**：`test_cast` 新增严格 Lua 类型断言矩阵（bool 7 项 / int 4 项 / cstr 4 项），3 个版本全通过；原有测试零回归；example 正常编译运行。

### 【低危 L2】`const char*` 静默强转 bool/number 为字符串（已定性为 Lua tostring，见 L1）

- **复现**：`test_cast` → `echo_cstr(true)`→`"true"`、`echo_cstr(123)`→`"123"`
- **涉及代码**：`luaaa.hpp:569-587`（`LuaStack<const char*>::get`）
- **现状**：传 `true` 给 `const char*` 参数，静默得到字符串 `"true"`；传 `123` 得到 `"123"`。无类型错误。
- **影响**：类型不匹配的调用被静默「修好」，掩盖调用方 bug（例如误传布尔标志给字符串参数）。
- **建议**：至少对 bool 输入报错；number→string 强转可保留但文档化。

#### ✅ 决策（L2 —— 等价 Lua tostring()，作为既定语义保留）
经 L1 统一决策，`const char*` 的 bool/number→字符串强转**有意保留**——它正是 Lua `tostring()` 的标准行为，是「`const char*` 对应 tostring」这一映射的必要组成，与 bool/int 的严格性不冲突（三者按各自 Lua 操作）。不再视为缺陷。文档（README/GUIDE）应说明「字符串参数接受一切可 stringify 的值，等价 `tostring()`」。

### 【低危 L3】`const char*` 存储跨调用悬垂（已知限制，文档化）

- **复现**：`test_lifetime` → `[const char* 悬垂行为]`
- **涉及代码**：`luaaa.hpp:579`（`lua_tostring` 返回 Lua 内部指针，GC 后失效）
- **现状**：C++ 函数把 `const char*` 存到成员/全局，Lua 侧字符串 GC 后该指针悬垂。单次调用内安全。`std::string` 特化会拷贝，无此问题。
- **建议**：文档显著警告「`const char*` 仅在调用内有效；需跨调用存储请用 `std::string`」。

### 【低危 L4】`__gc` 元方法路径部分通过 `fun("__gc")` 工作（验证记录）

- **复现**：`test_lifetime` → `[用户 __gc 与 C++ 析构]` → `user_gc=1 real_dtor=1`（5.5 全通过）
- **说明**：`fun("__gc", &Class::method)` 注册的 Lua 侧 `__gc`（存为 `!__gc`，经 `luaaa.hpp:1334-1345` 的 `f__objgc` 调用）**正常工作**。用户 `__gc` 与 C++ 析构都执行，顺序正确，无 double-free。此项为正向验证，非缺陷。

### 【低危 L5】`PROPERTY=0` + `fun("__index")` 交互致方法访问失效

- **复现**：`test_features_no_property` → `P2.new().method` 返回数字 42 而非函数
- **涉及代码**：`luaaa.hpp:1492-1496`（关闭 property 时 `__index = metatable 自身`）与 `luaL_setfuncs` 注册 `__index` 方法的交互
- **根因**：`PROPERTY=0` 时先把 metatable 设为自身的 `__index`；随后 `luaL_setfuncs` 注册用户 `fun("__index", f)`，把 `metatable.__index` 覆盖成该函数 `f`。于是所有属性访问都调用 `f`（返回固定值），方法查找完全失效。
- **影响**：仅在 `LUAAA_FEATURE_PROPERTY=0`（非默认）且同时注册 `__index` 时触发。低概率组合，但行为反直觉。
- **建议**：`PROPERTY=0` 模式应禁用或警告 `fun("__index")` 注册；或调整注册顺序（先 `setfuncs` 再设 `__index = self`）。

### 【低危 L6】`ctor(name, spawner, deleter)` 漏检构造名冲突（验证记录）

- **复现**：`test_features` → `ctor(spawner,deleter) 同名重复注册` 通过（实证当前不报错）
- **涉及代码**：`luaaa.hpp:1637-1638`（该 overload 不调 `luaaa_check_constructor_name_conflict`，而 `luaaa.hpp:1548/1601/1757` 的其它 overload 都调）
- **现状**：重复注册同名 ctor，后者静默覆盖，不报错。其它 overload 会触发冲突检查。
- **影响**：小——同名 ctor 覆盖是合理行为，但与其它 overload 不一致。
- **建议**：补齐 `luaaa_check_constructor_name_conflict(name)` 调用，或文档说明 spawner+deleter overload 不检查。

### 【低危 L7】无符号 64 位溢出在 LuaJIT 下回绕为有符号（跨版本差异，属 M2 表现）

- **复现**：`test_cast`（LuaJIT）→ `echo_ull(ULLONG_MAX)` 返回 `9.22337e+18`
- **说明**：这是 M2（整数 put 无溢出检测）在 LuaJIT 上的具体表现。LuaJIT 数字为 double，`ULLONG_MAX` 经 `static_cast<lua_Integer>` 后回绕成 `LLONG_MAX`。5.5 下因字面量解析返回 0。两版本都无错误提示。

### 【低危 L8】`__index` 数字 key 在 5.5 下经字符串强转通过（M3 的另一面）

- **说明**：M3 中 `Widget[123]` 在 5.5 下「通过」，是因为 `luaL_checkstring` 把数字强转为字符串 "123"——这本身也是一种静默强转。理想行为应是按原始 key 类型查找并返回 nil。

### 【低危 L9】测试文件 `test_functional.cpp` 使用 5.2+ API，LuaJIT 下编译失败

- **复现**：`bash tests/run.sh all` → `[COMPILE FAIL] [luajit] test_functional.cpp`：`use of undeclared identifier 'lua_rawlen'`
- **涉及代码**：`tests/test_functional.cpp:186`（`lua_rawlen`）
- **根因**：`lua_rawlen` 是 Lua 5.2+ API；Lua 5.1/LuaJIT 对应的是 `lua_objlen`。该测试驱动（非库本身）用了高版本 API。
- **影响**：测试本身在 LuaJIT 下不可编译，使最核心的功能测试无法在 LuaJIT 上运行。**库本身 `luaaa.hpp` 不含 `lua_rawlen`，库是 LuaJIT 兼容的**——这是测试可移植性问题。
- **建议**：测试文件用 `#if LUA_VERSION_NUM <= 501` 选择 `lua_objlen`/`lua_rawlen`，或统一用 `lua_rawlen` 并为 5.1 提供 polyfill（与 `luaaa.hpp` 顶部已有的 polyfill 风格一致）。

#### ✅ 修复与验证（L9）
在 `tests/test_functional.cpp` 顶部 `#include "luaaa.hpp"` 后补 polyfill：`#if LUA_VERSION_NUM <= 501 inline int lua_rawlen(...) { return lua_objlen(...); } #endif`。并对 LuaJIT/5.1 不支持的「模块 property」（依赖 `USE_NEW_MODULE_REGISTRY`/`luaL_setfuncs`）在 Lua 端特性检测后计为跳过。**验证**：`test_functional` 现可在 LuaJIT 编译运行（24/24 或跳过模块 property 2 项）；全矩阵编译失败归零。

---

## 四、已验证正常的功能（回归保护）

本轮测试在 3 个版本下确认以下功能全部正常（无 FAIL，ASan/UBSan 无报错）：

| 分类 | 用例 | 结果 |
|---|---|---|
| 类与构造 | 默认/单参/双参/spawner/spawner+deleter ctor | ✅ |
| 成员函数 | 可变成员、const 成员、字符串返回、静态成员 | ✅ |
| 类 property | member getter/setter、只读/只写保护与报错 | ✅ |
| Module | 常量 def、函数、module property | ✅ |
| 容器 | vector/list/set/map/deque 往返 | ✅ |
| tuple | 往返（C++14 快速路径 + C++11 回退）| ✅ |
| 数值类型 | long/unsigned/long long/short/size_t/int64_t/float/double/long double 往返 | ✅ |
| 数值溢出 | float get 溢出报错、long double put 溢出报错、NaN/inf 边界 | ✅ |
| 生命周期 | GC ctor=dtor 平衡、多次 GC 无 double-free | ✅ |
| 用户 `__gc` | Lua 侧 `__gc` + C++ 析构协同执行 | ✅ |
| 继承 | `luaaa:extend` + override + base 调用、多 state 独立 | ✅ |
| 多 state | per-state registry：并存同名/异名、关闭后存活、同 state 冲突拒绝 | ✅ |
| std::function 回调 | 多次调用、同签名多回调并存、重入、TSan race-free | ✅ |
| 特性宏 | `EXTEND=0`、`CONFLICT=0`、`PROPERTY=0`（无 `__index` 时）| ✅ |
| 跨版本 | 上述功能在 LuaJIT/5.4/5.5 一致（除 H2/M2/L7/L9 注明的差异）| ✅ |

---

## 五、修复优先级建议

1. **H2（构造函数异常崩溃）**——最紧急：脚本错误变成进程宕机，生产环境不可接受。在调用边界加 `try/catch → lua_error`。
2. **H1（类 `__index` 返回值丢失）**——一行修复（`nresults: 0→1`），消除类与模块的不对称。
3. **H3（函数指针回调线程不安全）**——文档显著警告 + 引导 `std::function`；TSan 实证已确凿。
4. **M1（整数 get 静默 0）**——影响调试体验；改分支条件即可。
5. **M4（`T*` nil→nullptr）**——加 `luaL_argcheck`，消除崩溃风险。
6. **M3（`__index` 非 string key）**——改善 Lua 契约一致性。
7. **M2（整数 put 溢出）**——与浮点特化对称化。
8. **L1/L2（类型强转一致性）**——统一策略 + 文档。
9. **L9（测试文件 LuaJIT 兼容）**——补 polyfill，让核心测试能在所有声明支持的版本上运行。

---

## 六、复现指南

```bash
# 全版本 ASan+UBSan 矩阵 (推荐)
bash tests/run.sh all

# 单一当前 Lua (兼容旧行为)
bash tests/run.sh

# TSan 矩阵 (仅并发测试, 探测 data race)
SAN_MODE=tsan CXX=clang++ bash tests/run.sh all

# 自选版本子集
LUA_TARGETS="luajit 5.5" bash tests/run.sh all

# 查看完整日志
cat tests/results.txt       # ASan/UBSan 全量输出
cat tests/tsan_report.txt   # TSan data race 报告
```

**新增测试文件**：`test_cast.cpp`、`test_property.cpp`、`test_lifetime.cpp`、`test_callbacks_ptr.cpp`、`test_concurrency.cpp`、`test_features.cpp`、`test_features_no_property.cpp`、`test_features_no_extend.cpp`、`test_features_no_conflict.cpp`。

**断言约定**：测试对「正确/合理行为」断言；FAIL = 当前实现有 bug（实证）。标注「文档化」「验证记录」的项是正向记录或已知限制，不构成新缺陷。
