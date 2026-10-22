# Lush 开发里程碑（M1–M6）

本文是开发路线图，说明每个里程碑的目标、文件、任务、测试和完成标准。语言语义仍以 `docs/spec.md` 为唯一来源；AGENTS.md 里的构建/测试约定同样适用。

## 当前基线

| 里程碑 | 状态 | 说明 |
|---|---|---|
| M0 构建骨架 | ✅ 已完成 | CMake presets、CLI 占位、测试 harness、CTest |
| M1 词法 | ✅ 已完成 | UTF-8、Source、Diagnostic、Token、Lexer、`--dump-tokens` |
| M2 语法 | ⬜ 下一步 | AST、Parser、命令参数切词、`Incomplete` |
| M3 求值 | ⬜ 未开始 | Value、Cell/Env、Eval、格式化、内建、非命令 golden |
| M4 命令 | ⬜ 未开始 | 命令 spec/registry、参考宿主、命令 golden |
| M5 会话 / REPL | ⬜ 未开始 | Session、Driver、脚本/REPL CLI |
| M6 补全 | ⬜ 未开始 | 补全上下文与候选 API |

M1 已完成：`lush_tests` 当前 20 个测试，CTest 目标为 `lush_tests`、`lush_version`、`lush_dump_tokens`。M2 尚未开始。

---

## M1 — 词法（已完成）

**目标**：把源文本稳定地变成 Token 流，并为后续语法层提供正确的 Span、诊断和 `wsBefore`。

**文件**

- `src/lush/core/utf8.h/.cpp`
- `src/lush/core/source.h/.cpp`
- `src/lush/core/diagnostic.h/.cpp`
- `src/lush/core/token.h`
- `src/lush/core/lexer.h/.cpp`
- `tests/unit_lexer.cpp`
- `src/cli/main.cpp`：`--dump-tokens`

**已完成内容**

- UTF-8 解码/编码、合法性校验、码点计数、按码点取字符。
- `Source` 持有原始字节；处理 BOM、LF/CRLF/CR；支持 `lineCol`、行文本、Span 切片。
- 全量 L/P/S/R `ErrorCode`、`Diagnostic`、spec §11.2 格式化。
- Token 携带 kind、原始 `offset/length`、`wsBefore` 和删除续行后的文本。
- 词法器覆盖：
  - `#` 注释；
  - `\` 续行与物理行号；
  - 字符串与 `\\ \" \n \r \t \0 \uXXXX`；
  - 整数、浮点、`2^63` 边界字面量；
  - 标识符/关键字大小写不敏感；
  - 运算符最大匹配；
  - `(` / `[` 内 NL 抑制，`{}` 不抑制；
  - EOF、非法 UTF-8、未闭合字符串、坏转义、孤立 `\`；
  - L001–L006。
- `--dump-tokens <file>` 和 `--dump-tokens -e "<source>"` 可打印 token 流与 `wsBefore`。

**验证**

- `unit_lexer` 全绿；
- `/W4` / `/WX` 无警告；
- Debug、Release、Ninja 三套 CTest 全绿。

**M1 已确认的关键边界**

- BOM 不计列，但 token offset 仍指原始字节。
- `wsBefore` 表示逻辑文本中 token 左侧是否紧邻空格/tab；`NL`/EOF 固定 false。
- 数字严格 L004；浮点上溢/下溢超出 double 也报 L004。
- `\` + 水平空白 + EOF → L005；`\` + 水平空白 + 非换行（包括 `#`）→ L006。
- 字符串内 `\` 后跟换行 → L003；代理区不合并。

---

## M2 — 语法 / AST / Parser（下一步）

**目标**：把 Token 流解析成带 Span 的 AST，完成命令参数切词、语句/表达式判定、`Incomplete` 原因，并提供 `--dump-ast`。

**文件**

- `src/lush/core/ast.h`
- `src/lush/core/ast.cpp`（AST dump/visitor）
- `src/lush/core/parser.h/.cpp`
- `tests/unit_parser.cpp`
- `src/cli/main.cpp`：`--dump-ast <file>`、`--dump-ast -e "<source>"`

**AST schema**

语句：

- `Program`
- `Block`
- `If`（cond、then、可选 else）
- `While`
- `ForIn`（变量名、iterable、body）
- `For`（可选 init 变量/表达式、可选 cond、可选 step、body）
- `Break` / `Continue`
- `Assign`（lvalue、value）
- `RefAssign`（目标名、源名）
- `Command`（name、args）
- `EmptyStmt`（显式 `;` 出现在语句位置或控制语句 body 时产生；仅作为非空语句终止符的分号、空行不产生节点）

表达式：

- `Int`（int64）、`Float`（double）、`String`（解码后）、`Bool`
- `Variable`
- `Unary`、`Binary`、`Ternary`
- `Call`、`Index`、`Member`
- `IncDec`（prefix/postfix）
- `Deref`（只允许紧贴的 `*IDENT`）
- `Point`（坐标表达式列表 + `bare` 标志）
- `List`

约定：

- 所有节点带 `Span`，指向原始源文本；复合节点覆盖第一个 token 到最后一个 token。
- 括号分组不保留为独立节点，`(x)` 直接是 `x`；分组对可赋值性透明，`(x)++` 合法。
- `line 1,2` 生成 `Point(bare=true)`；`line (1,2)` 生成 `Point(bare=false)`。
- 命令名保存原始拼写；M4 再大小写不敏感查找。
- `INT64_MIN` 折叠：边界字面量只允许作为（可带括号的）单目 `-` 的直接操作数；其它位置在 M2 报 L004。`--9223372036854775808` 的 AST 是外层一元负号，溢出留给 M3。
- Parser 不做 Point 坐标类型检查、不做 `++` 的 int 类型检查；这些留给 M3。

**Parser API**

```text
ParseOptions { Script | Repl }
ParseStatus  { Ok | Error | Incomplete }
IncompleteReason =
    UnclosedBrace | UnclosedParen | UnclosedBracket
  | TrailingContinuation | NeedStatement

