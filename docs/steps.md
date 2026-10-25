# Lush 开发里程碑（M1–M11）

本文是开发路线图，说明每个里程碑的目标、文件、任务、测试和完成标准。语言语义仍以 `docs/spec.md` 为唯一来源；AGENTS.md 里的构建/测试约定同样适用。

## 当前基线

| 里程碑 | 状态 | 说明 |
|---|---|---|
| M0 构建骨架 | ✅ 已完成 | CMake presets、CLI 占位、测试 harness、CTest |
| M1 词法 | ✅ 已完成 | UTF-8、Source、Diagnostic、Token、Lexer、`--dump-tokens` |
| M2 语法 | ✅ 已完成 | AST、Parser、命令参数切词、`Incomplete`、`--dump-ast`；`unit_parser` 全绿 |
| M3 求值 | ✅ 已完成 | Value、Cell/Env、Eval、Validate、格式化、内建、非命令 golden |
| M4 命令 | ✅ 已完成 | 命令 spec/registry、参数绑定、参考宿主、命令 golden |
| M5 会话 / REPL | ✅ 已完成 | Session、Driver、SourceId/SourceTable、脚本/REPL CLI |
| M6 补全 | ✅ 已完成 | 补全核心、类型过滤、`returnType`、参考宿主 `read`；CLI Tab 移至 M9 |
| M7 数值字符串 | ⬜ 计划 | `int(string)` / `double(string)`、R013、严格解析 |
| M8a 函数语法 | ⬜ 计划 | `def`/`return`、`->`、类型注解、AST、Parser、静态校验 |
| M8b 函数运行时 | ⬜ 计划 | 用户函数值、Source/AST 所有权、作用域、return、递归、REPL |
| M8c 函数工具链 | ⬜ 计划 | 用户函数签名、补全、类型注解补全、REPL 诊断 |
| M9 CLI Tab | ⬜ 计划 | vendored replxx、行编辑/历史/补全、Windows 实测 |
| M10 编辑器/宿主 API | ⬜ 计划 | trivia、best-effort token、公开宿主 API、QSyntaxHighlighter 示例 |
| M11 用户文档 | ⬜ 计划 | 语言使用简述、宿主集成说明、可编译示例 |

M1–M6 已完成。`lush_tests` 当前 219 个测试，CTest 目标为 `lush_tests`、`lush_version`、`lush_dump_tokens`、`lush_dump_ast`、`lush_eval`。M7–M11 的语义目标见 `docs/spec.md` 0.4.0；实现顺序按本文执行，不得提前声称后续功能已可用。

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

## M2 — 语法 / AST / Parser（已完成）

**目标**：把 Token 流解析成带 Span 的 AST，完成命令参数切词、语句/表达式判定、`Incomplete` 原因，并提供 `--dump-ast`。

**文件**

- `src/lush/core/ast.h`
- `src/lush/core/ast.cpp`（AST dump/visitor）
- `src/lush/core/parser.h/.cpp`
- `tests/unit_parser.cpp`
- `src/cli/main.cpp`：`--dump-ast <file>`、`--dump-ast -e "<source>"`

**AST schema**

语句：

- `Program`（statements）
- `Block`（statements）
- `If`（cond、then、可选 else）
- `While`（cond、body）
- `ForIn`（变量 Name、iterable、body）
- `For`（init 为可选 `Assign`、cond/step 为可选表达式、body）
- `Break` / `Continue`
- `Assign`（target、value、`=` 的 Span）
- `RefAssign`（目标 Name、源 Name、`ref` 的 Span）
- `Command`（Name、args）
- `EmptyStmt`

表达式：

- `Int`（int64）、`BoundaryInt`（无值）、`Float`（double）、`String`（解码后）、`Bool`
- `Variable`（Name）
- `Unary`、`Binary`、`Ternary`
- `Call`、`Index`、`Member`
- `IncDec`（prefix/postfix、op）
- `Deref`（保留 `Variable` 子节点 + `*` 的 Span）
- `Point`（坐标表达式列表 + `bare` 标志）
- `List`

公共结构：

- `Name { text, span }`：变量名、命令名、成员名、for 变量名、ref 目标/源。
- 所有节点继承 `Node`，带 `Span`；`Stmt`、`Expr` 是中间基类。
- `Program` / `Block` 持有 `vector<unique_ptr<Stmt>>`；`For` 的 init 是 `unique_ptr<Assign>`。
- AST 通过公开 visitor 访问；每个节点一个 `visit(const X&)`，默认空实现。

Span 约定：

- 节点的 Span 指向原始源文本，end-exclusive。
- 表达式节点的 Span 覆盖它在源码中书写的完整表达式，**包括直接包住它的分组括号**。
  - `(x)` 的 Variable Span 是 `(x)`，但 `Variable::name.span` 只覆盖 `x`。
  - `(1+2)` 的 Binary Span 覆盖 `(1+2)`。
  - `((1,2))` 的 Point Span 包含最外层分组括号；Point 字面量自身的括号属于 Point。
  - Point 的坐标节点不包含 Point 字面量的括号；`(1,2)` 中 `1`、`2` 只覆盖自己。
  - `BoundaryInt` 是唯一例外：Span 只覆盖字面量，不包含外层括号。
  - `(-922...)` 的 Unary Span 包含最外层 `(` `)`；`-(922...)` 的 Unary Span 包含 `-` 和操作数括号。
- 语句 Span 不包含末尾终止符 `;`；`EmptyStmt` 自己的 Span 包含它的 `;`。
- `Program` 非空时从第一条语句的起点到最后一条语句的终点；空程序为 `1:1..1:1`。
- `Block` 包含 `{` 到 `}`；`Command` 从命令名到最后一个参数；无参数时到命令名末尾。
- `If` 从 `if` 到 then/else body 末尾；`While`/`ForIn`/`For` 从关键字到 body 末尾。
- `Assign` 从 target 起点到 value 终点（不含 `;`）；`RefAssign` 从目标名起点到源名终点。
- `Call` 从 callee 起点到 `)`；`Index` 从 receiver 起点到 `]`；`Member` 从 receiver 起点到成员名末尾；`Point`/`List` 覆盖自己的括号；`Deref` 从 `*` 到变量名末尾。

子 Span（供 M3/M4 诊断）：

- `Binary::opSpan`、`Unary::opSpan`、`IncDec::opSpan`
- `Deref::starSpan`
- `Ternary::questionSpan` / `colonSpan`
- `Member::member.span`、`Command::name.span`、`ForIn::var.span`
- `RefAssign::target.span` / `source.span` / `refSpan`
- `Assign::assignSpan`
- `Call::lparenSpan` / `rparenSpan`
- `Index::openSpan` / `closeSpan`

约定：

- 括号分组不保留为独立节点，`(x)` 直接是 `x`；分组对可赋值性透明，`(x)++` 合法。
- `line 1,2` 生成 `Point(bare=true)`；`line (1,2)` 生成 `Point(bare=false)`。
- 命令名保存原始拼写；M4 再大小写不敏感查找。
- `INT64_MIN`：`BoundaryInt` 只允许作为（可带括号的）单目 `-` 的直接操作数；AST 保留 `Unary(-, BoundaryInt)`，不折叠成 `Int(INT64_MIN)`。其它位置在 M2 报 L004。`--922...` 词法为 `MinusMinus` 前缀，报 S009 在 `--`；`- -922...` / `-(-922...)` 的 AST 是外层一元负号，溢出留给 M3。
- `BoundaryInt` 不存值；dump 不打印值。
- Parser 不做 Point 坐标类型检查、不做 `++` 的 int 类型检查；这些留给 M3。

**Parser API**

```text
ParseOptions { Script | Repl }
ParseStatus  { Ok | Error | Incomplete }
IncompleteReason =
    UnclosedBrace | UnclosedParen | UnclosedBracket
  | TrailingContinuation | NeedStatement

ParseResult { status, incompleteReason, program, diagnostic }
ExprParseResult { status, incompleteReason, expression, diagnostic }

TokenRange { begin, end }        // token 下标，end-exclusive
WordEnd     { WordBoundary | StatementTerminator | InputEnd }
Boundary    { WordBoundary | StatementTerminator | InputEnd
            | ClosingParen | ClosingBracket | Comma | Colon }
CommandWord { range, end }
```

