Rillshot 1.2.0 预览包
======================

这是给受邀测试者使用的 Windows x64 Portable 版本，不包含源码，也不需要安装。

开始使用
1. 把整个 ZIP 解压到新的、可写的目录；不要直接在 ZIP 内运行。
2. 图形界面运行根目录的 Rillshot.exe；命令行调用根目录的 rillshot-cli.exe。
3. 选择截图区域、滚动点与方向，然后开始截图。
4. 截图和诊断默认保存在 app\captures 与 app\logs；分享前请删除账号、工作内容和本地路径。

反馈
请复制 FEEDBACK-TEMPLATE-ZH.md，填写 RELEASE-METADATA.json 中的 artifactVersion、
sourceRevision 和平台信息。发送图片或 JSONL 前，请逐项检查并删除不愿公开的内容。

已知边界
- 首发支持矩阵为 64 位 Windows 11 x64。
- 受保护内容、安全桌面或权限更高的窗口可能拒绝捕获或输入。
- 极低信息或重复纹理页面可能提前停止，以避免错误拼接。
