Rillshot 1.1.9 Portable
=======================

这是正式的 Windows x64 Portable 版本，不包含源码，也不需要安装。

开始使用
1. 把整个 ZIP 解压到新的、可写的目录；不要直接在 ZIP 内运行。
2. 双击根目录唯一的启动入口 Rillshot.cmd；不要进入 app 目录寻找 EXE。
3. 选择截图区域、滚动点与方向，然后开始截图。
4. 截图和诊断默认保存在 app\captures 与 app\logs，请在分享前检查是否含敏感信息。

安全核对
- 只从发布者给出的正式下载页获取文件。
- 核对 ZIP 的 SHA-256 与发布者提供的 SHA256SUMS.txt。
- 在 app\Rillshot.WinUI.exe 的“属性 > 数字签名”中核对发布者和时间戳。
- 若数字签名缺失、无效或发布者不符，请停止运行并联系发布者。

许可、源码与反馈
LICENSE、COPYRIGHT.md、SOURCE_OFFER.md 和 THIRD_PARTY_NOTICES.txt 说明社区许可、权利链、对应源码获取与第三方材料。
公开二进制必须与构建它的 v1.1.9 tag 对应源码在同一发布位置等价提供；若下载页没有源码入口，请停止分发。
RELEASE-METADATA.json 记录 productVersion、artifactVersion、sourceRevision 和平台信息。
报告问题时请复制 FEEDBACK-TEMPLATE-ZH.md；不要发送含私人截图内容的图片或 JSONL，
除非你已经检查并同意分享。

已知边界
- 仅支持 64 位 Windows 10/11。
- 受保护内容、安全桌面或权限更高的窗口可能拒绝捕获或输入。
- 极低信息或重复纹理页面可能提前停止，以避免错误拼接。
