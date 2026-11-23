# Native 业务名单与重新资格

本流程对应[ADR-0056](adr/0056-qualified-native-product-catalog.md)及[ADR-0059](adr/0059-fresh-qualification-and-offline-recovery.md)。当前产品业务工作台只消费固定受审源 `qualification/native-business-p6-3-20261123.json`，CMake同时校验明确采用的SHA `44758309aede7a215874d4d8307fd3421e20da2befbdd5f288516de5b7382246`，每个匹配构建只生成自己的四条Native批准，不读取用户批准文件。原 `qualification/native-business.json`（6c）保留为P5历史基线。普通ModuleManager默认名单为空，Qt始终驻留。资格不替代后续产品、完整矩阵和准确运行包验收；本轮状态见[P6工作单](p6-development.md)，原P5证据范围见[M5-e工作单](p5-m5e-development.md)。

## 运行产品

已有匹配资格的Release构建后，在普通PowerShell运行：

```powershell
./tools/desktop.ps1 -Action run -Preset ela-msvc-release -Workbench business
```

使用独立文件及存储目录时，同时传ConfigurationFile和StoreRoot：

```powershell
./tools/desktop.ps1 -Action run -Preset ela-msvc-release -Workbench business -ConfigurationFile "$PWD/build/manual-business/session.yaml" -StoreRoot "$PWD/build/manual-business/plugins"
```

如Qt kit不在默认位置，追加-QtRoot指向实际Qt5.15.2 MSVC x64目录。默认Workbench=configuration仍打开原配置工作台；它不用于本次三模块升级验收。

业务工作台固定装配Counter、Reporter和Notebook；模块窗口可选择已有产物升级。Home开关是配置启停，模块Applied与磁盘保存分别操作。Native四份A/B按完整SHA批准，任何字节变化都需重新资格；版本、路径、相同源码都不能代替匹配。缺profile或SHA不符时原explicit门禁返回ModulePinned，不自动批准或降为resident。

## 新构建准备

使用Visual Studio 2022 x64 Developer PowerShell、CMake3.21+、Ninja及Qt5.15.2 MSVC x64 kit。资格工具需要Python3.9+；Windows可用py -3，也可指定实际解释器。

```powershell
$env:CORDIS_QT5152_ROOT = 'C:/Qt/5.15.2/msvc2019_64' # 替换为实际kit
$qualificationRoot = "$PWD/build/native-qualification-new" # 必须选择不存在的新目录
if (Test-Path -LiteralPath $qualificationRoot) { throw 'Use a new qualification campaign directory.' }
cmake --preset ela-msvc-release -B "$qualificationRoot/ela-msvc-release" -DCORDIS_SHARED_CORE=ON -DCORDIS_BUILD_EXAMPLES=ON -DBUILD_TESTING=ON
cmake --preset ela-msvc-debug -B "$qualificationRoot/ela-msvc-debug" -DCORDIS_SHARED_CORE=ON -DCORDIS_BUILD_EXAMPLES=ON -DBUILD_TESTING=ON
cmake -S . -B "$qualificationRoot/p3-q3-asan" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCORDIS_REQUIRE_QT5152=ON -DCORDIS_BUILD_ELA_UI=ON -DCORDIS_SHARED_CORE=ON "-DCMAKE_PREFIX_PATH=$env:CORDIS_QT5152_ROOT" -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL '-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /GR /EHsc /fsanitize=address /Zi' '-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS /fsanitize=address /Zi' '-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=/O1 /Zi /DNDEBUG' '-DCMAKE_C_FLAGS_RELWITHDEBINFO=/O1 /Zi /DNDEBUG'
cmake --build "$qualificationRoot/ela-msvc-release" --parallel 2
cmake --build "$qualificationRoot/ela-msvc-debug" --parallel 2
cmake --build "$qualificationRoot/p3-q3-asan" --parallel 2
$env:PATH = "$env:CORDIS_QT5152_ROOT/bin;" + $env:PATH
```

新campaign的三个profile目录归同一资格根；不得覆盖已冻结的P6-3构建树或把旧cache冒称新证据。普通构建消费受审名单，不会更新它；新生成DLL因链接元数据等变化不匹配时，产品测试/入口可能拒绝，原固定资格入口仍可验证新字节。先完成下面的显式资格，再审核新候选，不要直接改SHA绕过。

## 捕获、导出与审核

在同一开发环境执行；run目录必须是新的，工具不编译或链接业务目标、不改批准源：

```powershell
py -3 tools/export_native_qualification.py --qualify --build-root "$qualificationRoot" --run-dir "$qualificationRoot/capture"
py -3 tools/export_native_qualification.py --evidence "$qualificationRoot/capture/qualification.json" --output "$qualificationRoot/native-business-candidate.json"
```

--ctest可指定CTest绝对路径。工具按三个配置串行运行固定27项原资格：9实际故障、12平台替换、同进程8+100及5项seed/回放/无效/缩减，排除依赖产品名单的product测试。显式资格可刷新原CMake/Ninja元数据，随后由Ninja检查原依赖图；任何业务目标需要编译或链接时都拒绝，需先自行构建。它检查实际命令/退出/原事件、产物和来源稳定性，将固定旧输出与本轮证据保存到独立bundle；失败也保留原现场，不可导出成功候选。

导出仅接受当前capture格式，并核对原归档引用、SHA及当前待打包十二份DLL/manifest。candidate不是批准，工具拒绝覆写原6c及当前固定新受审JSON两个路径。审核需确认每profile仅Counter/Reporter A/B、toolchain/CRT/模式/ASan、原Native生命周期契约及完整资格事实，不混入Qt、resident 1.0、故障产物、其他配置或十二项并集。采证开始至候选导出、独立重导审查结束期间，完整source_inputs和被测字节必须冻结。

候选经明确审查采用后，代码固定新受信路径及SHA，再重新配置、只构建名单消费者并运行产品及相关回归，逐项核生产模块和依赖不变，打包同一批已测字节。名单生成不参与业务DLL链接；四项不变时批准行保持，来源注释仍可能变化，须核实际消费者产物。最终发布档案另行绑定名单SHA、实际产品exe、最终五配置矩阵和新增人工回执，不通过回填报告反复改业务字节。

## 本轮已审范围

P6-3已独立完成三profile各27项新资格、固定447采用后的各6项产品验证，以及五配置完整5904项；准确preview.3包为41cd（138文件）。这三层测试与实包、GUI各有原件，不合并计算。完整960/961源码快照与旧19份报告的缺失索引分别保存；旧原件无备份、未恢复，新采证不替代旧报告，也不允许CI自动采用新候选。离线更新/回退、旧版两入口重开及最终closed均经post独审通过并由根采用，D12/D13技术验收通过，准确原件见[P6当前工作](p6-development.md)。P6-3已以56c47c1提交同步并核clean，独立总包68b和关闭回执be8e已保存。P6-4进行中，P6-5未实施，最终P6交付未关闭。
