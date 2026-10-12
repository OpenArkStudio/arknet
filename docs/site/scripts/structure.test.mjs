import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
const routes = JSON.parse(await readFile(new URL("../src/legacy-routes.json", import.meta.url), "utf8"));

test("legacy routes have matching English and Chinese destinations", () => {
  for (const [from, redirect] of Object.entries(routes)) {
    assert.match(redirect.to, /^\/(?:zh\/)?(?:get-started|concepts|features|guides|reference|project)\//);
    assert.notEqual(from, redirect.to);
    if (from.startsWith("/zh/")) continue;
    const translated = routes["/zh" + from];
    assert.equal(translated?.to, "/zh" + redirect.to, from);
    assert.equal(Object.keys(translated.anchors).length, Object.keys(redirect.anchors).length, from);
  }
});

test("split legacy pages retain deep links to the right content", () => {
  assert.equal(routes["/tcp/usage"].anchors["echo-client"], "/features/tcp/client");
  assert.equal(routes["/zh/runtime"].anchors["发送和数据所有权"], "/zh/concepts/sending");
  assert.equal(routes["/testing"].anchors["metrics-and-charts"], "/performance/methodology");
});
