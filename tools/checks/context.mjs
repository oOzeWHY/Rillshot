import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

export function createCheckContext(entryUrl) {
  const toolsRoot = path.dirname(fileURLToPath(entryUrl));
  const projectRoot = path.resolve(toolsRoot, "..");
  const failures = [];
  const ignoredRoots = new Set([".git", ".vs", "artifacts", "build", "node_modules", "out"]);

  function absolute(relativePath) {
    return path.join(projectRoot, relativePath);
  }

  function read(relativePath) {
    try {
      return fs.readFileSync(absolute(relativePath), "utf8");
    } catch (error) {
      failures.push(`无法读取 ${relativePath}：${error.message}`);
      return "";
    }
  }

  function requireFile(relativePath) {
    if (!fs.existsSync(absolute(relativePath))) {
      failures.push(`缺少文件：${relativePath}`);
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

  function walk(root = projectRoot, predicate = () => true) {
    if (!fs.existsSync(root)) return [];
    const files = [];
    const visit = (directory) => {
      for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
        const target = path.join(directory, entry.name);
        const relative = path.relative(projectRoot, target);
        const [topLevel] = relative.split(path.sep);
        if (entry.isDirectory()) {
          if (!ignoredRoots.has(topLevel) &&
              !relative.startsWith(path.join("apps", "rillshot_winui", "Generated Files")) &&
              !relative.startsWith(path.join("apps", "rillshot_winui", "bin")) &&
              !relative.startsWith(path.join("apps", "rillshot_winui", "obj"))) {
            visit(target);
          }
        } else if (predicate(target)) {
          files.push(target);
        }
      }
    };
    visit(root);
    return files;
  }

  return {
    absolute,
    failures,
    fs,
    path,
    projectRoot,
    read,
    requireFile,
    requireMatch,
    requireNoMatch,
    walk,
    xamlHandlers: new Set(),
  };
}