```cpp
ParseResult parse(const LexResult&, ParseOptions);
ExprParseResult parseExpression(const LexResult&, TokenRange, Boundary, ParseOptions);
ExprParseResult parseWord(const LexResult&, TokenRange, WordEnd, ParseOptions);
std::vector<CommandWord> splitCommandWords(const std::vector<Token>&,
                                           std::size_t begin, std::size_t end);
std::string dumpAst(const Program&, const Source&);
std::string dumpExpr(const Expr&, const Source&);
```

- `ParseResult.program` 是 `std::unique_ptr<Program>`；`ExprParseResult.expression` 是 `std::unique_ptr<Expr>`；Error/Incomplete 时两者为空。
- `LexResult` 增加（放在现有 `tokens, diagnostic` 之后）：`endsWithContinuation`、`lastContinuationOffset`、`endOffset`。
  - `endsWithContinuation`：最后一个被删除的合法续行之后没有非 NL token 时为 true；注释/空白/空行产生的 NL 不算；L005 也置 true。
  - `lastContinuationOffset`：最后一个被删除合法续行的原始 offset；L005 是反斜杠 offset；没有续行时为 `npos`。Parser 用它判断显式终止符是否位于续行之后。
  - `endOffset`：源文本末尾 offset，供无 EOF token 的 L005 生成 P002/Incomplete。
- `parse` 不接受 `Source`；`dumpAst`/`dumpExpr` 才需要 `Source` 计算 line:col。
- 词法错误处理：
  - 非 L005：直接返回 Error。
  - L005 + Script：Error(L005)。
  - L005 + Repl：用 tokens + endOffset 继续解析；有确定错误先报错，否则按 Incomplete 优先级处理。
- Repl Incomplete 优先级：
  1. 已经出现的确定性错误（不是“输入末尾缺少 token”的 P001/P003/P005/S004/S007/S009/L004 等）→ Error；`a = ref` 这种仅因 RHS 在末尾缺失而产生的 S004 可被尾随续行 defer；
  2. `Unclosed*`（多个未闭合取最内层）；
  3. `NeedStatement`；
  4. `TrailingContinuation`：最后一个被删除的合法续行之后没有产生任何**非 NL** token；注释、空白行、注释行产生的 NL 都不算。包括 `\` 位于 EOF（L005）、`\` 后只有水平空白/注释/空行，或最后一行以 `\` 后跟零个或多个水平空白结束。只有错误位置在 `lastContinuationOffset` 之后（或 `a = ref` 的 RHS 末尾缺失）才 defer。
  5. 以上都不是时，按普通结束错误处理：缺少表达式是 P002（显式语句终止符或 EOF）；`a = ref` 缺少 RHS 仍是 S004。显式终止符上的 P002/S004 只有在没有尾随续行时才会走到这里。
- `WordEnd` 语义：
  - `WordBoundary`：词后还有词；缺少表达式/操作数 → P004 在词首 token；
  - `StatementTerminator`：词后是 NL / `;` / `}` / `else`；缺少表达式/操作数 → P002 在终止符起点；
  - `InputEnd`：词后是 EOF；缺少表达式/操作数 → P002 在 `endOffset`；Repl + 尾随续行 → `Incomplete(TrailingContinuation)`。
  - 已经确定非法的词（多余坐标、尾随逗号、barePoint 逗号后坐标不完整、残留闭括号、词首根本无法开始表达式如 `+` 等）不受 `WordEnd` 影响。
- `Boundary` 用于 `parseExpression`；`parseWord` 只接受前三种。`)`/`]`/`,`/`:` 处缺少表达式是 P001。
- `splitCommandWords` 只切词、不解析：按顶层 `wsBefore` 切分，跟踪 `(`/`[` 深度，遇到语句终止符停止；返回每个词的 range 和 `WordEnd`。命令无参数时返回空 vector。
- `parseExpression` / `parseWord` 必须整段消费 range；残留 token 按 P004/P001 处理。
- 有明确 P 错误时错误优先，不转 `Incomplete`；错误/Incomplete 不返回部分 AST。

**语法行为**

- 语句位置只允许 block/if/while/for/break/continue/赋值/命令。单独的 `(`、`[`、字面量、`++`/`--` 等报 P001 在第一个 token；P003/P005 只在表达式位置。
- 语句位置是命令；表达式位置是后缀调用。M2 只按语法解析，S008 由 M4 注册表校验。
- `f(1,2)` 在语句位置是命令 `f` + 一个 Point 参数；`f()` → P004 在 `(`。
- 命令参数按顶层 `wsBefore` 切词，每词优先 `barePoint`，再 `expr`，必须整词消费。
- P004 位置：
  - 默认词首 token；
  - barePoint 坐标本身是非 bare Point 字面量 → 该坐标起点；
  - 多余坐标 → 词首 token（多余坐标优先于 Point 字面量）；
  - 具体结构错误保留 P003/P005/P001（括号内尾随/多余逗号、残留闭括号）/P002/S004/S007/S009/L004。
- barePoint 一旦出现逗号但后续坐标不完整，P004 在词首 token：`line 1,`、`line 1,2,`、`line 1,2,3,`、`line 1, 2`、`line 1,,2` 都是 P004 在 `1`；`line 1 ,2` → P004 在 `,`。
- 命令词缺少操作数且到达语句终止符/EOF → P002（见 WordEnd）；在空白词边界结束 → P004。例：`line a+ b` P004 在 `a`；`line a+` EOF P002 在 EOF；`line 1?2` EOF P002；`line 1?2 3` P004 在 `1`；`line +` EOF P004 在 `+`；`line -` EOF P002。
- `line (a + )` / `line f(a + )` / `line [1 + ]` / `line (1 ? 2 : )` → P004 在包含缺失操作数的表达式起点：`(` / `f` / `[` / `(`。
- 尾随逗号：`line f(1,)` P001 在 `)`；`line [1,]` P001 在 `]`；`line (1,)`、`line (1,2,)`、`line (1,2,3,)` P001 在 `)`。
- 残留闭括号：`line 1,2)` P001 在 `)`；`line 1,2]` P001 在 `]`；`line (1,2))` P001 在第二个 `)`。
- 回退规则：
  - `x[0]` 单独一行 → 命令 `x` + 参数 `[0]`（语法合法，未知命令留给 S001）；
  - `x.y` 单独一行 → P004 在 `.`；
  - `x++` 单独一行 → P004 在 `++`；`x + 1` → P004 在 `+`；`x == 1` → P004 在 `==`；
  - `p .x = 5` → P004 在 `.x`；
  - `a [0] = 1` → P004 在 `=`。
- `*` 必须紧贴变量：`x = *h` 合法，`x = * h` 是 P001（在 `h`）；`*h = v` 合法，`* h = v` 是 P001；`*h[0]`、`*h.x`、`*h(1)`、`*h++` → P001 在对应后缀。
- 前缀 `-`/`!`/`++`/`--` 不要求紧贴；后缀必须紧贴。`i ++` 在表达式位置是 P001（在 `++`）。
- `++`/`--` 的目标不是变量/元素/成员 → S009 在运算符；Deref 不可赋值；`(x)++` 合法；`line 1++` / `x = 1++` → S009 在 `++`。
- 紧贴 `.` 后必须是 IDENT；否则 P004 在 `.`（`ref`/`def`/`return` 也不例外）。`.` 后直接是终止符/EOF → P002。`a = x.ref`、`a = x.def` → P004 在 `.`；`a = x .ref` → P001 在 `.`。
- `a = b = c` → P001 在第二个 `=`。
- `a = ref` / `a = ref` + 普通换行 + 变量 → S004 在 `ref`；`a = ref \` 续行 + 变量 → 合法；Repl 尾随续行 → `Incomplete(TrailingContinuation)`；`a = ref 1 \` / `a = ref f + \` → S004。
- `ref` 出现在其它位置、左右侧不是简单变量名 → S004 在 `ref`。
- `def` / `return` 出现在任何位置 → S007 在关键字；紧贴 `.` 后的成员名位置例外（P004 在 `.`）。
- `a = 1 b = 2` → P006 在 `b`；`a = 1 2` → P001 在 `2`。
- 空语句：`if (a);`、`while (a);`、`for (;;);` 合法；`if (a); b = 2` 合法；`a = 1;;b = 2` 第二个 `;` 产生 EmptyStmt；`{};` 的 `;` 是 EmptyStmt。
- `if (a) b=1; else c=2` 合法；`if (a) b=1;; else c=2` / `if (a) b=1\n; else c=2` / `if (a) {}; else c=2` → P001 在 else；`if (a) {} \n else c=2` 合法。
- `if (a) {} b=2`、`{ a=1 } b=2` 合法；`if (a) b=1 c=2` → P006 在 c。
- `for (i=0; i<n; i++)`：init 是 Assign 节点；`for (p.x=0; ...)` P001 在 `.`；`for (i=ref j; ...)` S004；`for (i; ; )` P001 在 `;`；step 赋值 P001 在 `=`。
- `for i in expr` 后允许换行；`for i in a b` 是 iterable `a`、body 命令 `b`；缺 body 遇 `}` 是 P001 在 `}`。
- 换行省略位置按 spec §3.8。
- 语法错误 P001–P006 的位置按 spec §3.9。

**AST dump**

- `--dump-ast <file>` / `--dump-ast -e "<source>"`，使用**脚本模式**。
- 2 空格缩进；每个节点打印 `类型 [字段] @line:col..line:col`，end-exclusive，不打印字节 offset。
- 节点名使用 `Command`、`Point`、`BoundaryInt` 等 schema 名称。
- 固定角色用标签：`target:`、`value:`、`cond:`、`then:`、`else:`、`body:`、`init:`、`step:`、`iterable:`、`callee:`、`args:`、`receiver:`、`index:`、`operand:`、`left:`、`right:`。
- 可选字段缺失写 `<none>`：`else: <none>`、`init: <none>`、`cond: <none>`、`step: <none>`、`args: <none>`。
- 可变容器（`Program` / `Block` / `Command` / `Point` / `List`）的子节点直接缩进，不额外加标签。
- `Deref` 打印 `Deref @...`，其下用 `target:` 包一个 `Variable`。
- `Member` 打印 `Member name="..." @...`，其下用 `receiver:` 包接收者。
- 字面量：int 十进制；`BoundaryInt` 不打印值；double 最短往返表示；string 始终加双引号并按 token dump 规则转义；bool `true`/`false`。
- 空程序 dump 为 `Program @1:1..1:1`；空行不产生节点；显式 `;` 在语句位置产生 `EmptyStmt`，只有作为非空语句终止符的分号不产生节点。
- 解析失败只打印 §11.2 诊断并退出 1；不打印半成品 AST。
- `--dump-ast` 与 `--dump-tokens` 互斥；同时给出时按用法错误退出 2。
- 增加 CTest `lush_dump_ast`。

示例：

```text
Program @1:1..1:9
  Command name="line" @1:1..1:9
    Point bare=true @1:6..1:9
      Int 0 @1:6..1:7
      Int 0 @1:8..1:9
