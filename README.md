# Lush

一个面向「命令行 + GUI 深度融合」的嵌入式命令语言与命令执行框架。

- 语言规范：[docs/spec.md](docs/spec.md)
- 开发里程碑：[docs/steps.md](docs/steps.md)
- 当前状态：**M5 已完成，M6 设计已定，代码未开始**（M1 词法 + M2 AST/Parser/`--dump-ast` + M3 值/环境/求值/内建 + M4 命令注册/参数绑定/参考宿主 + M5 会话/REPL/脚本执行；补全尚未实现）

## 环境

- CMake 3.25+
- MSVC 19.44+（Visual Studio 2022 Build Tools）
- 可选：Ninja 1.13+

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
- `src/lush/driver/`：脚本 / REPL 执行状态机、SourceTable（已实现）
- `src/lush/completion/`：补全（M6 设计已定，代码未实现）
- `src/reference/`：参考宿主 `line` / `circle` / `move`（已实现；M5 CLI 演示宿主；`read` 在 M6 设计）
- `src/lush/`：公共头
- `src/cli/`：命令行前端（REPL、脚本、`-e`、dump）
- `tests/`：单元测试（M3 非命令 golden、M4 命令 golden、M5 驱动/会话测试已落地；M6 补全/`read` 测试待实现）
