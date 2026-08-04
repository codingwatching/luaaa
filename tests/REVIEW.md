# luaaa 工程级测试与代码 Review 报告

- 被测对象：`luaaa.hpp`（v1.4，header-only，3362 行）
- 测试环境：macOS (arm64)、g++ / clang++、Lua 5.5.0、`-std=c++11`、`-fsanitize=address,undefined`
- 方法：C++ 侧完成绑定 + Lua 侧断言，覆盖正常功能；再用受控输入触发疑似缺陷路径；全程 ASan/UBSan 监控。
- 测试资产：`tests/test_functional.cpp`（24 项功能断言）、`tests/test_bugs.cpp`（缺陷触发）、`tests/test_edge.cpp`（多 state / 继承）、`tests/run.sh`（一键运行）。

## 修复记录（本轮已修 + 回归验证）

| 缺陷 | 修复内容 | 位置 | 验证 |
|---|---|---|---|
| H3 | `std::pair::get` 改为用 `lua_rawgeti` 从 table 内按下标读取，与 `put` 对称 | `luaaa.hpp:3229` 附近 | `test_bugs` B5：`echoPair({10,20})→{11,21}` ✅ |
| H4 | module 的 `f_internal_index`/`f_internal_newindex` 中 3 处 `lua_getmetatable(state,1)` 改为 `lua_pushvalue(state,1)`（property 闭包实际存在 module table 自身） | `luaaa.hpp:2182/2209/2241` 附近 | 功能 24/24；`test_bugs` B15、newindex 三路径全部 ✅ |
| M1 | `luaL_error` 补齐缺失的 `key` 实参，消除格式串 UB 并修正语义（属性名/模块名） | `luaaa.hpp:2217` | 读只写属性正确报 `Write-Only property 'wo' of 'MOD'`，无崩溃 ✅ |

### 第二轮（除 H1/H2 外全部修复）

| 缺陷 | 修复内容 | 位置 | 验证 |
|---|---|---|---|
| M2 | 存储 `F`（可能是 `std::function`）改用 placement new（`new (funPtr) F(f)`），消除对未构造存储 `memset`+`operator=` 的 UB；新增 `#include <new>` | `luaaa.hpp` 三处 `_registerClassFunction`/`_registerModuleFunction` | `test_fixes` M2：无捕获/值捕获/返回 string 的 lambda 均正常；`memset` 警告消失 |
| M3 | **per-state registry 方案**：类型→lua 名的映射从 C++ 全局 static 改为存进**每个 state 的 registry**（以 `&s_typeKey` 为键）。`klassName` 由 static 变量改为 `klassName(lua_State*)` 静态方法（从当前 state 取名）。移除 `name$` class-metatable、占位 userdata、`"$"` 字段、`f__clsgc` 与进程级 `new[]`；补 `lua_rawgetp/rawsetp` 的 5.1 polyfill | `luaaa.hpp` 顶部 polyfill + 构造函数 + 成员定义 + 约 30 处 `klassName`→`klassName(state)` | `test_edge` 14/14：并存 state **同名**、并存 state **不同名**（La=Animal / Lb=Beast 同一类型）、L1 关闭后 L2 仍可用、同一 state 内异名冲突被拒；embedded(no-stdlib) 与 example(full) 均编译运行通过 |
| L1 | `std::array::get` 改为 `lua_rawgeti` 按下标读取（顺序确定、无栈残留、短表遇 nil 停止、超长表安全截断）；`Container result{}` 值初始化消除短表未定义尾部 | `luaaa.hpp` array 特化 | `test_fixes` L1：正常/短表/超长表/连续调用均正确 |
| L2 | 无效 `static_assert` 补 `typename std::decay<...>::type`（原比较模板本身，形同虚设） | `luaaa.hpp` 两处 getter/setter | 编译通过，断言恢复语义 |
| L3 | getter/setter 与用户 `__index`/`__newindex` 的 `lua_pcall` 补返回码检查并 `lua_error` 传播（`__gc` 里的 pcall 保持静默）；read-only/write-only 错误信息补全模块名；`"use use"` 文案修正；清理 unused parameter 警告 | `luaaa.hpp` 多处 | `test_fixes` L3：用户 `__index` 抛错正确传播；零编译警告 |

