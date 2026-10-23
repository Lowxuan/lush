# AGENTS.md

给在本仓库工作的 OpenCode / AI agent 的高信号上下文。这里是地图，不是规范。

## 先读什么

- **语义唯一来源：`docs/spec.md`（v0.3.4）。** 代码与 spec 冲突时以 spec 为准；要改语义，先改 spec 和 golden，再改代码。
- `docs/steps.md`：M1–M6 开发里程碑、文件、任务、测试与完成标准。
- `README.md`：构建入口。
- `start.md` / `teaching.md`：历史需求与选型讨论，**不是规范**。`teaching.md` 里推荐的 peglib / ANTLR / Lua 等均未采用；本项目使用手写递归下降 + 优先级爬升，零第三方依赖。
- **`docs/spec.md` 是 UTF-8。** 直接按 UTF-8 读取；不要按 GBK/cp936 解码。

spec 很长（约 2220 行），按需读：§0.1 决策表、§2–§8 语义、§11.1 错误码、§12 补全、§15 golden G01–G52。

## 当前状态（不要假设功能已存在）

- **M0、M1、M2 已完成**：
  - `lush_core` 已实现 `utf8`、`source`、`diagnostic`、`token`、`lexer`、`version`、`ast`、`parser`；
  - CLI 支持 `--version`、`--help`、`--dump-tokens <file>`、`--dump-tokens -e "<source>"`、`--dump-ast <file>`、`--dump-ast -e "<source>"`；
  - 仍无 REPL、无脚本执行模式；`-e` 只用于 `--dump-tokens` / `--dump-ast`；普通文件路径仍报 `unknown option` 并退出 2。
- 求值 / 命令 / 会话 / 补全**尚未实现**。
- M2 的 AST/Parser 规则已在 `docs/spec.md` 0.3.4 和 `docs/steps.md` 实现并测试。下一步是 **M3（值、环境、求值、内建）**；不要提前写 M4 的命令系统。
- spec §15 的 G01–G52 是规范/目标，**不是可运行测试**；不要声称它们已通过。
- 当前 `lush_tests` 有 45 个测试；CTest 有四个目标：`lush_tests`、`lush_version`、`lush_dump_tokens`、`lush_dump_ast`。

M1 词法实现约定（已完成）：

- Token 自己持有删除续行后的文本，同时保留原始 `offset/length`；字符串 token 的文本是解码后的值，`text.size()` 可以不等于 `length`。
- `NL` / `EOF` 的 `wsBefore` 固定为 `false`；EOF 文本为空、length 为 0。
- 首错按源文本位置排序；非法 UTF-8 按其第一个非法字节参与排序。
- `--dump-tokens` 支持 `lush --dump-tokens <file>` 和 `lush --dump-tokens -e "<source>"`；输出包含 `NL` / `EOF`，词法错误输出 §11.2 诊断并退出 1。

M2 语法实现约定（已完成）：

- `LexResult` 额外携带 `endsWithContinuation`、`lastContinuationOffset`、`endOffset`。
- `ast.h/.cpp`：节点带 Span，表达式 Span 包含外层分组括号（`BoundaryInt` 例外，只覆盖字面量）；`BoundaryInt` 只允许在 `Unary(-, ...)` 中。
- `parser.h/.cpp`：`parse`、`parseExpression`、`parseWord`、`splitCommandWords`，`Incomplete` 取最内层未闭合定界符；P002/P004 与词边界规则按 spec 0.3.4。
- `--dump-ast` 使用脚本模式，解析失败只输出诊断并退出 1；`--dump-ast` 与 `--dump-tokens` 互斥。

## 构建 / 测试

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug
ctest --preset vs2022-debug --output-on-failure
```

- `vs2022` 使用 Visual Studio 生成器，**不需要**先进入 VS developer shell。
- Release：
  ```powershell
  cmake --preset vs2022-release
  cmake --build --preset vs2022-release
  ctest --preset vs2022-release --output-on-failure
  ```
- Ninja 需要 vcvars（`cl` 不在 PATH）：
  ```powershell
  cmd /c ""C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat" && cmake --preset ninja-debug && cmake --build --preset ninja-debug && ctest --preset ninja-debug"
  ```
- 产物：
  - VS Debug：`build/vs2022/bin/Debug/lush.exe`、`build/vs2022/lib/Debug/lush_core.lib`
  - VS Release：`build/vs2022-release/bin/Release/lush.exe`
  - Ninja：`build/ninja-debug/bin/lush.exe`
- **测试没有单测过滤参数。** `lush_tests.exe` 忽略命令行参数，总是跑完全部注册测试。CTest 只能按测试名过滤（当前有 `lush_tests`、`lush_version`、`lush_dump_tokens`），例如 `ctest --preset vs2022-debug -R lush_version`。
- **新增 `.cpp` 必须手动写进 CMake**：`src/CMakeLists.txt` 或 `tests/CMakeLists.txt`；项目不用 `GLOB`，只放文件不会参与构建。
- `/W4` 目标零警告；`LUSH_WERROR=ON` 可升级为错误，但不要靠关闭警告来通过。

## 目录与依赖

```text
src/lush/core/        只依赖标准库（utf8/source/diagnostic/token/lexer/version）
src/lush/command/     命令注册、参数绑定、会话（未实现）
src/lush/driver/      脚本 / REPL 执行状态机（未实现）
src/lush/completion/  补全（未实现）
src/reference/        示例宿主（未实现）
src/cli/              CLI 前端
tests/                单一 lush_tests 可执行文件
```

- 依赖方向：`cli → driver → command → core`；`completion` 依赖 `core + command`；`reference` 依赖 `command`。反向依赖不允许。
- 公共 include 根是 `src`，所以写 `#include "lush/core/version.h"`，不要写 `../` 相对路径。
- `LUSH_VERSION` 由 CMake 以 PUBLIC 宏注入 `lush_core`；`version.h` 里的 fallback 只用于脱离 CMake 的场景。

