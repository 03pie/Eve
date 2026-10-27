# ADR-0027：原生 Qt 信号的显式值前缀

日期：2026-10-27（北京时间）。状态：已实施，限源码 SDK 与内置插件。

## 背景

Qt 5.15.2 的 QFileSystemWatcher::fileChanged/directoryChanged 在 QString 后带有 QPrivateSignal。完整参数冻结会按既有白名单拒绝这个私有类型，因此不能直接用 observeValues 接入原生监听。[固定版本声明](https://github.com/qt/qtbase/blob/v5.15.2/src/corelib/io/qfilesystemwatcher.h)。

## 决定

增加 QtConnection::observePrefixValues<Values...>。调用方显式选择非空、连续的前 N 个参数；去除 cv/ref 后，选择类型必须与信号成员指针中的对应类型完全一致。错误类型、过长及空前缀在编译期拒绝。QObject* 等选中数据仍受既有白名单拒绝；不能借助 Qt 可接受的隐式转换将自定义数据或指针转成允许类型。

完整 observeValues 和前缀入口复用同一个注册实现。前缀桥只接收选中数据，不访问、冻结或按名称判断尾参数。预算只计算托管的选中数据，Qt 及发射方自身构造参数的开销不属于该预算。仅需触发时继续使用零参数 observe。

原生线程先保留票据、冻结数据，owner 再按 Context/epoch 门禁执行。数据保留至业务栈及返回 Task 完成，取消、预算、释放批次、原生快照和关闭屏障全部沿用 [ADR-0026](0026-typed-qt-connections.md)，没有增加独立的生命周期分支。

## 验证与范围

八项场景覆盖 raw-data 深拷贝/忽略自定义尾参数、多项前缀的字节预算与异步寿命、真实 QObject 名称变化、真实文件及目录监听、注册门禁/停止重入、旧代撤销和 const 信号。headless 示例联合关闭原生 watcher、连接、时钟及 Kernel。

两编译器检查原生 watcher 的正向实例化，以及类型不符、空前缀、过长、指针、隐式转换、完整私有标记六种拒绝路径。完整私有标记拒绝是 observeValues 保持原契约的证据，不将其自动降级为前缀。

本接口不实现通用 Qt 元类型转换、文件监听重连或二进制模块驻留。完整矩阵及日志见 [开发记录](../p3-development.md#q2g-原生-qt-信号前缀与验收映射)，P3 场景范围见 [验收映射](../p3-acceptance.md)。
