# Lush

一个面向「命令行 + GUI 深度融合」的嵌入式命令语言与命令执行框架。

- 语言规范：[docs/spec.md](docs/spec.md)（0.4.0；M7 已实现，M8–M11 规划）
- 开发里程碑：[docs/steps.md](docs/steps.md)（M1–M7 已完成，M8–M11 计划）
- 当前状态：**M7 已完成，M8–M11 已规划**（M1 词法 + M2 AST/Parser + M3 求值/内建 + M4 命令系统/参考宿主 + M5 会话/REPL/脚本执行 + M6 补全核心与 `read` + M7 数值字符串；M8 计划引入 `Env` 函数表/Source 表、`TypeToken`、用户函数和 `CompletionScope`；CLI Tab、编辑器分析和两份用户文档见 `docs/steps.md`）

## 环境

- CMake 3.25+
- MSVC 19.44+（Visual Studio 2022 Build Tools）
- 可选：Ninja 1.13+
- M9 计划：vendored replxx（BSD，CLI 可选依赖；core 仍零第三方依赖）

## 构建

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug
ctest --preset vs2022-debug
```

或不用 preset：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

## 运行

```powershell
.\build\vs2022\bin\Debug\lush.exe
.\build\vs2022\bin\Debug\lush.exe .\sample.lush
.\build\vs2022\bin\Debug\lush.exe -e "echo 1"
.\build\vs2022\bin\Debug\lush.exe --version
.\build\vs2022\bin\Debug\lush.exe --help
.\build\vs2022\bin\Debug\lush.exe --dump-tokens -e "line 0,0 1,1"
.\build\vs2022\bin\Debug\lush.exe --dump-tokens .\sample.lush
.\build\vs2022\bin\Debug\lush.exe --dump-ast -e "line 0,0 1,1"
.\build\vs2022\bin\Debug\lush.exe --dump-ast .\sample.lush
```

## 目录

- `src/lush/core/`：语言核心（已实现 UTF-8、Source、Diagnostic、Token、Lexer、AST、Parser、Value、Cell/Env、Eval、Validate、格式化和内建）
- `src/lush/command/`：命令注册、参数绑定、宿主、交互会话
- `src/lush/driver/`：脚本 / REPL 执行状态机、SourceTable（当前；M8b 上移 Env）
- `src/lush/completion/`：补全核心（已实现；M8c 加 CompletionScope，M9 接 CLI Tab）
- `src/reference/`：参考宿主 `line` / `circle` / `move` / `read`（已实现；CLI 演示宿主）
- `src/lush/`：公共头
- `src/cli/`：命令行前端（REPL、脚本、`-e`、dump；M9 接 replxx）
- `third_party/replxx/`：M9 计划中的 vendored CLI 行编辑依赖
- `examples/`：M10/M11 的宿主和文档示例
- `tests/`：单元测试（M3–M7 golden/单元测试已落地；`lush_tests` 223 个测试）
- `docs/language.md`、`docs/embedding.md`：M11 计划交付的两份用户文档
