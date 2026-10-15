// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { mkdtemp, readFile, readdir, rm, stat, symlink, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";
import { test } from "node:test";

import { generate } from "./report_database_clients.mjs";

function metadata(protocol) {
  const sources = ["include/arknet/base/io_pool.hpp", "include/arknet/external/asio.hpp",
    "include/arknet/base/detail/deadline.hpp", `benchmarks/${protocol}.cpp`,
    `include/arknet/${protocol}/${protocol === "redis" ? "client" : "connection"}.hpp`];
  return {
    rounds: 3, expected_groups: { [protocol]: 1 }, warmup_seconds: 0.1, seconds: 1,
    environment: [{ en: "Machine", zh: "机器", value: "Synthetic <host> & test" }],
    topology: { en: "Synthetic test data only.", zh: "仅为合成测试数据。" },
    raw_data_archive: "https://example.com/benchmark-data.tar.gz",
    source_sha256: Object.fromEntries(sources.map(path => [path, "a".repeat(64)])),
  };
}

function row(protocol, round, throughput) {
  const shared = { protocol, tls: false, round, clients: 1, payload_bytes: 1024,
    process_peak_rss_bytes: round * 1048576 };
  return protocol === "redis" ? { ...shared, dependency: "Boost.Asio", api: "callback",
    io_model: "one_context_per_connection", io_threads: 1, pipeline_commands: 4,
    inflight_batches_per_connection: 1, elapsed_seconds: 1, successful_commands: throughput,
    failed_commands: 0, commands_per_second: throughput, batch_roundtrip_p50_us: round * 10,
    batch_roundtrip_p99_us: round * 20, client_cpu_percent_of_one_core: round * 50,
    latency_sample_count: 1000, ok: true } : { ...shared, dependency: "standalone", mode: "prepared",
    io_model: "one_context_thread_per_connection", execution: "callback", window: 4,
    wall_seconds: 1, successful_queries: throughput, failed_queries: 0, queries_per_second: throughput,
    query_p50_us: round * 10, query_p99_us: round * 20, client_cpu_percent: round * 50,
    latency_samples: 1000 };
}

