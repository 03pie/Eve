# Native 业务名单与重新资格

本流程对应[ADR-0056](adr/0056-qualified-native-product-catalog.md)。产品业务工作台使用qualification/native-business.json在本构建生成的四条Native批准，不读取用户批准文件。普通ModuleManager默认名单为空，Qt始终驻留。名单只是精确产物的Native资格，最终P5矩阵和新增桌面验收见[M5-e工作单](p5-m5e-development.md)。

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
cmake --preset ela-msvc-release
cmake --preset ela-msvc-debug
cmake -S . -B build/p3-q3-asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCORDIS_REQUIRE_QT5152=ON -DCORDIS_BUILD_ELA_UI=ON -DCORDIS_SHARED_CORE=ON "-DCMAKE_PREFIX_PATH=$env:CORDIS_QT5152_ROOT" -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL '-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /GR /EHsc /fsanitize=address /Zi' '-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS /fsanitize=address /Zi' '-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=/O1 /Zi /DNDEBUG' '-DCMAKE_C_FLAGS_RELWITHDEBINFO=/O1 /Zi /DNDEBUG'
cmake --build build/ela-msvc-release --parallel 2
cmake --build build/ela-msvc-debug --parallel 2
cmake --build build/p3-q3-asan --parallel 2
$env:PATH = "$env:CORDIS_QT5152_ROOT/bin;" + $env:PATH
```

这些命令复用已有三个目录。普通构建消费受审名单，不会更新它；新生成DLL因链接元数据等变化不匹配时，产品测试/入口可能拒绝，原固定资格入口仍可验证新字节。先完成下面的显式资格，再审核新候选，不要直接改SHA绕过。

## 捕获、导出与审核

在同一开发环境执行；run目录必须是新的，工具不编译或链接业务目标、不改批准源：

```powershell
py -3 tools/export_native_qualification.py --qualify --build-root build --run-dir build/native-qualification-run
py -3 tools/export_native_qualification.py --evidence build/native-qualification-run/qualification.json --output build/native-business-candidate.json
```

--ctest可指定CTest绝对路径。工具按三个配置串行运行固定27项原资格：9实际故障、12平台替换、同进程8+100及5项seed/回放/无效/缩减，排除依赖产品名单的product测试。显式资格可刷新原CMake/Ninja元数据，随后由Ninja检查原依赖图；任何业务目标需要编译或链接时都拒绝，需先自行构建。它检查实际命令/退出/原事件、产物和来源稳定性，将固定旧输出与本轮证据保存到独立bundle；失败也保留原现场，不可导出成功候选。

导出仅接受当前capture格式，并核对原归档引用、SHA及当前待打包十二份DLL/manifest。candidate不是批准，工具拒绝覆写qualification/native-business.json。审核需确认每profile仅Counter/Reporter A/B、toolchain/CRT/模式/ASan、原Native生命周期契约及完整资格事实，不混入Qt、resident 1.0、故障产物、其他配置或十二项并集。

经审查更新受审源后重新配置、构建名单消费者并运行产品及相关回归，打包同一批DLL/manifest/依赖。名单生成不参与业务DLL链接；只换证据元数据且四项不变时生成C++内容保持。最终发布档案另行绑定名单SHA、实际产品exe、最终五配置矩阵和新增人工回执，不通过回填报告反复改业务字节。
