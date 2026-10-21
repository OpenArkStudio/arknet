import assert from "node:assert/strict";
import { parse } from "parse5";

export const controlLabels = {
  en: { skip: "Skip to content", search: "Search", searchDocumentation: "Search documentation", theme: "Toggle dark mode", toc: "On this page", contents: "Table of contents", jump: "Jump to section", overview: "Overview", filter: "Filter…", filterNavigation: "Filter navigation", pagination: "Pagination", previous: "Previous", next: "Next", edit: "Edit this page", placeholder: "Search documentation…", empty: "Type to search…", closeSearch: "Close search" },
  "zh-CN": { skip: "跳转到正文", search: "搜索", searchDocumentation: "搜索文档", theme: "切换深色模式", toc: "本页内容", contents: "页内目录", jump: "跳转到章节", overview: "概览", filter: "筛选…", filterNavigation: "筛选导航", pagination: "相邻页面", previous: "上一篇", next: "下一篇", edit: "编辑此页", placeholder: "搜索文档…", empty: "输入关键词开始搜索…", closeSearch: "关闭搜索" },
};

const attr = (node, name) => node.attrs?.find(attribute => attribute.name === name)?.value;
const has = (node, name) => attr(node, name) !== undefined;
const text = node => node.nodeName === "#text" ? node.value : (node.childNodes ?? []).map(text).join("");
function find(root, predicate) {
  const matches = [];
  function walk(node) {
    if (predicate(node)) matches.push(node);
    for (const child of node.childNodes ?? []) walk(child);
  }
  walk(root);
  return matches;
}
function one(root, predicate, name) {
  const found = find(root, predicate);
  assert.equal(found.length, 1, `${name}: expected one control`);
  return found[0];
}

/** Check actual control nodes; English code samples on a Chinese page are valid. */
export function assertUiControls(html, locale) {
  const labels = controlLabels[locale];
  assert.ok(labels, "known UI locale");
  const root = parse(html);
  assert.equal(text(one(root, node => node.tagName === "a" && attr(node, "href") === "#main-content", "skip link")).trim(), labels.skip);
  const control = attribute => one(root, node => has(node, attribute), attribute);
  const search = control("data-search-trigger");
  assert.equal(attr(search, "aria-label"), labels.searchDocumentation);
  assert.ok(text(search).includes(labels.search));
  assert.equal(attr(control("data-nb-theme-toggle"), "aria-label"), labels.theme);
  const toc = control("data-nb-toc");
  assert.equal(text(one(toc, node => node.tagName === "h2", "TOC heading")), labels.toc);
  assert.equal(attr(one(toc, node => node.tagName === "nav", "TOC navigation"), "aria-label"), labels.contents);
  assert.equal(attr(control("data-nb-mobile-toc"), "aria-label"), labels.toc);
  const mobile = control("data-nb-mobile-toc-select");
  assert.equal(attr(mobile, "aria-label"), labels.jump);
  assert.equal(text(find(mobile, node => node.tagName === "option")[0]), labels.overview);
  const filter = control("data-nb-sidebar-filter-input");
  assert.equal(attr(filter, "placeholder"), labels.filter);
  assert.equal(attr(filter, "aria-label"), labels.filterNavigation);
  const pagination = one(root, node => node.tagName === "nav" && attr(node, "aria-label") === labels.pagination, "pagination");
  for (const label of [labels.previous, labels.next]) {
    assert.ok(find(pagination, node => node.tagName === "span" && text(node) === label).length, label);
  }
  for (const edit of find(root, node => node.tagName === "a" && attr(node, "href")?.includes("/edit/"))) {
    assert.equal(text(edit).trim(), labels.edit);
  }
  const dialog = control("data-search-dialog");
  assert.equal(attr(dialog, "data-search-locale"), locale);
  assert.equal(attr(dialog, "aria-label"), labels.searchDocumentation);
  assert.equal(attr(control("data-search-input"), "placeholder"), labels.placeholder);
  assert.equal(text(control("data-search-empty")).trim(), labels.empty);
  assert.equal(attr(one(dialog, node => has(node, "data-dialog-close"), "close search"), "aria-label"), labels.closeSearch);
}
