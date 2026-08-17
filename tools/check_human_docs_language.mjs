import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const projectRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const deliveryRoot = projectRoot;
const ignoredDirectories = new Set([
  ".git", "node_modules", "build", "build-audit", "build-asan",
  "build-direct", "build-direct-sanitize", "build-linux", "build-vs2026"
]);
const explicitMachineFiles = new Set([
  "CMakeLists.txt", "LICENSE", "THIRD_PARTY_NOTICES.txt",
]);

function collect(directory, files) {
  for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
    if (entry.isDirectory() && ignoredDirectories.has(entry.name)) {
      continue;
    }
    const fullPath = path.join(directory, entry.name);
    if (entry.isDirectory()) {
      collect(fullPath, files);
      continue;
    }
    if (explicitMachineFiles.has(entry.name)) {
      continue;
    }
    const extension = path.extname(entry.name).toLowerCase();
    if (extension === ".md" || extension === ".txt") {
      files.push(fullPath);
    }
  }
}

function looksLikeEnglishProse(line) {
  const trimmed = line.trim();
  if (!trimmed || /[\u3400-\u9fff]/u.test(trimmed)) {
    return false;
  }
  if (/^(https?:\/\/|[|+\-=:>#*\s]+$)/u.test(trimmed)) {
    return false;
  }
  if (/^-\s+`[^`]+`[。；，、]?$/u.test(trimmed)) {
    return false;
  }
  if (/^[A-Za-z]:\\|^[./\\]|^[\w.-]+\.(md|txt|png|bmp|jsonl|cpp|h|xaml)$/u.test(trimmed)) {
    return false;
  }
  const words = trimmed.match(/[A-Za-z]{3,}/gu) ?? [];
  const letters = (trimmed.match(/[A-Za-z]/gu) ?? []).length;
  return words.length >= 6 && letters >= 40;
}

const files = [];
collect(deliveryRoot, files);
const failures = [];

for (const file of files.sort()) {
  let inFence = false;
  const lines = fs.readFileSync(file, "utf8").split(/\r?\n/u);
  lines.forEach((line, index) => {
    if (/^\s*(```|~~~)/u.test(line)) {
      inFence = !inFence;
      return;
    }
    if (!inFence && looksLikeEnglishProse(line)) {
      failures.push(`${path.relative(deliveryRoot, file)}:${index + 1}: ${line.trim()}`);
    }
  });
}

if (failures.length > 0) {
  console.error("发现疑似英文说明行：");
  failures.forEach((failure) => console.error(`- ${failure}`));
  process.exit(1);
}

console.log(`中文文档检查通过：${files.length} 个 Markdown/TXT 文件。`);
