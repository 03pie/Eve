# 原生业务服务示例

M3d2a 提供两个独立 Windows x64 DLL，只依赖业务 C ABI 头，不链接 Core 或 Qt，也没有测试专用导出。计数器提供 example.counter/v1 的 next 方法（输入 {}，输出 {"value":N}）；报告器在启动时异步读取计数器，提供 example.report/v1 的 read 方法，并在清理时通过旧绑定再读取一次。每个 DLL 最多 32 个实例，业务在 owner 线程执行；返回值和缓冲区不跨调用借用。

正常配置为 [pipeline.yaml](pipeline.yaml)。构建在链接 DLL 后生成 module.json，以该配置实际产物的 SHA256 填入声明；Debug、Release、ASan 各自生成，不能混用声明。默认驻留策略不变。

在 Qt 5.15.2/MSVC x64 开发终端中运行（Qt bin 须在 PATH）：

```powershell
cmake --build build/ela-msvc-release --target cordis_native_services_demo --parallel 4
./build/ela-msvc-release/bin/cordis_native_services_demo.exe
# 同一计数器、两个不同报告器：启动分别读到 1 和 2
./build/ela-msvc-release/bin/cordis_native_services_demo.exe "$PWD/build/ela-msvc-release/bin/native-services" multi
# 两组不同 realm：分别从 1 开始
./build/ela-msvc-release/bin/cordis_native_services_demo.exe "$PWD/build/ela-msvc-release/bin/native-services" realms
```

入口使用真实 QCoreApplication/QtScheduler、ModuleManager、Core Runtime、KernelReconcileDriver、ConfigurationSession 和 QtConfigurationWriter。保存到独立临时文件，重新读取并核对持久化内容，退出后清理该文件。成功时依次关闭 Session、Kernel、模块会话，排空代次，再关闭调度器，打印 DLL 仍驻留；不会打开桌面窗口或修改默认工作台配置。

另有 start_failure 和 cleanup_failure 两个显式故障演示。前者用报告器配置 {"failStart":true} 验证失败回滚后仍能完整退出；后者用计数器配置 {"failCleanup":true} 验证不静止报告、DLL 和执行引用保留。故障场景返回 0 表示它符合预期的验收结果，不能解释为清理成功。这两个参数仅为本示例 schema 的故障演示能力。

五项检查在关闭 CORDIS_BUILD_EXAMPLES 时仍由 cordis_native_services_probe 执行。业务链、真实磁盘保存、多实例/realm 与上述故障已覆盖；report/read 方法由示例提供，本轮联合检查观察启动业务日志，未单独发起该方法调用。S06 取消后的晚到 stop completion 与 S07 DLL 内捕获析构已由 [M3d2b 专用夹具分层验证](../../docs/p4-m3d2b-development.md)，没有向本示例加入测试控制导出；M4 UI DLL 另行实施，见 [阶段记录](../../docs/p4-m3d2a-development.md)。
