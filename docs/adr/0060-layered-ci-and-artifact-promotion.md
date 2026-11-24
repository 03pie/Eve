# ADR-0060：分层CI与产物晋升

日期：2026-11-23。状态：已采纳并实施，P6-4已由0e215ea提交同步与独立关闭回执ec096收口。前驱P6-3已由56c47c1b98ee2d37ce992e753e2a65322fd20fb3提交、同步并核clean；本ADR不改变已关闭的960/961源码快照、固定447批准源或41cd运行包。

## 问题与职责

D14要求仓库内可执行入口和真实执行/失败记录，分开源码、资格候选、受审产品及发行验证。GitHub托管只承载无Qt源码验证；精确Qt/Native环境可由本地或指定runner用同一入口执行。环境不满足要明确PendingEnvironment，不能因没有托管runner而虚写完整通过，或自动扩大Native名单。

使用一个薄Python CLI `tools/ci.py`，只编排现有CMake预设、CTest、公开资格CLI及stock PowerShell验包器。原exporter继续唯一解释固定27项和候选格式；CMake固定受审源继续唯一生成产品Native四项，Session/Store及业务实现不依赖CI。入口不增加配置模型、批准源选择、故障注入或打包实现。

## 四个有限入口与状态

| subcommand | 输入/动作 | 成功状态与晋升前驱 |
|---|---|---|
| source | 新独占output内fresh二进制目录；core-gcc/msvc Debug/Release preset，显式关闭Qt/Ela；全build、完整CTest选择与实际Passed逐名匹配；packaging55/update24和CI自身纯测试 | SourceVerified；仅证明实际编译器/范围，Native/GUI另Pending |
| candidate | 原三profile目录、实际CTest路径；公开`--qualify`后独立`--evidence/--output`导出 | PendingReview；原capture/候选保留，绝不写任一受审JSON。缺配置/依赖为PendingEnvironment，真实子进程非0为Failed |
| product | 固定受审JSON447与原`inspect_profile`逐值匹配三个profile及四Native/七依赖；各精确6项产品CTest | ProductVerified；只验证受审已有实物，不build、不声称重跑5904。fresh不匹配时PendingProductValidation且非0，不能删正向用例 |
| release | 显式外部ZIP SHA和manifest锚；安全解压、精确库存与stock验包 | StaticVerified；GUI/cleanMachine仍Pending。只验证现包，不repackage、不重链、不调用要求旧961源码全同的fresh打包loader |

PendingReview只表示候选命令执行成功，并非批准；PendingEnvironment/PendingProductValidation和Failed均非0。每次output必须不存在，失败现场保留，不覆盖或自动重试。已存在的output拒绝时不写入其内容。

## 证据与资源所有权

每个实际命令保存argv、cwd、真实Popen PID、退出码、原始stdout/stderr及摘要，进程对象持有至自然退出；CTest继续拥有原每测试超时，不只杀其父进程。CLI记录有限工具路径/版本和编译器元数据，不打印完整环境或令牌。复用source_inputs生成本轮输入图，并另绑定CLI/tests/YAML/presets/ADR摘要；前后不一致即Failed。新增CI源码的输入图不回填旧包的961来源。

产品入口在运行前后重复原profile检查；Native批准及生产字节没有变更入口。release只在新output内展开，拒绝路径逃逸、重解析点、重复/大小写碰撞及链接条目，完整读取ZIP逐项核长度/SHA后由固定stock验包器核准确manifest和文件集合；运行包与源输入前后保持。没有自动晋升、发布或运行产品的release步骤。

## GitHub与兼容

新增单一workflow，仅push master与workflow_dispatch，Windows2022源码job；checkout固定3d3c42e5aac5ba805825da76410c181273ba90b1，contents:read且persist-credentials:false。通过预装vswhere选择VS2022 VC x64，在同cmd子进程call VsDevCmd后启动Python入口。优先使用预装CMake/Ninja/Python，实际版本写原件；不得宣称托管环境等同本机19.44/Qt5.15.2。

公开工作流仅保留源码验证日志。Native 候选、受审产品、发行静态核验分别在本地执行。P6-4 的独立审查、失败记录、阶段提交及远端同步记录已收入阶段归档。

P6-5 在同一源码入口加入拒写采证器的两项回归。提交 `e98e60a` 的 Windows x64 工作流实际通过 459 项 C++ 测试和四组 Python 测试（55、24、18、2 项）。该运行绑定当时的 964 项源码快照及当时的 ADR 文本。随后本 ADR 更新验收状态，测试记录沿用运行时的摘要。见[工作流原始运行](https://github.com/03pie/Eve/actions/runs/45340802804)及[源码与日志复核](../../build/p6-5-ci-stock-powershell-fix-20261124-1/review.json)。
