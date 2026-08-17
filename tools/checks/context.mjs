import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

export function createCheckContext(entryUrl) {
  const toolsRoot = path.dirname(fileURLToPath(entryUrl));
  const projectRoot = path.resolve(toolsRoot, "..");
  const deliveryRoot = projectRoot;
  const failures = [];

  function absolute(relativePath, root = projectRoot) {
    return path.join(root, relativePath);
  }

  function read(relativePath, root = projectRoot) {
    const target = absolute(relativePath, root);
    try {
      return fs.readFileSync(target, "utf8");
    } catch (error) {
      failures.push(`无法读取 ${path.relative(deliveryRoot, target)}：${error.message}`);
      return "";
    }
  }

  function readBuffer(relativePath, root = projectRoot) {
    const target = absolute(relativePath, root);
    try {
      return fs.readFileSync(target);
    } catch (error) {
      failures.push(`无法读取 ${path.relative(deliveryRoot, target)}：${error.message}`);
      return Buffer.alloc(0);
    }
  }

  function requireFile(relativePath, root = projectRoot) {
    if (!fs.existsSync(absolute(relativePath, root))) {
      failures.push(`缺少文件：${path.relative(deliveryRoot, absolute(relativePath, root))}`);
      return false;
    }
    return true;
  }

  function requireMatch(message, content, expression) {
    if (!expression.test(content)) failures.push(message);
  }

  function requireNoMatch(message, content, expression) {
    if (expression.test(content)) failures.push(message);
  }

  function walk(root, predicate = () => true) {
    if (!fs.existsSync(root)) return [];
    const files = [];
    const visit = (directory) => {
      for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
        const target = path.join(directory, entry.name);
        if (entry.isDirectory()) {
          if (entry.name !== "artifacts" && entry.name !== ".git") visit(target);
        } else if (predicate(target)) {
          files.push(target);
        }
      }
    };
    visit(root);
    return files;
  }

  function checkLineBudget(relativePaths, maximum) {
    for (const relativePath of relativePaths) {
      const lineCount = read(relativePath).split(/\r?\n/u).length;
      if (lineCount > maximum) {
        failures.push(`${relativePath} 为 ${lineCount} 行，超过 ${maximum} 行职责预算`);
      }
    }
  }

  return {
    absolute,
    checkLineBudget,
    deliveryRoot,
    failures,
    fs,
    path,
    projectRoot,
    read,
    readBuffer,
    requireFile,
    requireMatch,
    requireNoMatch,
    walk,
    xamlHandlers: new Set(),
  };
}