回归：`bash tests/run.sh`，功能 24/24 + fixes 9/9 + edge 8/8 全部通过，ASan/UBSan 无内存错误，零编译警告。

### M3 增强（per-state registry，支持并存 state 不同名）

第二轮的 M3 曾用「进程级持久 `klassName`」实现，只支持并存 state **同名**（不同名需 `TAG`）。本轮进一步重构为 **per-state registry**：
- 每个 `LuaClass<TCLASS>` 有一个唯一静态地址 `&s_typeKey` 作类型标识；lua 名存到**该 state** 的 `registry[&s_typeKey]`。
- `klassName(lua_State*)` 从当前 state 取名——因此**同一 C++ 类型可在不同（甚至并存）state 中用不同 lua 名绑定**，各 state 只认自己的名字。
- 冲突判定变为 per-state：仅同一 state 内对同类型用两个不同名才算冲突。
- 顺带**消除**了上一版的进程级 `new[]` 泄漏；`lua_rawgetp/rawsetp` 为 5.1/luajit 提供 polyfill。
- 代价：`LuaStack::get/put` 每次多一次 O(1) registry 查找。

> **后续（TAG 移除）**：模板此前还带一个 `int TAG` 参数（`LuaClass<T, TAG>`），本意是让同一类型在同一 state 内绑定多个名字。但 `LuaStack<T>::get`（解析 `self`/参数）恒用 `LuaClass<T, 0>`，非零 `TAG` 的实例方法在调用时必然找不到 `self`——该特性从未真正工作。已彻底删除 `TAG` 模板参数；"不同 state 不同名"由 per-state registry 提供，与 `TAG` 无关。

**遗留 / 未修**：
- ~~M2 的遗留：存储的 `std::function` 在 holder userdata 回收时其析构仍未被调用（一次性 setup 期泄漏，非本轮 UB 问题；彻底修复需为 holder 加 `__gc`）。~~ **已修复（第四轮）**：见下。

### 第四轮（M2 遗留：holder `__gc`）

第二轮 M2 用 placement new（`new (funPtr) F(f)`）消除了 `memset`+`operator=` 的 UB，但只构造未析构——存 `F` 的 holder userdata 无 `__gc`，回收时不调用 `~F()`，`std::function` 的堆捕获随 state 生命周期泄漏。

| 缺陷 | 修复内容 | 位置 | 验证 |
|---|---|---|---|
| M2 遗留 | 新增命名空间级 `AttachHolderFinalizer<F>(L)`：仅对**非平凡析构** `F`（如 `std::function`）给 holder userdata 挂一个调用 `p->~F()` 的 `__gc`（`__gc` 在 `lua_setmetatable` 前即写入 metatable，满足 5.4+ 终结器登记规则）；平凡 `F`（函数指针）跳过——no-stdlib/embedded 零开销。在 3 处 placement new 后各插一行调用。另补 `#include <new>`（placement new 本应自带，原先靠 includer 传递性引入，no-stdlib 下脆弱） | `luaaa.hpp` `AttachHolderFinalizer` + `_registerClassFunction` + `_registerModuleFunction`（新/旧注册表两路径） | `test_fixes` M2b：按值捕获带存活计数 `Tracker` 的 `std::function`（类 + 模块两条存储路径），`lua_close` 后 `Tracker::alive` 归零；未修复版该断言 FAIL（已实测）。fixes 9→11 全通过 |

### 第三轮（H1/H2 回调机制重构）

回调把 Lua 函数转成 C++ 可调用体。原实现（函数指针 / `std::function` 各 `RET`/`void` 共 4 处）都用一对 per-signature `static cacheLuaState/cacheLuaFuncId` 存 ref，且**每次 `pcall` 后 `luaL_unref`**，导致：**H1** 回调只能调一次；**H2** 同签名回调共享 static 槽、互相踩踏、不可重入。

