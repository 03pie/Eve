---
description: "Use when adding, editing, or reviewing first-party code comments; use Chinese Doxygen format for C++ files and the applicable language documentation format elsewhere."
applyTo:
  - "src/**/*.h"
  - "src/**/*.hpp"
  - "src/**/*.cpp"
  - "src/**/*.mm"
  - "sdk/**/*.h"
  - "sdk/**/*.hpp"
  - "sdk/**/*.cpp"
  - "sdk/**/*.mm"
  - "examples/**/*.h"
  - "examples/**/*.hpp"
  - "examples/**/*.cpp"
  - "examples/**/*.mm"
  - "examples/**/*.ts"
  - "examples/**/*.js"
  - "tests/**/*.h"
  - "tests/**/*.hpp"
  - "tests/**/*.cpp"
  - "tests/**/*.mm"
  - "tests/**/*.ts"
  - "tests/**/*.js"
  - "tools/**/*.ps1"
---

# 中文代码注释规范

- 新增或修改注释时使用中文；保留代码标识符、标准名称和必要的英文技术术语。
- 每个第一方源文件都要有文件说明；每个类/结构体及每个具名函数/方法（包括私有成员和测试函数）都要有对应说明。匿名 lambda 无需单独添加注释。
- C++ 文件、类/结构体和具名函数/方法的文档说明统一使用 Doxygen 格式：文件使用 `@file`，说明使用 `/** ... */` 并以 `@brief` 概述；函数按需使用 `@tparam`、`@param`、`@return`、`@throws` 描述模板、参数、返回值和异常约定。
- Doxygen 注释放在所说明的声明之前；在头文件说明公开 API 的契约，在实现文件说明文件职责及未在声明处体现的实现细节，避免重复堆叠相同文档。
- TypeScript/JavaScript 按 Google 风格使用 JSDoc；PowerShell 为脚本和具名函数提供注释帮助块，说明用途、参数和输出。
- 注释优先说明接口契约、参数约束、所有权、线程要求、状态不变量、边界条件，以及不明显的算法或设计取舍。
- 注释应简洁、准确且与实现同步；按要求为每个文件、类型和具名函数提供说明，但不复述逐行实现或为匿名 lambda 添加冗余注释。
- 不为没有额外语义的字段和局部变量生成注释；Doxygen 标签仅用于澄清相关契约，不添加无实际内容的标签。
- 不修改第三方、vendor、生成、构建产物或只读参考文件；本项目的 `research/` 与 `sources/` 均不在注释范围内。
