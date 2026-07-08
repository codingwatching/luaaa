# luaaa

[English](README.md) | **中文** · 进阶指南：[GUIDE.md](GUIDE.md) | [GUIDE.zh-CN.md](GUIDE.zh-CN.md)

**用一个头文件把 C++ 类和函数绑定到 Lua —— 无需代码生成，无需手写包装层。**

```cpp
LuaClass<Cat>(L, "Cat")
    .ctor<std::string>()
    .fun("eat", &Cat::eat)
    .get("name", &Cat::name).set("name", &Cat::setName);
```
```lua
local c = Cat.new("Bingo")
c:eat{ "fish", "milk" }
c.name = "Bingo the Brave"
```

核心就这么简单。下面由浅入深地带你走一遍。

---

## 目录

1. [为什么用 luaaa](#为什么用-luaaa)
2. [环境要求](#环境要求)
3. [安装](#安装)
4. [快速开始](#快速开始)
5. [绑定成员函数与静态函数](#绑定成员函数与静态函数)
6. [构造函数](#构造函数)
7. [属性](#属性)
8. [模块与全局](#模块与全局)
9. [在 Lua 中继承](#在-lua-中继承)
10. [把 Lua 函数当作 C++ 回调](#把-lua-函数当作-c-回调)
11. [运行示例](#运行示例)
12. [下一步](#下一步)
13. [许可证](#许可证)

---

## 为什么用 luaaa

* **单头文件。** 拷贝 `luaaa.hpp`，`#include` 即可，没有构建步骤、没有 `.cpp`、不依赖任何外部工具。
* **无需包装。** 直接绑定你**现有**的类和函数，不用为绑定而重写它们。
* **接口极小。** 三个名字几乎覆盖一切：`LuaClass`、`LuaModule`，以及（用于自定义类型的）`LuaStack`。
* **自动转换。** 数字、字符串、`std::string` 以及所有标准容器都会在 C++ 与 Lua 之间自动转换。
* **可移植。** 支持 Lua 5.1 – 5.4 与 LuaJIT，并提供一个不依赖 C++ 标准库的嵌入式模式。

## 环境要求

* 一个 C++11 编译器（C++14 及以上会启用更快的 `std::tuple` 路径，但并非必需）。
* Lua 5.1、5.2、5.3、5.4 或 LuaJIT —— 构建时能找到其头文件与库。

## 安装

luaaa 是纯头文件库。把 `luaaa.hpp` 放进你的项目并包含它：

```cpp
#include "luaaa.hpp"
using namespace luaaa;   // 可选，但本文示例默认已引入
```

除了 Lua 和 C++ 标准库之外，它不依赖任何东西。

## 快速开始

假设你已经有这样一个普通的 C++ 类 —— 它对 Lua 一无所知：

```cpp
class Cat {
public:
    explicit Cat(const std::string& name) : m_name(name), m_age(1) {}
    void eat(const std::list<std::string>& foods);   // 接收一个列表
    const std::string& name() const;
    void setName(const std::string& n);
private:
    std::string m_name;
    int m_age;
};
```

把它绑定到一个已经创建好的 `lua_State* L`：

```cpp
#include "luaaa.hpp"
using namespace luaaa;

void bind(lua_State* L) {
    LuaClass<Cat>(L, "Cat")          // 把 C++ 的 Cat 以 "Cat" 之名暴露给 Lua
        .ctor<std::string>()          // 一个接收字符串的构造函数
        .fun("eat", &Cat::eat)        // 一个方法
        .get("name", &Cat::name)      // 一个可读属性……
        .set("name", &Cat::setName);  // ……它同时也可写
}
```

现在 Lua 就能用它了 —— 传给 `eat` 的数组会自动变成 `std::list<std::string>`：

```lua
local c = Cat.new("Bingo")     -- 调用 C++ 构造函数
c:eat{ "fish", "milk" }        -- Lua 表 -> std::list<std::string>
print(c.name)                  --> Bingo
c.name = "Bingo the Brave"     -- 调用 setName()
```

对象的生命周期交由 Lua 的垃圾回收器管理：当 `c` 被回收时，`Cat` 的析构函数会被调用。

## 绑定成员函数与静态函数

`fun` 能绑定任何可调用体：成员函数、`static` 成员、自由函数、lambda。

```cpp
class Cat {
public:
    void eat(const std::list<std::string>&);   // 成员函数
    static void meow(const std::string& who);  // 静态函数
    std::string toString() const;              // 用于 __tostring
};

LuaClass<Cat>(L, "Cat")
    .ctor<std::string>()
    .fun("eat",  &Cat::eat)
    .fun("meow", &Cat::meow)              // 静态成员，绑定方式与普通方法相同
    .fun("__tostring", &Cat::toString)    // Lua 元方法同样适用
    .fun("play", [](int minutes) {        // lambda 也可以
        printf("played for %d min\n", minutes);
    });
```

```lua
local c = Cat.new("Bingo")
c:eat{ "fish" }
c:meow("Bingo")        -- 静态成员同样通过实例来调用
print(c)               -- __tostring：打印 "Bingo (1y)"
c:play(10)
```

> 注意：用 `fun` 绑定的 `static`/自由函数是**当作方法来调用**的 —— `c:meow("Bingo")`。冒号前的实例会作为隐藏的 `self` 传入并被跳过，其余参数依次对应函数的形参。

## 构造函数

每个类至少需要一个构造函数。模板参数就是 C++ 构造函数的形参类型；字符串是它在 Lua 侧的名字（默认为 `"new"`）。

```cpp
LuaClass<Cat>(L, "Cat")
    .ctor()                       // Cat.new()          -> new Cat()
    .ctor<std::string>("create"); // Cat.create("Tom")  -> new Cat("Tom")
```

```lua
local a = Cat.new()
local b = Cat.create("Tom")
```

你可以用不同的名字注册多个构造函数。工厂函数、单例以及自定义析构器请见[进阶指南](GUIDE.zh-CN.md#构造函数详解)。

## 属性

`get` 和 `set` 把 C++ 的取值/赋值函数变成 Lua 的字段，于是 Lua 用普通的 `.字段` 语法就能读写它们。

```cpp
LuaClass<Cat>(L, "Cat")
    .ctor<std::string>()
    .get("name", &Cat::name).set("name", &Cat::setName)   // 可读可写
    .get("age",  &Cat::age ).set("age",  &Cat::setAge);
```

```lua
local c = Cat.new("Bingo")
print(c.name)      -- 调用 name()
c.age = 3          -- 调用 setAge(3)
```

* 只有 getter → **只读**（写入会抛出 Lua 错误）。
* 只有 setter → **只写**（读取会抛出 Lua 错误）。
* 两者都有 → 可读可写。

getter 与 setter 也可以是自由函数或 lambda（可带、也可不带 `Cat&` 作为第一个参数）。详见[属性详解](GUIDE.zh-CN.md#属性详解)。

## 模块与全局

`LuaModule` 把自由函数和常量归拢到一个 Lua 表里 —— 适合那些不属于某个对象的东西。

```cpp
void adopt(const std::string& name, std::function<void(const std::string&)> onDone);

LuaModule(L, "shelter")
    .def("city",     "Catville")   // 一个常量
    .def("capacity", 50)
    .fun("adopt", adopt);          // 一个自由函数
```

```lua
print(shelter.city)            --> Catville
shelter.adopt("Bingo", function(who) print(who .. " adopted!") end)
```

不给模块名（或用 `"_G"`），就能把东西直接放进 Lua 的全局：

```cpp
LuaModule(L).def("pi", 3.14159);   // 全局的 pi
```

## 在 Lua 中继承

只要你绑定了任意一个类，luaaa 就会往那个 `lua_State` 注入两个辅助函数 —— 无需粘贴任何胶水代码：

* `luaaa:extend(Base, fields)` —— 为一个已导出的类创建子类（`fields` 可选）。
* `luaaa:base(obj)` —— 取得某个实例底层的 C++ 对象（用于调用被覆盖的方法）。

```lua
local SpecialCat = luaaa:extend(Cat, { tricks = 0 })

function SpecialCat:learn() self.tricks = self.tricks + 1 end

function SpecialCat:meow(who)          -- 覆盖……
    print(self.name .. " purrs first")
    luaaa:base(self):meow(who)         -- ……再调用 C++ 的方法
end

local felix = SpecialCat:new("Felix")
felix:learn()
felix:meow("Felix")
```

## 把 Lua 函数当作 C++ 回调

C++ 函数可以接收一个 Lua 函数。只需把参数声明为 `std::function`（推荐）或普通函数指针：

```cpp
void onEach(std::function<int(int)> cb);   // 推荐
void onEvent(int (*cb)(const char*));      // 原始指针（有限制）
```

```lua
onEach(function(x) return x * 2 end)
```

优先使用 `std::function` 形式：它自己持有对 Lua 函数的引用，因此可以被保存、拷贝、多次调用，并且可重入。两种形式的区别以及原始指针形式的注意事项，见[回调详解](GUIDE.zh-CN.md#回调详解)。

## 运行示例

[`example/`](example/) 目录里有一个完整可运行的故事 —— *小小猫咪收容所*，外加一个嵌入式（不依赖标准库）版本 —— *喂食器节点*。

```bash
bash example/build.sh            # 编译并运行两者
bash example/build.sh desktop    # 只跑完整标准库示例
bash example/build.sh embedded   # 只跑无标准库示例
```

脚本会通过 `pkg-config` 自动探测 Lua。如果找不到你的 Lua，手动指定路径：

```bash
LUA_CFLAGS=-I/usr/include/lua5.4 LUA_LIBS=-llua5.4 bash example/build.sh
```

或者手动编译：

```bash
cd example
c++ -std=c++11 example.cpp -I/usr/include/lua5.4 -llua5.4 -lm -o example && ./example
```

## 下一步

日常使用需要的东西你已经全部见过了。**[进阶指南](GUIDE.zh-CN.md)** 覆盖其余内容，可当作参考手册与 FAQ：

* 自动类型转换，以及完整的容器 / `pair` / `tuple` 支持表
* 用 `LuaStack` 教会 luaaa 你自己的类型
* 构造函数的各种形态：工厂、单例、自定义析构器、命名冲突
* 重载函数与消歧
* 回调详解（`std::function` 对比原始指针）
* 元方法（`__index`、`__newindex`、`__gc`、`__tostring`）
* 多个 `lua_State`
* 面向单片机的**嵌入式 / 无标准库**构建
* 特性宏、GC 所有权规则与排错

## 许可证

MIT。见 [LICENSE](LICENSE)。
