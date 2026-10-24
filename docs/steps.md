# Lush 开发里程碑（M1–M6）

本文是开发路线图，说明每个里程碑的目标、文件、任务、测试和完成标准。语言语义仍以 `docs/spec.md` 为唯一来源；AGENTS.md 里的构建/测试约定同样适用。

## 当前基线

| 里程碑 | 状态 | 说明 |
|---|---|---|
| M0 构建骨架 | ✅ 已完成 | CMake presets、CLI 占位、测试 harness、CTest |
| M1 词法 | ✅ 已完成 | UTF-8、Source、Diagnostic、Token、Lexer、`--dump-tokens` |
| M2 语法 | ✅ 已完成 | AST、Parser、命令参数切词、`Incomplete`、`--dump-ast`；`unit_parser` 全绿 |
| M3 求值 | ✅ 已完成 | Value、Cell/Env、Eval、Validate、格式化、内建、非命令 golden |
| M4 命令 | ⬜ 下一步（设计已定） | 命令 spec/registry、参考宿主、命令 golden；设计约定见下文 |
| M5 会话 / REPL | ⬜ 未开始 | Session、Driver、脚本/REPL CLI |
| M6 补全 | ⬜ 未开始 | 补全上下文与候选 API |

M1、M2、M3 已完成：`lush_tests` 当前 142 个测试，CTest 目标为 `lush_tests`、`lush_version`、`lush_dump_tokens`、`lush_dump_ast`。下一步是 M4（命令注册与参考宿主）；M4 的语义与 API 约定已定，代码尚未开始。

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
- 测试接口：不新增 CLI 执行入口；`golden_spec` 直接链接 `lush_core`，组合 `Lexer` → `parse` → `validate` → `evalProgram`，用 `StringOutput` 捕获输出与诊断。`lush file.lush` / `lush -e` 留 M5。
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
- M3 不新增 CLI 执行入口；`lush file.lush` / `lush -e` 留 M5；
- `echo` 是 core 内建命令，但完整命令系统在 M4；
- M3 的 `Command` 只支持 `echo`；语句位置的内建函数调用（`int 1`）留 M4。

---

## M4 — 命令注册与参考宿主

**目标**：实现声明式命令/函数注册、参数绑定和参考宿主，让命令 golden 跑通。

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
  - `CommandSpec` 注册时校验成 `Signature`（校验后的参数表 + `required`/`maxFixed`/`variadic`/`interactive`/`returnsValue`）；
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
  - handler 接收绑定后的 `vector<Value>` + `CommandContext` + `Output&`；
  - `CommandResult { optional<Value> value; optional<HostError> error; }`；`HostError { message; optional<Span> span; }`；
  - 宿主失败统一 R010；默认 Span 为语句位置命令名、表达式位置 callee，`HostError.span` 可覆盖；error 优先于 value，void/value 返回值不匹配是宿主编程错误；
  - 宿主 value 函数做最小实现：`returns=value` 的 `CommandSpec` + handler，表达式位置和语句位置都可用。
- 构建：
  - 新增 `lush_command`、`lush_reference` 两个 static library；
  - `lush_tests` 链接两者；CLI 仍只链接 core，M5 再接 driver。

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
- handler 使用 `core::Output`；`CommandResult` / `HostError`；宿主失败 R010。
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
- 脚本模式不进入会话；交互命令缺参报 S006，非交互命令缺参报 S002；参数过多一律 S002。
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
- 待定边界：按 §7.4，`required = 0` 的 interactive 命令不会进入会话，因此只有可选参数的 interactive 命令永远不会提示可选参数；M5 再确认是否调整。

**测试**

- G22；
- 多行块；
- `if (true)` / `for (;;)` 后 EOF → `Incomplete(NeedStatement)`；
- 空白行取消 `NeedStatement` / `TrailingContinuation`；
- 未闭合块内空白行继续等待；
- 注释行不取消待完成输入；
- `if (a);` / `while (a);` / `for (;;);` 空 body；
- 取消；
- 脚本 vs REPL 的 S006/S002 差异；
- 会话中输入 `0,0`；
- `unit_driver` 全绿。

**完成标准**

- CLI 可用；
- G22 通过；
- 脚本模式交互命令缺参报 S006，非交互命令缺参报 S002；
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
