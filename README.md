# Lush

一个面向「命令行 + GUI 深度融合」的嵌入式命令语言与命令执行框架。

- 语言规范：[docs/spec.md](docs/spec.md)
- 当前状态：**M0**（CMake 骨架 + 版本 + CTest；语言核心尚未实现）

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
```

## 目录

- `src/lush/core/`：语言核心（词法、语法、值、求值）
- `src/lush/command/`：命令注册、参数绑定、会话
- `src/lush/driver/`：脚本 / REPL 执行状态机
- `src/lush/completion/`：补全
- `src/lush/`：公共头
- `src/cli/`：命令行前端
- `tests/`：单元测试与 golden tests
