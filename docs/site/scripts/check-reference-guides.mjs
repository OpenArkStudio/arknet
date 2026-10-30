import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { parse } from "parse5";
import { unified } from "unified";
import remarkParse from "remark-parse";

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