| 缺陷 | 修复内容 | 位置 | 验证 |
|---|---|---|---|
| **H1+H2**（`std::function` 回调，彻底修复） | `get` 重写为**捕获 lambda 携带 `(L, ref)`**，`shared_ptr` guard 在回调对象及其所有拷贝销毁时才 `luaL_unref`；移除 `static` 槽与调用后 `unref`；补 `lua_pop` 修正原有栈不平衡。新增 `#include <memory>` | `luaaa.hpp` 两处 `LuaStack<std::function<...>>` 特化 | `test_callbacks` 7/7：多次调用、同签名多回调并存、可重入、存储后延迟调用、连调 50 次栈不增长、void 副作用 |
| **H1**（函数指针回调，部分修复） | 删除调用后 `luaL_unref`（单回调可多次调用），补 `lua_pop` 平衡栈；`void` 分支 `pcall` 改 0 返回值 | `luaaa.hpp` `IMPLEMENT_CALLBACK_INVOKER` 宏 | `test_callbacks`：函数指针回调连调两次都生效 |
| **H2**（函数指针回调，语言限制） | 无捕获函数指针无法携带上下文，保留 `static` 槽；宏上方补注释说明限制，README 引导用 `std::function` | 同上 + `README.md` | 文档化；`test_bugs` B2/B3 由缺陷转为 OK（结论「0 个缺陷」） |

回归：`bash tests/run.sh` 全绿（functional 24 + fixes 9 + callbacks 7 + edge 14，test_bugs「0 个缺陷」）；ASan/UBSan 无报错（重点确认多次/重入调用后无栈失衡、无 use-after-free）；embedded(no-stdlib) 与 example(full) 编译运行通过；零编译警告。

**H1/H2 遗留（文档注明）**：
- 函数指针回调 H2 是 C++ 语言限制，无依赖前提下无法根治 —— 引导用 `std::function`。
- 回调内 `pcall` 失败仍经 `lua_error` longjmp（可能跳过调用者 C++ 析构）；彻底解决需 C++ 异常桥接，但 embedded 为 `-fno-exceptions`，超出范围。
- `std::function` 回调不应在其 `lua_State` 关闭后再销毁（`shared_ptr` 析构会 `luaL_unref(L,...)`）。

## 总体结论

核心能力（类导出、构造/析构与 GC、成员/静态/lambda 函数、**类** property、容器与 tuple 往返、Lua 侧继承）工作正常，析构生命周期平衡（ctor=dtor=11），功能路径下 ASan/UBSan 无内存错误。

但存在 **3 个高危缺陷**（回调不可重复调用、Module property 完全失效、`std::pair` 入参不可用）和若干中低危问题。这些缺陷都不在 `example` 覆盖范围内，因此长期未被发现。

---

## 缺陷清单（按严重程度）

### 【高危 H1】传入 C++ 的 Lua 回调只能被调用一次
- 位置：`IMPLEMENT_CALLBACK_INVOKER` 及 `std::function` 特化，`luaaa.hpp:624 / 667 / 708 / 752`
- 根因：回调包装体在每次 `lua_pcall` 之后立即 `luaL_unref(...cacheLuaFuncId)`，把注册在注册表里的 Lua 函数释放掉。第二次调用时 `lua_rawgeti` 取回的是 `nil`，退化为返回 `nil`，随后 `LuaStack<RET>::get(nil)` 类型检查失败并 `lua_error`。
- 复现（`test_bugs.cpp` B2a/B2b）：C++ 侧 `f(10); f(20)` → 第二次报 `bad argument ... number expected, got nil`。对照组「只调用一次」正常。
- 影响：任何「多次触发的回调 / 事件处理器 / 比较器」用法都会失败。更严重的是错误以 `lua_error` 从 C++ 调用栈深处 `longjmp` 抛出，会**跳过途中 C++ 对象的析构**（异常构建下的资源泄漏/未定义行为）。
- 建议：不要在调用后 `unref`；应把 ref 的生命周期绑定到回调包装对象（例如用带 `shared_ptr`/析构 unref 的仿函数持有 ref），并在真正不再使用时释放。

