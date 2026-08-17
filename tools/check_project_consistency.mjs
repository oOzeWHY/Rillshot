import { createCheckContext } from "./checks/context.mjs";
import { checkCore } from "./checks/core.mjs";
import { checkProjectIdentity } from "./checks/project-identity.mjs";
import { checkRelease } from "./checks/release.mjs";
import { checkWinUI } from "./checks/winui.mjs";

const context = createCheckContext(import.meta.url);

checkProjectIdentity(context);
checkCore(context);
checkWinUI(context);
checkRelease(context);

if (context.failures.length > 0) {
  console.error("工程一致性检查失败：");
  for (const failure of context.failures) {
    console.error(`- ${failure}`);
  }
  process.exit(1);
}

console.log(
  `工程一致性检查通过：Rillshot 1.1.9 身份、${context.xamlHandlers.size} 个 XAML 事件、` +
  "模块边界、八个测试目标、PowerShell AST 前置门禁、同源 Windows 工具发现、WinRT 合成器完整命名空间、快捷键超时停止状态、人工输入 EOF 拒绝与精确停止分类、输入异常隔离与鼠标位置恢复、粗筛/精排双重预算、双相位双向拼接、真实稳定截止时间、图像内存预算、MSIX LocalState、显式区域/滚动点与 220ms 设置窗口协同动效、捕获缓冲区验证、稳定 WinUI 依赖图、洁净 Portable 布局、GPL 对应源码、直发/Store 签名分流及无字母品牌资产一致。",
);
