import { readdir, readFile, mkdir, writeFile, cp, rm } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { posix, join } from "node:path";
import { convertPage, pageRoute } from "./content.mjs";

const site = fileURLToPath(new URL("../", import.meta.url));
const docs = fileURLToPath(new URL("../../", import.meta.url));
async function pages(directory, prefix = "") {
  const result = [];
  for (const item of await readdir(directory, { withFileTypes: true })) {
    if (item.name.startsWith(".") || item.name === "site") continue;
    const relative = posix.join(prefix, item.name);
    if (item.isDirectory()) result.push(...await pages(join(directory, item.name), relative));
    else if (item.name.endsWith(".md") && item.name !== "_sidebar.md") result.push(relative);
  }
  return result;
}
const files = new Set(await pages(docs));
const content = join(site, "src/content");
await rm(content, { recursive: true, force: true });
for (const file of files) {
  const chinese = file.endsWith("_CN.md");
  const sibling = chinese ? file.replace(/_CN\.md$/, ".md") : file.replace(/\.md$/, "_CN.md");
  if (!files.has(sibling)) throw new Error(`${file}: missing translation`);
  const slug = file.replace(/(?:_CN)?\.md$/, "").replace(/^README$/, "index");
  const target = join(content, chinese ? "zh" : "docs", slug + ".md");
  await mkdir(join(target, ".."), { recursive: true });
  await writeFile(target, convertPage(await readFile(join(docs, file), "utf8"), file, files));
}
await mkdir(join(site, "public/performance"), { recursive: true });
await cp(join(docs, "performance/assets"), join(site, "public/performance/assets"), { recursive: true });
for (const name of await readdir(join(docs, "performance"))) {
  if (name.endsWith(".json")) await cp(join(docs, "performance", name), join(site, "public/performance", name));
}
console.log(`Prepared ${files.size} bilingual Markdown pages.`);
