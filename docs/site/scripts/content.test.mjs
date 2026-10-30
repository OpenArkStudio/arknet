import assert from "node:assert/strict";
import { test } from "node:test";
import { convertPage, pageExtension, pageRoute } from "./content.mjs";

test("bilingual routes and home aliases", () => {
  assert.equal(pageRoute("README_CN.md"), "/zh/");
  assert.equal(pageRoute("udp/performance.md"), "/udp/performance");
  assert.equal(pageRoute("THIRD_PARTY_NOTICES_CN.md"), "/zh/third-party-notices");
  assert.equal(pageRoute("features/udp/index_CN.md"), "/zh/features/udp");
  assert.equal(pageRoute("features/index.md"), "/features");
});

test("convert parsed links while preserving code and labels", () => {
  const source = '# UDP\n\n[用法](usage_CN.md#示例)\n\n![吞吐](../performance/assets/udp.svg)\n\n```cpp\n// [use](usage_CN.md)\n```\n';
  const result = convertPage(source, "udp/overview_CN.md", new Set(["udp/usage_CN.md"]));
  assert.match(result, /\[用法\]\(\/zh\/udp\/usage#%E7%A4%BA%E4%BE%8B\)/);
  assert.match(result, /!\[吞吐\]\(\/arknet\/performance\/assets\/udp.svg\)/);
  assert.match(result, /\/\/ \[use\]\(usage_CN.md\)/);
  assert.match(result, /title: "UDP"/);
  assert.doesNotMatch(result, /# UDP/);
});

test("source references point at GitHub; missing documents fail", () => {
  const result = convertPage("# Page\n\n[source](../include/arknet.hpp)", "page.md", new Set());
  assert.match(result, /https:\/\/github.com\/OpenArkStudio\/arknet\/blob\/main\/include\/arknet.hpp/);
  assert.throws(() => convertPage("# Page\n\n[missing](missing.md)", "page.md", new Set()), /missing document/);
});

test("native Nimbus components use MDX and retain nested Markdown links", () => {
  const source = '# Guide\n\n<Steps>\n\n<Step title="Install">\n\n[Setup](installation.md)\n\n</Step>\n\n</Steps>\n\n<Aside type="caution" title="Warning">\n\nRead the [setup](installation.md).\n\n</Aside>\n';
  const result = convertPage(source, "guide.md", new Set(["installation.md"]));
  assert.equal(pageExtension(source), ".mdx");
  assert.match(result, /import \{ Aside \} from "@\/components\/ui\/aside"/);
  assert.match(result, /import \{ Step, Steps \} from "@\/components\/ui\/steps"/);
  assert.match(result, /\[Setup\]\(\/installation\)/);
  assert.match(result, /\[setup\]\(\/installation\)/);
});

test("component examples inside code blocks and inline code stay plain Markdown", () => {
  const source = '# Authoring\n\nUse `<Steps>`.\n\n```md\n<Aside type="note">\n\nExample\n\n</Aside>\n```\n';
  const result = convertPage(source, "authoring.md", new Set());
  assert.equal(pageExtension(source), ".md");
  assert.doesNotMatch(result, /import \{/);
  assert.ok(result.includes('```md\n<Aside type="note">'));
});

test("cards and tabs import vendored components and resolve bilingual card destinations", () => {
  const source = '# Routing\n\n<CardGrid>\n\n<LinkCard title="安装" href="../get-started/installation_CN.md#环境要求" description="依赖与构建。" />\n<LinkCard title="Source" href="../../include/arknet.hpp" />\n<LinkCard title="Nimbus" href="https://nimbus-docs.com/components/" />\n\n</CardGrid>\n\n<Tabs>\n\n<TabItem label="Boost">\n\n[Install](../get-started/installation.md)\n\n</TabItem>\n\n</Tabs>\n';
  const result = convertPage(source, "concepts/index.md", new Set(["get-started/installation.md", "get-started/installation_CN.md"]));
  assert.equal(pageExtension(source), ".mdx");
  assert.match(result, /import \{ CardGrid \} from "@\/components\/ui\/card-grid"/);
  assert.match(result, /import \{ LinkCard \} from "@\/components\/ui\/link-card"/);
  assert.match(result, /import \{ Tabs, TabItem \} from "@\/components\/ui\/tabs"/);
  assert.match(result, /href="\/zh\/get-started\/installation#%E7%8E%AF%E5%A2%83%E8%A6%81%E6%B1%82"/);
  assert.match(result, /href="https:\/\/github.com\/OpenArkStudio\/arknet\/blob\/main\/include\/arknet.hpp"/);
  assert.match(result, /href="https:\/\/nimbus-docs.com\/components\/"/);
  assert.match(result, /\[Install\]\(\/get-started\/installation\)/);
  assert.throws(() => convertPage('# Missing\n\n<LinkCard title="Missing" href="missing.md" />', "index.md", new Set()), /missing document/);
});

test("card and tab samples in code do not activate MDX or rewrite hrefs", () => {
  const source = '# Authoring\n\nUse `<TabItem label="A">`.\n\n```md\n<CardGrid>\n<LinkCard title="Example" href="missing.md" />\n</CardGrid>\n```\n';
  assert.equal(pageExtension(source), ".md");
  const result = convertPage(source, "authoring.md", new Set());
  assert.doesNotMatch(result, /import \{/);
  assert.ok(result.includes('href="missing.md"'));
});
