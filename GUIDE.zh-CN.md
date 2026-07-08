# luaaa —— 进阶指南

[English](GUIDE.md) | [中文](GUIDE.zh-CN.md) · 返回 [README.zh-CN.md](README.zh-CN.md)

本指南是参考手册和常见问题解答，适合已经读过 [README](README.zh-CN.md) 基础内容的朋友。各章节互相独立，按需跳转即可。

1. [类型转换与 `LuaStack`](#类型转换与-luastack)
2. [教会 luaaa 你自己的类型](#教会-luaaa-你自己的类型)
3. [构造函数详解](#构造函数详解)
4. [重载函数与消歧](#重载函数与消歧)
5. [回调详解](#回调详解)
6. [属性详解](#属性详解)
7. [`def`：常量、数组与内嵌实例](#def常量数组与内嵌实例)
8. [元方法](#元方法)
9. [多个 `lua_State`](#多个-lua_state)
10. [嵌入式 / 无标准库构建](#嵌入式--无标准库构建)
11. [特性宏](#特性宏)
12. [对象生命周期与 GC 所有权](#对象生命周期与-gc-所有权)
13. [兼容性说明](#兼容性说明)
14. [排错 / FAQ](#排错--faq)

---

## 类型转换与 `LuaStack`

所有在 C++ 和 Lua 之间传递的值，都要经过 `LuaStack<T>` 来处理。它只做两件事：`get`（从 Lua 取数据转成 C++ 类型）和 `put`（从 C++ 推数据到 Lua）。luaaa 已经内置了以下类型的转换：

| 类别 | 类型 |
|---|---|
| 浮点数 | `float`、`double`、`long double` |
| 布尔 | `bool` |
| 整数 | `int`、`long`、`long long`、`short`、`char` 以及它们对应的 `unsigned` 版本 —— 所以各种别名（`size_t`、`int64_t`、`uint32_t`、`ptrdiff_t` 等）也都天然支持 |
| 字符串 | `const char*`、`char*`、`std::string` |
| 顺序容器 | `std::array`、`vector`、`deque`、`list`、`forward_list` |
| 集合 | `set`、`multiset`、`unordered_set`、`unordered_multiset` |
| 映射 | `map`、`multimap`、`unordered_map`、`unordered_multimap` |
| 元组类 | `std::pair`、`std::tuple` |
| Lua 状态机指针 | `lua_State*`（用来获取当前 state 的指针；不消耗函数参数） |
| 已绑定类 | 任何你用 `LuaClass` 导出过的类，`T`、`T&`、`const T&`、`T*` 均可 |

容器的转换规则：顺序容器在 Lua 侧表现为数组式表（`{1,2,3}`），映射变为键值表（`{a=1}`），`pair`/`tuple` 变为按位置排列的表（`{first, second}`）。

> ⚠️ **整型在不同 Lua 版本下的精度问题。** 所有整型都通过 `lua_Integer` 来中转。在 Lua 5.3/5.4 里 `lua_Integer` 是 64 位的，所以 `int`/`long`/`size_t` 之类都能完整地往返。但在 **Lua 5.1/5.2 和 LuaJIT** 里，数字底层是 double，超过 2⁵³ 的整数就会丢精度。此外，即便在 5.3+ 上，64 位无符号整数值 ≥ 2⁶³ 也会被映射为负的 `lua_Integer`。这些都属于 Lua 本身的设计限制 —— 如果你在旧版 Lua 上需要精确传递大整型，建议改用字符串。
>
> ⚠️ **浮点溢出会被检测。** 每个浮点值都会经过 `lua_Number` 来转换。如果值超出了目标类型能表示的范围 —— 比如把一个很大的 `long double` 塞进 `double` 型的 `lua_Number`，或者把 double 值的 Lua number 读到 `float` 里 —— luaaa 会直接报 Lua 错误，而不是悄悄生成一个 `inf`。对于范围以内但精度有损的情况（比如尾数截断，以及在 Lua 5.1/5.2/LuaJIT 上的任何浮点操作），这是 Lua 本身决定的，不会额外报错。
>
> 非算术类型的标量（如 `enum`、裸结构体）目前没有内置特化，需要自己写 `LuaStack`（见下一节）。

## 教会 luaaa 你自己的类型

如果要传递 luaaa 不认识的类型，就为它写一个 `LuaStack<你的类型>` 的模板特化。**这个特化必须放在 `namespace luaaa` 里面**（GCC 强制要求，其他编译器不写也不影响）。通常你可以在已有的特化基础上搭建。

```cpp
struct Color { int r, g, b; };   // 自定义类型；Lua 侧看到的是 { r=.., g=.., b=.. }

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

这样，任何接收或返回 `Color` 的绑定函数都可以直接使用了。`example/example.cpp` 里的项圈颜色就是按这种方式实现的。

## 构造函数详解

每个类必须要至少有一个构造函数。构造函数分为四种形态，彼此可以用不同名字共存，而且每种都会返回一个新的 Lua 对象。

```cpp
LuaClass<Cat> cat(L, "Cat");

// 1) placement 构造 —— 对象直接在 Lua 的 userdata 内存块里创建，
//    Lua 回收时自动调用 ~Cat()。模板参数 = C++ 构造函数的参数类型。
cat.ctor<std::string>("new");            // Cat.new("Tom") -> new Cat("Tom")

// 2) 工厂  / spawner —— 静态函数或自由函数返回一个堆上的对象。
//    默认情况下，luaaa 在 GC 时直接 delete 它。
cat.ctor("fromShelter", &Cat::adopt);    // Cat* adopt() { return new Cat(...); }

// 3) spawner + 自定义析构器 —— GC 时调用你的析构器。
cat.ctor("managed", &Cat::adopt, &Cat::release);   // void release(Cat*)

// 4) spawner + nullptr 析构器 —— Lua 永不销毁它。用于单例
//    或由别处持有的对象。
cat.ctor("shared", &Cat::instance, nullptr);
```

spawner 的类型是 `TCLASS* (*)(ARGS...)`；析构器是 `R (*)(TCLASS*)`。两者都可以是静态成员或自由函数。

**命名冲突。** 如果两个构造函数用了同一个 Lua 名字，后注册的会覆盖先注册的。`LUAAA_CHECK_CONSTRUCTOR_NAME_CONFLICT` 默认值为 `1`，luaaa 会在这种情况下打出一条警告，帮你及时发现问题。

## 重载函数与消歧

C++ 的函数名被重载以后，编译器没法单凭一个 `&函数名` 判断你要的是哪个版本。这时候需要显式地把函数指针转成确定的签名：

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

因为 Lua 不支持函数重载，每个版本都需要给它一个不同的 Lua 名字。

## 回调详解

绑定的 C++ 函数可以接收 Lua 函数作为回调。声明参数时，可以选 `std::function` 或裸函数指针 —— 但这两种方式的差异非常大。

```cpp
void onEach(std::function<int(int)> cb);   // (a) 推荐的方式
void onEvent(int (*cb)(const char*));      // (b) 裸函数指针
```

**(a) `std::function` —— 推荐。** 每个回调会各自独立地持有对 Lua 函数的引用（背后用 `shared_ptr` 维护）。这也意味着它可以：
* 存起来反复调用；
* 和其他同签名的回调同时存在，互不干扰；
* 支持重入（一个回调内部再触发另一个也完全没问题）。

只有当回调对象本身和它的所有副本都被销毁后，对应的 Lua 引用才会被释放。**有一点要注意：回调绝对不能比它所在的 `lua_State` 活得更久**（因为析构时要调 `luaL_unref`）。

**(b) 裸函数指针 —— 功能受限。** 无捕获的函数指针没办法携带额外的上下文，所以 luaaa 只能在内部用*按签名索引*的 `static` 槽位来存放 `(state, ref)` 信息。这会带来几个限制：
* 同一个签名在同一时刻最多只能有**一个**有效回调 —— 再注册一个新的就会把旧的覆盖掉；
* **不支持**重入，也不是线程安全的；
* Lua 的引用要等到整个 state 关闭时才会释放（在此之前算泄漏）。

除非你只需要一个简单的、长期有效的回调（或者在使用[嵌入式构建](#嵌入式--无标准库构建)模式，那是唯一选择），否则一律推荐用 `std::function`。

## 属性详解

`get`/`set` 支持多种写法。下面的 `TCLASS` 表示被绑定的类。

**Getter**（返回属性值；返回值不能是 void）：
```cpp
cat.get("name", &Cat::name);                       // 成员函数：  P (Cat::*)() const
cat.get("total", &globalCount);                    // 自由函数：  P (*)()
cat.get("label", &describe);                       // 自由函数：  P (*)(const Cat&)
cat.get("w", [](Cat& c){ return c.weight(); });    // lambda：   []( [const] Cat& )->P
cat.get("k", []{ return 3.14; });                  // lambda：   []()->P
```

**Setter**（接收新值；返回值会被忽略）：
```cpp
cat.set("name", &Cat::setName);                    // 成员函数：  R (Cat::*)(P)
cat.set("flag", &setGlobalFlag);                   // 自由函数：  R (*)(P)
cat.set("w", &applyWeight);                        // 自由函数：  R (*)(Cat&, P)
cat.set("w", [](Cat& c, float v){ c.setW(v); });   // lambda：   [](Cat&, P)
cat.set("k", [](float v){ /*...*/ });              // lambda：   [](P)
```

**访问规则。** 只有 getter → 只读；写入会直接报错 `attempt to write Read-Only property '...'`。只有 setter → 只写；读取会直接报错 `attempt to read Write-Only property '...'`。访问未定义的属性返回 `nil`。

**模块属性**的绑定方式跟类一样，区别在于 getter/setter 不带 `self` 参数（模块不像对象有实例）。模块的 getter 返回值不能是 void（由 `static_assert` 在编译期强制保证）。

## `def`：常量、数组与内嵌实例

`def` 用来发布一些“准只读”的数据。

```cpp
// 类和模块都可以用：标量常量或字符串
mod.def("version", 3);
mod.def("name", "shelter");

// 仅模块可用：把一个 C 数组变成 Lua 数组表
static const int primes[] = { 2, 3, 5, 7 };
mod.def("primes", primes, sizeof(primes)/sizeof(primes[0]));

// 仅模块可用：把一个绑定类的实例内嵌到模块表中。
// obj 填 nullptr 会默认构造一个；传入析构器可以控制清理行为。
LuaClass<Cat> cat(L, "Cat"); cat.ctor();
mod.def("mascot", cat);                 // shelter.mascot 是一个 Cat 对象
mod.def("mascot", cat, existingCatPtr); // 也可以内嵌一个已有的对象
```

## 元方法

你可以通过 `fun` 来绑定 Lua 的元方法。绝大多数（`__tostring`、`__add`、`__len`、`__eq` 等）直接注册就行：

```cpp
cat.fun("__tostring", &Cat::toString);   // print(obj) / tostring(obj)
```

但 `__index`、`__newindex` 和 `__gc` 这三个比较特别，因为 luaaa 内部依赖它们来实现属性和 GC。当你绑定了这几个元方法时，luaaa 会保留自己的分发逻辑，然后把你提供的实现**作为后备**，在内置方法和属性查找执行完毕之后再调用。也就是说，你自定义的 `__index` 只处理那些既不是绑定方法也不是注册属性的键；而你自定义的 `__gc` 会在对象的析构逻辑之外额外执行一遍。

实例读取（`obj.key`）时的查找顺序为：绑定方法与常量 → 已注册的 getter → 你的 `__index`（函数或表）→ *（如果有 setter）* 只写报错 → `nil`。

## 多个 `lua_State`

luaaa 会把每个类型的 Lua 名字保存在一个**与 lua_State 绑定的注册表**里。这意味着同一个 C++ 类型可以在**不同的 state 当中用不同的名字**导出：

```cpp
LuaClass<Widget>(A, "Button").ctor().fun("get", &Widget::get);  // state A
LuaClass<Widget>(B, "Slider").ctor().fun("get", &Widget::get);  // state B
// state A 里 Button.new():get() 和 state B 里 Slider.new():get() 都能正常工作。
```

但在**同一个** state 内部，一个 C++ 类型只能对应**一个** Lua 名字。如果在同一个 state 里把同一个类型绑定到第二个名字，会直接触发冲突错误。如果确实需要在一个 state 里为同一个类型提供两种不同的 Lua 视角，那就把它封装成两个不同的 C++ 类型（比如写两个简单的子类）—— 这才是靠谱的做法。

## 嵌入式 / 无标准库构建

面向单片机等无操作系统的自由（freestanding）运行环境时，在引入头文件**之前**定义以下宏：

```cpp
#define LUAAA_WITHOUT_CPP_STDLIB 1
#include "luaaa.hpp"
```

通常配合 `-fno-exceptions -fno-rtti` 一起编译。完整的可运行示例参见 `example/embedded.cpp`。

在这个模式下，**下面的特性不可用**（因为它们依赖 STL）：`std::string`、`std::function`、所有的 STL 容器转换、`std::tuple`/`std::pair`，以及 lambda 绑定（lambda 底层依赖 `std::function`）。

**替代方案：**
* 用 `const char*` 处理文本（在你的类里用定长缓冲区存放）。
* 用 `def(name, array, length)` 把 C 数组变成 Lua 表。
* 用**裸函数指针**接收回调（参见[回调详解](#回调详解)）。
* 在方法参数里加上 `lua_State*`，手动从栈上读取表和参数：
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

**自行提供 `operator new`/`delete`。** luaaa 用 placement `new` 在 userdata 内存块里构造对象；但无标准库的环境里没有自带的分配器。你需要自己实现（在已有标准库的环境里用宏把它们隔离掉，避免重定义冲突）：
```cpp
#ifdef LUAAA_FREESTANDING
void* operator new(size_t, void* p) noexcept { return p; }
void  operator delete(void*, void*) noexcept {}
void* operator new(size_t n)            { return malloc(n); }
void  operator delete(void* p) noexcept { free(p); }
#endif
```

**关于 RTTI。** 用 `-fno-rtti` 编译时，luaaa 无法读取类型名称，所以 `not export` 和类型冲突的错误信息里，类型名会显示为 `?` 而不是 C++ 的具体名字。功能上完全不受影响，只是错误提示会简略一些。

## 特性宏

以下宏需要在 `#include "luaaa.hpp"` **之前**定义。

| 宏 | 默认值 | 作用 |
|---|---|---|
| `LUAAA_WITHOUT_CPP_STDLIB` | `0` | 去掉所有 C++ 标准库的依赖（启用嵌入式模式）。 |
| `LUAAA_FEATURE_PROPERTY` | `1` | 启用 `get`/`set` 属性机制（即 `__index`/`__newindex` 底层）。 |
| `LUAAA_FEATURE_EXTEND` | `1` | 自动注入 `luaaa:extend` 和 `luaaa:base` 以支持 Lua 侧的类继承。 |
| `LUAAA_CHECK_CONSTRUCTOR_NAME_CONFLICT` | `1` | 两个构造函数使用了相同的 Lua 名字时打印警告。 |
| `LUAAA_DEBUG` | `0` | 启用 `LUAAA_DUMP(L)` 栈内容转储的辅助功能。 |

## 对象生命周期与 GC 所有权

对象的析构由谁负责，取决于它是怎么进入 Lua 的：

| 创建方式 | GC 时的行为 |
|---|---|
| `ctor<Args...>()`（placement 构造） | 对象存在于 userdata 内部；GC 时自动调用 `~T()` |
| `ctor(name, spawner)`（生成器） | `delete` 生成器返回的指针（如果 `T` 不可析构则什么事都不做） |
| `ctor(name, spawner, deleter)`（生成器 + 自定义析构器） | GC 时调用你的 `deleter(T*)` |
| `ctor(name, spawner, nullptr)`（生成器 + 空析构器） | Lua 永远不销毁它（借用/单例场景） |
| 绑定函数以 `T*` 或 `T&` 返回 | 作为轻量引用压栈；Lua **不**拥有所有权，也不会回收它 |

把对象的指针/引用返回给 Lua，相当于给了 Lua 一个句柄，可以在别的绑定函数之间传来传去，但 Lua 不会管理它的生命周期 —— 它的存活需要你自己来保证。

## 兼容性说明

* **Lua 版本。** 支持 5.1、5.2、5.3、5.4 以及 LuaJIT。针对 5.1/LuaJIT，luaaa 会自行补充它需要的少数 5.2+ 辅助函数（`luaL_setfuncs`、`lua_rawgetp` 等）。
* **模块注册方式。** 在 Lua > 5.1 且没有定义 `LUA_COMPAT_MODULE` 的情况下，模块会以普通的全局表形式创建（现代风格）；否则走老的 `luaL_openlib` 路径。这一切都是自动判断的。
* **C++ 标准。** 最低要求 C++11。C++14 及以上会启用更快的 `std::tuple` 转换路径；同时也带了 C++11 的备选方案，所以 tuple 在两种标准下都能用。

## 排错 / FAQ

**`cpp class 'X' not export`** —— luaaa 被要求转换一个它还没有绑定或特化的类型。常见原因：(1) 你在当前 state 里的 `LuaClass<X>` 构造完成之前就调用了方法；(2) 绑定的函数签名中用到了没有特化的标量类型（比如 `enum` 或裸结构体）—— 给它补一个 `LuaStack` 就行；(3) 你把这个对象用在了跟绑定时不同的 `lua_State` 里。

**`C++ class '...' bind to conflict lua name`** —— 你在同一个 state 里把同一个 C++ 类型绑定到了两个不同的 Lua 名字。解决方法：每个 state 用一个名字；或者用不同的 state；或者把它封装成不同的包装类型（参见[多个 lua_State](#多个-lua_state)）。

**静态方法用 `Class.method()` 调用报 "nil value"** —— 通过 `fun` 绑定的静态/自由函数是按方法方式调用的，所以要用 `instance:method(args)` 的写法。冒号前面的实例充当被跳过的 `self`。

**`attempt to read Write-Only` / `write Read-Only property`** —— 这个属性你只注册了 setter（只写）或只注册了 getter（只读）。如果两个方向都需要，把缺的那个访问器也注册上就好。

**lambda 绑不上去** —— 嵌入式模式下 lambda 不可用（因为需要 `std::function`），请改用自由函数指针。普通模式下，注意 lambda 不能是泛型的（参数不能用 `auto`），否则编译器推导不出它的签名。

**回调跑一阵就崩了** —— 裸函数指针的回调是按签名单槽的，且不支持重入；`std::function` 回调则需要确保它不会比对应的 `lua_State` 活得更久。详见[回调详解](#回调详解)。
