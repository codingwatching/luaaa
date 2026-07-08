# luaaa —— 进阶指南

[English](GUIDE.md) | [中文](GUIDE.zh-CN.md) · 返回 [README.zh-CN.md](README.zh-CN.md)

本指南面向已经读过 [README](README.zh-CN.md) 基础的用户，作为参考手册与 FAQ。各节相互独立，按需跳转即可。

1. [类型转换与 `LuaStack`](#类型转换与-luastack)
2. [教会 luaaa 你自己的类型](#教会-luaaa-你自己的类型)
3. [构造函数详解](#构造函数详解)
4. [重载函数与消歧](#重载函数与消歧)
5. [回调详解](#回调详解)
6. [属性详解](#属性详解)
7. [`def`：常量、数组与内嵌实例](#def常量数组与内嵌实例)
8. [元方法](#元方法)
9. [多个 `lua_State` 与 `TAG` 参数](#多个-lua_state-与-tag-参数)
10. [嵌入式 / 无标准库构建](#嵌入式--无标准库构建)
11. [特性宏](#特性宏)
12. [对象生命周期与 GC 所有权](#对象生命周期与-gc-所有权)
13. [兼容性说明](#兼容性说明)
14. [排错 / FAQ](#排错--faq)

---

## 类型转换与 `LuaStack`

每个跨越 C++ ⇄ Lua 边界的值都要经过 `LuaStack<T>`，它负责两件事：`get`（Lua → C++）和 `put`（C++ → Lua）。luaaa 内置了以下特化：

| 类别 | 类型 |
|---|---|
| 浮点 | `float`、`double`、`long double` |
| 布尔 | `bool` |
| 整数 | `int`、`long`、`long long`、`short`、`char` 及其 `unsigned` 形式 —— 因而也覆盖所有别名（`size_t`、`int64_t`、`uint32_t`、`ptrdiff_t` 等） |
| 字符串 | `const char*`、`char*`、`std::string` |
| 顺序容器 | `std::array`、`vector`、`deque`、`list`、`forward_list` |
| 集合 | `set`、`multiset`、`unordered_set`、`unordered_multiset` |
| 映射 | `map`、`multimap`、`unordered_map`、`unordered_multimap` |
| 元组类 | `std::pair`、`std::tuple` |
| Lua 句柄 | `lua_State*`（接收当前 state；不消耗任何实参） |
| 已绑定的类 | 任何你用 `LuaClass` 导出的类的 `T`、`T&`、`const T&`、`T*` |

容器映射为 Lua 表：顺序容器变成数组式表（`{1,2,3}`），映射变成键值表（`{a=1}`），`pair`/`tuple` 变成位置式表（`{first, second}`）。

> ⚠️ **整数在不同 Lua 版本下的精度。** 所有整型都经由 `lua_Integer` 转换。在 Lua 5.3/5.4 上它是 64 位，`int`/`long`/`size_t` 都能精确往返。但在 **Lua 5.1/5.2 与 LuaJIT** 上数字底层是 double，超过 2⁵³ 的整数会丢精度；此外无符号 64 位值 ≥ 2⁶³ 即便在 5.3+ 上也会映射成负的 `lua_Integer`。这是 Lua 固有的限制 —— 若需要在旧版 Lua 上精确表示大的 64 位整数，请改用字符串传递。
>
> ⚠️ **浮点溢出会被检查。** 每个浮点值都经由 `lua_Number` 转换。若装不下 —— 例如把很大的 `long double` 压入 `double` 型的 `lua_Number`，或把 double 值的 Lua 数字读回 `float` —— luaaa 会抛出 Lua 错误，而不是静默产生 `inf`。范围*之内*的精度损失（尾数截断，或在 Lua 5.1/5.2/LuaJIT 上的任何浮点）是 Lua 固有的，不会报告。
>
> 非算术标量（`enum`、裸结构体）仍没有特化；这些请自行添加 `LuaStack`（见下一节）。

## 教会 luaaa 你自己的类型

要传递 luaaa 不认识的类型，就特化 `LuaStack<你的类型>`。**该特化必须写在 `namespace luaaa` 里**（GCC 要求如此，其他编译器也无害）。通常可以复用已经存在的特化来搭建。

```cpp
struct Color { int r, g, b; };   // 你的类型；Lua 侧看到的是 { r=.., g=.., b=.. }

namespace luaaa {
    template<> struct LuaStack<Color> {
        static Color get(lua_State* L, int idx) {
            auto t = LuaStack<std::map<std::string, int>>::get(L, idx);
            return Color{ t["r"], t["g"], t["b"] };
        }
        static void put(lua_State* L, const Color& c) {
            std::map<std::string, int> t{ {"r", c.r}, {"g", c.g}, {"b", c.b} };
            LuaStack<decltype(t)>::put(L, t);
        }
    };
}
```

此后任何接收或返回 `Color` 的绑定函数都能直接工作。`example/example.cpp` 里的项圈颜色正是这么做的。

## 构造函数详解

每个类至少需要一个构造函数。共有四种形态；它们可以以不同名字并存，且都会返回一个新的 Lua 对象。

```cpp
LuaClass<Cat> cat(L, "Cat");

// 1) placement 构造 —— 对象直接在 Lua 的 userdata 内构造，
//    Lua 回收它时调用 ~Cat()。模板参数 = C++ 构造函数的形参。
cat.ctor<std::string>("new");            // Cat.new("Tom") -> new Cat("Tom")

// 2) 工厂 / spawner —— 静态或自由函数返回一个堆对象。
//    默认由 luaaa 在 GC 时 delete 它。
cat.ctor("fromShelter", &Cat::adopt);    // Cat* adopt() { return new Cat(...); }

// 3) spawner + 自定义析构器 —— GC 时调用你的析构器。
cat.ctor("managed", &Cat::adopt, &Cat::release);   // void release(Cat*)

// 4) spawner + nullptr 析构器 —— Lua 永不销毁它。用于单例
//    或由别处持有的对象。
cat.ctor("shared", &Cat::instance, nullptr);
```

spawner 的类型是 `TCLASS* (*)(ARGS...)`；析构器是 `R (*)(TCLASS*)`。两者都可以是静态成员或自由函数。

**命名冲突。** 两个构造函数用了同一个 Lua 名字时，后者会覆盖前者。当 `LUAAA_CHECK_CONSTRUCTOR_NAME_CONFLICT` 为 `1`（默认）时，luaaa 会打印一条警告帮你发现问题。

## 重载函数与消歧

如果一个 C++ 名字被重载了，编译器无法从裸的 `&Name` 推断你要哪一个。请强制转换到确切的签名：

```cpp
bool  parse(const std::string&);
void  parse(int);

LuaModule(L, "m")
    .fun("parseStr", (bool(*)(const std::string&)) parse)
    .fun("parseInt", (void(*)(int))               parse);

struct Calc { int add(int,int); int add(int); };

LuaClass<Calc>(L, "Calc")
    .ctor()
    .fun("add2", (int(Calc::*)(int,int)) &Calc::add)
    .fun("add1", (int(Calc::*)(int))     &Calc::add);
```

给每个重载起一个不同的 Lua 名字（Lua 没有重载）。

## 回调详解

绑定的 C++ 函数可以接收一个 Lua 函数。把参数声明为 `std::function` 或裸函数指针 —— 两者行为差别很大。

```cpp
void onEach(std::function<int(int)> cb);   // (a) 推荐
void onEvent(int (*cb)(const char*));      // (b) 裸指针
```

**(a) `std::function` —— 推荐。** 每个回调各自持有对 Lua 函数的引用（由 `shared_ptr` 守护）。因此它可以：
* 被保存并多次调用；
* 与其他同签名的回调同时存在；
* 可重入（一个回调可以触发另一个）。

当回调对象及其所有副本被销毁时，Lua 引用才被释放。**回调不能比它的 `lua_State` 活得更久**（析构会调用 `luaL_unref`）。

**(b) 裸函数指针 —— 有限制。** 无捕获的函数指针无法携带上下文，于是 luaaa 把 `(state, ref)` 存进按*签名*索引的 `static` 槽位里。后果：
* 同一签名同一时刻只能有**一个**存活的回调 —— 注册新的会覆盖旧的；
* **不可**重入、非线程安全；
* 引用直到 state 关闭才释放（期间会泄漏）。

只在需要单个、简单、长期存在的回调时才用裸指针形式（它也是[嵌入式构建](#嵌入式--无标准库构建)里唯一的选择）；否则请用 `std::function`。

## 属性详解

`get`/`set` 接受多种形态。`TCLASS` 是被绑定的类。

**Getter**（返回属性值；不能返回 void）：
```cpp
cat.get("name", &Cat::name);                       // 成员：  P (Cat::*)() const
cat.get("total", &globalCount);                    // 自由：  P (*)()
cat.get("label", &describe);                       // 自由：  P (*)(const Cat&)
cat.get("w", [](Cat& c){ return c.weight(); });    // lambda：[]( [const] Cat& )->P
cat.get("k", []{ return 3.14; });                  // lambda：[]()->P
```

**Setter**（接收新值；返回类型被忽略）：
```cpp
cat.set("name", &Cat::setName);                    // 成员：  R (Cat::*)(P)
cat.set("flag", &setGlobalFlag);                   // 自由：  R (*)(P)
cat.set("w", &applyWeight);                        // 自由：  R (*)(Cat&, P)
cat.set("w", [](Cat& c, float v){ c.setW(v); });   // lambda：[](Cat&, P)
cat.set("k", [](float v){ /*...*/ });              // lambda：[](P)
```

**访问规则。** 只有 getter → 只读；写入抛出 `attempt to write Read-Only property '...'`。只有 setter → 只写；读取抛出 `attempt to read Write-Only property '...'`。读取不存在的属性返回 `nil`。

**模块属性**用法相同，但 getter/setter 是没有 `self` 参数的自由函数或 lambda（模块没有实例）。模块的 getter 必须返回非 void（由 `static_assert` 强制）。

## `def`：常量、数组与内嵌实例

`def` 用于发布近似只读的数据。

```cpp
// 类与模块都可用：标量常量或字符串
mod.def("version", 3);
mod.def("name", "shelter");

// 仅模块：一个 C 数组会变成 Lua 数组表
static const int primes[] = { 2, 3, 5, 7 };
mod.def("primes", primes, sizeof(primes)/sizeof(primes[0]));

// 仅模块：把一个被绑定类的活实例内嵌进模块表。
// obj = nullptr 会默认构造一个；传入析构器可控制清理。
LuaClass<Cat> cat(L, "Cat"); cat.ctor();
mod.def("mascot", cat);                 // shelter.mascot 是一个 Cat
mod.def("mascot", cat, existingCatPtr); // 或内嵌一个已存在的对象
```

## 元方法

你可以用 `fun` 绑定 Lua 元方法。大多数（`__tostring`、`__add`、`__len`、`__eq` 等）按原样注册：

```cpp
cat.fun("__tostring", &Cat::toString);   // print(obj) / tostring(obj)
```

三个是特殊的，因为 luaaa 内部用它们来实现属性与 GC：`__index`、`__newindex`、`__gc`。当你绑定其中之一时，luaaa 仍保留自己的分发器，并把**你的实现作为后备**在内置方法与属性查找之后调用。因此自定义 `__index` 只处理那些既非绑定方法、也非属性的键；自定义 `__gc` 会在对象析构之外额外运行。

实例读取（`obj.key`）时的方法/属性查找顺序：绑定的方法与常量 → 已注册的 getter → 你的 `__index`（函数或表）→ *（若存在 setter）* 只写错误 → `nil`。

## 多个 `lua_State` 与 `TAG` 参数

luaaa 把每个类型的 Lua 名字存在一个**按 state 隔离的注册表**里，因此同一个 C++ 类型可以在**不同 state 中以不同名字**导出：

```cpp
LuaClass<Widget>(A, "Button").ctor().fun("get", &Widget::get);  // state A
LuaClass<Widget>(B, "Slider").ctor().fun("get", &Widget::get);  // state B
// A 里的 Button.new():get() 与 B 里的 Slider.new():get() 都能工作。
```

在**单个** state 内，一个 C++ 类型恰好对应**一个** Lua 名字。在同一 state 内把同一类型绑定到第二个名字会触发冲突错误。如果你确实需要在一个 state 内为同一类型提供两种 Lua 视图，就把它包成两个不同的 C++ 类型（例如两个平凡子类）—— 这才是可靠的做法。

模板还接受一个整型 `TAG` —— `LuaClass<T, TAG>` —— 让 `<T, TAG>` 拥有独立的注册槽位与元表。它可用于避免不同绑定相互冲突，**但请注意**：实例方法的 `self` 始终通过 `LuaClass<T, 0>` 解析，所以在非零 `TAG` 上绑定*实例方法*会在调用时找不到 `self`。请把 `TAG` 当作高级逃生舱，而非"在一个 state 内绑定同一类型两次"的通用方案。

## 嵌入式 / 无标准库构建

面向单片机等自由（freestanding）目标，在包含头文件**之前**定义：

```cpp
#define LUAAA_WITHOUT_CPP_STDLIB 1
#include "luaaa.hpp"
```

通常配合 `-fno-exceptions -fno-rtti` 编译。完整可运行的节点见 `example/embedded.cpp`。

此模式下**不可用**（它们依赖 STL）：`std::string`、`std::function`、所有 STL 容器转换、`std::tuple`/`std::pair`，以及 lambda 绑定（lambda 依赖 `std::function`）。

**改用：**
* `const char*` 表示文本（在你的类里用定长缓冲区）。
* 通过 `def(name, array, length)` 用 C 数组生成表。
* 裸**函数指针**回调（见[回调详解](#回调详解)）。
* 在方法上加一个 `lua_State*` 参数，手动读取表/参数：
  ```cpp
  void feedAll(lua_State* L) {
      if (lua_istable(L, -1)) {
          lua_pushnil(L);
          while (lua_next(L, -2)) {
              const char* who = LuaStack<const char*>::get(L, lua_gettop(L));
              /* ... */
              lua_pop(L, 1);
          }
      }
  }
  ```

**提供 `operator new`/`delete`。** luaaa 用 placement `new` 在 userdata 内构造对象；自由目标没有分配器。请自己提供（在已经定义了它们的宿主工具链上用宏隔离掉）：
```cpp
#ifdef LUAAA_FREESTANDING
void* operator new(size_t, void* p) noexcept { return p; }
void  operator delete(void*, void*) noexcept {}
void* operator new(size_t n)            { return malloc(n); }
void  operator delete(void* p) noexcept { free(p); }
#endif
```

**RTTI。** 使用 `-fno-rtti` 时 luaaa 读不到类型名，于是 `not export`/冲突信息里会显示 `?` 而非 C++ 类型名。功能一切正常，只是诊断信息更简略。

## 特性宏

在包含 `luaaa.hpp` **之前**定义。

| 宏 | 默认 | 作用 |
|---|---|---|
| `LUAAA_WITHOUT_CPP_STDLIB` | `0` | 去掉所有 C++ 标准库用法（嵌入式模式）。 |
| `LUAAA_FEATURE_PROPERTY` | `1` | 启用 `get`/`set` 属性（`__index`/`__newindex` 机制）。 |
| `LUAAA_FEATURE_EXTEND` | `1` | 自动注入 `luaaa:extend` / `luaaa:base` 以支持 Lua 侧继承。 |
| `LUAAA_CHECK_CONSTRUCTOR_NAME_CONFLICT` | `1` | 两个构造函数重名时发出警告。 |
| `LUAAA_DEBUG` | `0` | 启用 `LUAAA_DUMP(L)` 栈转储辅助函数。 |

## 对象生命周期与 GC 所有权

由谁调用析构，取决于对象是如何进入 Lua 的：

| 创建方式 | GC 时 |
|---|---|
| `ctor<Args...>()`（placement） | 对象位于 userdata 内；运行 `~T()` |
| `ctor(name, spawner)` | `delete` 返回的指针（若 `T` 不可析构则无操作） |
| `ctor(name, spawner, deleter)` | 运行你的 `deleter(T*)` |
| `ctor(name, spawner, nullptr)` | Lua 永不销毁（借用 / 单例） |
| 从绑定函数返回 `T*`/`T&` | 作为轻量引用压栈；Lua **不**拥有也不回收它 |

把指针/引用返回给 Lua 会给它一个句柄，可以传回其他绑定函数，但 Lua 不管理其生命周期 —— 请自己保证该 C++ 对象存活。

## 兼容性说明

* **Lua 版本。** 5.1、5.2、5.3、5.4 与 LuaJIT。对 5.1/LuaJIT，luaaa 会补上它需要的少数 5.2+ 辅助函数（`luaL_setfuncs`、`lua_rawgetp` 等）。
* **模块注册。** 在 Lua > 5.1 且未定义 `LUA_COMPAT_MODULE` 时，模块以普通全局表创建（现代风格）；否则走旧的 `luaL_openlib` 路径。这是自动的。
* **C++ 标准。** 下限是 C++11。C++14+ 会选用更快的 `std::tuple` 转换路径；同时包含 C++11 回退实现，因此 tuple 两种情况下都能用。

## 排错 / FAQ

**`cpp class 'X' not export`** —— luaaa 被要求转换一个它没有绑定/特化的类型。原因：(1) 你在该 state 里的 `LuaClass<X>` 构造之前就调用了方法；(2) 绑定签名用了没有特化的标量（如 `enum`、裸结构体）—— 为它加一个 `LuaStack`；(3) 你在与绑定时不同的 `lua_State` 里使用该对象。

**`C++ class '...' bind to conflict lua name`** —— 你在一个 state 内把同一 C++ 类型绑定到了两个名字。请每个 state 用一个名字、用不同 state、或用不同的包装类型（见[多个 state](#多个-lua_state-与-tag-参数)）。

**静态方法用 `Class.method()` 调用报 "nil value"** —— 用 `fun` 绑定的静态/自由函数是当作方法调用的：请写 `instance:method(args)`。实例就是被跳过的 `self`。

**`attempt to read Write-Only` / `write Read-Only property`** —— 该属性只有 setter（只写）或只有 getter（只读）。若两者都需要，就把缺的那个访问器也注册上。

**lambda 无法绑定** —— 嵌入式模式下 lambda 不可用（它们需要 `std::function`），请用自由函数指针。普通模式下，确保 lambda 不是泛型的（不含 `auto` 形参），这样才能推导出它的签名。

**回调过一阵子就崩溃** —— 裸函数指针回调按签名单槽且不可重入；`std::function` 回调不能比它的 `lua_State` 活得更久。见[回调详解](#回调详解)。
