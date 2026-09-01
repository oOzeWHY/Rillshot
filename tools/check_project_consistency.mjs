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
  `工程一致性检查通过：Rillshot 1.1.9 身份、8 个核心测试、` +
  `${context.xamlHandlers.size} 个 XAML 事件、WinUI/Launcher 工程、` +
  "原生 Portable 入口、签名边界及实际 WinUI CI 构建契约。",
);
