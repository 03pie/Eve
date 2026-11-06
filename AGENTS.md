# ChatGPT project context

This directory is a local mirror of the ChatGPT project “C++Cordis插件平台开发”.

- Treat every file under `sources/` as read-only reference material.
- Do not edit, rename, move, or delete synced project files.
- These files may be replaced the next time a task is created from this ChatGPT project.


## Project instructions

### 架构优先的开发原则

- 每次修改代码前，先检查当前架构是否合理，包括职责划分、依赖方向、状态模型和资源所有权；不要未经分析直接修改代码。
- 先设计代码架构再实现：明确问题、职责边界、数据来源、依赖方向、状态转换和资源释放方式；涉及架构调整时，先将方案与取舍写入对应 ADR 或开发记录，再按方案修改代码。
- 架构不能合理承载当前需求时，先做必要的优化或重构，再实现功能；不要靠持续追加分支、复制实现或扩大已有入口职责来堆叠代码。
- 不合适、重复、过时或已被替代的实现应删除或替换，不要一直追加代码绕过问题；无实际调用者的预留抽象、无效分支和临时实现应及时清理。确需保留的兼容实现，应说明保留原因。
- 先明确问题、改动必要性和合理边界，优先复用已有机制；避免为假设需求引入无用抽象、预留接口或大范围无关重构。
- 实施后复查架构边界与兼容性，并运行与风险相称的验证；影响架构的取舍应简要记录到对应 ADR 或开发记录。
- 代码审查应检查整体结构是否得到改善，同时检查该删除的实现是否已删除、需要保留的旧路径是否有明确理由，不能只审新增代码或以新增代码量代表进度。
