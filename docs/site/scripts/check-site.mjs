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

// Exercise the emitted surfaces, rather than trusting MDX import detection alone.
// Every panel and card must remain available to agents without a selected UI tab.
for (const collection of ["docs", "zh"]) {
  const prefix = collection === "zh" ? "zh/" : "";
  for (const slug of ["index", "concepts/index", "concepts/io-models", "get-started/installation", "get-started/first-application", "features/tcp/index", "features/udp/index", "features/rudp/index", "features/http/index", "features/websocket/index", "features/tls/index", "features/mysql/index", "features/redis/index", "features/rudp/fec"]) {
    const source = await readFile(new URL(`../src/content/${collection}/${slug}.mdx`, import.meta.url), "utf8");
    const route = slug === "index" ? "" : slug.replace(/\/index$/, "");
    const html = await readFile(join(root, prefix, route, "index.html"), "utf8");
    const markdown = await readFile(join(root, prefix, route, "index.md"), "utf8");
    const label = `${prefix}${route || "home"}`;
    const plain = value => value.replace(/\s+/g, " ").trim();
    for (const card of source.matchAll(/<LinkCard\b[^>]*>/g)) {
      const attribute = name => new RegExp(`${name}="([^"]*)"`).exec(card[0])?.[1];
      const title = attribute("title");
      const href = attribute("href");
      const destination = href.startsWith("/") ? `/arknet${href}` : href;
      assert.ok(html.includes(`href="${destination}"`), `${label}: missing card destination ${destination}`);
      assert.ok(markdown.includes(`[${title}](${destination})`), `${label}: missing exported card ${title}`);
      assert.ok(markdown.includes(attribute("description")), `${label}: missing card description`);
    }
    for (const tab of source.matchAll(/<TabItem label="([^"]+)">([\s\S]*?)<\/TabItem>/g)) {
      assert.ok(html.includes(`data-nb-tab-label="${tab[1]}"`), `${label}: missing tab label`);
      assert.ok(markdown.includes(`### ${tab[1]}`), `${label}: missing exported tab label`);
      for (const code of tab[2].matchAll(/```[^\n]*\n([\s\S]*?)\n```/g)) {
        assert.ok(plain(markdown).includes(plain(code[1])), `${label}: missing tab code`);
      }
    }
    for (const step of source.matchAll(/<Step title="([^"]+)">/g)) {
      assert.ok(markdown.includes(`**${step[1]}**`), `${label}: missing exported step label`);
    }
    assert.doesNotMatch(html, /\/arknet\/arknet\//, `${label}: doubled project base`);
    assert.doesNotMatch(markdown, /\/arknet\/arknet\//, `${label}: doubled project base in export`);
    assert.doesNotMatch(markdown, /<(?:CardGrid|LinkCard|Tabs|TabItem|Aside|Step|Steps)[\s/>]/, `${label}: component leaked into export`);
    const constraints = {
      "features/rudp/fec": ["26", "1200", "1174", "256", "4096", "1 MiB", "data_shards", "parity_shards", "max_parity_shards", "min_data_shards", "group_wait_ms", "max_redundancy_percent", "group_lifetime_ms", "max_groups", "max_buffer_bytes", "sample_window_ms", "min_samples", "enable_loss_percent", "disable_loss_percent", "stable_windows", "cooldown_ms", "overload_queue_percent", "operation_not_supported", "missing_shards", "recovered_shards", "retransmissions", "AFEC", "CRC"],
      "concepts/io-models": ["serialize = false", "poll(0)", "std::logic_error", "TODO", "awaitable", "co_spawn", "manual_runtime", "io_context::run()"],
      "get-started/installation": ["1.90", "1.38", "1.1.1", "1.2", "NOMINMAX", "--config Release", "OPENSSL_ROOT_DIR"],
      "features/http": ["Boost.Asio", "Boost.Beast", "Standalone Asio", "8192", "16 MiB", "HTTP/1.1", "Host", "Expect", "TODO", "HTTP/2", "HTTP/3"],
      "features/websocket": ["Boost.Asio", "Standalone Asio", "get_upgrade_response()", "bind_upgrade", "got_binary()", "UTF-8"],
      "features/tls": ["TLS 1.2", "TLS 1.3", "verify_none", "verify_fail_if_no_peer_cert", "set_cert_file", "set_cert_buffer", "SNI", "mTLS"],
      "features/mysql": ["ARKNET_ENABLE_SSL", "OpenSSL", "8.4.11", "SQLSTATE", "LOCAL INFILE", "caching_sha2_password"],
      "features/redis": ["RESP2", "RESP3", "HELLO", "Sentinel", "Pub/Sub", "MONITOR", "close()"],
    }[route] ?? [];
    for (const constraint of constraints) assert.ok(markdown.includes(constraint), `${label}: missing exported constraint ${constraint}`);
  }
}
console.log("Checked all bilingual component-guide cards, tab paths, step labels and key constraints in emitted HTML/Markdown.");

for (const [page, locale] of [["features/rudp/fec/index.html", "en"], ["zh/features/rudp/fec/index.html", "zh-CN"]]) {
  assertUiControls(await readFile(join(root, page), "utf8"), locale);
}
console.log("Checked English/Chinese FEC controls, including the search dialog locale.");

