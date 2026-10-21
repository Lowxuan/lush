# AGENTS.md

> 面向在本仓库工作的 AI/人类开发者的**项目自解释手册**。
>
> - 语义唯一来源：`docs/spec.md`（当前 v0.3）
> - 当前里程碑：**M0 已完成，M1 未开始**
> - 构建方式：CMake + MSVC（Visual Studio 2022 Build Tools）
> - 核心约束：`src/lush/core` 只依赖标准库，不碰 Qt、不碰绘图

---

## 0. 一分钟了解

Lush 是一门嵌入 C++ 宿主的命令语言，目标场景是 AutoCAD 式的「命令行 + GUI 深度融合」：

- 命令可以一次写全参数直接执行；
- 参数不足时进入交互会话，由命令行或鼠标逐个补齐；
- 语言支持变量、list、Point、条件、循环、引用与一等内建函数；
- 核心与宿主解耦，未来可接 Qt / `QGraphicsScene` / `QUndoStack`。

当前仓库已经完成 **M0**：CMake 骨架、版本、CLI 占位、测试 harness、CTest。语言核心（词法/语法/求值/命令/REPL/补全）尚未实现。

### 0.1 第一件事

```powershell
git status
git log --oneline -5
cmake --preset vs2022
cmake --build --preset vs2022-debug
ctest --preset vs2022-debug
.\build\vs2022\bin\Debug\lush.exe --version
```

预期：`lush 0.1.0`，CTest 2/2 通过。

### 0.2 先读什么

| 目的 | 位置 |
|---|---|
| 了解语言语义 | `docs/spec.md` §0.1、§2–§8、§11、§15 |
| 了解当前进度 | 本文件 §3、§4 |
| 了解下一步 | 本文件 §9、§15 |
| 了解目录职责 | 本文件 §5、§6 |
| 了解构建/测试 | 本文件 §7、§10 |
| 了解编码约定 | 本文件 §11 |

---

## 1. 项目目标与非目标

### 1.1 目标

1. 自定义命令语言，语法控制权在自己手里。
2. 命令与 GUI 深度融合：缺参时挂起、提示、鼠标补点、动态预览。
3. 语言核心高度独立：换一个项目只注册新命令，不重写解析/求值/会话。
4. 补全、提示、帮助都由命令的声明式参数模式驱动。
5. 命令行、脚本、鼠标三种输入走同一套命令执行路径。

### 1.2 v1 非目标

- Qt / 图形 / 文件 / 网络 / 插件加载；
- 用户自定义函数、闭包、lambda、异常、模块；
- 多行字符串、正则、字符串插值；
- 类型标注与静态类型检查；
- 增量解析与 IDE 级错误恢复；
- 写时复制（COW）优化。

内建函数与类型构造器**是**一等值（见 spec §4、§8），但这不等于支持用户定义函数。

---

## 2. 不可违反的项目原则

1. **`docs/spec.md` 是语义唯一来源。** 代码与 spec 冲突时，以 spec 为准；要改语义，先改 spec 与 golden tests，再改代码。
2. **`src/lush/core` 不依赖 Qt、不依赖第三方库。** 只用标准库。
3. **位置决定调用写法**：语句位置是并置命令，表达式位置是后缀 `(...)` 调用。
4. **命令参数按顶层空白切词。** 复杂表达式在参数里必须加括号；运算符必须紧贴左操作数。
5. **值语义 + 显式 `ref`。** 读取/赋值默认深拷贝；`ref` 是唯一别名机制。
6. **命令不是值，函数/类型构造器是值。** 命令名出现在表达式位置是 S008。
7. **先解析、校验，再执行。** 脚本里参数不足不进入会话；只有 REPL 顶层单条交互命令可以。
8. **每个行为变更都要有测试。** 语义变更还要同步 `docs/spec.md`。
9. **保持 `/W4` 无警告。** 不靠关警告来“通过”。
10. **不提交 `build/`。** 构建产物全部忽略。
11. **未经要求不要提交 git。** 可以建议提交信息，但不要擅自 commit。
12. **不要静默发明语义。** spec 有歧义时，先提出并记录，不要按猜测实现。

---

## 3. 已确认的环境

| 组件 | 结果 |
|---|---|
| CMake | 4.4.4 · `C:\Tools\cmake-4.4.4-windows-x86_64\bin\cmake.exe` |
| MSVC | 19.44.35229 · VS 2022 Build Tools 17.14.41 |
| MSVC toolset | 14.44.35207 |
| Windows SDK | 10.0.26100.0 |
| Ninja | 1.13.2 · `C:\Tools\ninja\ninja.exe` |
| Git | 2.55.0 |
| `cl` 在 PATH | 否 |
| `vcvars64.bat` | `C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat` |
| CMake 默认 generator | Visual Studio 17 2022 |

结论：

- 默认使用 **Visual Studio 17 2022 generator**，零配置；
- 需要快速增量构建时用 **Ninja**，但必须先进入 VS developer shell；
- 标准：**C++20**；
- 不引入任何第三方依赖。

---

## 4. 当前状态

### 4.1 里程碑