```

其他节点示例：

```text
Assign @...
  target:
    Variable name="a" @...
  value:
    Int 1 @...

For @...
  init:
    Assign @...
      target:
        Variable name="i" @...
      value:
        Int 0 @...
  cond:
    Binary op="<" @...
      left:
        Variable name="i" @...
      right:
        Variable name="n" @...
  step:
    IncDec op="++" prefix=false @...
      target:
        Variable name="i" @...
  body:
    Block @...
      ...

RefAssign target="h" source="f" @...
EmptyStmt @...
BoundaryInt @...
```

**测试**

- spec 中所有语法示例；
- 语句位置 vs 表达式位置：单独的 `(1,2]`、`(1,2,3,4)`、`1++`、`(x)++` → P001 在首 token；`a = (1,2]` → P005 在 `]`；`a = (1,2,3,4)` → P003 在 `(`；
- `line a - b`、`line -1 -2`、`line 1,2 3,4`、`line 1, 2`、`line 1 ,2`、`line 1,,2`、`line 1,2,3,4`；
- `line(1,2)` 在语句位置是命令 + Point；表达式位置 `x = type(4.4)(2)`、`x = (type(1))(2)` 是链式调用；单独 `(type(1))(2)` 在语句位置是 P001；
- 语句位置 `f(1,2)` 是命令 + Point 参数；`f()` → P004 在 `(`；
- P004 位置：`line 1,2,3,4` → `1`；`line 1,(2,3)` → 第二个坐标的 `(`；`line ((1,2)),3` / `line 1,((2,3))` → 坐标起点 `(`；`line 1,2,(3,4)` → `(`；`line 1,2,3,(4,5)` → `1`；`line 1,2,3,` / `line 1,2,` / `line 1,` → `1`；`line a+ b` → `a`；
- 词边界 vs 输入结束：`line a+` EOF → P002；`line 1?2` EOF → P002；`line 1?2 3` → P004 在 `1`；`line +` EOF → P004 在 `+`；`line -` EOF → P002；`line * h` → P004 在 `*`；
- 命令词内部缺少操作数：`line (a + )` → P004 在 `(`；`line f(a + )` → P004 在 `f`；`line [1 + ]` → P004 在 `[`；`line (1 ? 2 : )` → P004 在 `(`；
- 尾随逗号：`line f(1,)` → P001 在 `)`；`line [1,]` → P001 在 `]`；`line (1,)`、`line (1,2,)`、`line (1,2,3,)` → P001 在 `)`；
- 残留闭括号：`line 1,2)` → P001 在 `)`；`line 1,2]` → P001 在 `]`；`line (1,2))` → P001 在第二个 `)`；
- `p = point [1,2]` → P001 在 `[`；
- `x = *h` / `echo *r` 合法；`x = * h`、`* h = v`、`*h[0]`、`*h.x`、`*h(1)`、`*h++` → P001；
- 表达式位置 `y = (x)++` 合法；`x = 1++` / `line 1++` → S009 在 `++`；单独一行 `1++` / `(x)++` 是 P001（裸表达式不是语句）；表达式位置 `x = (*h)++` → S009，`*h++` → P001；
- 成员：`a = x.ref`、`a = x.def` → P004 在 `.`；`a = x .ref` → P001 在 `.`；`a = x.` → P002；`a = x.;` → P002 在 `;`；
- `a = b = c` → P001 在第二个 `=`；
- `a = 1 b = 2` → P006 在 `b`；`a = 1 2` → P001 在 `2`；
- `x[0]` 回退为命令 `x` + `[0]`；`x.y` → P004 在 `.`；`x++` → P004 在 `++`；`x + 1` → P004 在 `+`；`x == 1` → P004 在 `==`；`p .x = 5` → P004 在 `.x`；`a [0] = 1` → P004 在 `=`；
- `a = ref` / `a = ref` + 普通换行 + 变量 → S004；`a = ref \` 续行 + 变量 → 合法；`a = ref 1 \` / `a = ref f + \` / `a = ref ; \` → S004；Repl `a = ref \` 尾随 → `Incomplete`；Repl `a = ref \` + 注释行 + EOF → `Incomplete`；
- `def` / `return` → S007；`a = x.def` 例外为 P004；
- `if (a);`、`while (a);`、`for (;;);`、`if (a); else b = 2` 合法；`if (a); b = 2` 合法；`a = 1;;b = 2` 中的第二个 `;` 产生 `EmptyStmt`；`{};` 产生 `EmptyStmt`；
- `if (a) b=1; else c=2` 合法；`if (a) b=1;; else c=2` / `if (a) b=1\n; else c=2` / `if (a) {}; else c=2` → P001 在 else；`if (a) {} \n else c=2` 合法；
- `if (a) {} b=2`、`{ a=1 } b=2` 合法；`if (a) b=1 c=2` → P006 在 c；
- `for (i=0; i<n; i++)` 的 init 是 Assign；`for (p.x=0; ...)` → P001 在 `.`；`for (i=ref j; ...)` → S004；`for (i; ; )` → P001 在 `;`；`for (;; i=i+1)` → P001 在 `=`；`for i in expr` 后换行合法；`for i in a b` 是 iterable `a` + body `b`；缺 body 遇 `}` → P001；
- Lexer：`endsWithContinuation` / `lastContinuationOffset` 覆盖合法续行、注释/空行、多个续行、L005；
- 多行块；
- Repl 未闭合 `{}` / `()` / `[]` → `Incomplete(Unclosed*)`，多个未闭合取最内层；
- Repl 尾随 `\` / L005 → `Incomplete(TrailingContinuation)`；
- Repl `if (true)` / `for (;;)` EOF → `Incomplete(NeedStatement)`；`if (a) else ...` → P001；
- Repl Incomplete 优先级：确定错误 > Unclosed* > NeedStatement > TrailingContinuation > P002；
- 脚本 EOF 未闭合 → P002；脚本 `if (true)` EOF → P002；脚本 `a = 1 \` + 换行 + EOF 合法；脚本 `a = 1 + \` + 换行 + EOF → P002；
- AST dump 覆盖命令、赋值、if/else、while、for-in、C 风格 for、ref、Point/barePoint、list、EmptyStmt、BoundaryInt、成员/索引/调用/三元/IncDec/Deref；
- AST dump 的 `Program`/`Command`/`Point`/`Assign`/`For`/`RefAssign`/`EmptyStmt` 文本按本文格式精确断言；
- `parseWord` 的 `WordBoundary` / `StatementTerminator` / `InputEnd` 三种结果；
- `parseExpression` 的 `Boundary`：`)`/`]`/`,`/`:` 处缺少表达式为 P001；
- G15/G16/G34/G46 的解析结果符合 spec。

**完成标准**

- `unit_parser` 全绿；
- `--dump-ast` 可用于人工核对，dump 文本是单元测试的精确契约；
- 解析阶段不执行、不注册命令；
- `/W4` 无警告。

**边界**

- 只做语法层；求值、命令绑定、会话都不在 M2。
- 命令参数切词在 Parser 中完成，Lexer 只提供 `wsBefore`、Token、`endsWithContinuation` 和 `endOffset`。

---

## M3 — 值、环境、求值、内建（已完成）

**目标**：实现 v1 的值模型、环境/作用域、表达式求值、语句执行、格式化与内建函数；不依赖参考宿主即可跑通非命令 golden。

**文件**

- `src/lush/core/value.h/.cpp`
- `src/lush/core/cell.h`
- `src/lush/core/eval.h/.cpp`
- `src/lush/core/validate.h/.cpp`
- `src/lush/core/format.h/.cpp`
- `src/lush/core/checked.h`
- `src/lush/core/output.h`
- `src/lush/core/builtins.h/.cpp`
- `tests/unit_value.cpp`
- `tests/unit_ref.cpp`
- `tests/unit_eval.cpp`
- `tests/golden_spec.cpp`

**任务**

- `Value`：int64、double、bool、string、list、Point、callable。
- 值语义：读取/赋值默认深拷贝；`list` 用 `shared_ptr` 时必须自定义拷贝构造/赋值来克隆。
- `Cell` + `Env` + `hasInboundRef`；`ref` 单层规则（spec §4.5）。
- 普通赋值 target-first：目标路径中的后缀 `++`/`--` 延迟到写入后提交；RHS 的后缀仍立即写回；`ref`/`for` init 例外（spec §5.7/§6.1）。
- 作用域、循环、`break`/`continue`。
- 算术、比较、逻辑、三元、索引、成员、`++`/`--`。
- `type` 值与 `bool`/`list`/`string` 等构造器；callable 可存储、可传递、可调用。
- `echo` 输出；格式化按 spec §9。
- `checked.h` 负责 int64 检查算术（溢出、`INT64_MIN / -1`）。
- 运行时错误 `RuntimeError`，带 `Diagnostic`。
- `output.h` 提供输出抽象，便于后续宿主/cli 接管。
- 最小校验（`validate`）：S001（相对 M3 `NameCatalog`）、S003、S005、S008；S002/S006 留 M4/M5。
- M3 的 `Command` 只支持 `echo`；语句位置的内建函数调用（如 `int 1`）暂报 S001，M4 再开放。

**测试**

- 非命令 golden：G01–G10、G17–G19、G29–G32、G38–G43、G45–G49。
- 测试接口：M3 阶段不新增 CLI 执行入口；`golden_spec` 直接链接 `lush_core`，组合 `Lexer` → `parse` → `validate` → `evalProgram`，用 `StringOutput` 捕获输出与诊断。`lush file.lush` / `lush -e` 已在 M5 实现。
- `ref` 专项：G04–G07、G35–G36、G50–G52。
- 只读成员/索引、越界、除零、溢出、`INT64_MIN`。
- target-first：`a[i] = i++`、`a[i++] = i`、`a[i++] = i++`、`a[i++] = i++ + i`；目标错误优先；RHS/写入失败时仍提交目标后缀；多重目标后缀；`i = i++` 保持旧值。
- 最小校验：`int = 1` / `ECHO = 1` / `for int in ...` / `int[0] = 1` / `x = int++` → S003；`x = ref int` → R001；`x = int[0]` → R002；顶层 `break`/`continue` → S005；`x = echo` / `echo echo` → S008；`foo 1` → S001。
- 错误位置按 §11.3（含 R006、S003、S005、S008）。
- 格式化：指数规范化、`-0.0`、嵌套字符串转义。
- `INT64_MIN % -1 == 0`，`INT64_MIN / -1` → R005。
- ref：`hasInboundRef` 离开作用域后仍不清除、ref 到 callable、ref 到 list/point 写穿透。
- `unit_value`、`unit_ref`、`unit_eval`、`golden_spec` 全绿。

**完成标准**

- 非命令 golden 全绿；
- 不依赖 reference host 即可运行；
- `unit_value`、`unit_ref`、`unit_eval`、`golden_spec` 全绿；
- `/W4` 无警告。

**边界**

- 不实现命令注册表、交互会话或 CLI 脚本模式；
- M3 阶段不新增 CLI 执行入口；`lush file.lush` / `lush -e` 已在 M5 实现；
- `echo` 是 core 内建命令，但完整命令系统在 M4；
- M3 的 `Command` 只支持 `echo`；语句位置的内建函数调用（`int 1`）留 M4。

---

## M4 — 命令注册与参考宿主（已完成）

**目标**：实现声明式命令/函数注册、参数绑定和参考宿主，让命令 golden 跑通。

**实现结果**

- core：`CallableInfo` / `callableInfo`、`ValidationOptions`、builtin arity 表、语句位置 builtin 调用已落地；`int 1` / `int(1)` 合法，`int 1 2` → S002。
- command：`spec.h` / `host.h` / `registry` 已落地；`Signature`、`RegistrationResult`、`shared_ptr<const RegisteredCallable>`、`bindStatement` / `bindCall`、R010。
- reference：`line` / `circle` / `move` 已落地，通过 `core::Output` 写转录。
- 测试：`unit_command` 已加入；M4 完成时 `lush_tests` 共 169 个测试，Debug / Release / Werror / Ninja 全绿。

**文件**

- `src/lush/core/builtins.h/.cpp`：`CallableInfo`、`NameCatalog`、builtin arity 表、语句位置 builtin 调用
- `src/lush/core/validate.h/.cpp`：`ValidationOptions`
- `src/lush/command/spec.h`
- `src/lush/command/registry.h/.cpp`
- `src/lush/command/host.h`
- `src/reference/reference_host.h/.cpp`
- `tests/unit_command.cpp`
- `tests/unit_eval.cpp`：更新 M3 的语句位置 builtin 断言
- `src/CMakeLists.txt`、`tests/CMakeLists.txt`

**设计约定（已拍板）**

- core 的 `NameCatalog` 增加 `CallableInfo`：
  - 字段为 `required`、`maxFixed`、`variadic`、`interactive`、`returnsValue`；
  - 只描述 arity/交互/返回值，不描述参数类型；
  - `isCommand` / `isReserved` 由 `callableInfo` 派生，避免注册状态漂移；
  - `validate` 增加 `ValidationOptions { bool allowInteractiveSession = false; }`。
- builtin 的联合类型/重载留在 `BuiltinCallable` 的 `callX` 中，不进入 `CallableInfo`：
  - `BuiltinCatalog` 维护一张 `BuiltinId` → arity 表，`callableInfo` 与 `BuiltinCallable::call` 共用；
  - `BuiltinCatalog::invokeCommand` 支持语句位置 builtin 函数调用（调用后丢弃返回值）；
  - `int 1` / `int(1)` 从此合法；`int 1 2` → S002；M3 的 `unit_eval` 两个 S001 断言改为合法。
- 变参采用解释 A：
  - 变参命令不得有可选参数；变参参数 `optional=false` 且无 `default`；
  - `echo` 的 `CallableInfo` 为 `required=0, maxFixed=0, variadic=true`。
- 宿主命令/函数：
  - `CommandSpec` 注册时校验成 `Signature`（校验后的参数表 + `help`/`aliases` + `required`/`maxFixed`/`variadic`/`interactive`/`returnsValue`）；
  - 默认值在注册时预绑定；默认值只接受字面量，拒绝 callable；
  - `Registry` 存 `shared_ptr<const RegisteredCallable>`；value 函数的 `Callable` adapter 也持有同一个 shared_ptr，保证 `t = f` 后 registry 销毁仍可调用；
  - 注册失败是宿主编程错误，`registerCommand` 返回 `RegistrationResult`；参考宿主用断言/终止。检查项至少包括：名称/别名冲突与关键字、与 builtin/`echo` 冲突、必选在可选后、变参不在最后/多个变参、变参+可选/默认值、可选缺默认值、必选或变参带默认值、重复参数名、默认值无法绑定、`interactive` 与 `returns=value` 同时为真、非 point 参数 `dim != 0`。
- 绑定：
  - 语句位置 `bindStatement`：arity S002 在命令名；
  - 表达式位置 `bindCall`：arity R002 在调用 `(`；
  - 类型错误 R002、point 转换 R006 在违规参数；共享转换/default 逻辑，不共享错误码/Span 选择。
- 参数求值：一次调用先从左到右求值全部参数，再统一绑定；绑定错误发生在求值之后。
- 宿主 API：
  - 直接复用 `core::Output`，不新增 `OutputSink`；
  - void 命令用 `CommandHandler`：接收绑定后的 `vector<Value>` + `CommandContext` + `Output&`；
  - value 函数用纯 `FunctionHandler`：接收绑定后的 `vector<Value>` + `CommandContext`，不接收 `Output`；
  - `CommandResult { optional<Value> value; optional<HostError> error; }`；`HostError { message; optional<Span> span; }`；
  - 宿主失败统一 R010；默认 Span 为语句位置命令名、表达式位置 callee，`HostError.span` 可覆盖；error 优先于 value，void/value 返回值不匹配是宿主编程错误；
  - 宿主 value 函数做最小实现：`returns=value` 的 `CommandSpec` + `FunctionHandler`，表达式位置和语句位置都可用。
- 构建：
  - 新增 `lush_command`、`lush_reference` 两个 static library；
  - `lush_tests` 链接两者；M5 已把 CLI 接到 driver 与 reference host。

**任务**

- core：在 `NameCatalog` 增加 `CallableInfo` 与 `callableInfo`；`isCommand`/`isReserved` 由 `callableInfo` 派生；`validate` 增加 `allowInteractiveSession`。
- core：`BuiltinCatalog` 增加 builtin arity 表与 `callableInfo`，支持语句位置 builtin 函数调用（`int 1` 合法，返回值丢弃）；`int 1 2` → S002。
- `ParamType / ParamSpec / CommandSpec / Signature`；注册约束与 `RegistrationResult`。
- 大小写不敏感注册表、别名、与 builtin/`echo` 的冲突检查。
- 参数绑定转换：int → double、list → Point、`any` 接受 callable 等。
- 元数检查、变参（解释 A）、可选参数、默认值（注册时预绑定，拒绝 callable）。
- 区分命令调用与函数调用；命令不是值；宿主 value 函数最小实现。
- 函数在语句位置合法（`int 1`，返回值丢弃）；参数个数错误 S002。
- 变量中的 callable 不是命令：`t = int; t 1` 仍 S001，`t(1)` 是表达式调用。
- `point` 参数：list 转换失败 → R006；非 list/point 类型或维度不符 → R002。
- 校验错误 S001/S002/S006/S008（以及相关的 S003/S004/S005/S007）。
- 脚本缺参：交互命令 S006，非交互命令 S002；参数过多一律 S002。
- `Registry` 存 `shared_ptr<const RegisteredCallable>`；value 函数 adapter 共享所有权。
- `bindStatement` / `bindCall` 两套错误码与 Span；参数先全部求值再绑定。
- void 命令用 `CommandHandler`（含 `core::Output`），value 函数用纯 `FunctionHandler`；`CommandResult` / `HostError`；宿主失败 R010。
- reference host 实现 `line` / `circle` / `move`，通过 `core::Output` 写转录。
- `echo` 已在 core 内建，不重复实现。

**测试**

- 命令 golden：G11–G16、G20–G21、G23–G28、G33、G37、G44；已有用例保留，新增命令用例放 `unit_command`。
- 参数类型错误、元数错误、命令进入表达式。
- 语句位置函数调用：`int 1` / `int(1)` 合法；`int 1 2` → S002；`t = int; t 1` → S001；`t(1)` 合法。
- builtin 语句位置 arity 边界：`int 1 2`、`point 1 2 3 4`、`len`、`echo` 的 0 个/多个参数；表达式位置对照 R002 在 `(`。
- 脚本缺参：`line 0,0` → S006；`move 1` → S002；`line 0,0 1,1 2,2` → S002。
- point 参数：`line [1] ...` → R006；`line "x" ...` → R002；维度不符 → R002。
- 宿主 value 函数：表达式调用、语句调用（返回值丢弃）、表达式/语句的 arity 差异、`t = f` 后 registry 销毁仍可调用。
- 宿主失败：`HostError` → R010；默认 Span 命令名/callee；已产生输出保留。
- 注册失败：名称/别名冲突、与 builtin/`echo` 冲突、变参+可选、变参带默认值、可选缺默认值、callable 默认值、默认值类型不符、`interactive` + `returns=value`。
- `tests/eval_test_utils.h` 增加接受 `NameCatalog&`/Registry 的 helper；M3 的 `int 1` / `int(1)` S001 断言改为合法。
- `unit_command` 全绿。

**完成标准**

- 命令 golden 全绿；
- `line` / `circle` 的提示文本和转录格式正确；
- `int 1` / `int(1)` 合法，`int 1 2` → S002；
- R010 已定义并有测试；
- 不实现会话状态机（M5）。

---

## M5 — 会话与 REPL（已完成）

**目标**：把命令系统接到可暂停/恢复的交互会话和脚本/REPL 驱动上。

**实现结果**

- `command::Session` 只收集必选参数；可选参数默认值由最终 `invokeCommand` 补齐。
- `driver::Driver` 支持脚本、REPL、`NeedMore`/`NeedInput`/可恢复错误/取消/EOF。
- `SourceId` / `SourceTable`、`Diagnostic` / `CommandContext` / `CallContext` / `EvalContext` / `HostError` 的 Source 信息已落地；无效 Source 回退默认。
- CLI 支持 `lush`、`lush file.lush`、`lush -e`；使用 ReferenceHost；提示符、非 TTY、`已取消`、Ctrl+C 和退出码按约定实现。
- `tests/unit_driver.cpp` 已加入；`lush_tests` 共 195 个测试；CTest 共 5 个目标。Debug / Release / Werror / Ninja 全绿。

**文件**

- `src/lush/command/session.h/.cpp`
- `src/lush/driver/result.h`
- `src/lush/driver/source_table.h/.cpp`
- `src/lush/driver/driver.h/.cpp`
- `src/cli/repl.h/.cpp`
- `src/cli/console_sink.h/.cpp`
- `tests/unit_driver.cpp`

**任务**

- `command::Session` 只做必选参数状态机和参数绑定，不做解析/求值/输出；`NeedInput { prompt, type, dim, index }`，不包含 optional/default/sourceId。
- 会话只收集必选参数；`argc >= required` 直接执行，可选参数由 `bindStatement`/`invokeCommand` 补默认值。`hint [default]` 属 M6 补全/Tooltip，不是会话提示。
- Driver 采用 pull 模型：宿主提交文本/Point，Driver 返回状态；会话错误可恢复并继续当前参数。
- 驱动状态：`Done / NeedMore / NeedInput / Error / Cancelled`；`NeedMore` 是 REPL 语法未完成，`NeedInput` 是会话缺参数，两者分开。
- 顶层判定：只允许一条非空 `Command`；命令之后允许 `EmptyStmt`；开头的 `;line ...` 不允许进入会话。只有 `single && interactive && argc < required` 才设置 `allowInteractiveSession = true`。
- 初始命令已提供的参数先全部求值再统一绑定；会话中每次输入只求值并绑定当前必选参数。
- 会话输入按 `barePoint | expr` 解析，使用单行 `ParseOptions::Script`；纯注释行忽略并重新提示；空白行按空输入；必选空输入取消并返回 R011；会话内不允许多行表达式。
- SourceId 方案：`SourceId = uint32_t`，0 表示无 Source，有效 ID 从 1 开始；`SourceTable` 由 Driver 内部持有。`Diagnostic`、`CommandContext`、`CallContext`、`EvalContext`、`HostError` 增加 Source 信息；无效 `HostError.source` 回退默认 Source。
- 直接提交 Point：没有文本 Source；绑定失败仍为 R002/R006，Span 为空，以 `InputOrigin::DirectPoint` 标记来源；`NeedInput` 不增加 sourceId，`CommandContext` 不增加 argOrigins。
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
- REPL EOF：有待完成输入时按 Script 重解析，必要时合成 P002；会话中 EOF 取消会话，不执行命令，不产生 R011；正常 EOF 退出 0；pending EOF 错误退出 1。
- Ctrl+C：会话中取消会话，pending 取消 pending，空闲清行重提示；Windows/POSIX 实现，不写自动测试；脚本模式不特殊处理。
- CLI：
  - `lush` 进入 REPL，使用 `ReferenceHost` 作为演示宿主；
  - `lush file.lush` 执行脚本；
  - `lush -e "..."` 执行一段源码；
  - 顺序解析参数，保留 `--dump-tokens` / `--dump-ast`；
  - 主提示符 `lush> `，续行 ` ...> `，会话提示 `hint: `；
  - 可选参数提示不在 CLI 实现；
  - 非 TTY 不打印提示符；
  - 会话必选空行在 TTY 下只打印 `已取消`，不打印完整 R011 诊断；
  - Windows console 保持 UTF-8。
- 错误策略：解析/校验首错中止；会话错误停留当前参数；取消结束会话；REPL 普通行错误不累计退出码。
- interactive + variadic 允许注册，但会话只补必选固定参数，变参不参与会话补齐，可以为 0 个。

**测试**

- G22；
- 多行块；
- `if (true)` / `for (;;)` 后 EOF → `Incomplete(NeedStatement)`；
- 空白行取消 `NeedStatement` / `TrailingContinuation`；
- 未闭合块内空白行继续等待；
- 注释行不取消待完成输入；
- `if (a);` / `while (a);` / `for (;;);` 空 body；
- 顶层单条判定：结尾 `EmptyStmt` 允许，开头 `;` 拒绝；
- 取消、R011、会话 EOF、pending EOF；
- 脚本 vs REPL 的 S006/S002 差异；
- 会话中输入 `0,0`、注释行、空输入；
- 可选参数不进入会话、执行时取默认值；
- interactive 变参不补变参；
- SourceId：初始命令 + 会话输入混合 Source、`HostError.source`、无效 Source 回退；
- 直接 Point：`InputOrigin::DirectPoint`、无文本 Span；
- CLI：`lush file`、`lush -e`、非 TTY 无提示；
- Ctrl+C 不写自动测试，只做手工/编译路径检查；
- `unit_driver` 全绿。

**完成标准**

- CLI 可用；
- G22 通过；
- 脚本模式交互命令缺参报 S006，非交互命令缺参报 S002；
- REPL 未闭合输入能继续读取；
- SourceId、R011、直接 Point Origin 和 EOF 有测试；Ctrl+C 已实现，按约定不做自动测试；
- M5 阶段不实现 M6 补全（M6 已完成）。

---

## M6 — 补全（已完成）

**目标**：提供与命令声明一致的补全核心 API，复用同一套词法/切词规则；同时完成 `CommandSpec` / `Signature` 的返回类型元数据和参考宿主 `read` 的设计落地。

**设计结论**

- 补全核心是纯计算 API：完整缓冲区 + byte cursor + `Env` + `Registry` + 可选 `NeedInput`；不执行表达式、不调用宿主、不写输出、不产生诊断。
- cursor 紧接 Identifier 时替换整个当前 token；字符串、注释、数字字面量和词法错误位置返回空候选。
- 上下文覆盖：语句开头、表达式、命令参数、`for i in`、`[`/`(`、表达式调用 `f(`/`t(`、`.` 成员和会话参数。
- 只有表达式位置的紧贴 `IDENT(` 是 callable 调用；`line(` 仍是命令参数，不是调用。
- 命令/函数/类型名和关键字大小写不敏感；变量和成员大小写敏感；别名作为独立候选。
- 参数类型按 §7.2 过滤；类型不匹配变量仍显示并降权。
- 核心插入纯文本；Point 参数额外提供显式 `(`；不自动追加空格或 `(`。
- `CommandSpec` / `Signature` 增加可选 `returnType`（`ValueType + dim`），缺省为 unknown；只作补全元数据，运行时校验不做。
- 参考宿主增加 `read(prompt: string = "") -> string`；通过注入的 `std::istream` + `core::Output` 工作；EOF/读取失败/无效 UTF-8 → R010。
- 不引入 HostInputService；`read` 直接使用宿主输入流，不接入 Driver 的 pending/session/SourceId/Ctrl+C 状态机。
- CLI Tab 移至 M9；M6 完成不依赖 Tab。

**实现结果**

- 新增 `lush_completion`（`completion.h/.cpp`），覆盖 §12 的上下文、候选、类型过滤、成员和会话规则。
- `CommandSpec` / `Signature` 已带可选 `returnType`，注册时校验 void/维度；运行时不校验 handler 返回值。
- 参考宿主已增加 `read`，CLI 注入 `std::cin` + `ConsoleSink`；默认无 I/O 构造返回 R010。
- `unit_completion`、`unit_reference` 已加入；`lush_tests` 共 219 个测试，CTest 仍为 5 个目标。
- CLI Tab 未接入，符合 M6 边界。

**文件**

- `src/lush/completion/completion.h/.cpp`
- `src/lush/command/spec.h`、`host.h`、`registry.h/.cpp`（`returnType` 与枚举支持）
- `src/reference/reference_host.h/.cpp`（`read` 与 I/O 注入）
- `src/cli/main.cpp`（构造 `ReferenceHost` 时注入 `std::cin` + `ConsoleSink`）
- `tests/unit_completion.cpp`
- `tests/unit_reference.cpp`（`read` 测试）
- `src/CMakeLists.txt`、`tests/CMakeLists.txt`

**任务**

- 定义 `CompletionRequest` / `CompletionResult` / `CompletionItem` 和上下文枚举。
- 用 `Lexer`、token 栈、`splitCommandWords` 和浅层表达式解析实现上下文判定；禁止另写字符扫描器。
- 增加 `Registry` 的可调用体枚举和 `BuiltinCatalog` 的补全签名描述。
- 实现关键字/变量/命令/函数/类型构造器/成员/字面量候选、别名、类型匹配、排序和替换 span。
- 实现 `for i in`、`[`、`(`、`f(`/`t(`、`.` 和会话参数上下文。
- 增加 `ReturnType { type, dim }`；`returns=void` 禁止声明；非 point 维度必须为 0；注册失败为宿主编程错误。
- 实现 `read` 的可选 prompt、CR 去除、空行、EOF/无效 UTF-8 R010，以及 ReferenceHost 默认无 I/O 的确定性行为。
- 放宽 `FunctionHandler` 的“纯函数”注释为“不接收 Output，但宿主函数可以执行显式 I/O”；不改变函数签名。
- 更新 CMake；新增测试文件必须手动加入目标。

**测试**

- 上下文与替换：`line `、`line 1,2 `、`line(`、`line 1,`、`f(`、`t(`、`[`、`(`、`.`、`for i in`、会话、块内语句。
- 光标边界：token 中间、空白、字符串/注释、词法错误、空输入。
- 候选与类型：`int`/`number`/`point`/`string`/`bool`/`list`/`any`；Point dim2/dim3；list → point 转换；类型不匹配降权。
- 匹配：关键字/命令/函数/类型大小写不敏感；变量大小写敏感；别名 `str`。
- 成员：Point dim2/dim3、list/string `length`、callable/未知无成员；`returnType` 已知/未知。
- 会话：`NeedInput` 的 prompt/type/dim、Point `(`、不提示可选参数。
- `read`：prompt、空行、CRLF、EOF、无效 UTF-8、默认无 I/O、注入流和 Output；不得使用真实 `std::cin`。
- spec §12 的补全用例。

**完成标准**

- spec §12 用例全绿；
- `unit_completion` 与 `read` 测试全绿；
- 不依赖 CLI Tab；CLI Tab 在 M9 接入；
- `/W4` 零警告；Debug / Release / Werror / Ninja 全绿；
- 测试数、CTest 目标数和文档同步更新。

---

## M7 — 数值字符串（计划）

**目标**：让 `read()` 等宿主输入得到的 string 能显式转成数字，同时不引入隐式转换。

**语义**

- `int(string)`：允许首尾 ASCII 空格/水平制表符/`\r`；只接受可选 `+`/`-` 加十进制数字；不接受小数点、指数、下划线、十六进制；越界 R005，格式非法 R013。
- `double(string)`：与 §2.6 的数值语法一致；不接受 `1.`、`.5`、`nan`、`inf`；越界/下溢/非有限 R013。
- 使用 locale 无关的 `std::from_chars`；不做隐式 string → number。
- 静态返回类型仍为 int / double。

**文件**

- `src/lush/core/builtins.cpp`（`callInt` / `callDouble` 和签名说明）
- `tests/unit_eval.cpp` / `tests/golden_spec.cpp`
- `docs/spec.md`、本文件、`AGENTS.md`、`README.md`

**测试**

- 空串、纯空白、`+42`、`-42`、前导零、`INT64_MIN/MAX`、越界；
- `"3.14"` 到 `double`、`"3.14"` 到 `int` 拒绝；
- `1e3`、`1e-3`、`1.`、`.5`、`1e`、`nan`、`inf`、前后空格、内部空格、locale 分隔符；
- `read()` 结果直接转数字的集成用例（库内测试不读真实 stdin）。

**完成标准**

- 不改变非 string 输入的既有语义；
- R005/R013 的位置符合 §11.3；
- Debug / Release / Werror / Ninja 全绿，测试数更新。

---

## M8a — 用户函数：语法、类型与静态结构（计划）

**目标**：完成 `def` / `return` 的语法、AST、类型注解和静态校验，不执行函数体。

**语法**

```lush
def f(a, b: string, c: point2) -> double { ... }
def f(x: int) -> int return x + 1
def f() { ... }
```

- 只允许顶层 `def`；嵌套/块内/函数内 `def` → S011。
- 参数注解省略 = `any`；返回注解省略 = `void`；`void` 只能作为返回类型。
- 支持类型名：`int`、`double`、`bool`、`string`、`list`、`point`、`point2`、`point3`、`any`、`callable`；`void` 仅返回。
- 参数不支持默认值、可选参数、变参；参数名必须唯一。
- 支持 block body 和单条 statement body。

**实现点**

- Lexer 增加 `Arrow` token，按最长匹配处理 `->`。
- AST 增加 `Def` / `Return`、参数声明、`TypeAnnotation`、函数体；AST dump 更新。
- Parser 增加 `def` / `return`，更新 `Incomplete` 判定（未闭合 body/括号）。
- 校验阶段收集全部顶层 `def`，建立函数签名表；支持前向引用、递归调用和同名冲突检测。
- 静态检查：
  - 函数名/参数名/类型注解合法性；
  - `return` 只能出现在函数体；
  - 值函数必须有 `->`，void 函数不得返回表达式；
  - 值函数的 `return expr` 只做 int → double 提升，其它返回类型不兼容；
  - 参数 `double` 接受 int 并转换；`int` 只接受 int；
  - `point2`/`point3` 不接受 list → point；必须显式 `point(...)`；
  - 用户函数名大小写不敏感，进入 common callable namespace；冲突按 S011/S003 处理。
- `S007` 不再产生；新增 S010/S011/S012 的诊断位置和测试。
- 顶层 `def` 收集后按顺序安装函数值；同一脚本重复定义同名函数 → S011；REPL 重定义允许替换（运行时 M8b 处理）。

**文件**

- `src/lush/core/token.h`、`lexer.h/.cpp`
- `src/lush/core/ast.h/.cpp`
- `src/lush/core/parser.h/.cpp`
- `src/lush/core/validate.h/.cpp`
- `src/lush/core/eval.h`（预声明接口）
- `tests/unit_lexer.cpp`、`unit_parser.cpp`、`unit_eval.cpp`、`golden_spec.cpp`
- `src/cli/main.cpp` 的 `--dump-ast` 输出
- 文档同步

**测试**

- `def` 两种 body、类型注解、`->`、参数列表、空参数；
- 非顶层 `def`、重复参数、未知类型、`void` 参数、重复函数、名称冲突；
- `return` 位置、值函数/void 函数的静态误用；
- `def` 跨行/未闭合 body 的 `Incomplete`；
- `--dump-ast` 覆盖 Def/Return。

**完成标准**

- 语法和静态诊断稳定；
- 尚未执行函数体时，相关测试可只验证解析/校验；
- `S007` 相关旧测试迁移到 S010/S011/S012；
- 不提前修改 REPL 行为；M8b 再接执行。

---

## M8b — 用户函数：运行时、Source 与 REPL（计划）

**目标**：让用户函数作为一等 callable 执行，支持 return、递归、函数值传递和 REPL 持久化。

**运行时设计**

- `UserFunction` 持有：
  - 函数体 AST 的共享所有权；
  - `std::shared_ptr<const core::Source>`（或等价 SourceHandle）；
  - `SourceId`（仅用于诊断/SourceTable 查询）；
  - 参数声明、返回类型、函数名。
- **不能只存 SourceId**：SourceId 不拥有 AST，也不保证 SourceTable 生命周期；函数值必须能独立于 `Program` 存活。
- 顶层 `def` 在程序执行前安装到 common callable namespace；函数体在调用时执行。
- 函数调用创建新的局部作用域；参数按注解绑定；函数体读取当前全局环境；v1 不支持闭包，不捕获定义点的局部变量。
- 函数体内赋值沿用 §6.1：找到最近可见绑定就写入，找不到才创建局部绑定。
- Eval 增加返回控制流：`return expr` / `return;` 穿过 block/if/while/for 到函数边界；`break`/`continue` 仍只作用于循环。
- 值函数自然结束未 return → R014；递归深度 >1024 → R015；`return` 在函数外 → S010。
- void 函数只能作语句调用；在表达式位置使用 → S012。
- 函数值可存入变量/list、作为参数/返回值；`type(f)`、`string(f)`、`==` 按现有 callable 规则。
- REPL 中定义成功后函数值跨 `feedLine` 保留；Source/AST 生命周期与 `Env` 一致。

**文件**

- `src/lush/core/value.h/.cpp`（UserFunction / callable 接口，按实现需要调整）
- `src/lush/core/eval.h/.cpp`（Flow::Return、函数作用域、递归计数）
- `src/lush/core/cell.h`（如需环境捕获接口）
- `src/lush/driver/driver.h/.cpp`（REPL 顶层 def 安装、Source 持久化）
- `src/lush/driver/source_table.*`（如需公开查找/共享 Source）
- `tests/unit_eval.cpp`、`unit_driver.cpp`、`unit_command.cpp`、`golden_spec.cpp`

**测试**

- 值函数、void 函数、裸 return、自然 void、自然结束报 R014；
- 参数类型/转换：double 收 int、int 拒 double、point2/3 拒 list；
- 递归/互递归/前向引用、1024 边界；
- 函数值存变量/list、作为参数、作为返回值；
- 函数内全局读取/写入、局部参数、无闭包行为；
- REPL 跨行 def、跨行调用、错误 Source/Span、Ctrl+C/EOF 行为不回归；
- 函数体中的命令、`echo`、`read`。

**完成标准**

- 函数定义不悬空；REPL 多次调用稳定；
- 错误码、Span、Source 正确；
- 现有 219 个测试不回归；
- Debug / Release / Werror / Ninja 全绿。

---

## M8c — 用户函数：补全与工具链（计划）

**目标**：让用户函数在补全、签名详情、返回类型推断和 REPL 诊断中完整可见。

**实现点**

- Callable 签名增加用户函数元数据：参数名、注解类型、必选性、返回类型（void / typed / unknown）。
- 补全：
  - 语句开头、表达式、命令参数、调用参数、会话参数识别用户函数；
  - 插入纯文本名字；语句位置不自动补 `(`；
  - 用户函数候选详情显示 `name(a: int, b: string) -> double`；
  - `f().` 使用返回值类型做成员补全；void 不提供成员/可迭代候选；
  - 参数类型过滤使用 §3.10 规则（point2/3 不接受 list）。
- `def` 后支持函数名/参数名/类型名/`->`/`{` 的上下文（至少类型名和 `def` 关键字）；
- `return` 在函数体语句开头出现；
- REPL/Source：函数定义和调用错误保留正确 SourceId/Span；
- 函数重定义替换后，补全签名同步更新。

**文件**

- `src/lush/completion/completion.h/.cpp`
- `src/lush/command/registry.*`（如需统一签名描述）
- `src/lush/core/value.*`（UserFunction 元数据）
- `tests/unit_completion.cpp`
- `docs/spec.md` §12、本文件

**测试**

- 用户函数在语句/表达式/参数/会话中可见；
- 参数类型过滤、void 排除、返回类型成员；
- def/类型名补全、`->` 和类型错误；
- 用户函数重定义、签名更新；
- 任意函数值（变量中）的签名展示或明确的 unknown 降级。

**完成标准**

- 用户函数与内建/命令的补全行为一致；
- CLI Tab 仍未接入，但 completion 已准备好；
- 核心补全仍不执行表达式/宿主。

---

## M9 — CLI 行编辑器与 Tab（计划）

**目标**：在 CLI 中使用 replxx 提供历史、编辑、Tab 补全和候选展示，同时不把第三方库引入 core。

**依赖**

- vendored `third_party/replxx`。
- 固定 commit：`1f149bfe20bf6e49c1afd4154eaf0032c8c2fda2`（master，2021-11-25；比 `release-0.0.4` 多 15 个提交，包含 Windows raw mode/print/bracketed paste 修复）。
- 许可：BSD；保留上游 LICENSE 与来源说明。
- 使用 CMake 选项控制，例如 `LUSH_WITH_REPLXX`；关闭时回退到现有 `std::getline`，不提供 Tab。
- replxx 目标作为第三方/SYSTEM 目标，避免第三方 warning 被 `/W4`/Werror 放大。
- 核心 `lush_core` 仍零第三方依赖。

**实现点**

- `cli/repl.cpp` 改用 replxx `input(prompt)`；保留非 TTY 回退。
- 行编辑器适配：
  - `Driver::pendingText()` 或等价只读接口提供完整 pending buffer；
  - 将 pending buffer + 当前 replxx 行拼成 `CompletionRequest.source`；
  - Lush byte cursor / `replaceSpan` ↔ replxx `contextLen`（Unicode code point）转换；
  - 候选 `insertText` 回填；Point 的 `(` 语法项正常插入；
  - 会话 `NeedInput` 时传入 type/dim/prompt；
  - `read` 执行期间不占用 replxx 行编辑。
- 行为：
  - Enter 提交；`NeedMore` 继续提示；
  - Tab 补全；无候选/多候选行为按 replxx 配置；
  - Up/Down 历史；历史只记录完整提交行；
  - Ctrl+C 映射到 Driver cancel/清行；EOF 退出；非 TTY 行为不变。
- 可选：使用 replxx highlighter 做 CLI 词法着色；不替代 M10 的宿主编辑器分析。

**文件**

- `third_party/replxx/`（vendor）
- `src/cli/repl.cpp`、`src/cli/main.cpp`
- `src/CMakeLists.txt`、`CMakePresets.json` / 顶层 CMake
- `src/lush/driver/driver.h/.cpp`（pending 文本接口）
- `README.md`、`AGENTS.md`

**测试**

- Windows 实际控制台：Tab、候选、历史、Ctrl+C、EOF、多行 pending、会话参数、中文/UTF-8 编辑；
- 非 TTY 脚本/管道路径不回归；
- 纯 C++ 适配层测试：byte ↔ codepoint contextLen、pending 拼接、替换 span；
- Linux 至少完成 CMake 配置和编译（如能运行 CTest 更好）。

**完成标准**

- Windows 手工验收通过；Linux 编译通过；
- 关闭 replxx 时 CLI 仍可构建运行（无 Tab）；
- 不改变脚本模式、Driver 语义和 `read` 行为；
- 第三方许可与版本记录完整。

---

## M10 — 编辑器分析接口与宿主 API（计划）

**目标**：为宿主编辑器提供稳定的、容错的词法 span，并明确宿主嵌入 API；不把 Qt 引入 core。

**编辑器分析**

- 新增 best-effort 词法结果，与严格 `LexResult` 分离：
  - comments/trivia 区间；
  - 完整 token、未完成字符串 token、非法字符/数字错误 span；
  - `Complete / Incomplete / Error` 状态；
  - 多条诊断，不在首个错误处停止；
  - 保证扫描前进，避免死循环。
- 严格执行路径继续首错中止；Parser/Validator 不消费 best-effort 结果。
- 补全中对 `#` 的启发式扫描改为消费 trivia（如可行）。
- UTF-8 字节 offset 保持为契约；宿主负责转换为 UTF-16/Qt 位置。
- 提供/文档化：
  - token/trivia span 接口；
  - Source 生命周期；
  - 错误 span；
  - `QSyntaxHighlighter` 适配示例（Qt 仅在 examples/宿主层）。
- 不做语义高亮（变量/命令分类）作为首版必要条件；token 词法高亮优先。

**宿主 API**

- 明确公开 include 与 CMake target：
  - `lush::core`、`lush::command`、`lush::driver`、`lush::completion`；
  - 可选 `lush::reference`、CLI。
- 新增 umbrella header 或明确推荐 include 组合（当前 `lush.h` 只有 core）。
- 文档化：
  - `Env`/`Registry`/`Output`/`Driver` 所有权与生命周期；
  - `runScript` / `feedLine` / `NeedMore` / `NeedInput` / `Error` / `Cancelled`；
  - SourceId/SourceTable/diagnostic；
  - completion 和 editor analysis；
  - `read` 输入注入；
  - 线程安全（单线程）和递归上限；
  - 错误不跨异常边界。
- 提供可编译 `examples/embed.cpp`；如果做 Qt 示例，放在 examples，不链接 core。

**文件**

- `src/lush/core/lexer.h/.cpp` 或新增 `src/lush/analysis/`
- `src/lush/lush.h` / 新 umbrella
- `examples/embed.cpp`
- `src/CMakeLists.txt`、`tests/CMakeLists.txt`
- `tests/unit_lexer.cpp` / 新 `unit_analysis.cpp`

**测试**

- 注释/trivia、行注释 EOF、未闭合字符串、未知转义、非法 UTF-8、非法数字、多错误继续扫描；
- best-effort 不改变严格 lexer 结果；
- UTF-8/UTF-16 映射示例（宿主侧可单独测试）；
- 宿主 API 示例编译；
- completion 对 trivia 的回归。

**完成标准**

- 宿主能仅凭公开 API 获得 token/trivia/diagnostic 并渲染；
- core 仍无 Qt/第三方依赖；
- Qt 示例可独立构建或明确标为可选文档示例。

---

## M11 — 用户文档与示例（计划）

**目标**：交付两份用户文档，并与实际行为、测试和示例同步。

**1. `docs/language.md`**

- Lush 是什么、与宿主命令行的关系；
- 词法、类型、值语义、Point/list、ref；
- 命令 vs 函数、语句/表达式调用；
- 用户函数：类型注解、void/return、顶层/hoist、递归、无闭包；
- `int`/`double` 字符串转换与错误；
- 会话/REPL、Incomplete、Ctrl+C；
- 诊断与常见错误；
- 完整可运行示例。

**2. `docs/embedding.md`**

- 构建/链接/版本；
- `Env`/`Registry`/`Output`/`Driver` 生命周期；
- 命令和函数注册，`read` 注入；
- 脚本/REPL/会话状态机；
- completion 接入（byte cursor、replaceSpan、replxx 适配）；
- 编辑器词法分析、trivia、未完成输入、错误 span；
- UTF-8 ↔ Qt UTF-16 映射与 QSyntaxHighlighter 示例；
- SourceId/SourceTable、诊断归属；
- 线程、递归上限、错误边界；
- 最小可编译示例。

**文件**

- `docs/language.md`
- `docs/embedding.md`
- `examples/embed.cpp`（M10 产物）
- `README.md`、`AGENTS.md` 文档入口
- 文档示例的构建/测试

**完成标准**

- 两份文档均标注适用于 0.4.0；
- 文档中的代码示例可编译/可运行；
- 不把未实现功能写成已实现；
- README/AGENTS/spec 版本和测试数一致。

---

## 跨里程碑约束

- 依赖方向：`cli → driver → command → core`；`completion → core + command`；`reference → command`。反向依赖不允许。
- `core` 只依赖标准库，不引入 Qt 或第三方库。
- `replxx` 是 CLI 可选依赖，必须 vendored/pinned；不得被 core/command/driver/completion 反向依赖。关闭依赖时 CLI 仍要能构建。
- 每个语义变更先改 `docs/spec.md`，再补 golden/单元测试，最后改代码。
- 每个 bug 修复补回归测试；每个错误码至少一个用例。
- 每个里程碑结束都要：`/W4` 零警告、相关测试全绿、更新 `README.md` / `AGENTS.md` 的进度。
- 不要提前实现后续里程碑的模块；M8a 不执行函数，M8b 不接 CLI Tab，M9 不重写编辑器分析协议。
- 函数值必须拥有 AST 和 Source；SourceId 只做诊断/查询，禁止用裸 SourceId 指向已销毁的 AST/SourceTable。
- 测试基线：M6 结束 219 个测试；每个里程碑结束时更新实际数量，不固定提前写死。
- 建议验收顺序：M7 → M8a → M8b → M8c → M9 → M10 → M11；需要并行时只能并行文档整理和纯测试设计，不能跨语义门禁合并实现。