// Check emitted reference/report content within the existing documentation gate.
{
const { default: assert } = await import("node:assert/strict");
const { readFile } = await import("node:fs/promises");
const { parse } = await import("parse5");
const { unified } = await import("unified");
const { default: remarkParse } = await import("remark-parse");

// Run after the site build: node scripts/check-reference-guides.mjs.
// Keep reference entries visible and preserve complete source examples in AI output.
const pages = ["reference/api", "reference/configuration", "reference/errors", "guides/external-io", "guides/reconnect", "guides/timers", "project/troubleshooting"];
const parser = unified().use(remarkParse);
const plain = text => text.replace(/\s+/g, " ").trim();
const contracts = {
  "reference/api": ["mysql::connection", "redis::tls_pool", "io_pool", "iopool", "request_stop", "wait_stopped", "no_buffer_space", "16 MiB", "1024", "8192", "base/impl", "detail"],
  "reference/configuration": ["ARKNET_USE_BOOST_ASIO", "ARKNET_ENABLE_SSL", "ARKNET_HEADER_ONLY", "ARKNET_ENABLE_TIMER_CALLBACK_WHEN_ERROR", "Standalone Asio", "ON", "OFF", "16 MiB", "1024", "8192", "TLS 1.2", "SO_REUSEADDR"],
  "reference/errors": ["get_last_error()", "verify_peer | verify_fail_if_no_peer_cert", "verify_none", "DNS/IP", "SNI"],
  "guides/external-io": ["context.run()", "io_context&", "request_stop()", "wait_stopped()", "work.reset()", "io_context::stop()", "UDP", "strand"],
  "guides/reconnect": ["set_auto_reconnect", "operation_aborted"],
  "guides/timers": ["dispatch(callback)", "post(callback, delay)", "stop_all_timed_tasks()", "std::future_error", "std::future_errc::broken_promise", "ARKNET_ENABLE_TIMER_CALLBACK_WHEN_ERROR", "get_last_error()"],
  "project/troubleshooting": ["ARKNET_ASIO_INCLUDE_DIR", "CMAKE_PREFIX_PATH", "OPENSSL_ROOT_DIR", "ARKNET_ENABLE_SSL", "invalid_argument", "no_buffer_space", "in_progress", "operation_not_supported", "operation_aborted", "bind_handshake", "22.12", "8767"],
};
for (const page of pages) {
  for (const suffix of ["", "_CN"]) {
    const prefix = suffix ? "zh/" : "";
    const label = prefix + page;
    const source = await readFile(new URL(`../../${page}${suffix}.md`, import.meta.url), "utf8");
    const html = await readFile(new URL(`../dist/${prefix}${page}/index.html`, import.meta.url), "utf8");
    const markdown = await readFile(new URL(`../dist/${prefix}${page}/index.md`, import.meta.url), "utf8");
    const document = parse(html);
    let tables = 0;
    function checkVisible(node, concealed = false) {
      const hidden = concealed || node.tagName === "details" || (node.attrs ?? []).some(attr => attr.name === "hidden");
      if (node.tagName === "table") {
        tables++;
        assert.equal(hidden, false, `${label}: reference table is concealed`);
      }
      for (const child of node.childNodes ?? []) checkVisible(child, hidden);
    }
    checkVisible(document);
    const sourceTables = source.match(/^\|(?:\s*:?-+:?\s*\|)+$/gm) ?? [];
    assert.equal(tables, sourceTables.length, `${label}: missing reference table`);
    function checkSource(node) {
      if (node.type === "code") assert.ok(plain(markdown).includes(plain(node.value)), `${label}: incomplete code example in AI Markdown`);
      if (node.type === "heading" && node.depth > 1) {
        const title = source.slice(node.position.start.offset, node.position.end.offset);
        assert.ok(markdown.includes(title), `${label}: missing semantic heading ${title}`);
      }
      for (const child of node.children ?? []) checkSource(child);
    }
    checkSource(parser.parse(source));
    for (const token of contracts[page]) assert.ok(markdown.includes(token), `${label}: missing contract ${token}`);
    if (source.includes("<Steps>")) {
      assert.match(html, /data-steps/, `${label}: missing rendered procedure`);
      assert.match(markdown, /\n1\. /, `${label}: missing numbered AI procedure`);
    }
    for (const aside of source.matchAll(/<Aside[^>]*title="([^"]+)"/g)) {
      assert.ok(html.includes(`aria-label="${aside[1]}"`), `${label}: missing accessible caveat label`);
      assert.ok(markdown.includes(`> **${aside[1]}**`), `${label}: missing AI caveat label`);
    }
    assert.doesNotMatch(markdown, /<(?:Aside|Steps|Step|Tabs|TabItem|LinkCard|CardGrid)[\s/>]/, `${label}: component markup leaked into AI Markdown`);
    assert.doesNotMatch(html + markdown, /\/arknet\/arknet\//, `${label}: duplicated base prefix`);
  }
}
console.log(`Checked ${pages.length * 2} reference/integration HTML and AI Markdown exports: visible tables, headings, code, procedures and constraints.`);

}

