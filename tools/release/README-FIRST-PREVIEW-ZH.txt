Rillshot 1.1.9 预览包
======================

这是给受邀测试者使用的 Windows x64 Portable 版本，不包含源码，也不需要安装。

开始使用
1. 把整个 ZIP 解压到新的、可写的目录；不要直接在 ZIP 内运行。
2. 双击根目录唯一的启动入口 Rillshot.cmd；不要进入 app 目录寻找 EXE。
3. 选择截图区域、滚动点与方向，然后开始截图。
4. 截图和诊断默认保存在 app\captures 与 app\logs，请在分享前检查是否含敏感信息。

安全核对
- 只从发布者给出的原始链接下载。
- 核对 ZIP 的 SHA-256 与发布者提供的 SHA256SUMS.txt。
- 若发布者声明已签名，请在 app\Rillshot.WinUI.exe 的“属性 > 数字签名”中核对发布者。
- 未签名预览包可能触发 Windows 安全提示；不确定来源时不要绕过提示。

反馈
请复制 FEEDBACK-TEMPLATE-ZH.md，填写 RELEASE-METADATA.json 中的 artifactVersion、
sourceRevision 和平台信息。不要发送含私人截图内容的图片或 JSONL，除非你已经检查并同意分享。

已知边界
- 仅支持 64 位 Windows 10/11。
- 受保护内容、安全桌面或权限更高的窗口可能拒绝捕获或输入。
- 极低信息或重复纹理页面可能提前停止，以避免错误拼接。