| 阶段 | 状态 |
|---|---|
| M0 构建骨架 | ✅ 完成 |
| M1 词法 | ⬜ 未开始 |
| M2 语法 | ⬜ 未开始 |
| M3 求值 | ⬜ 未开始 |
| M4 命令 | ⬜ 未开始 |
| M5 会话 / REPL | ⬜ 未开始 |
| M6 补全 | ⬜ 未开始 |
| M7 收尾 | ⬜ 未开始 |

### 4.2 M0 已交付

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 顶层项目、C++20、MSVC 警告、输出目录、子目录 |
| `CMakePresets.json` | `vs2022`、`vs2022-release`、`ninja-debug` |
| `.gitignore` | 忽略 `build/`、`.vs/` 等 |
| `README.md` | 构建/运行/测试入口 |
| `src/CMakeLists.txt` | `lush_core` 静态库、`lush` 可执行 |
| `src/lush/lush.h` | 公共 umbrella header |
| `src/lush/core/version.h/.cpp` | 版本号 |
| `src/cli/main.cpp` | `--version`、`--help`、REPL 占位 |
| `tests/CMakeLists.txt` | `lush_tests` 与 CTest |
| `tests/test_harness.h` | 极简测试宏 |
| `tests/test_main.cpp` | 测试注册与运行 |
| `tests/unit_smoke.cpp` | 2 个版本 smoke tests |

### 4.3 M0 验证结果

| 配置 | 结果 |
|---|---|
| `vs2022` + Debug | ✅ 构建通过，`/W4` 无警告，2/2 tests |
| `vs2022-release` + Release | ✅ 构建通过，2/2 tests |
| `ninja-debug` | ✅ 构建通过，2/2 tests |

运行输出：

```text
> lush --version
lush 0.1.0

> lush --help
usage: lush [options]

options:
  -v, --version  显示版本
  -h, --help     显示帮助

> lush
lush 0.1.0 (M0 skeleton; REPL not implemented yet)
```

### 4.4 尚未实现

- `docs/spec.md` 中的 G01–G52 golden tests **只是规范**，代码里还没有对应测试；
- 词法/语法/求值/命令/会话/补全全部未开始；
- CLI 目前只有版本与帮助，不解析语言。

---

## 5. 目录结构

标记：`[M0]` 已存在，`[M1]`… 计划中。以实际 `git status` 为准。

```text
D:\_lush\
├─ AGENTS.md                       ← 本文件
├─ README.md                       [M0]
├─ CMakeLists.txt                  [M0]
├─ CMakePresets.json               [M0]
├─ .gitignore                      [M0]
├─ docs\
│  └─ spec.md                      [M0] v0.3
├─ src\
│  ├─ CMakeLists.txt               [M0]
│  ├─ lush\
│  │  ├─ lush.h                    [M0]
│  │  ├─ core\
│  │  │  ├─ version.h/.cpp         [M0]
│  │  │  ├─ source.h/.cpp          [M1]
│  │  │  ├─ diagnostic.h/.cpp      [M1]
│  │  │  ├─ utf8.h/.cpp            [M1]
│  │  │  ├─ token.h                [M1]
│  │  │  ├─ lexer.h/.cpp           [M1]
│  │  │  ├─ ast.h                  [M2]
│  │  │  ├─ parser.h/.cpp          [M2]
│  │  │  ├─ value.h/.cpp           [M3]
│  │  │  ├─ cell.h                 [M3]
│  │  │  ├─ eval.h/.cpp            [M3]
│  │  │  ├─ format.h/.cpp          [M3]
│  │  │  ├─ checked.h              [M3]
│  │  │  ├─ output.h               [M3]
│  │  │  └─ builtins.h/.cpp        [M3]
│  │  ├─ command\
│  │  │  ├─ spec.h                 [M4]
│  │  │  ├─ registry.h/.cpp        [M4]
│  │  │  ├─ host.h                 [M4]
│  │  │  └─ session.h/.cpp         [M5]
│  │  ├─ driver\
│  │  │  ├─ result.h               [M5]
│  │  │  └─ driver.h/.cpp          [M5]
│  │  └─ completion\
│  │     └─ completion.h/.cpp      [M6]
│  ├─ reference\
│  │  └─ reference_host.h/.cpp     [M4]
│  └─ cli\
│     ├─ main.cpp                  [M0]
│     ├─ repl.h/.cpp               [M5]
│     └─ console_sink.h/.cpp       [M5]
└─ tests\
   ├─ CMakeLists.txt               [M0]
   ├─ test_harness.h               [M0]
   ├─ test_main.cpp                [M0]
   ├─ unit_smoke.cpp               [M0]
   ├─ unit_lexer.cpp               [M1]
   ├─ unit_parser.cpp              [M2]
   ├─ unit_value.cpp               [M3]
   ├─ unit_ref.cpp                 [M3]
   ├─ unit_command.cpp             [M4]
   ├─ unit_driver.cpp              [M5]
   ├─ unit_completion.cpp          [M6]
   └─ golden_spec.cpp              [M3+]
```

### 5.1 依赖方向

```text
cli ──► driver ──► command ──► core
 │        │           │
 │        └──► completion ──► core + command
 └──► reference ──► command + core
```

