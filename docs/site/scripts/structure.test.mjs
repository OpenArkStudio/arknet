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

test("published summaries contain only the final UDP measurements", async () => {
  const load = async name => JSON.parse(await readFile(new URL(`../../performance/${name}`, import.meta.url), "utf8"));
  const summary = await load("summary.json");
  const udp = await load("udp-summary.json");
  const actual = summary.filter(row => row.protocol === "udp").map(({ source_sha256, ...row }) => row);
  const key = row => JSON.stringify([row.backend, row.implementation, row.payload_bytes, row.clients, row.window, row.io_model, row.io_threads, row.handler_work]);
  const sort = rows => rows.sort((a, b) => key(a).localeCompare(key(b)));
  assert.deepEqual(sort(actual), sort(udp));
  const dimensions = await load("dimensions-summary.json");
  const udpDimensions = await load("udp-dimensions-summary.json");
  assert.deepEqual(sort(dimensions.profiles.filter(row => row.protocol === "udp")), sort(udpDimensions.profiles));
  assert.deepEqual(sort(dimensions.failed_profiles.filter(row => row.protocol === "udp")), sort(udpDimensions.failed_profiles));
  assert.equal(dimensions.source_sha256_by_protocol.udp, udpDimensions.source_sha256);
});
