# Lush

一个面向「命令行 + GUI 深度融合」的嵌入式命令语言与命令执行框架。

- 语言规范：[docs/spec.md](docs/spec.md)
- 当前状态：**M1**（UTF-8 / Source / Diagnostic / Token / Lexer + `--dump-tokens`；语法、求值、命令、REPL 尚未实现）

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
.\build\vs2022\bin\Debug\lush.exe --version
.\build\vs2022\bin\Debug\lush.exe --help
.\build\vs2022\bin\Debug\lush.exe --dump-tokens -e "line 0,0 1,1"
.\build\vs2022\bin\Debug\lush.exe --dump-tokens .\sample.lush
```

## 目录

- `src/lush/core/`：语言核心（已实现 UTF-8、Source、Diagnostic、Token、Lexer；语法/求值待实现）
- `src/lush/command/`：命令注册、参数绑定、会话（计划）
- `src/lush/driver/`：脚本 / REPL 执行状态机（计划）
- `src/lush/completion/`：补全（计划）
- `src/lush/`：公共头
- `src/cli/`：命令行前端
- `tests/`：单元测试（golden tests 待后续里程碑实现）
