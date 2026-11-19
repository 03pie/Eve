第三方许可材料 / Third-party license materials

范围：ADR-0057/P6-1 的 Windows x64 内部技术预览，用户暂不公开发布。
本目录是实际许可原件、来源、用途及未决项的材料索引，不是 Native 批准、
第一方许可证选择或公开发行条件已满足的声明。sources.json 的 files 精确
覆盖其他所有材料；索引自身由外层包清单/SHA覆盖，避免自摘要循环。

Ela-MIT.txt / LibYAML-MIT.txt 保留仓库实际 MIT 原件。Ela 为 2.0.0 固定
导入基线 aae58f5d0d98e05eabe10c263aa7da5f2491d702 后本地维护；LibYAML
0.2.5 静态包含在宿主，不能因没有独立 DLL 而省略许可。

Qt-5.15.2 保存官方同版本源码包中的 LGPL3/GPL3/GPL2 和第三方原文；
SOURCE-ACQUISITION.txt 给准确官方源码 URL、完整 SHA 和获取/提取方式，
THIRD-PARTY-NOTICES.txt 给字体、图像、压缩、Unicode、编解码、Windows
可访问性及 ANGLE 等来源级版权、用途及原文路径。原 qt_attribution.json
个别多行字符串不符严格JSON，原字节改以TXT保存；派生索引只转义换行，
不改动许可原文。来源级条件不是每种可选代码路径已执行或已编入的证明。

This software is based in part on the work of the Independent JPEG Group.
Portions of this software are based on the FreeType Project (www.freetype.org).

Microsoft-CRT 保存官方条款 DOCX、可读 TXT 副本及正式来源/条款参考。
实际 app-local CRT 是新增部署闭包，P6-2仍须验证实际加载与运行。

未决项如实保留：第一方许可证未选；Ela五个嵌入资源已核上游同字节和
仓库MIT声明，但其独立作者/许可未单列确认；生成器网站不是授权。
Qt同版本官方源码获取已核，不宣称所选DLL位级可重建、vendor配方/补丁
已完全证实或已提供书面源码承诺。VS实际授权及将来公开发行决策不由
本材料目录推断。没有修改已资格 DLL/manifest、受审源或生产实现。