### 【高危 H2】回调实现依赖 per-type `static` 槽，无法并发/重入/多回调
- 位置：同上，`static lua_State* cacheLuaState; static int cacheLuaFuncId;`（`luaaa.hpp:610-611 / 653-654 / 694-695 / 738-739`）
- 根因：同一函数签名的所有回调共用一对静态变量。`get()` 在取参数阶段就把 `cacheLuaFuncId` 覆盖为最后一个回调，导致同签名的多个回调互相踩踏；也不具备可重入性与线程安全。
- 复现（`test_bugs.cpp` B3）：`callTwoCallbacks(f, g)` 报错/结果错误。
- 影响：一次调用中传入两个同签名回调、或在多线程/多 state 下使用回调，行为不可预期。
- 建议：改为把 Lua 函数 ref 存入 upvalue / 用户数据随包装体传递，彻底移除 `static` 缓存。H1 与 H2 应一并重构。

### 【高危 H3】`std::pair` 作为入参完全不可用（get 与 put 不对称）
- 位置：`LuaStack<std::pair<U,V>>::get`，`luaaa.hpp:3229-3230`
- 根因：`get` 从**栈的绝对位置 `idx+1`、`idx+2`** 读取两个元素，而 `put` 是生成 `{[1],[2]}` 的 table。二者不对称——`get` 根本没有从传入的 table 里取值，而是去读 table 之后的栈槽。
- 复现（`test_bugs.cpp` B5）：`echoPair({10,20})` 报 `bad argument #2 ... got no value`；对照组「pair 仅作返回值（put）」正常。
- 影响：任何以 `std::pair` 为参数的导出函数不可用；若 `idx+1/idx+2` 恰好落在其它有效栈槽，还会读到错误数据而不报错。
- 建议：`get` 改为 `first = LuaStack<...>::get(rawgeti(idx,1)); second = LuaStack<...>::get(rawgeti(idx,2));`，与 `put` 对齐。

### 【高危 H4】LuaModule 的 property（get/set）完全失效
- 位置：注册端 `_registerModuleFunction` 的 `lua_rawset(m_state, -4)`（`luaaa.hpp:2334`）把 `<name>` / `>name` 闭包写入 **module table 自身**；查找端 `f_internal_index` / `f_internal_newindex` 却从 **metatable** 读取（`luaaa.hpp:2182/2184`、`2241/2243`）。两者层级不一致。
- 复现（`test_functional.cpp` “module prop get” 失败；`test_bugs.cpp` B15）：`MOD.ro` 恒为 `nil`，getter 从不触发。
- 连带影响：由于 setter 也存错层级，只写属性的写保护、只读属性的写保护（`attempt to write Read-Only ...`）等 Module 侧属性语义全部不生效（`test_bugs.cpp` B1 中读只写属性未报错）。注意 **类（LuaClass）的 property 是正常的**，仅 Module 受影响。
- 建议：统一存取层级——要么 getter/setter 也写入 metatable，要么查找改为查 module table。

---

### 【中危 M1】`luaL_error` 格式串参数不匹配（潜在 UB）
- 位置：`luaaa.hpp:2217`
```cpp
luaL_error(state, "attempt to read Write-Only property '%s' of '%s'", luaL_optstring(state, -1, "?"));
```
- 问题：格式串有 **两个 `%s` 只提供了一个实参**，第二个 `%s` 读取未提供的可变参数 → 未定义行为（可能崩溃/输出乱码）。且唯一实参取的是 `__name`（模块名），而第一个 `%s` 语义应为属性名 `key`，参数含义也错位。
- 现状：该分支因 H4 目前走不到，属「潜伏」缺陷；一旦修复 H4，此处会立即成为真实崩溃点。
- 建议：`luaL_error(state, "attempt to read Write-Only property '%s' of '%s'", key, <module name>);`

### 【中危 M2】对 non-trivially-copyable 类型（`std::function`）做 `memset`
- 位置：`luaaa.hpp:1680 / 2327 / 2347`，`memset(funPtr, 0, sizeof(F))` 后紧跟 `*funPtr = f`
- 问题：`F` 可能是 `std::function`。对一块**未构造**的存储 `memset` 置零，再用 `operator=` 赋值，属未定义行为（编译器已给出 `-Wnontrivial-memcall` 警告）。当前 libc++ 实现下“恰好能跑”，但不可依赖。
- 建议：改用 placement new：`new (funPtr) F(f);`，并在对象析构/GC 时显式调用析构（若需要）。

