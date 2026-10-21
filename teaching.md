如果要用C++和Qt制作一个简单的画图程序，并且这个程序支持通过命令自动化画图（比如键入简单的画线命令和坐标可以画线，还要支持简单的变量，流程控制等），并且命令执行和图形界面深度融合（比如键入画线命令但没输入坐标，就进入画线命令且给出操作提示，类似于AutoDesk AutoCAD界面的命令行）。在C++中目前有哪些有哪些通用的命令解释相关的库，或者AST/上下文无关文法解析相关的库能在命令语言中动态添加程序功能（比如开发一个新的项目，不是画图程序，仍能通过这样的库集成命令解析功能）？

你的需求实际上涉及一个比较有意思的方向：**在 C++ 中实现一套可嵌入不同应用程序、支持脚本编程，并能与 Qt 图形界面双向交互的通用命令解释系统。**

这不只是一个画图程序的命令行，而是可以进一步发展成一个小型的**可扩展命令语言框架（Extensible Command Language Framework）**。

目前 C++ 生态中确实有不少成熟的库可以帮助实现它，其中有些支持直接执行脚本，有些支持定义自己的语法、生成 AST，也有些适合构建交互式命令解释器。

## 一、先明确：你的需求实际上包含三个不同层次

以 AutoCAD 风格的命令行为例：

**模拟命令行交互** 示意

Command: LINE

指定第一个点：10,20

指定第二个点：100,100

直线创建成功

---

Command: LINE

指定第一个点：  等待命令行输入或鼠标点击

要实现这样的效果，需要区分三个概念：

| 层次    | 职责                        | 典型技术                                  |
| ----- | ------------------------- | ------------------------------------- |
| 语法解析层 | 识别命令、参数、变量、表达式、循环、条件判断    | ANTLR4、PEG、Boost.Spirit               |
| 脚本执行层 | 管理变量、函数调用、作用域、控制流和执行上下文   | Lua、ChaiScript、QJSEngine，或自定义 AST 解释器 |
| 交互命令层 | 处理参数不完整、等待用户点选、实时预览、取消及恢复 | C++ 命令状态机、Qt 事件系统                     |

**其中最关键的是第三层。**

比如输入 `LINE` 后，程序不能简单地认为语法错误，也不能在同步函数内部阻塞等待鼠标点击，而应当创建一个可暂停、可恢复的命令会话。

后续不论坐标来自命令行、鼠标、脚本变量还是外部接口，都应当通过同一个命令执行系统处理。

这也是让整个系统能够从画图软件迁移到其他项目的核心。

## 二、可以直接嵌入 C++ 的脚本解释器

如果你希望命令语言直接支持变量、循环、条件语句和自定义函数，优先考虑成熟的嵌入式脚本引擎，而不是从零开发 AST 解释器。

| 库           | 支持的语言            | 主要优势                     | 与你的需求匹配度 |
| ----------- | ---------------- | ------------------------ | -------- |
| QJSEngine   | JavaScript       | Qt 原生集成，可直接暴露 QObject 方法 | (非常高)    |
| Lua + sol2  | Lua              | 轻量、成熟，支持协程，方便注册 C++ 函数   | (非常高)    |
| ChaiScript  | 类 C++/JavaScript | 专门面向 C++ 嵌入式脚本，绑定较自然     | (高)      |
| AngelScript | 类 C++            | 类型系统较强，适合应用内脚本扩展         | (高)      |
| ExprTk      | 数学表达式及小型语言       | 数学计算、变量、条件与循环，适合参数化命令    | (特定场景很高) |

以上库的基本能力可以从各自的官方文档核实。([doc.qt.io][1], [github.com][2], [github.com][3], [angelcode.com][4], [github.com][5])

### 1. QJSEngine：与 Qt 集成最方便

这是我建议你首先测试的方案。

Qt 自带 `QJSEngine`，能够在 C++ 应用程序中执行 JavaScript，并通过 Qt 元对象系统将 C++ 类和方法暴露给脚本。

例如，你可以设计这样的命令：

执行之后，绘图程序中出现五条平行线。

对应的 C++ 代码可以这样实现：

