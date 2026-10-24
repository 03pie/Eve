# LibYAML 固定基线

来源：[yaml/libyaml 0.2.5](https://github.com/yaml/libyaml/releases/tag/0.2.5)。库代码为 include/yaml.h、src/*.c、src/yaml_private.h，保留原 MIT LICENSE。仅新增本项目构建入口，不修改库实现，不运行上游测试/安装逻辑，不在构建时联网。

原始归档：https://codeload.github.com/yaml/libyaml/zip/refs/tags/0.2.5

SHA256：14605baa6dfc0c4d3ab943a46a627413c0388736e453b67fe4e90c9683c8cbc8

通过 yaml_parser_parse 的事件接口构造宿主有界配置树，不使用 yaml_parser_load 的自动文档组合或别名展开。使用者仍须检查输入大小、事件、标记及语义限额；库本身不构成平台配置校验。分发库或包含本库的可执行文件时须保留 LICENSE，构建另复制至 build/<preset>/licenses/LibYAML.LICENSE。