- `core` 只依赖标准库；
- `command` 依赖 `core`；
- `driver` 依赖 `core + command`；
- `completion` 依赖 `core + command`；
- `cli` / `reference` 依赖 `driver` / `command`；
- 任何层都不依赖 Qt（v1）。

---

## 6. 模块职责

### 6.1 `core/source`

- 持有源文本（`std::string`）；
- 记录行起点，支持 `offset → line:col`、行文本、片段提取；
- 处理 UTF-8、BOM、CRLF/CR；
- 诊断使用**物理行/列**，`\` 续行删除的换行仍然计数。

### 6.2 `core/diagnostic`

- `ErrorCode`：L/P/S/R 四段；
- `Diagnostic { code, span, message, help }`；
- 按 spec §11.2 格式化；
- 诊断位置是 `[start, end)` 字节区间。

### 6.3 `core/utf8`

- 码点解码/编码、码点计数、按码点取字符；
- 非法 UTF-8 → L001；
- 字符串索引/`len` 使用码点，不用字节。

### 6.4 `core/token` + `core/lexer`

- Token 携带 `kind`、`offset`、`length`、`wsBefore`；
- `wsBefore` 是命令参数切词与“后缀必须紧贴”的关键；
- 注释、`\` 续行、字符串转义、数值、关键字大小写、NL 抑制；
- 数值要区分普通整数与 `2^63` 边界字面量（INT64_MIN 折叠）；
- `(` / `[` 深度 > 0 时不产生 `NL`；`{}` 不抑制。

### 6.5 `core/ast` + `core/parser`

- AST 节点带 `Span`；
- 表达式：字面量、变量、调用、索引、成员、一元、二元、三元、`ref` 赋值形式；
- 语句：块、if、while、for、break、continue、赋值、引用赋值、命令；
- 解析器是**手写递归下降 + 优先级爬升**（已确认，不用 peglib）；
- 命令参数先按顶层空白切词，再逐词解析 `barePoint | expr`；
- 语句/表达式判定按 spec §3.7；
- `ParseResult` 支持 `Ok / Error / Incomplete`；
- `Incomplete` 用于 REPL 未闭合 `{}`/`()`/`[]` 或行尾 `\`；脚本 EOF 视为 P002。

### 6.6 `core/value` + `core/cell`

- `Value`：int64、double、bool、string、list、Point、callable；
- `list` 可变、异构、值语义；读取/赋值深拷贝；
- `Point` 2/3 维，坐标恒为 double；
- `Cell`：变量单元，带 `hasInboundRef`；
- `ref` 单层保守规则（spec §4.5）：
  - `h = ref f` 标记 f 的 cell；
  - 被引用的 cell 不能再改绑为引用；
  - 普通赋值仍允许，引用看到新值；
  - 自引用 R008；
  - 先校验右侧，再解析/创建左侧；
  - `hasInboundRef` 不清除。

### 6.7 `core/eval`

- 表达式求值返回 `Value`；
- 语句执行返回 `Flow { Normal, Break, Continue }`；
- 运行时错误抛 `RuntimeError`（带 Diagnostic），driver 捕获；
- 作用域：全局 + 块 + for；赋值写最近的同名绑定，否则当前作用域创建；
- 循环变量出循环失效；for-in 每轮新 cell；C 风格 for 一个 cell；
- 命令调用、函数调用、`type` 值调用都在这里。

### 6.8 `core/builtins` + `core/format` + `core/checked`

- 内建：`int`、`double`、`bool`、`string`、`list`、`point`、`type`、`len`、`echo`；
- 前七个是类型构造器/元类型，`len` 是普通函数，`echo` 是命令；
- `type(4.4)` 返回 `double`；`type(4.4)(2)` 等价 `double(2)`；
- `str` 是 `string` 的别名；
- 格式化按 spec §9：double 最短往返、整值不带 `.0`、callable 打印名字、嵌套 string 加引号；
- `checked.h` 负责 int64 检查算术；MSVC 没有 `__int128`，用 intrinsics 或手工边界检查。

### 6.9 `command`

- `spec.h`：`ParamType`、`ParamSpec`、`CommandSpec`、`Signature`；
- `registry`：大小写不敏感查找、别名、内建注册、批量注册；
- `session`：`NEED_INPUT / DONE / ERROR / CANCELLED`；
- 绑定转换：int→double、list→Point、any 接受 callable；
- 命令返回 void，函数返回值；命令不是值。

### 6.10 `driver`

- 脚本模式：整体解析 + 校验 + 执行；不进入会话；
- REPL：Env 跨行保留；`Incomplete` 续行；会话输入按 `barePoint | expr`；
- 结果：`DONE / NEED_INPUT / ERROR / CANCELLED`；
- 错误策略：解析/校验首错中止；运行时首错中止；会话错误停留当前参数。

### 6.11 `completion`

- 复用同一套 lexer/切词规则，禁止另写扫描器；
- 上下文：语句开头、表达式、命令参数、`for i in`、`[`、`(`、`f(`、`.`、会话；
- 候选来自关键字、可见变量、命令/函数/类型构造器；
- 命令参数按 `ParamSpec` 类型/维度过滤；
- CLI 的 Tab 补全暂缓（`std::getline` 行缓冲做不了），先做核心 API + 测试。

### 6.12 `reference` + `cli`

- `reference_host`：`line`、`circle`、`move`；`line`/`circle` 交互，`move` 否；
- `echo` 在 core 内建，不是 reference host；
- CLI：`lush` → REPL，`lush file.lush` → 脚本，`lush -e "..."` → 单段执行（M5）；
- Windows 启动设置 UTF-8 console code page；
- 退出码：0 成功，1 语言错误，2 用法错误。

---

## 7. 构建、运行、测试

### 7.1 默认构建

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug
ctest --preset vs2022-debug
```

### 7.2 Release

```powershell
cmake --preset vs2022-release
cmake --build --preset vs2022-release
ctest --preset vs2022-release
```

### 7.3 Ninja（可选）

需要先进入 VS developer shell：

```powershell
cmd /c ""C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat" && cmake --preset ninja-debug && cmake --build --preset ninja-debug && ctest --preset ninja-debug"
```

### 7.4 不走 preset

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

### 7.5 产物位置

| 配置 | 可执行 |
|---|---|
| VS Debug | `build/vs2022/bin/Debug/lush.exe` |
| VS Release | `build/vs2022-release/bin/Release/lush.exe` |
| Ninja Debug | `build/ninja-debug/bin/lush.exe` |

静态库在对应 `lib/<Config>/lush_core.lib`。

### 7.6 MSVC 选项

```text
/W4 /permissive- /utf-8 /EHsc /Zc:__cplusplus /Zc:preprocessor
```

- `/utf-8` 必须保留；
- `LUSH_WERROR=ON` 可把警告升级为错误（默认 OFF）；
- 标准 C++20；
- 目标：`/W4` 零警告。

---

## 8. 语义速查（spec §0.1 摘要）

完整表述以 `docs/spec.md` 为准。

| # | 决策 |
|---|---|
| D01 | 语句终止符 = 换行 / `;` / `}` / `else` / EOF；`\` 是唯一显式续行 |
| D02 | Point 独立类型，2/3 维，坐标恒为 double |
| D03 | 下标 0-based |
| D04 | 唯一列表类型 `list`，可变；`h = f` 深拷贝 |
| D05 | `h = ref f`；赋值重绑定；整值写穿透 `*h = v` |
| D06 | int64 + double；`2/4 == 0`，`2/4.0 == 0.5` |
| D07 | 块引入作用域；循环变量出循环失效 |
| D08 | 位置决定调用：语句位置=并置命令，表达式位置=后缀 `(...)` |
| D09 | 命令参数按顶层空白切词；一元运算符贴身；`line a - b` 是 P004 |
| D10 | `(x,y)` 是 Point；`[...]` 是 list；裸 `x,y` 只在命令参数位置 |
| D11 | 命令与函数共用注册表；函数/类型构造器是一等值，命令不是 |
| D12 | 交互会话仅限 REPL 顶层单条命令；脚本必须写全参数 |
| D13 | v1 不实现 `def`/`return`，保留关键字 |
| D14 | 先整体解析/校验，再执行；运行时错误中止 |
| D15 | `type(v)` 返回类型构造器；`type(4.4)(2)` 等价 `double(2)` |
| D16 | 内建、类型构造器、`type` 都是 callable；命令不是值 |
| D17 | v1 引用单层：`hasInboundRef` 的 cell 不能改绑为引用（R008） |
| D18 | REPL 未闭合括号或行尾 `\` → `Incomplete`；脚本模式 P002 |

### 8.1 最容易写错的几条

```lush
line(1,2)              # 命令 + 一个 Point 参数，不是函数调用
line 1,2 3,4           # 两个 Point 参数
line a - b             # P004（`-` 单独成词）
line -1 -2             # 两个参数
line 1, 2              # P004（裸坐标内不能有空格）
echo (1 + 2 * 3)       # 命令参数里的复杂表达式必须加括号
move 1,2               # 一个 Point，不是两个数值；两个数值写 move 1 2
type(4.4)(2)           # 先取 double，再调用 → 2.0
type(4.4) (2)          # 表达式位置 P001；命令参数位置是两个词
p(1,2)                 # p 是 Point → R012 不可调用
int + 1                # callable 不参与算术 → R002
```

### 8.2 错误码速查

| 段 | 含义 | 关键码 |
|---|---|---|
| L001–L006 | 词法 | 非法字符、字符串、转义、数值、`\` |
| P001–P006 | 语法 | 意外 token/EOF、Point 元数、命令参数、括号、终止符 |
| S001–S008 | 校验 | 未知命令、元数、保留名、ref 位置、break、会话上下文、def、命令入表达式 |
| R001–R012 | 运行时 | 未定义变量、类型、越界、除零、溢出、Point 转换、解引用、引用、NaN、宿主失败、取消、不可调用 |

完整的错误码表在 spec §11.1。

---

## 9. 开发规划

### 9.1 总览

| 阶段 | 目标 | 主要产出 | 完成标准 |
|---|---|---|---|
| M0 ✅ | 构建骨架 | CMake、presets、CLI、tests | 2/2 tests，无警告 |
| M1 | 词法 | source、diagnostic、utf8、token、lexer | `unit_lexer` 全绿 |
| M2 | 语法 | ast、parser、word splitter、Incomplete | `unit_parser` + 解析用例全绿 |
| M3 | 求值 | value、cell、eval、format、checked、builtins | 非命令 golden 全绿 |
| M4 | 命令 | spec、registry、reference host | 命令 golden 全绿 |
| M5 | 会话/REPL | session、driver、cli | G22 + 多行块 + 脚本模式 |
| M6 | 补全 | completion | spec §12 用例全绿 |
| M7 | 收尾 | 文档、诊断、性能、全量回归 | 全量 CTest + 文档同步 |

### 9.2 M1 — 词法（下一步）

**文件**

- `src/lush/core/source.h/.cpp`
- `src/lush/core/diagnostic.h/.cpp`
- `src/lush/core/utf8.h/.cpp`
- `src/lush/core/token.h`
- `src/lush/core/lexer.h/.cpp`
- `tests/unit_lexer.cpp`
- 可选：`src/cli/main.cpp` 加 `--dump-tokens`

**任务**

1. `Source`：UTF-8 校验、BOM、CRLF/CR、行起点、`lineCol`、行文本、span 片段。
2. `Diagnostic`：ErrorCode、Span、消息、help、spec §11.2 格式化。
3. `utf8`：码点解码/编码、计数、按码点取字符。
4. `Token`：kind、offset、length、`wsBefore`。
5. `Lexer`：
   - 注释 `#` 到行尾；
   - `\` 续行（字符串/注释外），物理行号继续递增；
   - 字符串双引号、转义 `\\ \" \n \r \t \0 \uXXXX`；
   - 数值：int、double、`2^63` 边界标记；
   - 标识符/关键字大小写不敏感；
   - 运算符最大匹配；
   - `(`/`[` 内 NL 抑制；`{}` 不抑制；
   - `wsBefore`；
   - EOF、非法 UTF-8、未闭合字符串、坏转义、孤立 `\`、EOF 前 `\`。
6. 词法错误 L001–L006。

**测试**

- BOM、LF/CRLF/CR；
- 注释、`#` 在字符串内；
- `\` 续行与物理行号；
- 字符串转义、Unicode 码点；
- 数值边界：`9223372036854775807`、`9223372036854775808`（边界标记）、`-9223372036854775808`、浮点；
- 关键字大小写；
- `wsBefore`；
- NL 抑制；
- L001–L006。

**完成标准**

- `unit_lexer` 全绿；
- `/W4` 无警告；
- `--dump-tokens`（若实现）能打印 token 流与 `wsBefore`。

### 9.3 M2 — 语法

**文件**

- `src/lush/core/ast.h`
- `src/lush/core/parser.h/.cpp`
- `tests/unit_parser.cpp`
- 可选：`--dump-ast`

**任务**

- AST 节点 + `Span`；
- 语句：块、if/else、while、for-in、C 风格 for、break、continue、赋值、引用赋值、命令；
- 表达式：优先级爬升、三元、一元/二元、后缀调用/索引/成员/++/--、Point/list 字面量；
- 语句/表达式判定（spec §3.7）；
- 命令参数切词 + `barePoint | expr` + 整词消费；
- lvalue 路径必须连续；
- 后缀必须紧贴；
- 换行省略位置；
- `ParseResult { Ok / Error / Incomplete }`；
- 语法错误 P001–P006。

**测试**

- spec 中所有语法示例；
- `line a - b`、`line -1 -2`、`line 1,2 3,4`、`line 1, 2`；
- `line(1,2)`、`type(4.4)(2)`、`(type(1))(2)`；
- `p = point [1,2]` P001；
- 多行块；
- 未闭合 `{}`/`()`/`[]` → Incomplete；
- 脚本 EOF 未闭合 → P002。

**完成标准**

- `unit_parser` 全绿；
- AST dump 可用于人工核对；
- G15/G16/G34/G46 解析结果符合 spec。

### 9.4 M3 — 值、环境、求值、内建

**文件**

- `src/lush/core/value.h/.cpp`
- `src/lush/core/cell.h`
- `src/lush/core/eval.h/.cpp`
- `src/lush/core/format.h/.cpp`
- `src/lush/core/checked.h`
- `src/lush/core/output.h`
- `src/lush/core/builtins.h/.cpp`
- `tests/unit_value.cpp`
- `tests/unit_ref.cpp`
- `tests/golden_spec.cpp`

**任务**

- `Value` 深拷贝；`shared_ptr<List>` + 自定义拷贝；
- Point/List/Callable；
- `Cell` + `Env` + `hasInboundRef`；
- 作用域、循环、break/continue；
- 算术/比较/逻辑/三元/索引/成员/++/--；
- `type` 值、`bool`/`list`/`string` 构造器；
- `echo` 输出；
- 格式化与 checked int64；
- `RuntimeError`。

**测试**

- G01–G10、G17–G19、G29–G32、G38–G43、G45–G49；
- G04–G07、G35–G36、G50–G52（ref）；
- 只读成员/索引、越界、除零、溢出。

**完成标准**

- 非命令 golden 全绿；
- 不需要 reference host 即可跑通。

### 9.5 M4 — 命令注册与参考宿主

**文件**

- `src/lush/command/spec.h`
- `src/lush/command/registry.h/.cpp`
- `src/lush/command/host.h`
- `src/reference/reference_host.h/.cpp`
- `tests/unit_command.cpp`

**任务**

- `ParamType / ParamSpec / CommandSpec / Signature`；
- 大小写不敏感注册表、别名；
- 参数绑定转换、元数、变参、`any`；
- 命令调用与函数调用的区别；
- S001/S002/S006/S008；
- reference host `line`/`circle`/`move`，通过 `OutputSink` 写转录；
- `echo` 已在 core 内建。

**测试**

- G11–G16、G20–G21、G23–G28、G33、G37、G44；
- 参数类型错误、元数错误、命令入表达式。

**完成标准**

- 命令 golden 全绿；
- `line`/`circle` 提示文本正确。

### 9.6 M5 — 会话与 REPL

**文件**

- `src/lush/command/session.h/.cpp`
- `src/lush/driver/result.h`
- `src/lush/driver/driver.h/.cpp`
- `src/cli/repl.h/.cpp`
- `src/cli/console_sink.h/.cpp`
- `tests/unit_driver.cpp`

**任务**

- `NEED_INPUT / DONE / ERROR / CANCELLED`；
- 必选/可选参数、默认值、空输入、取消；
- 会话输入按 `barePoint | expr`；
- 脚本模式不进入会话；
- REPL 多行 Incomplete；
- `lush` REPL、`lush file.lush`、`lush -e "..."`；
- UTF-8 console。

**测试**

- G22；
- 多行块；
- 取消；
- 脚本 vs REPL 的 S006 差异；
- 会话中输入 `0,0`。

**完成标准**

- CLI 可用；
- G22 通过；
- 脚本模式缺参报 S006。

### 9.7 M6 — 补全

**文件**

- `src/lush/completion/completion.h/.cpp`
- `tests/unit_completion.cpp`

**任务**

- 上下文判定（spec §12.1）；
- 候选：关键字、变量、命令/函数/类型构造器；
- 命令参数按 `ParamSpec` 过滤；
- `for i in`、`[`、`(`、`f(`、`.`、会话；
- 复用 lexer/切词规则，禁止另写扫描器。

**测试**

- `line `、`line 1,2 `、`f(`、`type(`、`[`、`.`、会话；
- 大小写匹配、变量大小写敏感。

**完成标准**

- spec §12 用例全绿；
- CLI Tab 暂缓，核心 API 可用。

### 9.8 M7 — 收尾

- 诊断消息质量与 help；
- 全量 golden + 回归；
- README/AGENTS 同步；
- 性能检查（列表深拷贝、词法）；
- 代码清理、`/W4` 零警告；
- 必要时补 fuzz/截断输入测试。

### 9.9 v1 之后

1. 用户函数 `def`/`return`，复用 callable 值模型；
2. 模块/import、插件注册；
3. list COW 优化；
4. 错误恢复（一行错误不影响后续语句）；
5. Qt 适配层：
   - `ICommandHost` 的 Qt 实现；
   - `QGraphicsScene` / `QGraphicsItem` 预览；
   - `QUndoStack` 撤销；
   - `QLineEdit` 补全 / ghost text；
   - 鼠标点选提交 Point；
6. 可选：tree-sitter 增量解析。

---

## 10. 测试策略

### 10.1 Harness

- `tests/test_harness.h`：`LUSH_TEST`、`LUSH_CHECK`、`LUSH_CHECK_EQ`；
- 无 Catch2/GTest；
- `lush_tests` 一个可执行，CTest 注册；
- 测试失败时打印文件:行与表达式。

### 10.2 分层

| 类型 | 位置 | 内容 |
|---|---|---|
| 单元 | `unit_*.cpp` | 词法/语法/值/引用/命令/补全 |
| Golden | `golden_spec.cpp` | G01–G52，数据驱动 |
| REPL | `unit_driver.cpp` | `feedLine` 序列、提示、取消 |
| 诊断 | 单元测试 | 错误码 + 行:列 |

### 10.3 Golden 约定

- 参考宿主按 spec §7.6 输出；
- 输出按时间顺序合并 `echo` 与命令转录；
- 错误比较**错误码 + 行:列**，消息只做包含匹配；
- 脚本模式输出是出错前已产生的部分；
- 每个 golden 用例独立 Env/Registry/OutputSink；
- 新增语义 → 先加 golden，再实现。

### 10.4 测试要求

- 每个 bug 修复补回归测试；
- 每个错误码至少一个用例；
- 不依赖 locale、时间、随机、绝对路径；
- 不写 `build/`；
- 提交前跑：

```powershell
cmake --build --preset vs2022-debug
ctest --preset vs2022-debug --output-on-failure
```

### 10.5 当前 M0 测试

- `version_is_not_empty`
- `version_matches_header`
- CTest `lush_version` 检查 `lush --version` 输出。

M1 开始后，`lush_tests` 会逐步加入 `unit_lexer` 等文件。

---

## 11. 编码约定

### 11.1 语言与风格

- C++20，MSVC 19.44+；
- 4 空格缩进，不使用 tab；
- 行宽约 100 列；
- K&R 大括号（左括号同行）；
- `#pragma once`；
- `namespace lush`、`lush::core`、`lush::command`、`lush::driver`、`lush::completion`、`lush::test`；
- 类型 `PascalCase`，函数/变量 `camelCase`，常量 `k` 前缀，宏 `LUSH_` 前缀；
- 不使用 `using namespace std;`；
- 拥有所有权用 `std::string` / `std::unique_ptr` / `std::shared_ptr`，只读视图用 `std::string_view`；
- 禁止裸 `new`/`delete`。

### 11.2 错误与控制流

- 用户可见错误 → `Diagnostic`（带 Span、错误码、help）；
- 运行时错误 → `RuntimeError` 异常，driver 边界捕获；
- `break`/`continue` → `Flow` 返回值，不用异常；
- 不用 `assert` 处理用户输入；
- 不吞异常，不静默忽略错误。

### 11.3 值与内存

- `Value` 深拷贝；`list` 用 `shared_ptr<List>` + 自定义拷贝；
- `Cell` 用 `shared_ptr` 管理生命周期；
- `Callable` 不可变，`shared_ptr<const Callable>`；
- 读变量返回拷贝；`ref` 是唯一别名；
- 避免不必要的拷贝，但语义优先。

### 11.4 数值与文本

- int64 溢出检查；MSVC 无 `__int128`；
- double 不接受 NaN/Inf；
- 格式化用 `std::to_chars` 最短往返；
- 整值 double 不带 `.0`；
- `-0.0` 输出 `0`；
- UTF-8 源码，`/utf-8`；
- 字符串按码点索引；
- 诊断中文，代码标识符英文。

### 11.5 禁止事项

- 不在 `core` 引入 Qt / 第三方库；
- 不用 `std::regex` 写词法器；
- 不用 locale 相关格式化；
- 不用 `std::format` 作为硬依赖（可用 `to_chars`/`ostringstream`）；
- 不把命令当值；
- 不静默改变 spec 语义；
- 不提交构建产物；
- 未经要求不 commit。

---

## 12. 关键实现注意事项 / 已知坑

1. **`wsBefore` 是切词核心。** lexer 必须给每个 token 记录前导空白；命令参数切词、后缀紧贴、调用紧贴都依赖它。
2. **切词只发生在命令参数顶层。** `()`/`[]` 内空白不切；`{}` 不抑制 NL。
3. **`Incomplete` 不是错误。** REPL 用它续行；脚本模式在 EOF 上是 P002。
4. **`hasInboundRef` 不清除。** 保守规则：曾经被引用过的 cell 不能再改绑为引用；普通赋值仍允许。
5. **`Value` 深拷贝。** `shared_ptr<List>` 的默认拷贝是浅拷贝；必须自定义拷贝构造/赋值克隆 list。
6. **MSVC 检查算术。** 没有 `__int128`；用 `_addcarry_u64`、`_mul128` 或手工边界检查；覆盖 `INT64_MIN / -1`。
7. **double 格式化。** `std::to_chars` 最短往返；整值不带 `.0`；`-0.0` → `0`；不接受 NaN/Inf。
8. **UTF-8 控制台。** Windows 下 `SetConsoleOutputCP(CP_UTF8)` / `SetConsoleCP(CP_UTF8)`。
9. **`type` 是元类型兼函数。** `type(4.4)` 返回 `double`；`type(int)` 返回 `type`；callable 的身份相等。
10. **命令不是值。** `echo line`、`x = line` → S008；函数/类型构造器是值。
11. **`echo` 与转录共用 OutputSink。** golden 输出按调用顺序合并；reference host 写同一个 sink。
12. **CLI Tab 暂缓。** `std::getline` 行缓冲做不了真正的 Tab；先做 completion API + 测试，Qt 控制台再实现交互。
13. **多配置输出路径。** VS generator 下产物在 `bin/<Config>/`；CTest 用 `$<TARGET_FILE:...>`，不要硬编码路径。
14. **Ninja 需要 vcvars。** `cl` 不在 PATH；Ninja preset 必须在 VS developer shell 中运行。
15. **`\` 续行与物理行号。** 删除的换行仍要递增行号；token offset 指向原始文本。
16. **`LUSH_VERSION` 宏。** CMake 传入 `LUSH_VERSION="x.y.z"`；`version.h` 有 fallback，但以 CMake 为准。

---

## 13. 常见任务配方

### 13.1 加一个内建函数

1. 在 `core/builtins.cpp` 注册 callable（名字、签名、原生函数）；
2. 若需要类型构造器，确保 `type(v)` 能映射到它；
3. 在 `tests/unit_value.cpp` 加正向/错误用例；
4. 若改变语义，更新 `docs/spec.md` §8；
5. 跑 `ctest`。

### 13.2 加一个宿主命令

1. 在 `reference_host` 或宿主代码里构造 `CommandSpec`；
2. 写 `ParamSpec`（类型、维度、可选、默认、变参、hint）；
3. 设置 `interactive`；
4. 注册到 `CommandRegistry`；
5. 在 `tests/golden_spec.cpp` 加输入/输出；
6. 若是交互命令，补会话用例。

### 13.3 加一个错误码

1. `core/diagnostic.h` 加枚举；
2. `docs/spec.md` §11.1 加一行；
3. 抛出/返回该码；
4. 加至少一个测试；
5. 若影响 golden，更新期望。

### 13.4 加一个 golden test

1. 在 `tests/golden_spec.cpp` 加 case（名字、源码、期望输出/错误）；
2. 用参考宿主或 TestHost；
3. 错误用例写 code + line + col；
4. 跑单个 case 和全量 CTest。

### 13.5 改语义

1. 先改 `docs/spec.md`（含 §0.1 决策表、相关章节、golden tests）；
2. 再改实现；
3. 更新/新增 golden；
4. 更新 `AGENTS.md` 的语义速查（如果影响工程约定）；
5. 不要只改代码不改 spec。

### 13.6 加补全上下文

1. 在 spec §12.1 补一行；
2. `completion.cpp` 复用 lexer/切词，不另写扫描器；
3. `unit_completion.cpp` 加候选断言；
4. 若 CLI 可见，更新 CLI 调试命令。

---

## 14. 术语表

| 术语 | 含义 |
|---|---|
| Source | 源文本 + 行列映射 |
| Span | `[start, end)` 字节区间 |
| Token | 词法单元，含 `wsBefore` |
| word | 命令参数顶层按空白切出的 token 片段 |
| barePoint | 命令参数里的裸坐标 `x,y` / `x,y,z` |
| Point | 2/3 维坐标值，坐标 double |
| list | 异构可变列表，值语义 |
| callable | 内建函数/类型构造器，一等值 |
| type 值 | callable 的类型；`type` 本身也是 callable |
| Cell | 变量单元，持有 Value 或引用 |
| ref | 单层变量别名，`h = ref f` |
| hasInboundRef | cell 被引用标记；被引用后不能改绑为引用 |
| Incomplete | REPL 需要继续读取的状态，不是错误 |
| NEED_INPUT | 交互会话等待下一个参数 |
| reference host | 测试/演示用 `line`/`circle`/`move` |
| transcript | 命令转录输出，与 `echo` 共用 sink |
| golden test | 来自 spec 的输入/输出回归用例 |

---

## 15. 当前下一步：M1

M1 尚未开始。建议顺序：

1. `utf8.h/.cpp` + 单元测试；
2. `source.h/.cpp`（行列映射、BOM、CRLF）；
3. `diagnostic.h/.cpp`（ErrorCode、Span、格式化）；
4. `token.h`；
5. `lexer.h/.cpp`；
6. `tests/unit_lexer.cpp`；
7. 可选：`lush --dump-tokens`。

M1 完成标准：

- L001–L006 全覆盖；
- BOM/CRLF/CR、注释、`\`、字符串、数值边界、关键字、`wsBefore`、NL 抑制全覆盖；
- `ctest --preset vs2022-debug` 全绿；
- `/W4` 无警告；
- 不引入第三方依赖；
- 不修改 `docs/spec.md` 语义（如发现 spec 缺口，先记录并提问）。

---

## 16. Agent 工作守则

1. 先 `git status` / `git diff`，不要把用户未提交的改动覆盖掉。
2. 先读 `docs/spec.md` 的相关章节，再写代码。
3. 确认当前里程碑；不要在 M1 阶段去写 M4 的命令系统。
4. 小步实现：一个模块 + 对应测试。
5. 每次改完跑 `cmake --build --preset vs2022-debug` 和 `ctest --preset vs2022-debug`。
6. 语义变更必须同步 spec + golden；结构变更同步 `AGENTS.md` / `README.md`。
7. 不引入 Qt / 第三方库到 `core`。
8. 不提交 `build/`，不未经要求 commit。
9. 不静默发明语义；有歧义先提问并给出建议。
10. 保持项目自解释：新增模块时写一句职责注释；测试命名对应 spec 编号（如 `G22`、`L004`）。

---

## 17. 提交建议

按主题拆分，避免把 spec、实现、构建混在一个提交里：

```text
spec: <语义变更>
core: <词法/语法/求值>
command: <注册/绑定/会话>
cli: <REPL/参数/输出>
test: <新增或修复测试>
build: <CMake/presets/CI>
docs: <README/AGENTS>
```

推荐：

```powershell
git add docs/spec.md
git commit -m "spec v0.3: single-level refs + REPL incomplete"

git add .gitignore CMakeLists.txt CMakePresets.json README.md src tests
git commit -m "M0: CMake skeleton, version, CTest"
```

之后每个里程碑单独提交，确保 `main` 始终可构建、可测试。

---

## 18. 自解释检查清单

新增/修改代码后，问自己：

- [ ] 新模块在 `AGENTS.md` 的目录树里有位置吗？
- [ ] 语义变化写进 `docs/spec.md` 了吗？
- [ ] 对应 golden/单元测试加了吗？
- [ ] 错误码在 spec §11.1 和代码里一致吗？
- [ ] `core` 仍然不依赖 Qt/第三方吗？
- [ ] `/W4` 无警告吗？
- [ ] `ctest` 全绿吗？
- [ ] README/AGENTS 的构建命令还能用吗？
- [ ] 新术语加进术语表了吗？
- [ ] 下一个 agent 只看仓库能知道下一步做什么吗？

如果以上有“否”，这次改动还不算完成。