这段代码展示的是基本的脚本调用机制。正式项目中应当让 `DrawApi::line()` 调用统一的命令执行器，而不是直接修改 `QGraphicsScene`，这样才能统一处理撤销、记录和校验。

QJSEngine 的几个优势是：

* 变量、表达式、函数、条件和循环由现成 JavaScript 引擎负责。
* 通过 `Q_INVOKABLE` 或适当的 Qt 接口暴露应用功能。
* 在运行时注册不同应用程序的对象和方法。
* 与 Qt Widgets 和 Qt Quick 都可以结合使用。

但有一个限制：**`QJSEngine::evaluate()` 是同步执行脚本的，不会自动把一个缺失参数的 C++ 函数变成等待鼠标事件的可暂停命令。** 交互状态仍需额外实现，耗时脚本也必须避免阻塞 GUI 事件循环。([doc.qt.io][1], [doc.qt.io][6])

官方文档：[QJSEngine](https://doc.qt.io/qt-6/qjsengine.html)

### 2. Lua + sol2：更适合长期开发可扩展命令平台

如果你不仅打算做画图软件，而且准备开发一套通用的脚本化应用框架，那么 Lua 值得重点考虑。

`sol2` 是 Lua 与 C++ 的绑定库，不是另一个脚本解释器。它允许你较方便地把 C++ 函数和类型注册进 Lua。

例如：

这里的 `line` 可以在运行时注册，之后完全可以改为：

这样同一个 Lua 解释器就能应用于绘图、设备管理、仿真、数据处理等不同程序。

**Lua 还有一个非常值得关注的功能：协程（coroutine）。**

例如，理论上可以设计这样的交互脚本：

其中 `requestPoint()` 可以通过协程机制挂起脚本，在 GUI 收到点选操作之后，再恢复脚本执行。

不过这需要自行设计协程调度和 C++ 绑定层。Lua 的 C API 对跨 C 函数边界的挂起有明确限制，必要时需使用 `lua_yieldk` 等延续机制；并不是直接绑定一个同步 C++ 函数就能实现。([github.com][2], [lua.org][7])

官方项目：[sol2](https://github.com/ThePhD/sol2) · [Lua](https://www.lua.org/)

### 3. ChaiScript 和 AngelScript：希望脚本语言更接近 C++

如果你不喜欢 Lua 语法，希望脚本与 C++ 编程方式较为相似，可以考虑这两个库。

**ChaiScript** 的典型注册方式如下：

它提供变量、函数、控制流以及 C++ 函数绑定，库本身以头文件方式集成为主。其优势是适合 C++ 开发者直接嵌入脚本能力。([github.com][3])

**AngelScript** 则更像专门嵌入应用程序的 C++ 风格脚本语言，可以显式注册函数签名、对象和方法，例如：

它更适合希望对脚本 API 的类型和调用方式进行严格控制的项目。([angelcode.com][4])

### 4. ExprTk：容易被忽略，但很适合参数化命令

值得特别强调，ExprTk 并非只能计算简单的数学表达式。

根据其官方文档，它还支持变量、符号表、自定义函数以及 `if`、`for`、`while` 等流程控制语句。([github.com][5])

假设你的画图程序主要是基于坐标和公式生成图形，ExprTk 就很适合作为计算层：

例如命令中允许：

将坐标表达式交给 ExprTk 求值，而命令解析和 GUI 交互自己实现。

其优势是无需引入完整的通用脚本语言，能够保留更强的命令语法自主性。

不过，如果未来需要复杂对象、插件 API、丰富的数据类型以及交互式脚本执行，那么 Lua 或 QJSEngine 通常更灵活。

---

## 三、如果想自己定义语法：重点考虑这些 AST / 文法解析库

假如你希望采用类似这样的语言：

那么直接使用 JavaScript 或 Lua 解释器就不一定是最理想的，因为你希望掌握自己的语言形式，包括 `LINE`、`FOR`、`END`、坐标表达式以及自动补充参数的行为。

这时就应该考虑 Parser Generator 或 Parsing Library。

### 主要库对比

| 库                   | 文法机制            | AST 支持       | 文法运行时可调整     | 适合场景        |
| ------------------- | --------------- | ------------ | ------------ | ----------- |
| **cpp-peglib**      | PEG             | 内建 AST 生成    | 可以加载新的文法文本   | 自定义命令语言、DSL |
| **ANTLR4**          | ANTLR 文法        | 生成解析树，可转 AST | 一般需重新生成解析器   | 复杂、规范的脚本语言  |
| **PEGTL**           | C++ 模板 PEG      | 自定义构建        | 主要在编译期定义     | 纯 C++ 解析器   |
| **Boost.Spirit X3** | 解析器组合子          | 通过属性及自定义结构构建 | 主要在编译期定义     | 深度 C++ 集成   |
| **Tree-sitter**     | 增量解析            | 生成语法树        | 通常需重新生成语言解析器 | 实时编辑、语法提示   |
| **Flex + Bison**    | 词法分析 + CFG 系列解析 | 自定义构建        | 一般需重新生成解析器   | 传统编译器与解释器   |

这里有一个概念需要区分：PEG（Parsing Expression Grammar，解析表达文法）和传统上下文无关文法 CFG 并不完全等价，尤其是有序选择与歧义处理的语义不同。它们都可用于构建你的命令语言，但不能把 PEG 简单看成 CFG 的另一种写法。

上述能力依据各项目文档整理。([github.com][8], [github.com][9], [github.com][10], [boost.org][11], [tree-sitter.github.io][12])

### 重点推荐：cpp-peglib

项目地址： [github.com/yhirose/cpp-peglib](https://github.com/yhirose/cpp-peglib)

如果你想开发一个通用的 C++ 命令解释框架，**我认为 cpp-peglib 尤其适合做第一版自定义语言的解析器**。

原因在于它具有以下几个很实用的特征：

* 单头文件，基于 C++17。
* 可以使用字符串定义 PEG 文法，并在程序运行时加载。
* 支持通过 `enable_ast()` 创建 AST。
* 支持语义动作（Semantic Actions）。
* 具备左递归处理、错误恢复等机制。
* 解析器与应用程序执行逻辑可以分离。

例如，官方 API 允许这样创建解析器：

这只是一个简单的命令语法示例，尚未支持坐标对、变量、表达式及流程控制。完整的语言还需要扩展文法，并实现 AST 求值器。

但它表明一个重要事实：**语法解析器和业务命令系统可以是两套独立的组件。**

例如，解析器识别出：

并得到概念上的：

之后只需要在命令注册表中查找 `LINE`，并调用相应的 C++ 函数。

如果新程序增加 `CREATE_DEVICE` 命令，只要语法仍属于相同的命令调用结构，就无需修改文法，只需注册新的执行函数。

需要注意，cpp-peglib 的 AST 节点会引用原始输入文本，因此必须妥善管理源字符串的生命周期。([github.com][8], [github.com][8])

### ANTLR4 与 cpp-peglib 应该怎么选？

当语言越来越复杂，例如需要函数定义、嵌套作用域、类型检查、错误恢复，以及 IDE 风格的诊断信息时，ANTLR4 很值得考虑。

ANTLR4 采用 `.g4` 文件定义语言，通过工具生成 C++ Lexer、Parser、Visitor 和 Listener。([github.com][9])

而 cpp-peglib 可以在运行时加载 PEG 文法，因此在希望程序能够加载不同语言配置时更方便。

但是有一个重要设计原则：

**动态新增业务命令，不等于必须动态修改语言文法。**

理想情况下，基础语言解析规则相对固定，程序提供的命令通过动态注册改变。这样更稳定，也更容易维护。

Tree-sitter 可以作为另一个补充。它擅长在用户每次修改脚本时增量更新语法树，因此适合以后为命令编辑器增加语法高亮、错误标记和智能提示，但它不负责执行 AST。([tree-sitter.github.io][12])

---

## 四、真正决定能否实现 AutoCAD 交互的，是 CommandRegistry + CommandSession

对于你描述的需求，我建议不要让解释器直接持有并控制 GUI，而是建立一个独立的命令执行框架。

一个比较合理的架构是：

通用命令框架架构

三种输入来源最终调用同一个命令系统。业务模型负责真实操作，GUI 负责显示和交互。

这个框架中有两个特别重要的类。

### 1. CommandRegistry：动态注册应用程序功能

它可以提供这样的接口（架构示意）：

例如：

对于新开发的设备管理软件，可以注册：

这样，命令解释框架不用知道绘图、设备或者仿真是什么。

它只需要知道命令名称、参数定义、执行函数，以及执行过程中需要如何交互。

而且，这种声明式的参数描述还能够用于生成命令帮助、自动补全、输入提示和参数校验。

### 2. CommandSession：实现半输入命令和鼠标操作

可以为交互式命令建立一个统一接口：

这里 `StepResult` 是你自己定义的结果类型，可以区分 `NeedInput`、`Completed`、`Error` 和 `Cancelled`，并附带下一步所需的输入类型和提示文本。

例如，`LINE` 命令可以有三个状态：

等待起点

输入坐标或点击画布

等待终点

动态预览，继续输入终点

执行完成

提交图元并生成撤销记录

重点是，下面几种调用形式全部可以复用同一套命令逻辑：

区别只在于已知参数的数量。

当参数不足时，`CommandSession` 返回 `NeedInput`，告诉界面当前需要什么，而不是向用户返回普通语法错误。

如果使用 JavaScript 脚本，则可以把完整调用设计为：

它会直接调用相同的 `CommandExecutor`，不必创建鼠标交互会话。

对于 GUI，可以用 `QGraphicsView::mapToScene()` 将鼠标坐标转换为画布坐标，再将结果提交给 `CommandSession`。鼠标移动时使用临时 `QGraphicsItem` 预览，完成后才正式修改文档模型。Qt Graphics View 为这类交互提供了现成的事件机制。([doc.qt.io][13])

另外还应集成 `QUndoStack` / `QUndoCommand`，确保命令行绘图、鼠标绘图和批量脚本绘图采用一致的撤销机制。([doc.qt.io][14])

---

## 五、如何把命令框架移植到新的 C++ 项目？

我建议将通用模块进一步拆成：

其中，`CommandCore` 最好不依赖 `QGraphicsScene`、主窗口或任何绘图类。

甚至可以进一步让核心层仅依赖标准 C++，通过适配器接入 Qt。

未来开发新程序时，你只需新增业务命令，例如：

而变量、流程控制、命令历史、解析器、命令注册和交互会话都可以复用。

如果希望新功能以 DLL 或共享库形式安装，可以利用 Qt 的 `QPluginLoader` 实现插件加载。插件导出统一接口，加载时将业务命令注册到 `CommandRegistry`，无需在主程序中硬编码每个命令。([doc.qt.io][15])

这会比在解释器里直接写一大串 `if/else` 判断命令名称可维护得多。

## 六、一个值得参考的现有项目：QCAD

**QCAD**

QCAD 是与你的目标相当接近的实际项目。它提供 ECMAScript 脚本接口、交互式绘图操作以及脚本命令行。

官方提供 Script Shell、脚本操作和命令行工具相关的开发文档。([qcad.io][16])

尤其值得参考：

* [QCAD Script Shell 教程](https://www.qcad.org/doc/qcad/latest/reference/en/tutorials/development/the_script_shell/the_script_shell_en.html)
* [QCAD 脚本开发教程](https://qcad.io/en/tutorial-script-programming)

QCAD 的脚本控制台可以使用 `addLine(0,0,100,100)` 绘制直线，也可以执行循环来批量绘图。([qcad.org][17])

不过，QCAD 的实现是一个完整 CAD 产品中的应用架构，并不是可直接拿来作为独立通用命令解释器的单一库。可以借鉴它的脚本 API 与交互操作分层设计。

---

## 七、最终该怎么选？

针对你的设想，我会将技术路线归纳为三个选项：

**方案 A：Qt + QJSEngine + CommandRegistry + CommandSession**

最适合快速完成第一版。变量、循环和判断直接使用 JavaScript，交互式命令由自定义 C++ 状态机实现。开发量相对小，也容易验证 GUI 与命令行的协同效果。

---

**方案 B：Qt + cpp-peglib + 自定义 AST 解释器 + CommandSession**

最适合希望创造一套自己的命令语言，例如 `LINE 0,0 100,100`、`LET`、`FOR`、`IF`，并让其长期复用到不同应用程序的情况。语法控制权最大，但需要自己实现变量作用域、表达式求值与执行流程。

---

**方案 C：Qt + Lua/sol2 + CommandRegistry + 协程调度器**

适合准备长期建设脚本平台，并希望脚本执行过程中也能自然暂停、等待 GUI 事件再继续的场景。不过协程与 Qt 事件循环之间需要专门设计调度层。

补充一点：`CLI11`、`cxxopts`、`Boost.Program_options` 等常见命令行解析库，主要针对程序启动参数和命令行选项解析，不适合作为你这里的完整交互式脚本语言引擎。

**我的建议是从方案 A 入手，但按照方案 B 的可复用框架思路设计核心接口。**

先完成四个验证功能：`LINE` 直接绘图、`LINE` 缺少参数时进入交互、鼠标点击与文本坐标混合输入、通过变量和循环自动批量绘图。

只要把统一命令注册与交互会话这两个部分设计好，未来更换 JavaScript 为自定义 AST 解释器或者 Lua，并不需要重写整个 GUI 和业务命令系统。

最值得从一开始就确定的原则是：**命令的含义由业务模块定义，命令的语法由语言模块解析，命令的交互生命周期由会话模块管理。三者相互独立，才能真正实现你希望的跨项目复用。**

[1]: https://doc.qt.io/qt-6/qjsengine.html "QJSEngine Class | Qt Qml | Qt 6.11.2"
[2]: https://github.com/thephd/sol2 "GitHub - ThePhD/sol2: Sol3 (sol2 v3.0) - a C++ <-> Lua API wrapper with advanced features and top notch performance - is here, and it's great! Documentation: · GitHub"
[3]: https://github.com/ChaiScript/ChaiScript "GitHub - ChaiScript/ChaiScript: Embedded Scripting Language Designed for C++ · GitHub"
[4]: https://www.angelcode.com/angelscript/sdk/docs/manual/doc_register_func.html "AngelScript: Registering a function"
[5]: https://github.com/ArashPartow/exprtk/blob/master/readme.txt "exprtk/readme.txt at master · ArashPartow/exprtk · GitHub"
[6]: https://doc.qt.io/qt-6/qtjavascript.html "Making Applications Scriptable | Qt Qml | Qt 6.12.0"
[7]: https://www.lua.org/manual/5.4/manual.html?external_link=true "Lua 5.4 Reference Manual"
[8]: https://github.com/yhirose/cpp-peglib "GitHub - yhirose/cpp-peglib: A single file C++ header-only PEG (Parsing Expression Grammars) library · GitHub"
[9]: https://github.com/antlr/antlr4/blob/dev/doc/cpp-target.md "antlr4/doc/cpp-target.md at dev · antlr/antlr4 · GitHub"
[10]: https://github.com/taocpp/PEGTL "GitHub - taocpp/PEGTL: Parsing Expression Grammar Template Library · GitHub"
[11]: https://www.boost.org/doc/libs/latest/libs/spirit/doc/x3/html/spirit_x3/tutorials/minimal.html "https://www.boost.org/doc/libs/latest/libs/spirit/doc/x3/html/spirit_x3/tutorials/minimal.html"
[12]: https://tree-sitter.github.io/ "Introduction - Tree-sitter"
[13]: https://doc.qt.io/qt-6/qgraphicsview.html "QGraphicsView Class | Qt Widgets | Qt 6.12.0"
[14]: https://doc.qt.io/qt-6/qundostack.html "QUndoStack Class | Qt GUI | Qt 6.11.2"
[15]: https://doc.qt.io/qt-6/plugins-howto.html "How to Create Qt Plugins | Qt 6.12"
[16]: https://qcad.io/en/tutorial-script-programming "QCAD - Tutorial: Script Programming"
[17]: https://www.qcad.org/doc/qcad/latest/reference/en/tutorials/development/the_script_shell/the_script_shell_en.html "The Script Shell"