ParseResult { status, incompleteReason, program, diagnostic }
```

- Lexer 的 `LexResult` 增加 `endsWithContinuation`：最后一个被删除的合法续行之后没有产生任何 token 时为 true（注释/空白/空行不算 token）。
- Repl 模式下，尾随续行（含 `\` 位于 EOF）转成 `Incomplete(TrailingContinuation)`。
- Repl 模式下，控制头缺 body 返回 `Incomplete(NeedStatement)`：
  - `if (...)` 后 EOF
  - `else` 后 EOF
  - `while (...)` 后 EOF
  - `for (...)` 后 EOF
  - `for i in expr` 后 EOF
- 未闭合 `{}` / `()` / `[]` 在 Repl 模式下返回对应的 `Unclosed*`。
- Script 模式：未闭合括号和控制头缺 body 都是 P002；`\` 位于 EOF（没有换行）是 L005；`\` + 换行仍是合法续行。
- 缺少表达式的输入（`a = 1 +`、`a =`）仍是 P002，不返回 `Incomplete`。
- 有明确 P 错误时错误优先，不转 `Incomplete`；错误/Incomplete 不返回部分 AST。

**语法行为**

- 句子位置是命令；表达式位置是后缀调用。M2 只按语法解析，S008 由 M4 注册表校验。
- `f(1,2)` 在语句位置是命令 `f` + 一个 Point 参数；`f()` 在语句位置是命令 `f` + 空括号词，`()` 不是合法表达式 → P004。
- 命令参数按顶层 `wsBefore` 切词，每词优先 `barePoint`，再 `expr`，必须整词消费。
- 回退规则：
  - `x[0]` 单独一行 → 命令 `x` + 参数 `[0]`（语法合法，未知命令留给 S001）；
  - `x.y` 单独一行 → 命令 `x` + 词 `.y` → P004 在 `.y`；
  - `p .x = 5` → P004 在 `.x`；
  - `a [0] = 1` → P004 在 `=`。
- `*` 必须紧贴变量：`x = *h` 合法，`x = * h` 是 P001（在 `h`）；`*h = v` 合法，`* h = v` 是 P001；`*h[0]` 是 P001（在 `[`）。
- 前缀 `-`/`!`/`++`/`--` 不要求紧贴；后缀必须紧贴。`i ++` 在表达式位置是 P001（在 `++`）。
- `++`/`--` 的目标不是变量/元素/成员 → S009；`(x)++` 合法。
- `a = b = c` → P001 在第二个 `=`。
- `a = ref` 单独出现，或 `a = ref` 后用普通换行接变量，都是 S004（在 `ref`）；只有显式 `\` 续行才允许 `ref` 与变量拼到同一逻辑行。
- `ref` 出现在其它位置、左右侧不是简单变量名 → S004（位置在 `ref`）。
- `def` / `return` 出现在任何位置 → S007（在关键字）。
- 空语句 `;` 可作为控制语句 body：`if (a);`、`while (a);`、`for (;;);`；`if (a); b = 2` 合法，`b = 2` 是下一条语句。
- 换行省略位置按 spec §3.8。
- 语法错误 P001–P006 的位置按 spec §3.9。

**AST dump**

- `--dump-ast <file>` / `--dump-ast -e "<source>"`，使用**脚本模式**。
- 缩进树；每个节点打印类型、起点和终点的 `line:col`，不打印字节 offset；终点表示最后一个字符之后的位置（end-exclusive）。
- 字面量保存实际值：int 十进制、double 最短表示、string 转义、bool `true`/`false`。
- 空程序 dump 为 `Program @1:1..1:1`；空行不产生节点；显式 `;` 在语句位置产生 `EmptyStmt`，只有作为非空语句终止符的分号不产生节点。
- 解析失败只打印 §11.2 诊断并退出 1；不打印半成品 AST。
- `--dump-ast` 与 `--dump-tokens` 互斥；同时给出时按用法错误退出 2。
- 增加 CTest `lush_dump_ast`。

示例风格：

```text
Program @1:1..1:9
  CommandStmt name="line" @1:1..1:9
    BarePoint @1:6..1:9
      Int 0 @1:6..1:7
      Int 0 @1:8..1:9
```

**测试**

- spec 中所有语法示例；
- `line a - b`、`line -1 -2`、`line 1,2 3,4`、`line 1, 2`；
- `line(1,2)`、`type(4.4)(2)`、`(type(1))(2)`；
- 语句位置 `f(1,2)` 是命令 + Point 参数；`f()` → P004；
- `line 1,2,3,4` → P004 在词起点；`line (1,2,3,4)` → P003 在 `(`；`line (1,2),3` → P004；
- `p = point [1,2]` → P001 在 `[`；
- `x = *h` / `echo *r` 合法；`x = * h`、`* h = v`、`*h[0]` → P001；
- 表达式位置 `y = (x)++` 合法；`x = 1++` → S009；单独一行 `1++` / `(x)++` 是 P001（裸表达式不是语句）；
- `a = b = c` → P001 在第二个 `=`；
- `a = 1 b = 2` → P006 在 `b`；`a = 1 2` → P001 在 `2`；
- `(1,2]` → P005 在 `]`；顶层 `)` → P001；
- `x[0]` 回退为命令 `x` + `[0]`；`x.y` → P004 在 `.y`；`p .x = 5` → P004 在 `.x`；`a [0] = 1` → P004 在 `=`；
- `a = ref` / `a = ref` + 普通换行 + 变量 → S004；`a = ref \` 续行 + 变量 → 合法；
- `def` / `return` → S007；
- `if (a);`、`while (a);`、`for (;;);`、`if (a); else b = 2` 合法；`if (a); b = 2` 合法；`a = 1;;b = 2` 中的第二个 `;` 产生 `EmptyStmt`；
- Repl `a = ref \` 续行结尾 → `Incomplete`；没有续行的 `a = ref` → S004；
- 多行块；
- Repl 未闭合 `{}` / `()` / `[]` → `Incomplete(Unclosed*)`；
- Repl 尾随 `\` → `Incomplete(TrailingContinuation)`；
- Repl `if (true)` / `for (;;)` EOF → `Incomplete(NeedStatement)`；`if (a) else ...` → P001；
- 脚本 EOF 未闭合 → P002；脚本 `if (true)` EOF → P002；
- AST dump 覆盖命令、赋值、if/else、循环、ref、Point/barePoint、list、EmptyStmt；
- G15/G16/G34/G46 的解析结果符合 spec。

**完成标准**

- `unit_parser` 全绿；
- `--dump-ast` 可用于人工核对；
- 解析阶段不执行、不注册命令；
- `/W4` 无警告。

**边界**

- 只做语法层；求值、命令绑定、会话都不在 M2。
- 命令参数切词在 Parser 中完成，Lexer 只提供 `wsBefore`、Token 和末尾续行标记。

---

## M3 — 值、环境、求值、内建

**目标**：实现 v1 的值模型、环境/作用域、表达式求值、语句执行、格式化与内建函数；不依赖参考宿主即可跑通非命令 golden。

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

- `Value`：int64、double、bool、string、list、Point、callable。
- 值语义：读取/赋值默认深拷贝；`list` 用 `shared_ptr` 时必须自定义拷贝构造/赋值来克隆。
- `Cell` + `Env` + `hasInboundRef`；`ref` 单层规则（spec §4.5）。
- 作用域、循环、`break`/`continue`。
- 算术、比较、逻辑、三元、索引、成员、`++`/`--`。
- `type` 值与 `bool`/`list`/`string` 等构造器；callable 可存储、可传递、可调用。
- `echo` 输出；格式化按 spec §9。
- `checked.h` 负责 int64 检查算术（溢出、`INT64_MIN / -1`）。
- 运行时错误 `RuntimeError`，带 `Diagnostic`。
- `output.h` 提供输出抽象，便于后续宿主/cli 接管。

**测试**

- 非命令 golden：G01–G10、G17–G19、G29–G32、G38–G43、G45–G49。
- `ref` 专项：G04–G07、G35–G36、G50–G52。
- 只读成员/索引、越界、除零、溢出、`INT64_MIN`。
- `unit_value`、`unit_ref` 全绿。

**完成标准**

- 非命令 golden 全绿；
- 不依赖 reference host 即可运行；
- `/W4` 无警告。

**边界**

- 不实现命令注册表、交互会话或 CLI 脚本模式；
- `echo` 是 core 内建命令，但完整命令系统在 M4。

---

## M4 — 命令注册与参考宿主

**目标**：实现声明式命令/函数注册、参数绑定和参考宿主，让命令 golden 跑通。

**文件**

- `src/lush/command/spec.h`
- `src/lush/command/registry.h/.cpp`
- `src/lush/command/host.h`
- `src/reference/reference_host.h/.cpp`
- `tests/unit_command.cpp`

**任务**

- `ParamType / ParamSpec / CommandSpec / Signature`。
- 大小写不敏感注册表、别名、内建注册。
- 参数绑定转换：int → double、list → Point、`any` 接受 callable 等。
- 元数检查、变参、可选参数、默认值。
- 区分命令调用与函数调用；命令不是值。
- 校验错误 S001/S002/S006/S008（以及相关的 S003/S004/S005/S007）。
- reference host 实现 `line` / `circle` / `move`，通过 `OutputSink` 写转录。
- `echo` 已在 core 内建，不重复实现。

**测试**

- 命令 golden：G11–G16、G20–G21、G23–G28、G33、G37、G44。
- 参数类型错误、元数错误、命令进入表达式。
- `unit_command` 全绿。

**完成标准**

- 命令 golden 全绿；
- `line` / `circle` 的提示文本和转录格式正确；
- 不实现会话状态机（M5）。

---

## M5 — 会话与 REPL

**目标**：把命令系统接到可暂停/恢复的交互会话和脚本/REPL 驱动上。

**文件**

- `src/lush/command/session.h/.cpp`
- `src/lush/driver/result.h`
- `src/lush/driver/driver.h/.cpp`
- `src/cli/repl.h/.cpp`
- `src/cli/console_sink.h/.cpp`
- `tests/unit_driver.cpp`

**任务**

- 会话状态：`NEED_INPUT / DONE / ERROR / CANCELLED`。
- 必选/可选参数、默认值、空输入、取消。
- 会话输入按 `barePoint | expr` 解析。
- 脚本模式不进入会话；脚本缺参报 S006。
- 复用 M2 的 `IncompleteReason`：
  - `NeedStatement`：`if` / `while` / `for` / `for-in` / `else` 缺 body；
  - `TrailingContinuation`：末尾续行；
  - `UnclosedBrace/Paren/Bracket`：未闭合括号。
- 空白行策略：
  - `NeedStatement` / `TrailingContinuation`：空白行取消待完成输入，不执行、不报错；
  - `Unclosed*`：空白行作为空白追加，继续等待；
  - 注释行不算空白行，继续等待；
  - 顶层空行是空语句；
  - 空 body 必须显式写 `;`：`if (a);`、`while (a);`、`for (;;);`。
- `if (true)` 后 EOF、`for (;;)` 后 EOF 在 Repl 中是 `Incomplete(NeedStatement)`；脚本中是 P002。
- `a = 1 +`、`a =` 等缺少表达式仍是 P002，不转 `Incomplete`。
- 脚本模式不续行：EOF 未闭合括号是 P002，`\` 位于 EOF（没有换行）是 L005，`\` + 换行仍是合法续行。
- CLI：
  - `lush` 进入 REPL；
  - `lush file.lush` 执行脚本；
  - `lush -e "..."` 执行一段源码；
  - Windows console 保持 UTF-8。
- 错误策略：解析/校验首错中止；会话错误停留当前参数；取消结束会话。

**测试**

- G22；
- 多行块；
- `if (true)` / `for (;;)` 后 EOF → `Incomplete(NeedStatement)`；
- 空白行取消 `NeedStatement` / `TrailingContinuation`；
- 未闭合块内空白行继续等待；
- 注释行不取消待完成输入；
- `if (a);` / `while (a);` / `for (;;);` 空 body；
- 取消；
- 脚本 vs REPL 的 S006 差异；
- 会话中输入 `0,0`；
- `unit_driver` 全绿。

**完成标准**

- CLI 可用；
- G22 通过；
- 脚本模式缺参报 S006；
- REPL 未闭合输入能继续读取。

---

## M6 — 补全

**目标**：提供与命令声明一致的补全核心 API，复用同一套词法/切词规则。

**文件**

- `src/lush/completion/completion.h/.cpp`
- `tests/unit_completion.cpp`

**任务**

- 上下文判定（spec §12.1）。
- 候选来源：关键字、可见变量、命令、函数/类型构造器。
- 命令参数候选按 `ParamSpec` 的类型/维度过滤。
- 支持上下文：`for i in`、`[`、`(`、`f(`、`.`、会话。
- 复用 lexer/切词规则，禁止另写扫描器。
- CLI 的 Tab 补全暂缓；先完成核心 API 与测试。

**测试**

- `line `、`line 1,2 `、`f(`、`type(`、`[`、`.`、会话；
- 大小写匹配；变量名大小写敏感；
- spec §12 的补全用例。

**完成标准**

- spec §12 用例全绿；
- `unit_completion` 全绿；
- CLI Tab 可后续接入，不阻塞 M6 完成。

---

## 跨里程碑约束

- 依赖方向：`cli → driver → command → core`；`completion → core + command`；`reference → command`。反向依赖不允许。
- `core` 只依赖标准库，不引入 Qt 或第三方库。
- 每个语义变更先改 `docs/spec.md`，再补 golden/单元测试，最后改代码。
- 每个 bug 修复补回归测试；每个错误码至少一个用例。
- 每个里程碑结束都要：`/W4` 零警告、相关测试全绿、更新 `README.md` / `AGENTS.md` 的进度。
- 不要提前实现后续里程碑的模块；M2 不写求值，M3 不写命令系统，M4 不写会话。

M7（收尾、全量 golden、性能与文档统一）暂不在本文展开；M6 完成后再单独规划。
