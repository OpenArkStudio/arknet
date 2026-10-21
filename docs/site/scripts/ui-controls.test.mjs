import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import ts from "typescript";
import { assertUiControls, controlLabels } from "./ui-controls.mjs";

const source = await readFile(new URL("../src/lib/ui-labels.ts", import.meta.url), "utf8");
const javascript = ts.transpileModule(source, { compilerOptions: { module: ts.ModuleKind.ESNext } }).outputText;
const { getUiLabels, uiLocaleFromPath } = await import(`data:text/javascript;base64,${Buffer.from(javascript).toString("base64")}`);

test("UI locale follows the language segment after the configured base", () => {
  assert.equal(uiLocaleFromPath("/arknet/zh/features/rudp/fec/", "/arknet/"), "zh-CN");
  assert.equal(uiLocaleFromPath("/arknet/features/rudp/fec/", "/arknet/"), "en");
  assert.equal(uiLocaleFromPath("/zh/features/rudp/fec/", "/"), "zh-CN");
  assert.equal(uiLocaleFromPath("/arknet/zh-other/", "/arknet"), "en");
  assert.equal(uiLocaleFromPath("/other/zh/", "/arknet"), "en");
  assert.equal(uiLocaleFromPath("/arknet/", "/arknet"), "en");
});

test("English defaults remain English and Chinese search states use translated labels", () => {
  assert.equal(getUiLabels().search, "Search");
  assert.equal(getUiLabels("en").toggleDarkMode, "Toggle dark mode");
  assert.deepEqual(Object.keys(getUiLabels("en")), Object.keys(getUiLabels("zh-CN")));
  for (const state of ["typeToSearch", "searching", "noResults", "searchBuildRequired", "searchLoadError", "searchUnavailable"]) {
    assert.match(getUiLabels("zh-CN")[state], /[\u3400-\u9fff]/);
    assert.notEqual(getUiLabels("zh-CN")[state], getUiLabels("en")[state]);
  }
});

function fixture(locale) {
  const l = controlLabels[locale];
  return `<a href="#main-content">${l.skip}</a><button data-search-trigger aria-label="${l.searchDocumentation}">${l.search}</button>
<button data-nb-theme-toggle aria-label="${l.theme}"></button>
<div data-nb-toc><h2>${l.toc}</h2><nav aria-label="${l.contents}"></nav></div>
<nav data-nb-mobile-toc aria-label="${l.toc}"><select data-nb-mobile-toc-select aria-label="${l.jump}"><option>${l.overview}</option></select></nav>
<input data-nb-sidebar-filter-input aria-label="${l.filterNavigation}" placeholder="${l.filter}">
<nav aria-label="${l.pagination}"><a><span>${l.previous}</span></a><a><span>${l.next}</span></a></nav>
<a href="https://example.com/edit/page">${l.edit}</a>
<dialog data-search-dialog data-search-locale="${locale}" aria-label="${l.searchDocumentation}"><input data-search-input placeholder="${l.placeholder}"><p data-search-empty>${l.empty}</p><button data-dialog-close aria-label="${l.closeSearch}"></button></dialog>
<article><pre><code>Search Previous Next Toggle dark mode</code></pre></article>`;
}

test("built-control checks accept bilingual controls without translating code samples", () => {
  assert.doesNotThrow(() => assertUiControls(fixture("en"), "en"));
  assert.doesNotThrow(() => assertUiControls(fixture("zh-CN"), "zh-CN"));
});

test("built-control checks reject English controls on a Chinese page and wrong dialog locale", () => {
  const html = fixture("zh-CN");
  for (const [chinese, english] of [["跳转到正文", "Skip to content"], ["切换深色模式", "Toggle dark mode"], ["本页内容", "On this page"], ["下一篇", "Next"], ["编辑此页", "Edit this page"], ["搜索文档…", "Search documentation…"], ['data-search-locale="zh-CN"', 'data-search-locale="en"']]) {
    assert.throws(() => assertUiControls(html.replace(chinese, english), "zh-CN"), assert.AssertionError);
  }
});
