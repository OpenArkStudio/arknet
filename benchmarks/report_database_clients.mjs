#!/usr/bin/env node
// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
import { createHash } from "node:crypto";
import { mkdir, readFile, readdir, realpath, stat, writeFile } from "node:fs/promises";
import { basename, dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const metrics = [
  ["throughput", "Throughput", "吞吐量", "operations/s", "操作数／秒", 1],
  ["p50", "End-to-end RT p50", "端到端 RT p50", "µs", "微秒", 1],
  ["p99", "End-to-end RT p99", "端到端 RT p99", "µs", "微秒", 1],
  ["cpu", "Client CPU", "客户端 CPU", "% of one core", "单核占用比例（%）", 1],
  ["rss", "Client process peak RSS", "客户端进程内存峰值", "MiB", "MiB", 1024 * 1024],
];
const palette = ["#1261a0", "#d15a12", "#2f7d58", "#9055a2", "#455b73", "#ad3670", "#8b720c", "#137f86"];

function hash(bytes) {
  return createHash("sha256").update(bytes).digest("hex");
}

function xml(value) {
  return String(value).replace(/[\u0000-\u0008\u000b\u000c\u000e-\u001f]/g, "")
    .replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;").replaceAll("'", "&apos;");
}

function markdown(value) {
  return String(value).replaceAll("&", "&amp;").replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;").replaceAll("|", "&#124;").replaceAll("\n", " ");
}

function number(value, digits = 2) {
  return value == null ? "N/A" : new Intl.NumberFormat("en-US", { maximumFractionDigits: digits }).format(value);
}

function finite(value, name, minimum = 0, nullable = false) {
  if (nullable && value === null) return null;
  if (!Number.isFinite(value) || value < minimum) throw new Error(`Invalid ${name}`);
  return value;
}

function integer(value, name, minimum = 1) {
  if (!Number.isSafeInteger(value) || value < minimum) throw new Error(`Invalid ${name}`);
  return value;
}

function normalize(row, source, line) {
  if (!row || (row.protocol !== "redis" && row.protocol !== "mysql")) throw new Error(`${source}:${line}: unsupported protocol`);
  const redis = row.protocol === "redis";
  const successes = integer(redis ? row.successful_commands : row.successful_queries, "success count");
  const failures = integer(redis ? row.failed_commands : row.failed_queries, "failure count", 0);
  if ((redis && row.ok !== true) || row.ok === false || failures) throw new Error(`${source}:${line}: failed measurement`);
  const dependency = row.dependency === "boost" ? "Boost.Asio" : row.dependency === "standalone" ? "Standalone Asio" : row.dependency;
  if (dependency !== "Boost.Asio" && dependency !== "Standalone Asio") throw new Error(`${source}:${line}: unknown dependency`);
  if (typeof row.tls !== "boolean") throw new Error(`${source}:${line}: missing TLS mode`);
  if (!redis && row.mode !== "text" && row.mode !== "prepared") throw new Error(`${source}:${line}: unknown MySQL mode`);
  if ((redis ? row.api : row.execution) !== "callback" ||
      row.io_model !== (redis ? "one_context_per_connection" : "one_context_thread_per_connection")) {
    throw new Error(`${source}:${line}: report supports callback / one-context-thread-per-connection workloads`);
  }
  const result = {
    protocol: row.protocol, dependency, tls: row.tls, mode: redis ? "callback" : row.mode,
    clients: integer(row.clients, "clients"), round: integer(row.round, "round"),
    payload: integer(row.payload_bytes, "payload", 0),
    window: integer(redis ? row.inflight_batches_per_connection : row.window, "window"),
    pipeline: redis ? integer(row.pipeline_commands, "pipeline") : 1,
    io_threads: integer(row.io_threads ?? row.clients, "IO threads"),
    elapsed: finite(redis ? row.elapsed_seconds : row.wall_seconds, "elapsed", Number.MIN_VALUE),
    samples: integer(redis ? row.latency_sample_count : row.latency_samples, "latency samples"),
    throughput: finite(redis ? row.commands_per_second : row.queries_per_second, "throughput", Number.MIN_VALUE),
    p50: finite(redis ? row.batch_roundtrip_p50_us : row.query_p50_us, "p50"),
    p99: finite(redis ? row.batch_roundtrip_p99_us : row.query_p99_us, "p99"),
    cpu: finite(redis ? row.client_cpu_percent_of_one_core : row.client_cpu_percent, "CPU", 0, true),
    rss: finite(row.process_peak_rss_bytes, "RSS", 0, true), successes, source, line,
  };
  if (result.io_threads !== result.clients) throw new Error(`${source}:${line}: unexpected IO thread count`);
  if (result.p99 < result.p50) throw new Error(`${source}:${line}: p99 is below p50`);
  if (Math.abs(result.throughput - successes / result.elapsed) > Math.max(1, result.throughput * 0.01)) {
    throw new Error(`${source}:${line}: throughput does not match successful count / elapsed time`);
  }
  return result;
}

function statistics(values) {
  const sorted = values.filter(value => value !== null).sort((left, right) => left - right);
  if (!sorted.length) return { minimum: null, median: null, maximum: null };
  const middle = Math.floor(sorted.length / 2);
  return {
    minimum: sorted[0], maximum: sorted.at(-1),
    median: sorted.length % 2 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2,
  };
}

function summarize(rows, metadata) {
  const grouped = new Map();
  const seen = new Set();
  for (const row of rows) {
    const key = JSON.stringify([row.protocol, row.dependency, row.tls, row.mode, row.clients,
      row.io_threads, row.payload, row.window, row.pipeline]);
    const identity = key + JSON.stringify(row.round);
    if (seen.has(identity)) throw new Error(`Duplicate round ${row.round} in ${row.source}`);
    seen.add(identity);
    if (!grouped.has(key)) grouped.set(key, []);
    grouped.get(key).push(row);
  }
  const groups = [...grouped.values()].map(rounds => {
    const first = rounds[0];
    const required = integer(metadata.rounds, "metadata rounds", 3);
    if (rounds.length !== required) throw new Error(`Incomplete ${first.protocol} group: ${rounds.length}/${required} rounds`);
    if (rounds.some(row => row.round > required)) {
      throw new Error(`Invalid ${first.protocol} round sequence: expected 1..${required}`);
    }
    for (const key of ["cpu", "rss"]) {
      const available = rounds.filter(row => row[key] !== null).length;
      if (available && available !== rounds.length) throw new Error(`Inconsistent ${key} availability across rounds`);
    }
    return { ...first, rounds: rounds.length, samples: Math.min(...rounds.map(row => row.samples)),
      ...Object.fromEntries(metrics.map(([key]) => [key, statistics(rounds.map(row => row[key]))])) };
  });
  for (const protocol of new Set(rows.map(row => row.protocol))) {
    const expected = integer(metadata.expected_groups?.[protocol], `expected ${protocol} group count`);
    if (groups.filter(group => group.protocol === protocol).length !== expected) throw new Error(`Incomplete ${protocol} matrix: expected ${expected} groups`);
    for (const path of ["include/arknet/base/io_pool.hpp", "include/arknet/external/asio.hpp",
      "include/arknet/base/detail/deadline.hpp", `benchmarks/${protocol}.cpp`,
      `include/arknet/${protocol}/${protocol === "redis" ? "client" : "connection"}.hpp`]) {
      if (!metadata.source_sha256[path]) throw new Error(`Missing measured source SHA-256: ${path}`);
    }
  }
  return groups.sort((left, right) => left.payload - right.payload || left.clients - right.clients ||
    left.window - right.window || left.pipeline - right.pipeline || left.dependency.localeCompare(right.dependency) ||
    Number(left.tls) - Number(right.tls) || left.mode.localeCompare(right.mode));
}

function nice_maximum(value) {
  if (!(value > 0)) return 1;
  const magnitude = 10 ** Math.floor(Math.log10(value));
  return Math.ceil(value / magnitude / 0.5) * 0.5 * magnitude;
}

function series_name(group, chinese = false) {
  const mode = chinese ? (group.mode === "prepared" ? "预处理" : "文本") : group.mode;
  return `${group.dependency} · ${group.tls ? "TLS" : "TCP"}${group.protocol === "mysql" ? " · " + mode : ""}`;
}

function metric_title(protocol, key, chinese, fallback) {
  if (key === "throughput") return protocol === "redis"
    ? (chinese ? "命令吞吐量" : "Command throughput") : (chinese ? "查询吞吐量" : "Query throughput");
  if (key === "p50" || key === "p99") return protocol === "redis"
    ? (chinese ? `整批 RT ${key}` : `Pipeline batch RT ${key}`)
    : (chinese ? `查询 RT ${key}` : `Query RT ${key}`);
  return fallback;
}

function figure(groups, metric, chinese) {
  const [key, english, translated, english_unit, translated_unit, divisor] = metric;
  const protocol = groups[0].protocol;
  const protocol_name = protocol === "redis" ? "Redis" : "MySQL";
  const title = metric_title(protocol, key, chinese, chinese ? translated : english);
  const unit = key === "throughput"
    ? (protocol === "redis" ? (chinese ? "命令数／秒" : "commands/s") : (chinese ? "查询数／秒" : "queries/s"))
    : (chinese ? translated_unit : english_unit);
  const series = [...new Set(groups.map(group => series_name(group, chinese)))].sort();
  const facets = new Map();
  for (const group of groups) {
    const identity = JSON.stringify([group.clients, group.payload, protocol === "redis" ? group.window : null]);
    if (!facets.has(identity)) facets.set(identity, []);
    facets.get(identity).push(group);
  }
  if (facets.size > 24 || series.length > palette.length) throw new Error("Too many chart panels or series; split the input report");
  const columns = Math.min(2, facets.size);
  const panel_width = 540;
  const panel_height = 320;
  const header_height = 100 + Math.ceil(series.length / 2) * 24;
  const width = columns * panel_width + 32;
  const height = header_height + Math.ceil(facets.size / columns) * panel_height + 32;
  const title_id = `${protocol}-${key}-${chinese ? "zh" : "en"}`;
  const parts = [`<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}" role="img" aria-labelledby="${title_id}">`,
    `<title id="${title_id}">${xml(protocol_name + " · " + title)}</title>`,
    `<desc>${xml(chinese ? "柱为各轮中位数，误差线为最小值到最大值。" : "Bars show round medians; whiskers show minimum to maximum.")}</desc>`,
    `<rect width="100%" height="100%" fill="#fff"/>`,
    `<g font-family="system-ui,-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif" fill="#162538">`,
    `<text x="28" y="32" font-size="21" font-weight="650">${xml(protocol_name + " · " + title)}</text>`,
    `<text x="28" y="58" font-size="12">${xml(chinese ? "中位数；误差线：各轮最小值—最大值" : "Median; whiskers: round minimum–maximum")}</text>`];
  series.forEach((name, index) => {
    const x = 28 + (index % 2) * panel_width;
    const y = 83 + Math.floor(index / 2) * 24;
    parts.push(`<rect x="${x}" y="${y - 10}" width="13" height="13" fill="${palette[index]}"/>`,
      `<text x="${x + 20}" y="${y}" font-size="12">${xml(name)}</text>`);
  });
  [...facets.values()].forEach((facet, panel) => {
    const left = 74 + (panel % columns) * panel_width;
    const top = header_height + Math.floor(panel / columns) * panel_height + 38;
    const chart_width = 430;
    const chart_height = 210;
    const values = [...new Set(facet.map(group => protocol === "redis" ? group.pipeline : group.window))].sort((a, b) => a - b);
    const maximum = nice_maximum(Math.max(0, ...facet.map(group => (group[key].maximum ?? 0) / divisor)) * 1.08);
    const first = facet[0];
    const payload_label = first.payload < 1024 ? first.payload + " B" : number(first.payload / 1024) + " KiB";
    const label = chinese
      ? `${payload_label} · ${first.clients} 条连接${protocol === "redis" ? " · 在途 " + first.window + " 批／连接" : ""}`
      : `${payload_label} · ${first.clients} connection${first.clients === 1 ? "" : "s"}${protocol === "redis" ? " · " + first.window + (first.window === 1 ? " batch/connection" : " batches/connection") : ""}`;
    parts.push(`<text x="${left}" y="${top - 22}" font-size="13" font-weight="600">${xml(label)}</text>`,
      `<text x="${left}" y="${top - 4}" font-size="11">${xml(unit)}</text>`);
    for (let tick = 0; tick <= 4; ++tick) {
      const y = top + chart_height - tick * chart_height / 4;
      parts.push(`<line x1="${left}" x2="${left + chart_width}" y1="${y}" y2="${y}" stroke="#dce3ec"/>`,
        `<text x="${left - 8}" y="${y + 4}" font-size="10" text-anchor="end">${xml(number(maximum * tick / 4))}</text>`);
    }
    const slot = chart_width / values.length;
    const bar_width = Math.min(25, slot * 0.8 / series.length);
    values.forEach((value, value_index) => {
      const center = left + slot * (value_index + 0.5);
      parts.push(`<text x="${center}" y="${top + chart_height + 18}" text-anchor="middle" font-size="11">${value}</text>`);
      series.forEach((name, series_index) => {
        const group = facet.find(candidate => series_name(candidate, chinese) === name &&
          (protocol === "redis" ? candidate.pipeline : candidate.window) === value);
        if (!group) return;
        const stats = group[key];
        const x = center + (series_index - series.length / 2) * bar_width;
        if (stats.median === null) {
          parts.push(`<text x="${x + bar_width / 2}" y="${top + chart_height - 6}" text-anchor="middle" font-size="9">N/A</text>`);
          return;
        }
        const y = top + chart_height - stats.median / divisor / maximum * chart_height;
        const minimum_y = top + chart_height - stats.minimum / divisor / maximum * chart_height;
        const maximum_y = top + chart_height - stats.maximum / divisor / maximum * chart_height;
        const midpoint = x + bar_width * 0.43;
        parts.push(`<g><title>${xml(`${name}: ${number(stats.median / divisor)} [${number(stats.minimum / divisor)}–${number(stats.maximum / divisor)}] ${unit}`)}</title>`,
          `<rect data-series="${xml(name)}" x="${x}" y="${y}" width="${bar_width * 0.86}" height="${top + chart_height - y}" fill="${palette[series_index]}"/>`,
          `<path d="M ${midpoint} ${maximum_y} V ${minimum_y} M ${midpoint - 3} ${maximum_y} H ${midpoint + 3} M ${midpoint - 3} ${minimum_y} H ${midpoint + 3}" stroke="#17202a" fill="none" stroke-width="1.1"/></g>`);
      });
    });
    parts.push(`<text x="${left + chart_width / 2}" y="${top + chart_height + 44}" text-anchor="middle" font-size="12">${xml(protocol === "redis"
      ? (chinese ? "每批命令数" : "Commands per pipeline batch")
      : (chinese ? "每连接尚未完成的查询数" : "Outstanding queries per connection"))}</text>`);
  });
  parts.push("</g></svg>\n");
  return parts.join("\n");
}

function report(groups, metadata, dataset_hash, chinese) {
  const protocol = groups[0].protocol;
  const redis = protocol === "redis";
  const suffix = chinese ? "_CN" : "";
  const language = value => typeof value === "string" ? value : value?.[chinese ? "zh" : "en"];
  const lines = [`# ${redis ? "Redis" : "MySQL"} ${chinese ? "性能" : "Performance"}`, "",
    chinese ? "柱为各轮中位数，误差线表示最小值到最大值；图中分位数是每轮分位数的中位数。"
      : "Bars show round medians; whiskers show minimum–maximum. Latency summaries are medians of the per-round percentiles.", "",
    chinese ? "## 测试环境" : "## Test environment", "",
    `| ${chinese ? "项目 | 配置" : "Item | Configuration"} |`, "| --- | --- |"];
  for (const item of metadata.environment) lines.push(`| ${markdown(item[chinese ? "zh" : "en"])} | ${markdown(item.value)} |`);
  lines.push(`| ${chinese ? "预热／每轮目标时长／轮数" : "Warmup / target duration / rounds"} | ${metadata.warmup_seconds} s / ${metadata.seconds} s / ${metadata.rounds} |`,
    `| ${chinese ? "负载组合" : "Workload combinations"} | ${groups.length} |`, "", markdown(language(metadata.topology)), "",
    chinese ? "## 测量范围" : "## Measurement scope", "",
    redis
      ? (chinese ? "二进制 ECHO，逐条验证回复；回调接口，每条连接一个 IO context 和线程。RT 为包含客户端排队的整批流水线耗时，不是单条命令延迟。"
        : "Binary ECHO with every reply verified; callback API, one IO context/thread per connection. RT measures the entire pipeline batch including client queueing, not individual command latency.")
      : (chinese ? "只读文本／预处理 SELECT，验证二进制结果；回调接口，每条连接一个 IO context 和线程。查询 RT 包含客户端排队，同一条连接依次执行命令。"
        : "Read-only text/prepared SELECT with binary results verified; callback API, one IO context/thread per connection. Query RT includes client queueing; commands execute sequentially per connection."), "",
    chinese ? "CPU 为客户端进程占用比例，以一个核心 100% 为基准，可超过 100%，不包含数据库服务端。RSS 是进程启动以来的内存峰值，包含预热及之前轮次，不是本轮新增内存。"
      : "CPU is client process usage with one full core at 100%; it can exceed 100% and excludes the database server. RSS is the process lifetime peak, including warmup and earlier rounds, not newly allocated memory in that round.", "",
    chinese ? `每组至少保留 ${Math.min(...groups.map(group => group.samples))} 个延迟样本；有上限的采样用于估计分位数。本地结果不等同于生产部署容量。`
      : `Every group retains at least ${Math.min(...groups.map(group => group.samples))} latency samples per round. Bounded sampling estimates percentiles. Local results do not establish production deployment capacity.`, "");
  for (const [key, english, translated] of metrics) {
    const title = metric_title(protocol, key, chinese, chinese ? translated : english);
    lines.push(`## ${title}`, "",
      `![${title}](../../performance/assets/${protocol}-${key}${suffix}.svg)`, "");
  }
  lines.push(chinese ? "## 详细结果" : "## Detailed results", "", "<details>",
    `<summary>${chinese ? "展开中位数表" : "Expand median results"}</summary>`, "",
    `| ${chinese ? `依赖库／传输／模式 | 包体 B | 连接 | ${redis ? "每批命令／在途批次" : "每连接在途查询"} | ${redis ? "命令数" : "查询数"}／秒 | ${redis ? "整批" : "查询"} p50 µs | ${redis ? "整批" : "查询"} p99 µs | CPU % | RSS MiB | 轮数`
      : `Dependency / transport / mode | Payload B | Clients | ${redis ? "Commands/batch / batches in-flight" : "Queries in-flight/connection"} | ${redis ? "Commands" : "Queries"}/s | ${redis ? "Batch" : "Query"} p50 µs | ${redis ? "Batch" : "Query"} p99 µs | CPU % | RSS MiB | Rounds`} |`,
    "| --- | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: |");
  for (const group of groups) {
    lines.push(`| ${markdown(series_name(group, chinese))} | ${group.payload} | ${group.clients} | ${redis ? group.pipeline + " / " : ""}${group.window} | ${number(group.throughput.median)} | ${number(group.p50.median)} | ${number(group.p99.median)} | ${number(group.cpu.median)} | ${number(group.rss.median === null ? null : group.rss.median / 1048576)} | ${group.rounds} |`);
  }
  lines.push("", "</details>", "", chinese ? "## 复现与来源" : "## Reproduction and provenance", "",
    `[${chinese ? "编译并运行基准程序" : "Build and run the benchmark"}](../../../benchmarks/${protocol}${suffix}.md) · [${chinese ? "接口使用" : "API usage"}](usage${suffix}.md)`, "",
    ...(metadata.raw_data_archive ? [`[${chinese ? "下载原始测量数据与元数据" : "Download raw measurements and metadata"}](<${metadata.raw_data_archive}>)`, ""] : []),
    `${chinese ? "原始数据集合 SHA-256" : "Raw dataset SHA-256"}: \`${dataset_hash}\``, "",
    `| ${chinese ? "测量源码 | SHA-256" : "Measured source | SHA-256"} |`, "| --- | --- |");
  for (const [path, digest] of Object.entries(metadata.source_sha256).sort(([left], [right]) => left.localeCompare(right))) {
    lines.push(`| ${markdown(path)} | \`${digest}\` |`);
  }
  return lines.join("\n") + "\n";
}

function arguments_for(argv) {
  const options = {};
  for (let index = 0; index < argv.length; ++index) {
    const key = argv[index];
    if (key === "--help") return { help: true };
    if (!["--input", "--metadata", "--out-dir"].includes(key) || index + 1 === argv.length || options[key]) {
      throw new Error("Use --input FILE_OR_DIRECTORY --metadata FILE --out-dir DOCS_ROOT");
    }
    options[key] = resolve(argv[++index]);
  }
  if (!["--input", "--metadata", "--out-dir"].every(key => options[key])) throw new Error("All three options are required; use --help");
  return options;
}

export async function generate(argv) {
  const options = arguments_for(argv);
  if (options.help) {
    console.log(`Usage: node benchmarks/report_database_clients.mjs --input JSONL_FILE_OR_DIR --metadata FILE --out-dir DOCS_ROOT
Reads completed Redis/MySQL benchmark rounds. Requires at least three rounds/group and the expected full group count.
Writes only features/<protocol>/performance{,_CN}.md and performance/assets/<protocol>-<metric>{,_CN}.svg.
Raw inputs remain external. Node is only needed to generate reports, not to run compiled benchmarks.
Metadata JSON fields: rounds (>=3), expected_groups ({redis:96} or mysql count), warmup_seconds, seconds,
topology ({en,zh}), environment ([{en,zh,value}, ...] with machine/OS/compiler/server/dependency settings),
source_sha256 ({"benchmarks/redis.cpp":"64 hex characters", ...}; include runtime and client source hashes),
raw_data_archive (optional HTTPS download URL; the tool does not fetch it).`);
    return;
  }
  const metadata = JSON.parse(await readFile(options["--metadata"], "utf8"));
  integer(metadata.rounds, "metadata rounds", 3);
  finite(metadata.warmup_seconds, "metadata warmup");
  finite(metadata.seconds, "metadata duration", Number.MIN_VALUE);
  if (metadata.raw_data_archive !== undefined) {
    const archive = new URL(metadata.raw_data_archive);
    if (archive.protocol !== "https:" || archive.username || archive.password) {
      throw new Error("Raw data archive must use HTTPS without URL credentials");
    }
    metadata.raw_data_archive = archive.href;
  }
  if (!Array.isArray(metadata.environment) || !metadata.environment.length ||
      metadata.environment.some(item => !item.en || !item.zh || item.value == null) ||
      !metadata.topology?.en || !metadata.topology?.zh || !metadata.source_sha256 ||
      !Object.keys(metadata.source_sha256).length ||
      Object.entries(metadata.source_sha256).some(([path, digest]) => path.startsWith("/") || path.split("/").includes("..") || !/^[a-f0-9]{64}$/.test(digest))) {
    throw new Error("Metadata needs bilingual environment/topology and relative-path source SHA-256 entries");
  }
  const input = options["--input"];
  const directory = (await stat(input)).isDirectory();
  const files = directory
    ? (await readdir(input)).filter(name => name.endsWith(".jsonl")).sort().map(name => join(input, name))
    : [input];
  if (!files.length || files.length > 10000) throw new Error("Expected 1..10000 JSONL inputs");
  const rows = [];
  const manifest = [];
  let total_bytes = 0;
  for (const file of files) {
    const bytes = await readFile(file);
    total_bytes += bytes.length;
    if (total_bytes > 256 * 1024 * 1024) throw new Error("Input exceeds 256 MiB");
    const source = directory ? relative(input, file) : basename(file);
    manifest.push(source + " " + hash(bytes));
    const lines = bytes.toString("utf8").split(/\r?\n/);
    if (!lines.some(line => line.trim())) throw new Error(`Empty benchmark input: ${source}`);
    for (let index = 0; index < lines.length; ++index) {
      if (lines[index].trim()) rows.push(normalize(JSON.parse(lines[index]), source, index + 1));
    }
  }
  const groups = summarize(rows, metadata);
  const dataset_hash = hash(manifest.join("\n"));
  const outputs = [];
  for (const protocol of new Set(groups.map(group => group.protocol))) {
    const selected = groups.filter(group => group.protocol === protocol);
    for (const chinese of [false, true]) {
      const suffix = chinese ? "_CN" : "";
      for (const metric of metrics) outputs.push([`performance/assets/${protocol}-${metric[0]}${suffix}.svg`, figure(selected, metric, chinese)]);
      outputs.push([`features/${protocol}/performance${suffix}.md`, report(selected, metadata, dataset_hash, chinese)]);
    }
  }
  for (const [path, contents] of outputs) {
    const target = join(options["--out-dir"], path);
    await mkdir(dirname(target), { recursive: true });
    await writeFile(target, contents);
  }
  console.log(`Generated ${outputs.length} report files for ${groups.length} groups / ${rows.length} rounds; dataset ${dataset_hash}`);
}

const entrypoint = process.argv[1] ? await realpath(process.argv[1]).catch(() => null) : null;
if (entrypoint === await realpath(fileURLToPath(import.meta.url))) {
  generate(process.argv.slice(2)).catch(error => {
    console.error(`Database report: ${error.message}`);
    process.exitCode = 1;
  });
}