## 硬约束

- `src/lush/core` **不引入 Qt、不引入任何第三方库**；本仓库目前也没有第三方依赖。
- C++20、MSVC、4 空格缩进、`#pragma once`、无 `using namespace std;`。
- 用户可见诊断用中文，代码标识符用英文。
- 源码保持 UTF-8；MSVC 的 `/utf-8` 不能删。
- 不要提交 `build/`、不要未经要求 `git commit`。
- 不要引入 Catch2/GTest；测试用自带的极简 harness。
- CLI 已在 `main.cpp` 设置 Windows console 为 UTF-8，新增输出不必重复设置。

## 语言陷阱（实现前重点看 spec）

- **位置决定调用**：语句位置的 `name args` 是并置命令；表达式位置的调用必须是紧贴的后缀 `name(...)`。命令名出现在表达式位置是 S008。
- **命令参数按顶层空白切词**：复杂表达式必须加括号；运算符必须紧贴左操作数。`line a - b`、`line 1, 2` 是 P004；`line(1,2)` 是命令 + 一个 Point 参数，不是函数调用。
- **`(x,y)` 是 Point，`[...]` 是 list**；裸 `x,y` 只允许出现在命令参数位置，且坐标之间不能有空格。
- **语句终止符**是换行 / `;` / `}` / `else` / EOF；`\` 是唯一显式续行。`(` / `[` 内抑制 NL，**`{}` 不抑制**。
- **值语义**：读取/赋值默认深拷贝，`list` 的 `shared_ptr` 必须自定义拷贝构造/赋值来克隆；`ref` 是唯一别名机制，v1 只支持单层，`hasInboundRef` 一旦置位不清除（R008）。
- **赋值先求目标**：普通赋值先求目标路径，再求 RHS；目标路径里的后缀 `++`/`--` 延迟到赋值写入后提交，RHS 的后缀仍立即写回。`a[i] = i++` 与 `a[i++] = i` 的 RHS 都是旧 `i`。`ref` 与 `for` init 例外（RHS-first）。
- **数值**：int64 + double；`2/4 == 0`、`2/4.0 == 0.5`；整数运算要检查溢出（R005）和 `INT64_MIN / -1`。
- **解析先于执行**：脚本先整体解析/校验再执行（D14），首错中止；脚本缺参不进入会话：交互命令缺参报 S006，非交互命令缺参报 S002；参数过多一律 S002。交互会话只允许 REPL 顶层单条命令。
- **命令不是值**：内建函数和类型构造器是一等 callable；`echo line`、`x = line` 是 S008。
- **`Incomplete` 不是错误**：仅 REPL 用于未闭合 `{}` / `()` / `[]` 或行尾 `\`；脚本 EOF 的未闭合是 P002。
- **v1 没有用户函数**：`def` / `return` 是保留字（S007），不要实现闭包、lambda、模块或异常。
- 诊断分 L/P/S/R 四段，位置是原始文本的物理 `line:col`；错误码必须与 spec §11.1 一致。
- `-9223372036854775808` 仅作为一元负号的操作数合法；裸 `9223372036854775808` 是 L004。

## 测试约定

- 在 `tests/unit_*.cpp` 里用 `LUSH_TEST(name) { ... }` 注册；`LUSH_CHECK` / `LUSH_CHECK_EQ` 断言。
- 新测试文件要加进 `tests/CMakeLists.txt` 的 `lush_tests` 源列表。
- 每个 bug 修复补回归测试；每个错误码至少一个用例。
- 测试名尽量对应 spec 编号（如 `G22`、`L004`），方便追溯。
- 不依赖 locale、时间、随机数、绝对路径。
- 测试输出是 `[ OK ]` / `[FAIL]`，末尾汇总 `N tests, M failed`。

## 改代码时

1. 先 `git status` / `git diff`，不要覆盖用户未提交的改动。
2. 语义变更顺序：`docs/spec.md` → golden/单元测试 → 实现 → 必要时同步 `README.md` / 本文件。
3. 结构变更（新增模块、改依赖、改构建方式）同步更新本文件的目录/命令。
4. 不静默发明语义；spec 有歧义时先记录并提问。
5. 提交建议前缀：`spec:`、`core:`、`command:`、`cli:`、`test:`、`build:`、`docs:`。
