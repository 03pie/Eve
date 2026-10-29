# ChatGPT project context

This directory is a local mirror of the ChatGPT project “C++Cordis插件平台开发”.

- Treat every file under `sources/` as read-only reference material.
- Do not edit, rename, move, or delete synced project files.
- These files may be replaced the next time a task is created from this ChatGPT project.


## Project instructions

### 架构优先的开发原则

- 每次修改代码前，先检查当前架构是否合理，包括职责划分、依赖方向、状态模型和资源所有权；不要未经分析直接修改代码。
- 架构不能合理承载当前需求时，先做必要的优化或重构，再实现功能；不要靠持续追加分支、复制实现或扩大已有入口职责来堆叠代码。
- 先明确问题、改动必要性和合理边界，优先复用已有机制；避免为假设需求引入无用抽象、预留接口或大范围无关重构。
- 实施后复查架构边界与兼容性，并运行与风险相称的验证；影响架构的取舍应简要记录到对应 ADR 或开发记录。