async function fixture(protocol, callback) {
  const directory = await mkdtemp(join(tmpdir(), "arknet-report-test-"));
  try {
    const input = join(directory, "rounds.jsonl");
    const details = join(directory, "metadata.json");
    const output = join(directory, "output");
    const rows = [row(protocol, 1, 100), row(protocol, 2, 300), row(protocol, 3, 200)];
    await writeFile(input, rows.map(value => JSON.stringify(value)).join("\n") + "\n");
    await writeFile(details, JSON.stringify(metadata(protocol)));
    await callback({ directory, input, details, output, rows, run: () => generate([
      "--input", input, "--metadata", details, "--out-dir", output]) });
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
}

for (const protocol of ["redis", "mysql"]) {
  test(`${protocol} report preserves medians, round ranges, units and bilingual vector output`, async () => {
    await fixture(protocol, async ({ output, run }) => {
      await run();
      const assets = await readdir(join(output, "performance/assets"));
      assert.equal(assets.length, 10);
      assert.ok(assets.every(path => path.endsWith(".svg")));
      const throughput = await readFile(join(output, "performance/assets", protocol + "-throughput.svg"), "utf8");
      assert.match(throughput, /200 \[100–300\]/);
      assert.match(throughput, /<path d="M /);
      assert.match(throughput, /<svg xmlns="http:\/\/www.w3.org\/2000\/svg"/);
      assert.match(throughput, new RegExp(protocol === "redis" ? "Redis · Command throughput" : "MySQL · Query throughput"));
      assert.doesNotMatch(throughput, /NaN|Infinity|<image|<script/);
      const chinese = await readFile(join(output, "performance/assets", protocol + "-cpu_CN.svg"), "utf8");
      assert.match(chinese, /单核占用比例/);
      const page = await readFile(join(output, "features", protocol, "performance.md"), "utf8");
      assert.match(page, /Synthetic &lt;host&gt; &amp; test/);
      assert.match(page, /Raw dataset SHA-256.*[a-f0-9]{64}/);
      assert.match(page, /Download raw measurements and metadata.*https:\/\/example.com\/benchmark-data.tar.gz/);
      assert.match(page, /RSS is the process lifetime peak/);
      assert.match(page, protocol === "redis" ? /entire pipeline batch/ : /Query RT includes client queueing/);
      assert.match(page, /\| 200 \| 20 \| 40 \| 100 \| 2 \| 3 \|/);
    });
  });
}

test("CLI runs through an absolute symlink from another working directory", async () => {
  await fixture("mysql", async ({ directory, input, details, output }) => {
    const command = join(directory, "report-link.mjs");
    await symlink(fileURLToPath(new URL("./report_database_clients.mjs", import.meta.url)), command);
    const result = await promisify(execFile)(process.execPath, [command, "--input", input,
      "--metadata", details, "--out-dir", output], { cwd: directory });
    assert.match(result.stdout, /Generated 12 report files for 1 groups \/ 3 rounds/);
    assert.equal((await readdir(join(output, "performance/assets"))).length, 10);
  });
});

test("failed, incomplete and duplicate measurements produce no report directory", async () => {
  await fixture("redis", async ({ input, output, rows, run }) => {
    for (const bad of [rows.slice(0, 2), [...rows, rows[0]], rows.map((value, index) => index === 1
      ? { ...value, ok: false, failed_commands: 1 } : value)]) {
      await writeFile(input, bad.map(value => JSON.stringify(value)).join("\n"));
      await assert.rejects(run);
      await assert.rejects(stat(output), { code: "ENOENT" });
    }
  });
});

test("duplicate dimensions and rounds across files are rejected", async () => {
  await fixture("redis", async ({ directory, details, output, rows }) => {
    await writeFile(join(directory, "copied-round.jsonl"), JSON.stringify(rows[0]) + "\n");
    await assert.rejects(() => generate(["--input", directory, "--metadata", details,
      "--out-dir", output]), /Duplicate round 1/);
    await assert.rejects(stat(output), { code: "ENOENT" });
  });
});

test("each group must contain exactly rounds 1 through metadata rounds", async () => {
  await fixture("mysql", async ({ input, output, rows, run }) => {
    for (const bad of [rows.map(value => ({ ...value, round: value.round + 1 })),
      [...rows, row("mysql", 4, 100)]]) {
      await writeFile(input, bad.map(value => JSON.stringify(value)).join("\n"));
      await assert.rejects(run, /round sequence|Incomplete mysql group/);
      await assert.rejects(stat(output), { code: "ENOENT" });
    }
  });
});

test("matrix completeness, source provenance and IO-model mismatches fail before output", async () => {
  await fixture("redis", async ({ details, input, output, rows, run }) => {
    const incomplete = metadata("redis");
    incomplete.expected_groups.redis = 2;
    await writeFile(details, JSON.stringify(incomplete));
    await assert.rejects(run, /Incomplete redis matrix/);
    const missing_source = metadata("redis");
    delete missing_source.source_sha256["include/arknet/base/detail/deadline.hpp"];
    await writeFile(details, JSON.stringify(missing_source));
    await assert.rejects(run, /Missing measured source SHA/);
    await writeFile(details, JSON.stringify(metadata("redis")));
    const invalid_archive = { ...metadata("redis"), raw_data_archive: "file:///tmp/benchmark-data.tar.gz" };
    await writeFile(details, JSON.stringify(invalid_archive));
    await assert.rejects(run, /Raw data archive must use HTTPS/);
    await writeFile(details, JSON.stringify(metadata("redis")));
    await writeFile(input, rows.map(value => JSON.stringify({ ...value, io_threads: 4 })).join("\n"));
    await assert.rejects(run, /unexpected IO thread count/);
    await assert.rejects(stat(output), { code: "ENOENT" });
  });
});

test("inconsistent metrics and throughput accounting are rejected", async () => {
  await fixture("mysql", async ({ input, rows, run }) => {
    await writeFile(input, rows.map((value, index) => JSON.stringify(index === 1
      ? { ...value, queries_per_second: 900 } : value)).join("\n"));
    await assert.rejects(run, /throughput does not match/);
    await writeFile(input, rows.map((value, index) => JSON.stringify(index === 1
      ? { ...value, client_cpu_percent: null } : value)).join("\n"));
    await assert.rejects(run, /Inconsistent cpu availability/);
  });
});

test("multi-dimensional charts retain every dependency, transport, payload, window and batch group", async () => {
  await fixture("redis", async ({ details, input, output, run }) => {
    const rows = [];
    for (const dependency of ["Boost.Asio", "Standalone Asio"]) {
      for (const tls of [false, true]) {
        for (const payload_bytes of [1024, 4096]) {
          for (const window of [1, 4]) {
            for (const pipeline of [1, 4]) {
              for (let round = 1; round <= 3; ++round) {
                rows.push({ ...row("redis", round, round * 100), dependency, tls, payload_bytes,
                  inflight_batches_per_connection: window, pipeline_commands: pipeline });
              }
            }
          }
        }
      }
    }
    const options = metadata("redis");
    options.expected_groups.redis = 32;
    await writeFile(details, JSON.stringify(options));
    await writeFile(input, rows.map(value => JSON.stringify(value)).join("\n"));
    await run();
    const svg = await readFile(join(output, "performance/assets/redis-throughput.svg"), "utf8");
    assert.equal((svg.match(/data-series=/g) ?? []).length, 32);
    assert.equal((svg.match(/<path d="M /g) ?? []).length, 32);
    for (const name of ["Boost.Asio · TCP", "Boost.Asio · TLS", "Standalone Asio · TCP", "Standalone Asio · TLS"]) {
      assert.match(svg, new RegExp(name.replaceAll(".", "\\.")));
    }
    assert.match(svg, /4 KiB · 1 connection · 4 batches\/connection/);
    assert.doesNotMatch(svg, /NaN|Infinity/);
  });
});
