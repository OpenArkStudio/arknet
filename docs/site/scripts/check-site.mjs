import assert from "node:assert/strict";
import { readFile, readdir, stat } from "node:fs/promises";
import { join, resolve } from "node:path";
import { parse } from "parse5";
import { assertUiControls } from "./ui-controls.mjs";

const root = resolve("dist");
const pages = [];
async function collect(directory) {
  for (const item of await readdir(directory, { withFileTypes: true })) {
    const file = join(directory, item.name);
    if (item.isDirectory()) await collect(file);
    else if (item.name.endsWith(".html")) pages.push(file);
  }
}
await collect(root);
const references = new Set();
const documents = new Map();
const failures = [];
const routes = JSON.parse(await readFile(new URL("../src/legacy-routes.json", import.meta.url), "utf8"));
const redirects = new Set(Object.keys(routes).map(route => join(root, route, "index.html")));
const navigation = new Set();
for (const page of pages) {
  const document = parse(await readFile(page, "utf8"));
  const ids = new Set();
  let headings = 0;
  function walk(node) {
    if (node.tagName === "h1") headings++;
    if (node.tagName === "a" && node.attrs?.some(attr => attr.name === "data-nb-sidebar-link")) {
      const href = node.attrs.find(attr => attr.name === "href")?.value;
      if (href) navigation.add(href.replace(/\/$/, ""));
    }
    for (const { name, value } of node.attrs ?? []) {
      if (name === "id") ids.add(value);
      if ((name === "href" || name === "src" || (name === "value" && node.tagName === "option")) && value.startsWith("/") && !value.startsWith("//")) {
        if (value !== "/arknet" && !value.startsWith("/arknet/")) failures.push(`${page}: reference escapes project base: ${value}`);
        else references.add(value);
      }
    }
    for (const child of node.childNodes ?? []) walk(child);
  }
  walk(document);
  if (!page.endsWith("404.html")) assert.equal(headings, 1, `${page}: one page heading required`);
  documents.set(page, ids);
}
for (const [page] of documents) {
  if (page.endsWith("404.html") || redirects.has(page)) continue;
  const route = "/arknet/" + page.slice(root.length + 1).replace(/(?:^|\/)index\.html$/, "").replace(/\/$/, "");
  if (!navigation.has(route.replace(/\/$/, ""))) failures.push(`${page}: missing navigation entry`);
}
for (const [from, redirect] of Object.entries(routes)) {
  const target = join(root, redirect.to, "index.html");
  if (!documents.has(target)) failures.push(`${from}: missing redirect destination ${redirect.to}`);
  for (const [anchor, destination] of Object.entries(redirect.anchors)) {
    if (!documents.get(join(root, destination, "index.html"))?.has(anchor)) failures.push(`${from}#${anchor}: missing redirected anchor in ${destination}`);
  }
}
for (const reference of references) {
  const url = new URL(reference, "https://openarkstudio.github.io");
  let target = join(root, decodeURIComponent(url.pathname.slice("/arknet".length)));
  try {
    if ((await stat(target)).isDirectory()) target = join(target, "index.html");
    await stat(target);
    const anchor = decodeURIComponent(url.hash.slice(1));
    if (anchor && documents.has(target) && !documents.get(target).has(anchor)) failures.push(`${reference}: missing anchor`);
  } catch {
    failures.push(`${reference}: missing static file`);
  }
}
assert.deepEqual(failures, [], "Broken site references:\n" + failures.join("\n"));
for (const file of ["llms.txt", "llms-full.txt", "zh/llms.txt", "features/udp/performance/index.md", "zh/features/udp/performance/index.md", "sitemap-index.xml", "robots.txt"]) {
  assert.ok((await stat(join(root, file))).size > 0, `missing agent/SEO output: ${file}`);
}
console.log(`Checked ${pages.length} static HTML pages and ${references.size} internal references, including AI Markdown endpoints.`);

const bundledLicenses = await readFile(join(root, "third-party-licenses.txt"), "utf8");
assert.match(bundledLicenses, /mermaid/, "missing bundled diagram renderer license");
assert.match(bundledLicenses, /Knut Sveidqvist/, "missing Mermaid copyright notice");

for (const prefix of ["", "zh/"]) {
  const diagramHtml = await readFile(join(root, prefix, "features/rudp/fec/index.html"), "utf8");
  assert.match(diagramHtml, /<figure[^>]*data-nb-lang="mermaid"/, `${prefix}FEC: missing diagram source`);
  assert.match(diagramHtml, /src="\/arknet\/_astro\/Mermaid\.[^"]+\.js"/, `${prefix}FEC: missing bundled diagram renderer`);
  const diagramMarkdown = await readFile(join(root, prefix, "features/rudp/fec/index.md"), "utf8");
  assert.match(diagramMarkdown, /```mermaid\nflowchart /, `${prefix}FEC: diagram source lost from Markdown export`);
  for (const route of ["get-started/installation", "get-started/first-application", "project/troubleshooting"]) {
    const html = await readFile(join(root, prefix, route, "index.html"), "utf8");
    assert.match(html, /data-steps/, `${prefix}${route}: missing Nimbus steps`);
    assert.match(html, /data-step/, `${prefix}${route}: missing Nimbus step items`);
    const markdown = await readFile(join(root, prefix, route, "index.md"), "utf8");
    assert.match(markdown, /\n1\. /, `${prefix}${route}: missing exported numbered steps`);
    assert.doesNotMatch(markdown, /<(?:Aside|Step|Steps)[\s>]/, `${prefix}${route}: component leaked into AI export`);
  }
  for (const route of ["guides/socket-buffers", "concepts/sending", "features/rudp", "features/rudp/fec", "features/tls/server", "performance/udp-update", "project/testing", "project/troubleshooting"]) {
    const html = await readFile(join(root, prefix, route, "index.html"), "utf8");
    assert.match(html, /role="note"/, `${prefix}${route}: missing accessible Nimbus callout`);
    const markdown = await readFile(join(root, prefix, route, "index.md"), "utf8");
    assert.match(markdown, /(?:^|\n)> \*\*/, `${prefix}${route}: missing labeled Markdown callout`);
    assert.doesNotMatch(markdown, /<Aside[\s>]/, `${prefix}${route}: callout leaked into AI export`);
  }
}
console.log("Checked bilingual Nimbus steps, callouts and plain Markdown exports.");

for (const [page, locale] of [["features/rudp/fec/index.html", "en"], ["zh/features/rudp/fec/index.html", "zh-CN"]]) {
  assertUiControls(await readFile(join(root, page), "utf8"), locale);
}
console.log("Checked English/Chinese FEC controls, including the search dialog locale.");