### 【中危 M3】同一 C++ 类无法在两个并存的 `lua_State` 中绑定
- 位置：`LuaClass<TCLASS>::klassName` 为 `static`（`luaaa.hpp:2121`）
- 复现（`test_edge.cpp` B13）：在 L1 绑定 `Animal` 后，L2 再绑定同类型直接报冲突错误。
- 影响：多 VM 场景（脚本沙箱、每线程一个 state）默认失败；这是隐式的全局状态耦合。（已由上文 M3 增强的 per-state registry 修复。）
- 建议：文档中显著说明该限制；或将 `klassName` 与注册信息改为按 `lua_State` 存储（如放入注册表）。

---

### 【低危 L1】`std::array<K,N>` 传入超长表：静默截断且内部栈残留
- 位置：`luaaa.hpp:2808`，`while (0 != lua_next(L, idx) && index < N)`
- 问题：当表元素多于 `N`，循环在 `index==N` 时退出，但最后一次 `lua_next` 压入的 key/value **未被 `lua_pop`**，造成局部栈残留；同时多余元素被静默丢弃。另外用 `lua_next` 取值依赖遍历顺序（对纯数组段通常有序，但非契约）。
- 复现（`test_bugs.cpp` B4）：未观察到崩溃（Lua 栈自动增长，函数返回时整体清理），属健壮性/整洁性问题。
- 建议：改用 `lua_rawgeti(idx, i)` 按下标读取 `N` 个元素，避免顺序依赖与栈残留。

### 【低危 L2】无效的 `static_assert`（语义写错）
- 位置：`luaaa.hpp:1878 / 1918`
```cpp
static_assert(std::is_same<std::decay<TCLASS>, std::decay<FCLASS>>::value, ...);
```
- 问题：比较的是 `std::decay<...>` **模板本身**而非其 `::type`。该断言实际等价于 `is_same<TCLASS,FCLASS>`（漏了 `typename ...::type`），既没有实现“忽略 cv/引用”的本意，也几乎总是恰好为真而形同虚设。
- 建议：`std::is_same<typename std::decay<TCLASS>::type, typename std::decay<FCLASS>::type>::value`。

### 【低危 L3】文案/健壮性琐项
- `luaaa.hpp:1147` 错误信息 “use use LuaClass...” 重复单词。
- `f_internal_index` / `f_internal_newindex` 内部多处 `lua_pcall` 未检查返回码，getter/setter 内部报错会被静默吞掉（`luaaa.hpp:1204/1214` 等）。
- `internal_name[256]` 固定缓冲，超长属性名会被 `snprintf` 截断（一般无害，属边界）。

---

## 已验证正常的功能

| 分类 | 用例 | 结果 |
|---|---|---|
| 类与构造 | 默认/单参/双参 ctor（不同名） | ✅ |
| 成员函数 | 可变成员、const 成员、字符串返回 | ✅ |
| 静态成员 | 作为 Lua 方法调用 | ✅ |
| 类 property | member getter/setter、只读/只写保护、报错 | ✅ |
| 容器 | vector / list / set / map 往返 | ✅ |
| tuple | 往返 | ✅ |
| 类型转换 | string→number 隐式转换 | ✅ |
| Module | 常量 def、函数、global def | ✅ |
| 生命周期 | GC 触发析构，ctor=dtor 平衡 | ✅ |
| 继承 | `luaaa:extend` + override + `base` 调用 | ✅ |
| 回调 | **单次**调用 | ✅ |
| 内存 | 功能路径 ASan/UBSan 无报错 | ✅ |

## 总体评价

设计简洁、接口友好、模板技巧到位，作为「零 wrapper、单头文件」绑定库定位清晰，常规用法可用。主要短板在**回调机制**（一次性 + 全局 static，属结构性缺陷，建议重构）和**Module property / `std::pair` 入参**（实现层级/对称性写错，可局部修复）。这些问题共性是缺乏针对性的自动化测试——`example` 只做手工冒烟，未对返回值与边界做断言。

**优先级建议**：先修 H1/H2（回调）与 H4（Module property）、H3（pair）；同步处理 M1（修 H4 后会显形）、M2（UB）；补齐 L 级整洁项；并将本 `tests/` 目录纳入 CI 常态运行。
