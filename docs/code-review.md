# W2/W3 提交前审核

日期：2026-10-23。范围：全部未提交的 W2 收尾、W3 Kernel/实例实现、SDK、测试及文档。

## 已修复问题

1. 移动后的 Runtime 保留原编号，但激活函数已为空。Kernel 仅核对编号会接受该句柄，随后访问空回调。现在移动后原句柄 id() 返回无效编号，Kernel 拒绝挂载；直接 Instance::mount 在登记资源前抛 invalid_argument。新增回归在修复前实际失败，日志为 `build/review-before-fix.log`。
2. 调度器关闭后 update/retry 可能先替换目标、分配操作，再投递失败或构造 EffectScope 抛错。现在在修改账目前返回 SchedulerClosed。回归同时检查 Active 更新和 Failed 重试被拒绝，操作编号、配置修订和状态保持原值。
3. 本机中文 MSVC 的 /showIncludes 前缀被 CMake 错误解码，Ninja 未记录头文件依赖。Release 增量构建因此混用旧 Kernel 目标和新 SDK。新增可选 CMake 缓存项 CORDIS_MSVC_SHOWINCLUDES_PREFIX，默认仍自动检测；本机使用 `注意: 包含文件:`。清洁构建后已核对依赖表，触碰 instance.hpp 会触发 instance.cpp、kernel.cpp 与 w3.cpp 重编译。

## 验证与边界

GCC 13.1、MSVC 19.44 x64 的 Debug/Release 各 61/61 通过。W3 共十组行为，共用一个 CTest 入口；新增测试不会增加 CTest 入口数量。日志为 `build/review-core-<compiler>-<mode>.log`；MSVC 清洁构建日志为 `build/review-build-core-msvc-<mode>.log`。`git diff --check` 通过。

本机 MSVC 配置需使用 W2 记录中的开发者环境及 TEMP/TMP 设置，再执行：

```powershell
cmake --preset core-msvc-debug '-DCORDIS_MSVC_SHOWINCLUDES_PREFIX=注意: 包含文件:'
cmake --preset core-msvc-release '-DCORDIS_MSVC_SHOWINCLUDES_PREFIX=注意: 包含文件:'
```

修复旧依赖数据库后需使用 `cmake --build --preset <preset> --clean-first` 重建一次。不要只修前缀而继续使用缺失依赖记录的旧目标。此覆盖参数仅在检测错误时使用，值应与实际编译器输出匹配。

未发现其他阻止本次增量提交的问题。W3 仍未完成独立 CleanupContext，服务/realm 未实现；历史操作未裁剪，非合作清理及投递阻断会保守保留资源。上述已知边界见 W3 记录；未运行 ASan/TSan、Qt 或 DLL 验证。
