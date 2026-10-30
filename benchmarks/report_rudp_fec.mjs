#!/usr/bin/env node
// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
// Offline report tool. The benchmark remains a standalone C++ executable.
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const BACKENDS = ["standalone", "boost"];
const POLICIES = ["off", "fixed", "adaptive"];
const NETWORKS = ["clean", "random", "burst"];
const MODELS = ["shared:1", "shared:4", "sharded:4"];
const COLORS = ["#68737d", "#458f82", "#206db0"];
const METRICS = ["roundtrips_per_second", "payload_roundtrip_mbit_per_second", "rtt_p50_us", "rtt_p99_us",
  "cpu_percent", "wire_tx_mbit_per_second", "wire_payload_overhead_percent", "relay_loss_percent"];
const COUNTERS = ["messages", "wire_bytes_tx", "wire_bytes_rx", "arq_transmissions", "arq_retransmissions",
  "fec_data_packets_tx", "fec_parity_packets_tx", "fec_parity_wire_bytes_tx", "fec_missing_shards",
  "fec_recovered_shards", "fec_duplicates", "fec_expired_groups", "fec_evicted_groups", "fec_policy_changes",
  "fec_current_parity_min", "fec_current_parity_max", "fec_observed_parity_max", "fec_retained_sampled_peak_bytes",
  "fec_groups_sampled_peak", "arq_pending_sampled_peak_bytes", "raw_queue_sampled_peak_bytes",
  "relay_received_packets", "relay_received_bytes", "relay_forwarded_packets", "relay_forwarded_bytes",
  "relay_random_drops", "relay_burst_drops", "relay_duplicate_packets", "relay_reordered_packets",
  "relay_sum_route_peak_bytes", "relay_sum_route_peak_packets"];
const ERRORS = ["failures", "content_errors", "order_errors", "unexpected_messages", "unfinished_messages",
  "send_rejections", "send_completion_errors", "disconnect_errors", "drain_timeouts", "callback_exceptions",
  "duplicate_completions", "pending_completions", "warmup_failures", "relay_overflow_drops", "relay_errors",
  "fec_invalid_packets"];
const SETTINGS = ["arq_send_window_segments", "arq_receive_window_segments", "arq_update_interval_ms",
  "udp_wire_mtu_bytes", "fec_data_shards", "fec_fixed_parity_shards", "fec_max_parity_shards",
  "fec_max_redundancy_percent", "fec_group_wait_ms", "fec_retention_limit_bytes_per_endpoint",
  "relay_queue_limit_bytes_per_route", "relay_queue_limit_packets_per_route", "statistics_sample_interval_ms"];

function check(condition, message) {
  if (!condition) throw new Error(message);
}
function integer(value, minimum = 0) {
  return Number.isSafeInteger(value) && value >= minimum;
}
function finite(value, minimum = 0) {
  return typeof value === "number" && Number.isFinite(value) && value >= minimum;
}
function close(actual, expected, name) {
  check(finite(actual, -Infinity) && Math.abs(actual - expected) <= 1e-6 + Math.abs(expected) * 1e-8,
    "inconsistent " + name);
}
function sha256(bytes) {
  return createHash("sha256").update(bytes).digest("hex");
}
function readJSON(path) {
  const value = JSON.parse(readFileSync(path, "utf8"));
  check(value !== null && typeof value === "object" && !Array.isArray(value), path + ": JSON object required");
  return value;
}
function labelBackend(backend) {
  return backend === "standalone" ? "Standalone Asio" : "Boost.Asio";
}
function labelPolicy(policy, zh = false) {
  return ({ off: zh ? "仅 ARQ" : "ARQ only", fixed: zh ? "固定 FEC" : "Fixed FEC",
    adaptive: zh ? "自适应 FEC" : "Adaptive FEC" })[policy];
}
function labelNetwork(profile, zh = false) {
  return ({ clean: zh ? "无主动丢包" : "No injected loss", random: zh ? "随机丢包" : "Random loss",
    burst: zh ? "连续丢包" : "Burst loss" })[profile];
}
function labelModel(model, threads, zh = false) {
  const contexts = model === "shared" ? 1 : threads;
  return zh ? contexts + " 个 io_context / " + threads + " 个线程" :
    contexts + " io_context" + (contexts > 1 ? "s" : "") + " / " + threads + " thread" + (threads > 1 ? "s" : "");
}
function format(value, digits = 1) {
  check(finite(value, -Infinity), "finite formatted value required");
  return value.toLocaleString("en-US", { minimumFractionDigits: digits, maximumFractionDigits: digits });
}
export function statistics(values) {
  check(values.length > 0 && values.every(value => finite(value, -Infinity)), "finite nonempty samples required");
  const sorted = [...values].sort((a, b) => a - b);
  const middle = Math.floor(sorted.length / 2);
  return { median: sorted.length % 2 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2,
    min: sorted[0], max: sorted.at(-1) };
}

export function expectedCases() {
  const cases = [];
  for (const backend of BACKENDS) {
    const add = (group, profile, policy, model, payload, window) => {
      const [io_model, count] = model.split(":");
      const name = group === "soak" ? "soak-" + policy + "-shared4" :
        profile + "-" + policy + "-" + io_model + count + "-p" + payload + "-w" + window;
      cases.push({ backend, group, profile, policy, io_model, io_threads: Number(count), payload, window, name });
    };
    for (const network of NETWORKS) for (const policy of POLICIES) for (const model of MODELS)
      add("main", network, policy, model, 1024, 16);
    for (const payload of [64, 4096, 16384]) for (const window of [1, 64]) for (const policy of POLICIES)
      add("dimensions", "random", policy, "shared:1", payload, window);
    for (const policy of POLICIES) add("soak", "burst", policy, "shared:4", 4096, 16);
  }
  return cases;
}

function validateMetadata(meta) {
  check(meta.complete === true && meta.schema_version === 1, "complete metadata required");
  check(/^[0-9a-f]{64}$/.test(meta.source_sha256), "source SHA-256 required");
  for (const backend of BACKENDS)
    check(/^[0-9a-f]{64}$/.test(meta.backends?.[backend]?.binary_sha256), backend + ": binary SHA-256 required");
  for (const field of ["started_at_utc", "finished_at_utc"])
    check(typeof meta[field] === "string" && Number.isFinite(Date.parse(meta[field])), field + ": UTC timestamp required");
  check(Date.parse(meta.finished_at_utc) >= Date.parse(meta.started_at_utc), "invalid collection interval");
  const settings = meta.settings;
  check(settings?.seconds === 3 && settings.warmup === 2 && settings.rounds === 3 && settings.clients === 4 &&
    settings.seed === 42 && settings.soak_seconds === 120, "metadata differs from the formal measurement plan");
  check(integer(meta.host?.logical_cpus, 1) && integer(meta.host?.memory_bytes, 1), "recorded host resources required");
  for (const field of ["model", "cpu", "system", "architecture"])
    check(typeof meta.host[field] === "string" && meta.host[field].length > 0, "recorded host " + field + " required");
  if (["Darwin", "macOS"].includes(meta.host.system))
    for (const field of ["os_version", "os_build"])
      check(typeof meta.host[field] === "string" && meta.host[field].length > 0,
        "recorded macOS " + field + " required");
  check(typeof meta.compiler_version === "string" && meta.compiler_version.length > 0 &&
    typeof meta.compiler_flags === "string" && meta.compiler_flags.includes("-O3") &&
    !/-fsanitize(?:=|\b)/.test(meta.compiler_flags), "Release compiler metadata required");
  check(typeof meta.cmake_version === "string" && meta.cmake_version.length > 0 &&
    typeof meta.dependencies?.openssl === "string" && meta.dependencies.openssl.length > 0,
    "CMake and OpenSSL version metadata required");
  for (const backend of BACKENDS)
    check(typeof meta.dependencies?.[backend] === "string" && meta.dependencies[backend].length > 0,
      backend + ": dependency version required");
  check(typeof meta.raw_repository_path === "string" && meta.raw_repository_path.startsWith("benchmarks/results/") &&
    !meta.raw_repository_path.split("/").includes(".."), "raw repository path required");
}

export function validateRun(value, expected, settings) {
  check(value.schema_version === 1 && value.ok === true && value.protocol === "rudp" &&
    value.wire_protocol === "kcp" && value.api === "callback", "successful callback RUDP measurement required");
  const rounds = expected.group === "soak" ? 1 : settings.rounds;
  const seconds = expected.group === "soak" ? settings.soak_seconds : settings.seconds;
  const same = { backend: expected.backend, payload_bytes: expected.payload, window: expected.window,
    io_model: expected.io_model, io_threads: expected.io_threads, fec_policy: expected.policy,
    impairment_profile: expected.profile, clients: settings.clients, seed: settings.seed,
    round_count: rounds, requested_seconds: seconds, warmup_seconds: settings.warmup,
    io_contexts: expected.io_model === "shared" ? 1 : expected.io_threads,
    relay_io_contexts: 1, relay_io_threads: 1, statistics_sample_interval_ms: 100, cpu_percent_per_logical_core: 100,
    fec_envelope: expected.policy === "off" ? "none" : "afec-v1" };
  for (const [field, wanted] of Object.entries(same)) check(value[field] === wanted, "unexpected " + field);
  const network = expected.profile === "clean" ? [0, 0, 0, 0, 0] :
    expected.profile === "random" ? [5, 0, 10, 1, 5] :
      [expected.group === "soak" ? 2 : 0, 3, 10, 1, 5];
  const networkFields = ["loss_percent", "burst_count", "one_way_delay_ms", "duplicate_percent", "reorder_percent"];
  networkFields.forEach((field, index) => check(value[field] === network[index], "unexpected " + field));
  check(value.burst_period_packets === 100 && value.reorder_extra_delay_ms === value.one_way_delay_ms + 5,
    "unexpected burst or reordering configuration");
  check(value.cpu_scope === "process: clients, server, relay, statistics sampling" &&
    value.wire_bytes_scope === "endpoint UDP payloads; excludes IP and UDP headers" &&
    value.protocol_statistics_scope === "post-warmup baseline through drained final snapshot",
    "recorded metric scope required");
  check(value.arq_congestion_control === true, "recorded ARQ congestion control required");
  for (const field of SETTINGS) check(integer(value[field], 1), "invalid " + field);
  check(value.arq_mtu_bytes === value.udp_wire_mtu_bytes - (expected.policy === "off" ? 0 : 26),
    "inconsistent ARQ/FEC MTU");
  check(integer(value.peak_rss_bytes, 1) && integer(value.udp_receive_buffer_requested_bytes, 1),
    "RSS and requested socket buffer required");
  check(Array.isArray(value.rounds) && value.rounds.length === rounds, "incomplete round set");
  check(value.summary?.successful_rounds === rounds && value.summary.failures === 0, "failed round summary");
  for (const [index, round] of value.rounds.entries()) {
    check(round.round === index + 1 && round.seed === settings.seed + index && round.ok === true && round.error === null,
      "invalid round identity or status");
    for (const field of ERRORS) check(round[field] === 0, "nonzero " + field);
    for (const field of COUNTERS) check(integer(round[field]), "invalid " + field);
    for (const field of METRICS) check(finite(round[field]), "invalid " + field);
    check(round.messages > 0 && round.messages_sent === round.messages && round.server_received_messages === round.messages,
      "message accounting differs");
    check(finite(round.elapsed_seconds, seconds) && integer(round.rtt_samples, 1) &&
      round.rtt_samples <= 8192 && round.rtt_samples <= round.messages && round.rtt_observations === round.messages &&
      round.rtt_p50_us <= round.rtt_p99_us, "invalid duration or RTT samples");
    for (const field of ["udp_server_receive_buffer_bytes", "udp_client_receive_buffer_min_bytes",
      "udp_client_receive_buffer_max_bytes"]) check(integer(round[field], 1), "invalid socket buffer");
    check(round.udp_client_receive_buffer_max_bytes >= round.udp_client_receive_buffer_min_bytes, "invalid socket buffer range");
    const payload = round.messages * value.payload_bytes * 2;
    close(round.roundtrips_per_second, round.messages / round.elapsed_seconds, "throughput");
    close(round.payload_roundtrip_mbit_per_second, payload * 8 / round.elapsed_seconds / 1e6, "payload bandwidth");
    close(round.wire_tx_mbit_per_second, round.wire_bytes_tx * 8 / round.elapsed_seconds / 1e6, "UDP bandwidth");
    close(round.wire_payload_overhead_percent, (round.wire_bytes_tx / payload - 1) * 100, "wire overhead");
    const drops = round.relay_random_drops + round.relay_burst_drops;
    check(drops <= round.relay_received_packets, "relay drops exceed input");
    for (const [configured, observed] of [["loss_percent", "relay_random_drops"], ["burst_count", "relay_burst_drops"],
      ["duplicate_percent", "relay_duplicate_packets"], ["reorder_percent", "relay_reordered_packets"]])
      if (value[configured] === 0) check(round[observed] === 0, "unexpected injected " + observed);
    close(round.relay_loss_percent, round.relay_received_packets ? drops / round.relay_received_packets * 100 : 0,
      "relay loss percentage");
    check(round.arq_retransmissions <= round.arq_transmissions && round.fec_recovered_shards <= round.fec_missing_shards &&
      round.fec_parity_wire_bytes_tx <= round.wire_bytes_tx, "inconsistent protocol counters");
    check(round.fec_current_parity_min <= round.fec_current_parity_max &&
      round.fec_current_parity_max <= value.fec_max_parity_shards &&
      round.fec_observed_parity_max <= value.fec_max_parity_shards, "invalid FEC parity range");
    check(round.fec_retained_sampled_peak_bytes <= 2 * value.clients * value.fec_retention_limit_bytes_per_endpoint &&
      round.relay_sum_route_peak_bytes <= value.clients * value.relay_queue_limit_bytes_per_route &&
      round.relay_sum_route_peak_packets <= value.clients * value.relay_queue_limit_packets_per_route,
      "retention exceeds configured aggregate bounds");
    if (expected.policy === "off")
      for (const field of COUNTERS.filter(field => field.startsWith("fec_")))
        check(round[field] === 0, "FEC activity recorded for ARQ-only policy");
  }
  const mappings = { roundtrips_per_second: "roundtrips_per_second", rtt_p50_us: "rtt_p50_us",
    rtt_p99_us: "rtt_p99_us", cpu_percent: "cpu_percent", wire_tx_mbit_per_second: "wire_tx_mbit_per_second",
    wire_payload_overhead_percent: "wire_payload_overhead_percent", payload_roundtrip_mbit_per_second: "payload_roundtrip_mbit_per_second" };
  for (const [field, summary] of Object.entries(mappings))
    close(value.summary[summary + "_median"], statistics(value.rounds.map(round => round[field])).median, field + " median");
  for (const bound of ["min", "max"])
    close(value.summary["roundtrips_per_second_" + bound],
      statistics(value.rounds.map(round => round.roundtrips_per_second))[bound], "throughput " + bound);
}

export function loadResults(directory, metadataPath = join(directory, "metadata.json")) {
  const metadata = readJSON(metadataPath);
  validateMetadata(metadata);
  const expected = expectedCases();
  for (const backend of BACKENDS) {
    const names = readdirSync(join(directory, backend)).filter(name => name.endsWith(".json")).sort();
    const wanted = expected.filter(item => item.backend === backend).map(item => item.name + ".json").sort();
    check(JSON.stringify(names) === JSON.stringify(wanted), backend + ": incomplete or unexpected matrix");
  }
  const rows = [];
  for (const entry of expected) {
    const base = join(directory, entry.backend, entry.name);
    const bytes = readFileSync(base + ".json");
    const value = readJSON(base + ".json");
    check(readFileSync(base + ".exit", "utf8").trim() === "0", entry.name + ": failed process exit");
    const command = readFileSync(base + ".command.sh", "utf8").trim();
    check(command.length > 0 && command.includes("arknet_rudp_benchmark"), entry.name + ": benchmark command required");
    const stderr = readFileSync(base + ".stderr", "utf8");
    try { validateRun(value, entry, metadata.settings); }
    catch (error) { throw new Error(entry.backend + "/" + entry.name + ": " + error.message); }
    const row = { ...entry, raw_file: entry.backend + "/" + entry.name + ".json",
      raw_sha256: sha256(bytes), command, stderr, rounds: value.rounds.length,
      total_messages: value.rounds.reduce((sum, round) => sum + round.messages, 0), peak_rss_bytes: value.peak_rss_bytes,
      protocol_settings: Object.fromEntries(SETTINGS.map(field => [field, value[field]])),
      udp_receive_buffer_requested_bytes: value.udp_receive_buffer_requested_bytes };
    for (const field of [...METRICS, ...COUNTERS]) row[field] = statistics(value.rounds.map(round => round[field]));
    for (const field of ["udp_server_receive_buffer_bytes", "udp_client_receive_buffer_min_bytes", "udp_client_receive_buffer_max_bytes"])
      row[field] = statistics(value.rounds.map(round => round[field]));
    for (const field of ["arq_retransmissions", "fec_recovered_shards"])
      row[field + "_per_1000_roundtrips"] = statistics(value.rounds.map(round => round[field] / round.messages * 1000));
    row.cpu_machine_percent = statistics(value.rounds.map(round => round.cpu_percent / metadata.host.logical_cpus));
    rows.push(row);
  }
  check(new Set(rows.map(row => JSON.stringify(row.protocol_settings))).size === 1, "protocol settings differ between cases");
  return { metadata, rows };
}

function escapeXML(text) {
  return String(text).replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;").replaceAll("'", "&apos;");
}
function text(x, y, label, size = 14, anchor = "middle") {
  return '<text x="' + x + '" y="' + y + '" font-size="' + size + '" text-anchor="' + anchor + '">' +
    escapeXML(label) + "</text>";
}
function niceCeiling(value) {
  const exponent = 10 ** Math.floor(Math.log10(Math.max(value, 1e-12)));
  return [1, 2, 4, 5, 10].find(multiple => multiple * exponent >= value) * exponent;
}
export function chartSVG(report, metric, title, unit, divisor = 1) {
  const width = 1140, height = 1100, plotWidth = 470, plotHeight = 210;
  const settings = report.metadata.settings;
  const fragments = ['<svg xmlns="http://www.w3.org/2000/svg" width="' + width + '" height="' + height +
    '" viewBox="0 0 ' + width + " " + height + '" role="img" aria-labelledby="title description">',
    "<title id=\"title\">" + escapeXML(title) + "</title>",
    "<desc id=\"description\">All endpoint IO models and both dependency configurations. Bars are medians; whiskers are actual minimum and maximum repeated results.</desc>",
    '<rect width="100%" height="100%" fill="#ffffff"/>',
    '<g font-family="Arial, Helvetica, sans-serif" fill="#20252b">',
    text(width / 2, 28, title, 20),
    text(width / 2, 54, "1,024 B | " + settings.clients + " clients | 16 in-flight requests/client | " +
      settings.rounds + " measured rounds", 14)];
  POLICIES.forEach((policy, index) => {
    const x = 325 + index * 190;
    fragments.push('<rect x="' + x + '" y="73" width="18" height="12" fill="' + COLORS[index] + '"/>',
      text(x + 26, 84, labelPolicy(policy), 14, "start"));
  });
  NETWORKS.forEach((profile, rowIndex) => {
    const subset = report.rows.filter(row => row.group === "main" && row.profile === profile);
    const maximum = Math.max(1, ...subset.map(row => row[metric].max / divisor));
    const ceiling = niceCeiling(maximum * 1.15);
    BACKENDS.forEach((backend, column) => {
      const x = 72 + column * 560, y = 145 + rowIndex * 295;
      const scale = value => y + plotHeight - value / divisor / ceiling * plotHeight;
      fragments.push(text(x + plotWidth / 2, y - 19, labelBackend(backend) + " | " + labelNetwork(profile), 16),
        text(x, y - 2, unit, 12, "start"));
      for (let tick = 0; tick <= 4; ++tick) {
        const value = ceiling * tick / 4, at = y + plotHeight - tick / 4 * plotHeight;
        fragments.push('<line x1="' + x + '" y1="' + at + '" x2="' + (x + plotWidth) + '" y2="' + at +
          '" stroke="#e3e6e8"/>', text(x - 8, at + 4, format(value, value >= 100 ? 0 : value >= 1 ? 1 : 2), 12, "end"));
      }
      MODELS.forEach((model, modelIndex) => {
        const [ioModel, count] = model.split(":");
        const center = x + (modelIndex + .5) * plotWidth / MODELS.length;
        POLICIES.forEach((policy, policyIndex) => {
          const datum = subset.find(row => row.backend === backend && row.io_model === ioModel &&
            row.io_threads === Number(count) && row.policy === policy)[metric];
          const barWidth = 27, at = center + (policyIndex - 1) * 32;
          const low = scale(datum.min), high = scale(datum.max), middle = scale(datum.median);
          fragments.push('<rect x="' + (at - barWidth / 2) + '" y="' + middle + '" width="' + barWidth +
            '" height="' + (y + plotHeight - middle) + '" fill="' + COLORS[policyIndex] + '"><title>' +
            escapeXML(labelPolicy(policy) + "; " + labelModel(ioModel, Number(count)) +
              "; median " + format(datum.median / divisor, 3) + "; range " +
              format(datum.min / divisor, 3) + " to " + format(datum.max / divisor, 3)) + "</title></rect>",
            '<path d="M ' + at + " " + low + " V " + high + " M " + (at - 4) + " " + low + " H " + (at + 4) +
              " M " + (at - 4) + " " + high + " H " + (at + 4) + '" stroke="#20252b" stroke-width="1.4" fill="none"/>');
        });
        const contexts = ioModel === "shared" ? 1 : Number(count);
        fragments.push(text(center, y + plotHeight + 24, contexts + " io_context" + (contexts > 1 ? "s" : ""), 13),
          text(center, y + plotHeight + 42, count + " thread" + (Number(count) > 1 ? "s" : ""), 13));
      });
      fragments.push('<path d="M ' + x + " " + y + " V " + (y + plotHeight) + " H " + (x + plotWidth) +
        '" stroke="#7e878e" fill="none"/>');
      if (subset.filter(row => row.backend === backend).every(row => row[metric].max === 0))
        fragments.push(text(x + plotWidth / 2, y + plotHeight / 2, "All measured values = 0", 16));
    });
  });
  fragments.push(text(width / 2, 1026, "Endpoint IO models shown above; relay always adds 1 io_context and 1 thread.", 13),
    text(width / 2, 1050, "Bars: median. Whiskers: min/max of " + settings.rounds + " rounds.", 14),
    metric === "cpu_percent" ? text(width / 2, 1075,
      "CPU includes clients, server, relay and sampler; 100% = one logical core.", 13) : "",
    "</g></svg>");
  return fragments.join("\n") + "\n";
}

function topologySVG() {
  const parts = [
    '<svg xmlns="http://www.w3.org/2000/svg" width="360" height="310" viewBox="0 0 360 310" role="img" aria-labelledby="title description">',
    '<title id="title">RUDP benchmark topology</title>',
    '<desc id="description">Client endpoints exchange UDP datagrams with server endpoints through a bounded bidirectional UDP relay, within one process on local IPv4 loopback.</desc>',
    '<defs><marker id="arrow" markerWidth="6" markerHeight="6" refX="6" refY="3" orient="auto-start-reverse"><path d="M 0 0 L 6 3 L 0 6 Z" fill="#68737d"/></marker></defs>',
    '<rect width="360" height="310" fill="#ffffff"/>',
    '<g font-family="Arial, Helvetica, sans-serif" fill="#20252b">',
  ];
  for (const [y, label, color] of [[16, "Client endpoints", "#f7f9fa"],
    [116, "Bounded UDP relay", "#e6f0ed"], [216, "Server endpoints", "#f7f9fa"]])
    parts.push('<rect x="55" y="' + y + '" width="250" height="56" rx="6" fill="' + color + '" stroke="#9ba4ad"/>',
      text(180, y + 36, label, 22));
  for (const y of [80, 180])
    parts.push('<path d="M 180 ' + y + ' V ' + (y + 28) + '" stroke="#68737d" stroke-width="1.5" fill="none" marker-start="url(#arrow)" marker-end="url(#arrow)"/>',
      text(202, y + 20, "UDP", 18, "start"));
  parts.push(text(180, 295, "Local IPv4 loopback", 18), "</g></svg>");
  return parts.join("\n") + "\n";
}

export function documentation(report, zh = false, rawURL) {
  const { metadata: meta, rows } = report, settings = meta.settings, host = meta.host;
  const main = rows.filter(row => row.group === "main"), dimensions = rows.filter(row => row.group === "dimensions");
  const soak = rows.filter(row => row.group === "soak"), protocol = rows[0].protocol_settings;
  const ticks = String.fromCharCode(96), inline = value => ticks + value + ticks, fence = ticks.repeat(3);
  const os = ["Darwin", "macOS"].includes(host.system) ? "macOS " + host.os_version + " (" + host.os_build + ")" :
    host.system + " " + host.release;
  const buffers = Array.from(new Set(rows.flatMap(row => [
    row.udp_server_receive_buffer_bytes.min, row.udp_server_receive_buffer_bytes.max,
    row.udp_client_receive_buffer_min_bytes.min, row.udp_client_receive_buffer_max_bytes.max]))).sort((a, b) => a - b);
  const configurations = rows.length, measured = rows.reduce((sum, row) => sum + row.rounds, 0);
  const messages = rows.reduce((sum, row) => sum + row.total_messages, 0);
  const lines = [zh ? "# RUDP 性能" : "# RUDP Performance", "",
    zh ? "本机 " + configurations + " 组配置、" + measured + " 次测量通过，共 " + format(messages, 0) +
      " 次消息往返；内容、顺序、发送完成与收尾检查通过，中继错误与溢出为零。" :
      "All " + configurations + " configurations / " + measured + " measured runs passed: " +
      format(messages, 0) + " message round trips; content, order, send-completion and drain checks passed; zero relay errors or queue overflows.",
    "", zh ? "[并发与端点线程数](rudp-concurrency_CN.md)比较 128／512／1024 个客户端、一个共享 io_context 下的 1 与 4 个端点线程。" :
      "[Concurrency and endpoint threads](rudp-concurrency.md) compares 128/512/1,024 clients with one shared io_context and one or four endpoint threads.",
    "", zh ? "## 测试环境" : "## Test Environment", "",
    zh ? "| 项目 | 实际参数 |" : "| Item | Recorded Parameters |", "| --- | --- |"];
  const environment = [
    [zh ? "时间（UTC）" : "Time (UTC)", meta.started_at_utc + " / " + meta.finished_at_utc],
    [zh ? "机器" : "Machine", (host.model === "Mac16,11" ? "Mac mini (" + host.model + ")" : host.model) +
      "；" + host.cpu + "；" + host.logical_cpus + (zh ? " 个逻辑核心；" : " logical CPUs; ") +
      format(host.memory_bytes / 2 ** 30, 0) + " GiB RAM"],
    [zh ? "系统" : "OS", os + "；" + host.architecture],
    [zh ? "编译" : "Build", meta.compiler_version.split("\n")[0] + "；C++20 Release " +
      inline(meta.compiler_flags.trim()) + "；CMake " + meta.cmake_version],
    [zh ? "依赖" : "Dependencies", "Standalone Asio " + meta.dependencies.standalone + "；Boost " +
      meta.dependencies.boost + "；OpenSSL " + meta.dependencies.openssl],
    [zh ? "端点负载" : "Endpoint Load", settings.clients + (zh ? " 个客户端，完整消息回显，无额外业务计算；回调 API" :
      " clients; complete-message echo; no added business computation; callback API")],
    [zh ? "主比较" : "Main Comparison", main.length + (zh ? " 组，每组 " : " cases; ") + settings.rounds +
      (zh ? " 轮；每轮预热 " : " rounds per case; ") + settings.warmup + (zh ? " s，测量 " : " s warmup; ") +
      settings.seconds + (zh ? " s；1024 B，每连接在途 16 条" : " s measured; 1,024 B; 16 in-flight/client")],
    [zh ? "包体／窗口" : "Payload / Window", dimensions.length + (zh ? " 组；64／4096／16384 B，窗口 1／64；随机丢包，1 个 io_context／1 个线程" :
      " cases; 64 / 4,096 / 16,384 B; windows 1 / 64; random loss; 1 io_context / 1 thread")],
    [zh ? "持续测试" : "Sustained Runs", soak.length + (zh ? " 组，每组单次 " : " cases; one ") +
      settings.soak_seconds + (zh ? " s；4096 B，窗口 16；1 个 io_context／4 个线程" :
        " s measured run per case; 4,096 B; window 16; 1 io_context / 4 threads")],
    [zh ? "重复采样" : "Repeated Seeds", (zh ? "主比较和维度测量使用 " : "Main and dimension runs use ") +
      settings.seed + "／" + (settings.seed + 1) + "／" + (settings.seed + 2) +
      (zh ? "；每条路由、每个方向使用独立序列" : "; separate streams per route and direction")],
    [zh ? "socket 接收缓冲" : "Socket Receive Buffers", (zh ? "端点实际 " : "Effective endpoint values: ") +
      buffers.map(value => format(value / 2 ** 20, 0)).join(" / ") + " MiB"],
    [zh ? "ARQ" : "ARQ", "UDP MTU " + protocol.udp_wire_mtu_bytes + " B；" +
      (zh ? "发送／接收窗口 " : "send / receive windows ") + protocol.arq_send_window_segments + "／" +
      protocol.arq_receive_window_segments + (zh ? " 个分片；更新周期 " : " segments; update interval ") +
      protocol.arq_update_interval_ms + (zh ? " ms；拥塞控制开启" : " ms; congestion control on")],
    [zh ? "FEC 分组" : "FEC Groups", (zh ? "最多 " : "Up to ") + protocol.fec_data_shards +
      (zh ? " 个数据分片；固定校验 " : " data shards; fixed parity ") + protocol.fec_fixed_parity_shards +
      (zh ? " 个，自适应上限 " : "; adaptive parity limit ") + protocol.fec_max_parity_shards +
      (zh ? " 个；分组等待设置 " : "; configured group wait ") + protocol.fec_group_wait_ms + " ms"],
    [zh ? "FEC 预算" : "FEC Budgets", (zh ? "每端点保留 " : "Per-endpoint retention limit ") +
      format(protocol.fec_retention_limit_bytes_per_endpoint / 2 ** 20, 0) + " MiB；" +
      (zh ? "每组校验报文字节上限为数据报文字节的 " : "parity datagram bytes limited to ") +
      protocol.fec_max_redundancy_percent + (zh ? "%" : "% of data datagram bytes")],
    [zh ? "中继容量" : "Relay Capacity", (zh ? "每路由 " : "Per route: ") +
      format(protocol.relay_queue_limit_bytes_per_route / 2 ** 20, 0) + " MiB／" +
      protocol.relay_queue_limit_packets_per_route + (zh ? " 个报文" : " datagrams")],
  ];
  lines.push(...environment.map(([key, value]) => "| " + key + " | " + value + " |"), "",
    zh ? "源码 SHA-256：" + inline(meta.source_sha256) + "。" : "Source SHA-256: " + inline(meta.source_sha256) + ".",
    "", zh ? "## 网络条件与测量口径" : "## Network Conditions and Metrics", "",
    zh ? "| 条件 | 丢包 | 单程延迟 | 重复／乱序 |" : "| Condition | Injected Loss | One-Way Delay | Duplication / Reordering |",
    "| --- | --- | ---: | --- |",
    zh ? "| 无主动丢包 | 0 | 0 ms | 0 |" : "| No injected loss | 0 | 0 ms | 0 |",
    zh ? "| 随机丢包 | 每个报文 5% | 10 ms | 1% 重复，5% 额外延迟 15 ms |" :
      "| Random loss | 5% per datagram | 10 ms | 1% duplicates; 5% delayed by an extra 15 ms |",
    zh ? "| 连续丢包 | 每方向、每 100 个报文连续丢 3 个 | 10 ms | 同上 |" :
      "| Burst loss | 3 consecutive datagrams per 100, per direction | 10 ms | Same as above |",
    zh ? "| 持续测试 | 连续丢包，另对剩余报文随机丢 2% | 10 ms | 同上 |" :
      "| Sustained runs | Burst loss plus 2% random loss of remaining datagrams | 10 ms | Same as above |",
    "", "![" + (zh ? "RUDP 测试拓扑" : "RUDP benchmark topology") + "](assets/rudp-fec-topology.svg)", "",
    zh ? '<Aside type="caution" title="本地模拟范围">' : '<Aside type="caution" title="Local simulation scope">', "",
    zh ? "同一进程、本机 IPv4 回环的网络条件模拟，不是 WAN 实测。" :
      "Network conditions are simulated in one process on local IPv4 loopback, not WAN measurements.",
    "", "</Aside>", "",
    zh ? "| 测量项 | 口径 |" : "| Measurement | Scope |", "| --- | --- |",
    zh ? "| IO 模型 | 图中仅含端点；中继另用 1 个 io_context 和 1 个线程，采样另有线程 |" :
      "| IO model | Charts describe endpoints; relay adds one io_context/thread, sampling another thread |",
    zh ? "| 服务端并发 | 会话共用 UDP socket 与 strand；4 个 io_context 各 1 线程主要分散客户端，不代表服务端并行分片 |" :
      "| Server concurrency | Sessions share a UDP socket/strand; four single-thread contexts mainly distribute clients, not proof of parallel server sharding |",
    zh ? "| RTT | 包含模拟延迟、拥塞控制、FEC 组包等待与 ARQ 重传 |" :
      "| RTT | Includes simulated delay, congestion control, FEC group wait and ARQ retransmission |",
    zh ? "| 吞吐量 | 完成的消息往返数；时长包含收完在途请求的时间 |" :
      "| Throughput | Completed message round trips; elapsed time includes in-flight drain |",
    zh ? "| 柱高／误差线 | 三轮中位数／实际最小和最大值，不是置信区间 |" :
      "| Bars / whiskers | Three-run medians / actual minimum and maximum, not confidence intervals |",
    "", zh ? "## 吞吐量与 p99 RTT" : "## Throughput and p99 RTT", "",
    "![RUDP FEC throughput](assets/rudp-fec-throughput.svg)", "",
    "![RUDP FEC p99 RTT](assets/rudp-fec-rtt.svg)", "",
    zh ? "## CPU 与网络开销" : "## CPU and Wire Overhead", "",
    "![RUDP FEC process CPU](assets/rudp-fec-cpu.svg)", "",
    zh ? "CPU 为进程平均占用，含客户端、服务端、中继和采样；100% 表示一个逻辑核心，多线程可超过 100%。整机占比 = 进程 CPU% / " +
      host.logical_cpus + "；完整数值保留两种口径。" :
      "Average process CPU includes clients, server, relay and sampling; 100% is one logical CPU, and multiple threads can exceed it. Machine share = process CPU% / " +
      host.logical_cpus + "; the summary retains both measures.",
    "", "![RUDP FEC wire overhead](assets/rudp-fec-wire-overhead.svg)", "",
    zh ? "网络开销 =（端点发送的 UDP 载荷字节／双向应用消息字节 − 1）× 100%。含 ARQ 控制／重传、FEC 头部、校验与填充，不含 IP／UDP 头或中继二次转发；不是 FEC 校验比例。" :
      "Wire overhead = (endpoint UDP payload bytes sent / bidirectional application bytes − 1) × 100%. Includes ARQ control/retransmission, FEC headers, parity and padding; excludes IP/UDP headers and the relay's second hop. This is not the FEC parity ratio.",
    "", zh ? "## 恢复、重传与保留内存" : "## Recovery, Retransmission and Retained Memory", "",
    "![RUDP FEC recovered shards](assets/rudp-fec-recovery.svg)", "",
    "![RUDP ARQ retransmitted segments](assets/rudp-fec-retransmissions.svg)", "",
    zh ? "恢复计 FEC 还原的数据分片，重传计 ARQ 重发的协议分片；均按每 1000 次消息往返归一化，不能互补计算。" :
      "Recovery counts FEC-restored data shards; retransmission counts resent ARQ segments. Both use units per 1,000 application round trips and are not complements.",
    "", "![RUDP FEC sampled retention](assets/rudp-fec-memory.svg)", "",
    zh ? "| 内存项 | 口径与限制 |" : "| Memory metric | Scope and limits |", "| --- | --- |",
    zh ? "| FEC 保留 | 各端点合计，100 ms 采样；可能漏掉短峰值，不含全部临时编解码空间 |" :
      "| FEC retention | Endpoint sum sampled every 100 ms; may miss shorter peaks and excludes some temporary codec storage |",
    zh ? "| 中继 | 各路由独立峰值之和，峰值未必同时出现 |" :
      "| Relay | Sum of independent route peaks, which may occur at different times |",
    zh ? "| RSS | 每组进程最高值，含预热与全部轮次，不是 FEC 独占内存 |" :
      "| RSS | Case process high-water mark including warmup and all rounds, not FEC-only memory |",
    "", zh ? "### 随机丢包下的资源数值" : "### Resource Values Under Random Loss", "",
    zh ? "1024 B、窗口 16、1 个 io_context／1 个端点线程；采样峰值使用三轮中位数。" :
      "1,024 B; window 16; one io_context / one endpoint thread. Sampled peaks are medians across three rounds.",
    "", zh ? "| 依赖库／策略 | 恢复／1000 往返 | 重传／1000 往返 | FEC 保留／KiB | 中继路由峰值和／KiB | 进程 RSS／MiB | 观测最大校验数 |" :
      "| Dependency / Policy | Restored / 1,000 | Retransmitted / 1,000 | FEC Retained / KiB | Relay Route Peaks Sum / KiB | Process RSS / MiB | Max Observed Parity |",
    "| --- | ---: | ---: | ---: | ---: | ---: | ---: |");
  for (const backend of BACKENDS) for (const policy of POLICIES) {
    const row = main.find(row => row.backend === backend && row.profile === "random" && row.policy === policy &&
      row.io_model === "shared" && row.io_threads === 1);
    lines.push("| " + labelBackend(backend) + " / " + labelPolicy(policy, zh) + " | " +
      format(row.fec_recovered_shards_per_1000_roundtrips.median) + " | " +
      format(row.arq_retransmissions_per_1000_roundtrips.median) + " | " +
      format(row.fec_retained_sampled_peak_bytes.median / 1024) + " | " +
      format(row.relay_sum_route_peak_bytes.median / 1024) + " | " + format(row.peak_rss_bytes / 2 ** 20) + " | " +
      row.fec_observed_parity_max.max + " |");
  }
  lines.push("", zh ? "## 包体大小与批量请求" : "## Payload Size and In-Flight Requests", "",
    zh ? "随机丢包，1 个 io_context／1 个端点线程。每格为三轮中位数「消息往返/s／p99 RTT 毫秒」；CPU、网络开销与资源计数见完整 JSON。" :
      "Random loss; one io_context / one endpoint thread. Cells: round trips/s / p99 RTT in ms, three-run medians. Full JSON adds CPU, wire overhead and resource counters.");
  for (const backend of BACKENDS) {
    lines.push("", "### " + labelBackend(backend), "",
      zh ? "| 消息／B | 每连接在途 | 仅 ARQ | 固定 FEC | 自适应 FEC |" :
        "| Message / B | In-Flight per Client | ARQ Only | Fixed FEC | Adaptive FEC |",
      "| ---: | ---: | ---: | ---: | ---: |");
    for (const payload of [64, 4096, 16384]) for (const window of [1, 64]) {
      const cells = POLICIES.map(policy => {
        const row = dimensions.find(row => row.backend === backend && row.payload === payload &&
          row.window === window && row.policy === policy);
        return format(row.roundtrips_per_second.median, 0) + " / " + format(row.rtt_p99_us.median / 1000, 1);
      });
      lines.push("| " + format(payload, 0) + " | " + window + " | " + cells.join(" | ") + " |");
    }
  }
  lines.push("", zh ? "消息按协议 MTU 分片；16 KiB 是应用消息，不是 UDP 数据报。FEC 从 1200 B UDP 载荷上限扣除 26 B，ARQ 数据报上限为 1174 B。" :
    "Messages fragment to the protocol MTU: 16 KiB is an application message, not a UDP datagram. FEC reserves 26 B of the 1,200 B UDP payload ceiling, leaving 1,174 B per ARQ datagram.",
    "", zh ? "## 120 秒持续测试" : "## 120-Second Sustained Runs", "",
    zh ? "每组单次测量，无重复测量误差线；消息校验全部通过。内存口径同上，峰值不能证明没有长期泄漏。" :
      "One measured run per case, without repeated-run whiskers; all message checks passed. Memory follows the definitions above; peaks cannot rule out long-term leaks.",
    "", zh ? "| 依赖库／策略 | 消息往返数 | 往返/s | p99／ms | CPU／% | 网络开销／% | FEC 采样峰值／KiB |" :
      "| Dependency / Policy | Message Round Trips | Round Trips/s | p99 / ms | CPU / % | Wire Overhead / % | FEC Sampled Peak / KiB |",
    "| --- | ---: | ---: | ---: | ---: | ---: | ---: |");
  for (const row of soak)
    lines.push("| " + labelBackend(row.backend) + " / " + labelPolicy(row.policy, zh) + " | " +
      format(row.total_messages, 0) + " | " + format(row.roundtrips_per_second.median, 0) + " | " +
      format(row.rtt_p99_us.median / 1000, 1) + " | " + format(row.cpu_percent.median) + " | " +
      format(row.wire_payload_overhead_percent.median) + " | " +
      format(row.fec_retained_sampled_peak_bytes.median / 1024) + " |");
  lines.push("", zh ? "## 结果与选型" : "## Results and Policy Selection", "",
    zh ? "仅比较主矩阵的 1 个 io_context／1 个端点线程，不跨 IO 模型排名。" :
      "Main matrix, one io_context / one endpoint thread; rank policies within the same IO model.",
    "", zh ? "| 依赖库 | 网络条件 | 最高往返吞吐量 | 最低 p99 RTT |" :
      "| Dependency | Network Condition | Highest Round-Trip Throughput | Lowest p99 RTT |",
    "| --- | --- | ---: | ---: |");
  for (const backend of BACKENDS) for (const profile of NETWORKS) {
    const candidates = main.filter(row => row.backend === backend && row.profile === profile &&
      row.io_model === "shared" && row.io_threads === 1);
    const fastest = [...candidates].sort((a, b) => b.roundtrips_per_second.median - a.roundtrips_per_second.median)[0];
    const shortest = [...candidates].sort((a, b) => a.rtt_p99_us.median - b.rtt_p99_us.median)[0];
    lines.push("| " + labelBackend(backend) + " | " + labelNetwork(profile, zh) + " | " +
      labelPolicy(fastest.policy, zh) + " " + format(fastest.roundtrips_per_second.median, 0) +
      (zh ? " 往返/s | " : " round trips/s | ") + labelPolicy(shortest.policy, zh) + " " +
      format(shortest.rtt_p99_us.median / 1000, 1) + " ms |");
  }
  const comparable = main.filter(row => row.io_model === "shared" && row.io_threads === 1);
  const range = (selected, metric, divisor = 1, digits = 1) => {
    const values = selected.map(row => row[metric].median / divisor);
    const low = format(Math.min(...values), digits), high = format(Math.max(...values), digits);
    return low === high ? low : low + " - " + high;
  };
  const cleanArq = comparable.filter(row => row.profile === "clean" && row.policy === "off");
  const randomArq = comparable.filter(row => row.profile === "random" && row.policy === "off");
  const randomFec = comparable.filter(row => row.profile === "random" && row.policy !== "off");
  const small = dimensions.filter(row => row.payload === 64 && row.window === 1);
  lines.push("",
    zh ? "以下范围为两种依赖方案的三轮中位数范围：" : "The ranges below span the two dependency configurations' three-run medians:", "",
    (zh ? "- 无主动丢包：仅 ARQ 达到 " : "- No injected loss: ARQ only reached ") +
      range(cleanArq, "roundtrips_per_second", 1, 0) + (zh ? " 往返/s，p99 为 " : " round trips/s, with p99 ") +
      range(cleanArq, "rtt_p99_us", 1000, 2) + (zh ? " ms；两种依赖方案的吞吐量和 p99 均优于本次测量的 FEC 策略。" :
        " ms. Both throughput and p99 favored ARQ-only over the measured FEC policies on both dependency configurations."),
    (zh ? "- 随机丢包：FEC 的吞吐量为 " : "- Random loss: FEC throughput was ") +
      range(randomFec, "roundtrips_per_second", 1, 0) + (zh ? " 往返/s，仅 ARQ 为 " : " round trips/s versus ") +
      range(randomArq, "roundtrips_per_second", 1, 0) +
      (zh ? "。代价是进程 CPU 从 " : " for ARQ only. Process CPU increased from ") +
      range(randomArq, "cpu_percent") + "%" + (zh ? " 升至 " : " to ") + range(randomFec, "cpu_percent") +
      "%" + (zh ? "，网络额外字节比例从 " : ", and wire-byte overhead from ") +
      range(randomArq, "wire_payload_overhead_percent") + "%" + (zh ? " 升至 " : " to ") +
      range(randomFec, "wire_payload_overhead_percent") + (zh ? "%\u3002" : "%."),
    (zh ? "- 随机丢包、64 B、每连接仅 1 条在途请求：固定 FEC 为 " : "- Random loss, 64 B and one in-flight request per client: fixed FEC reached ") +
      range(small.filter(row => row.policy === "fixed"), "roundtrips_per_second", 1, 0) +
      (zh ? " 往返/s，低于仅 ARQ 的 " : " round trips/s, below ARQ-only's ") +
      range(small.filter(row => row.policy === "off"), "roundtrips_per_second", 1, 0) +
      (zh ? "；小消息低并发场景也需要单独评估。" : ". Small-message, low-concurrency workloads need separate evaluation."));
  lines.push("",
    zh ? "这些结果不构成通用赢家；须同时考虑 p99、CPU、网络字节与内存预算。固定 FEC 校验数受分组密度和字节预算约束，自适应 FEC 需要足够样本与调整时间。完整数值保留实际丢包率、观测校验数与策略变化次数。" :
      "These results do not establish a universal winner; budget p99, CPU, wire bytes and memory together. Fixed FEC parity depends on group density and byte budgets; adaptive FEC needs sufficient samples and adjustment time. Full values retain realized loss, observed parity and policy changes.",
    "",
    zh ? '<Aside type="caution" title="测量与协议边界">' : '<Aside type="caution" title="Measurement and protocol limits">', "",
    zh ? "未测跨主机、真实 WAN、IPv6 压力或 RUDP 协程；公开 API 使用回调，协程为 TODO。FEC 不提供认证或加密，见[兼容与安全](../features/rudp/fec_CN.md#兼容与安全)。" :
      "No cross-host, real-WAN, IPv6 stress or RUDP coroutine measurements; the public API uses callbacks, with coroutines TODO. FEC provides no authentication or encryption; see [compatibility and security](../features/rudp/fec.md#compatibility-and-security).",
    "", "</Aside>", "",
    zh ? "仅 ARQ 与两种 FEC 共用中继拓扑；须保持网络条件和端点 IO 模型相同再比较。" :
      "ARQ-only and both FEC policies share the relay topology; compare under matching network conditions and endpoint IO models.",
    "", zh ? "## 复现与完整数据" : "## Reproduction and Full Data", "",
    "<Steps>", "", zh ? '<Step title="构建并运行 C++ 用例">' : '<Step title="Build and run the C++ case">', "", fence + "sh",
    "cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=Release -DARKNET_BUILD_BENCHMARKS=ON \\",
    "  -DARKNET_USE_BOOST_ASIO=OFF -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include",
    "cmake --build build-perf --target arknet_rudp_benchmark",
    "build-perf/benchmarks/arknet_rudp_benchmark --payload 1024 --clients 4 --window 16 \\",
    "  --io-model shared --io-threads 1 --fec adaptive --profile random \\",
    "  --loss-percent 5 --delay-ms 10 --duplicate-percent 1 --reorder-percent 5 \\",
    "  --seconds 3 --warmup 2 --rounds 3 --seed 42", fence, "",
    zh ? "Boost 在另一构建目录设置 " + inline("-DARKNET_USE_BOOST_ASIO=ON") +
      "。C++ 程序直接执行用例；维护者批量入口为 " + inline("bash benchmarks/run_rudp_fec.sh BUILD_DIR OUTPUT_DIR") +
      "，无需 Python 或 Ruby。原始数据保留各组实际命令、退出码与错误输出。" :
      "Configure Boost in a separate build directory with " + inline("-DARKNET_USE_BOOST_ASIO=ON") +
      ". Cases run as C++ executables; maintainers batch with " +
      inline("bash benchmarks/run_rudp_fec.sh BUILD_DIR OUTPUT_DIR") +
      ", without Python or Ruby. Raw data retains each actual command, exit code and stderr file.",
    "", "</Step>", "", zh ? '<Step title="生成离线报告">' : '<Step title="Generate the offline report">', "",
    zh ? "可选离线报告工具只需 Node，不启动网络测试：" :
      "The optional offline report tool needs only Node; it does not launch network tests:",
    "", fence + "sh",
    "node benchmarks/report_rudp_fec.mjs --self-check",
    "node benchmarks/report_rudp_fec.mjs \\",
    "  --results /path/to/arknet-results/rudp-fec --output docs/performance", fence, "",
    "</Step>", "", "</Steps>", "",
    "[" + (zh ? "完整指标与测量元数据" : "Full Metrics and Measurement Metadata") + "](rudp-fec-summary.json)", "",
    rawURL ? "[" + (zh ? "原始 JSON、实际命令与退出码" : "Raw JSON, Actual Commands and Exit Codes") + "](" + rawURL + ")" :
      (zh ? "原始输入保存在调用者的外部目录。发布归档后可用 `--raw-url` 指定链接。" :
        "Raw inputs remain in the caller's external directory. Set `--raw-url` after publishing their archive."), "");
  const document = lines.join("\n");
  return zh ? document : document.replaceAll("；", "; ").replaceAll("／", " / ");
}

function writeReport(report, output, rawURL) {
  const artifacts = [
    ["throughput", "roundtrips_per_second", "RUDP FEC: completed round-trip throughput", "Round trips / s", 1],
    ["rtt", "rtt_p99_us", "RUDP FEC: p99 round-trip latency", "p99 RTT / ms", 1000],
    ["cpu", "cpu_percent", "RUDP FEC: average process CPU", "Process CPU / %", 1],
    ["wire-overhead", "wire_payload_overhead_percent", "RUDP FEC: UDP-payload wire overhead", "Extra bytes / %", 1],
    ["recovery", "fec_recovered_shards_per_1000_roundtrips", "RUDP FEC: data-shard recovery", "Restored / 1,000 round trips", 1],
    ["retransmissions", "arq_retransmissions_per_1000_roundtrips", "RUDP FEC: ARQ segment retransmissions", "Retransmitted / 1,000 round trips", 1],
    ["memory", "fec_retained_sampled_peak_bytes", "RUDP FEC: sampled aggregate retention", "Sampled peak / KiB", 1024],
  ];
  const files = artifacts.map(([name, metric, title, unit, divisor]) =>
    [join(output, "assets", "rudp-fec-" + name + ".svg"), chartSVG(report, metric, title, unit, divisor)]);
  files.push([join(output, "assets", "rudp-fec-topology.svg"), topologySVG()],
    [join(output, "rudp.md"), documentation(report, false, rawURL)],
    [join(output, "rudp_CN.md"), documentation(report, true, rawURL)],
    [join(output, "rudp-fec-summary.json"), JSON.stringify({ report_schema_version: 1, ...report }, null, 2) + "\n"]);
  mkdirSync(join(output, "assets"), { recursive: true });
  for (const [path, content] of files) writeFileSync(path, content, "utf8");
}

function resultTableRows(document, zh) {
  const headers = zh ? ["| 项目 |", "| 条件 |", "| 依赖库／策略 |", "| 消息／B |", "| 依赖库 |"] :
    ["| Item |", "| Condition |", "| Dependency / Policy |", "| Message / B |", "| Dependency |"];
  return Array.from(document.matchAll(/^\|[^\n]*\n(?:\|[^\n]*(?:\n|$))*/gm), match => match[0])
    .filter(table => headers.some(header => table.startsWith(header)))
    .map(table => table.trimEnd()).join("\n");
}

function checkBuiltPresentation() {
  const root = new URL("../docs/site/dist/", import.meta.url);
  if (!existsSync(new URL("performance/rudp/index.html", root))) {
    console.log("RUDP presentation: emitted HTML/Markdown not checked; build docs/site to enable this check.");
    return;
  }
  const recorded = readJSON(new URL("../docs/performance/rudp-fec-summary.json", import.meta.url));
  for (const zh of [false, true]) {
    const source = readFileSync(new URL("../docs/performance/" + (zh ? "rudp_CN.md" : "rudp.md"), import.meta.url), "utf8");
    const prefix = zh ? "zh/" : "";
    const html = readFileSync(new URL(prefix + "performance/rudp/index.html", root), "utf8");
    const markdown = readFileSync(new URL(prefix + "performance/rudp/index.md", root), "utf8");
    const rawURL = Array.from(source.matchAll(/\]\((https:[^)]+)\)/g)).at(-1)?.[1];
    assert.equal(source, documentation(recorded, zh, rawURL), "published source must come from the recorded report renderer");
    assert.match(html, /data-steps/);
    assert.match(html, /data-step/);
    const plain = value => value.replace(/\s+/g, " ").trim();
    for (const block of source.matchAll(/^```[^\n]*\n([\s\S]*?)^```/gm))
      assert.ok(plain(markdown).includes(plain(block[1])), "complete reproduction command in AI Markdown");
    for (const heading of source.matchAll(/^#{1,6} .+$/gm)) assert.ok(markdown.includes(heading[0]));
    for (const label of source.matchAll(/<(Aside|Step)[^>]*title="([^"]+)"/g)) {
      assert.ok(html.includes(label[1] === "Aside" ? 'aria-label="' + label[2] + '"' : label[2]), "rendered component label");
      assert.ok(markdown.includes("**" + label[2] + "**"), "AI component label");
    }
    for (const image of source.matchAll(/!\[[^\]]*\]\((assets\/[^)]+)\)/g)) {
      const url = "/arknet/performance/" + image[1];
      assert.ok(html.includes('src="' + url + '"'), "rendered chart path");
      assert.ok(markdown.includes("](" + url + ")"), "AI chart path");
      const main = html.match(/<main\b[\s\S]*?<\/main>/)?.[0];
      assert.ok(main, "rendered report content");
      const beforeChart = main.slice(0, main.indexOf('src="' + url + '"'));
      assert.equal((beforeChart.match(/<details\b/g) || []).length,
        (beforeChart.match(/<\/details>/g) || []).length, "main chart must remain outside optional disclosures");
    }
    for (const row of resultTableRows(source, zh).split("\n")) assert.ok(markdown.includes(row), "recorded table row in AI Markdown");
    assert.ok(markdown.includes(recorded.metadata.source_sha256));
    assert.ok(markdown.includes(zh ? "不是 WAN 实测" : "not WAN measurements"));
    assert.ok(markdown.includes(zh ? "认证或加密" : "authentication or encryption"));
    assert.ok(markdown.includes("TODO"));
    assert.doesNotMatch(markdown, /<(?:Aside|Steps|Step)[\s/>]/);
    assert.doesNotMatch(html + markdown, /\/arknet\/arknet\//);
  }
  console.log("RUDP presentation: PASS (built bilingual HTML/Markdown; commands, charts, table values and limits).");
}

function selfCheck() {
  const directory = mkdtempSync(join(tmpdir(), "arknet-fec-report-test-"));
  const settings = { seconds: 3, warmup: 2, rounds: 3, clients: 4, seed: 42, soak_seconds: 120 };
  const meta = { schema_version: 1, complete: true, source_sha256: "a".repeat(64),
    started_at_utc: "2026-10-13T00:00:00Z", finished_at_utc: "2026-10-13T00:01:00Z",
    host: { model: "Test machine", cpu: "Test CPU", logical_cpus: 4, memory_bytes: 8 * 2 ** 30,
      system: "Test OS", release: "1", architecture: "test" },
    compiler_version: "Test compiler", compiler_flags: "-O3 -DNDEBUG", cmake_version: "test",
    dependencies: { standalone: "test", boost: "test", openssl: "test" },
    backends: { standalone: { binary_sha256: "b".repeat(64) }, boost: { binary_sha256: "c".repeat(64) } },
    settings, raw_repository_path: "benchmarks/results/test-only/rudp-fec" };
  const fixture = expected => {
    const seconds = expected.group === "soak" ? 120 : 3, count = expected.group === "soak" ? 1 : 3;
    const network = expected.profile === "clean" ? [0, 0, 0, 0, 0] : expected.profile === "random" ?
      [5, 0, 10, 1, 5] : [expected.group === "soak" ? 2 : 0, 3, 10, 1, 5];
    const value = { schema_version: 1, ok: true, protocol: "rudp", wire_protocol: "kcp", api: "callback",
      backend: expected.backend, payload_bytes: expected.payload, clients: 4, window: expected.window,
      io_model: expected.io_model, io_threads: expected.io_threads, io_contexts: expected.io_model === "shared" ? 1 : 4,
      fec_policy: expected.policy, fec_envelope: expected.policy === "off" ? "none" : "afec-v1",
      impairment_profile: expected.profile, loss_percent: network[0], burst_count: network[1],
      one_way_delay_ms: network[2], duplicate_percent: network[3], reorder_percent: network[4],
      burst_period_packets: 100, reorder_extra_delay_ms: network[2] + 5, seed: 42, relay_io_contexts: 1,
      relay_io_threads: 1, statistics_sample_interval_ms: 100, cpu_percent_per_logical_core: 100,
      cpu_scope: "process: clients, server, relay, statistics sampling",
      wire_bytes_scope: "endpoint UDP payloads; excludes IP and UDP headers",
      protocol_statistics_scope: "post-warmup baseline through drained final snapshot",
      requested_seconds: seconds, warmup_seconds: 2, round_count: count,
      arq_congestion_control: true, arq_mtu_bytes: expected.policy === "off" ? 1200 : 1174,
      udp_wire_mtu_bytes: 1200, arq_send_window_segments: 128, arq_receive_window_segments: 512,
      arq_update_interval_ms: 10, udp_receive_buffer_requested_bytes: 4194304,
      fec_data_shards: 8, fec_fixed_parity_shards: 2, fec_max_parity_shards: 4, fec_max_redundancy_percent: 50,
      fec_group_wait_ms: 2, fec_retention_limit_bytes_per_endpoint: 1048576,
      relay_queue_limit_bytes_per_route: 8388608, relay_queue_limit_packets_per_route: 8192,
      peak_rss_bytes: 5 * 2 ** 20, rounds: [] };
    for (let index = 0; index < count; ++index) {
      const messages = [10, 30, 20][index], elapsed = seconds + .1;
      const payload = messages * expected.payload * 2, wire = payload + messages * 80;
      const round = Object.fromEntries([...COUNTERS, ...ERRORS].map(field => [field, 0]));
      Object.assign(round, { round: index + 1, seed: 42 + index, ok: true, error: null,
        messages, messages_sent: messages, server_received_messages: messages, elapsed_seconds: elapsed,
        roundtrips_per_second: messages / elapsed, payload_roundtrip_mbit_per_second: payload * 8 / elapsed / 1e6,
        rtt_p50_us: 20000, rtt_p99_us: 60000 + index * 100, rtt_samples: messages, rtt_observations: messages,
        udp_server_receive_buffer_bytes: 4194304, udp_client_receive_buffer_min_bytes: 4194304,
        udp_client_receive_buffer_max_bytes: 4194304, cpu_percent: 30 + index * 10,
        wire_bytes_tx: wire, wire_bytes_rx: wire, wire_tx_mbit_per_second: wire * 8 / elapsed / 1e6,
        wire_payload_overhead_percent: (wire / payload - 1) * 100, relay_loss_percent: 0,
        arq_transmissions: messages * 2 + 10, arq_retransmissions: 10,
        relay_received_packets: messages * 4, relay_forwarded_packets: messages * 4,
        relay_received_bytes: wire, relay_forwarded_bytes: wire, relay_sum_route_peak_bytes: 2048,
        relay_sum_route_peak_packets: 2, arq_pending_sampled_peak_bytes: 4096, raw_queue_sampled_peak_bytes: 1024 });
      if (expected.policy !== "off") Object.assign(round, { fec_data_packets_tx: messages * 2,
        fec_parity_packets_tx: 2, fec_parity_wire_bytes_tx: 100, fec_missing_shards: 4,
        fec_recovered_shards: 3, fec_current_parity_min: 2, fec_current_parity_max: 2,
        fec_observed_parity_max: 2, fec_retained_sampled_peak_bytes: 8192, fec_groups_sampled_peak: 2 });
      value.rounds.push(round);
    }
    value.summary = { successful_rounds: count, failures: 0 };
    for (const metric of METRICS.filter(metric => metric !== "relay_loss_percent"))
      value.summary[metric + "_median"] = statistics(value.rounds.map(round => round[metric])).median;
    const rates = statistics(value.rounds.map(round => round.roundtrips_per_second));
    value.summary.roundtrips_per_second_min = rates.min;
    value.summary.roundtrips_per_second_max = rates.max;
    return value;
  };
  try {
    assert.deepEqual(statistics([10, 30, 20]), { median: 20, min: 10, max: 30 });
    assert.deepEqual(statistics([10, 30]), { median: 20, min: 10, max: 30 });
    assert.throws(() => statistics([]));
    assert.throws(() => statistics([Infinity]));
    const cases = expectedCases();
    assert.equal(cases.length, 96);
    assert.equal(cases.filter(entry => entry.group === "main").length, 54);
    assert.equal(cases.filter(entry => entry.group === "dimensions").length, 36);
    assert.equal(cases.filter(entry => entry.group === "soak").length, 6);
    for (const entry of cases) {
      const value = fixture(entry);
      validateRun(value, entry, settings);
      const base = join(directory, entry.backend, entry.name);
      mkdirSync(join(directory, entry.backend), { recursive: true });
      writeFileSync(base + ".json", JSON.stringify(value));
      writeFileSync(base + ".exit", "0\n");
      writeFileSync(base + ".command.sh", "arknet_rudp_benchmark --fixture-only\n");
      writeFileSync(base + ".stderr", "");
    }
    writeFileSync(join(directory, "metadata.json"), JSON.stringify(meta));
    const example = cases.find(entry => entry.profile === "random" && entry.policy === "fixed" && entry.group === "main");
    const original = fixture(example);
    for (const [field, invalid] of [["messages_sent", 999], ["cpu_percent", true], ["cpu_percent", NaN],
      ["wire_payload_overhead_percent", 999], ["rtt_p50_us", 1e6], ["seed", 99], ["pending_completions", 1],
      ["relay_overflow_drops", 1], ["fec_recovered_shards", 99], ["fec_observed_parity_max", 99]]) {
      const bad = structuredClone(original);
      bad.rounds[0][field] = invalid;
      assert.throws(() => validateRun(bad, example, settings), undefined, field);
    }
    const repeated = structuredClone(original);
    repeated.rounds[1].round = 1;
    assert.throws(() => validateRun(repeated, example, settings));
    const partial = structuredClone(original);
    partial.rounds.pop();
    assert.throws(() => validateRun(partial, example, settings));
    const report = loadResults(directory);
    assert.equal(report.rows.reduce((sum, row) => sum + row.rounds, 0), 276);
    assert.equal(report.rows.reduce((sum, row) => sum + row.total_messages, 0), 5460);
    assert.equal(report.rows[0].cpu_machine_percent.median, 10);
    const reportBeforePresentation = JSON.stringify(report);
    for (const zh of [false, true]) {
      const document = documentation(report, zh);
      // Golden hashes cover all pre-existing fixture result/environment rows;
      // layout-only measurement definitions are deliberately outside this set.
      assert.equal(sha256(resultTableRows(document, zh)), zh ?
        "7a389275f609e6911b7eaac0d74f87a39566a68b30c46b8cd775d8709bc99bf8" :
        "5be00e2b20fdeaa353089de68faaf5ac3fe91daceb3f7df78ccd5ca1f13f6a35");
      assert.equal((document.match(/<Aside /g) || []).length, 2);
      assert.equal((document.match(/<\/Aside>/g) || []).length, 2);
      assert.equal((document.match(/<Step /g) || []).length, 2);
      assert.equal((document.match(/<\/Step>/g) || []).length, 2);
      assert.equal((document.match(/<Steps>/g) || []).length, 1);
      assert.equal((document.match(/<\/Steps>/g) || []).length, 1);
      assert.ok(document.includes("276"));
      assert.ok(document.includes("5,460"));
      assert.ok(!document.includes("[object Object]") && !document.includes("undefined") && !document.includes("NaN"));
      assert.ok(document.includes("node benchmarks/report_rudp_fec.mjs --self-check"));
      assert.ok(document.includes(zh ? "不是 WAN 实测" : "not WAN measurements"));
      assert.ok(document.includes(zh ? "不构成通用赢家" : "do not establish a universal winner"));
      assert.ok(!document.includes("https://github.com/OpenArkStudio/arknet"));
      assert.ok(document.includes("--results /path/to/arknet-results/rudp-fec"));
      assert.ok(document.includes("--raw-url"));
      const linked = documentation(report, zh, "https://example.test/archive.tar.gz");
      assert.ok(linked.includes("](https://example.test/archive.tar.gz)"));
      assert.ok(document.includes("](assets/rudp-fec-topology.svg)"));
      assert.ok(!document.includes("flowchart") && !document.includes("mermaid"));
    }
    assert.equal(JSON.stringify(report), reportBeforePresentation, "rendering must not mutate report data");
    const output = join(directory, "generated");
    writeReport(report, output);
    const summaryBeforeLink = readFileSync(join(output, "rudp-fec-summary.json"), "utf8");
    const chartsBeforeLink = new Map(readdirSync(join(output, "assets")).map(name =>
      [name, readFileSync(join(output, "assets", name), "utf8")]));
    writeReport(report, output, "https://example.test/archive.tar.gz");
    assert.equal(readFileSync(join(output, "rudp-fec-summary.json"), "utf8"), summaryBeforeLink);
    for (const [name, bytes] of chartsBeforeLink)
      assert.equal(readFileSync(join(output, "assets", name), "utf8"), bytes, "presentation link must not change charts");
    assert.ok(readFileSync(join(output, "rudp.md"), "utf8").includes("](https://example.test/archive.tar.gz)"));
    assert.ok(readFileSync(join(output, "rudp_CN.md"), "utf8").includes("](https://example.test/archive.tar.gz)"));
    const diagram = readFileSync(join(output, "assets", "rudp-fec-topology.svg"), "utf8");
    assert.equal(diagram, topologySVG());
    assert.ok(diagram.includes('viewBox="0 0 360 310"'));
    assert.ok(diagram.includes("Client endpoints") && diagram.includes("Bounded UDP relay") &&
      diagram.includes("Server endpoints"));
    assert.equal((diagram.match(/marker-start=/g) || []).length, 2);
    assert.equal((diagram.match(/marker-end=/g) || []).length, 2);
    assert.equal(readdirSync(join(output, "assets")).filter(name => name.endsWith(".svg")).length, 8);
    for (const document of ["rudp.md", "rudp_CN.md"])
      for (const link of readFileSync(join(output, document), "utf8").matchAll(/!\[[^\]]*\]\((assets\/[^)]+)\)/g))
        assert.ok(readFileSync(join(output, link[1]), "utf8").startsWith("<svg "));
    const svg = chartSVG(report, "cpu_percent", "RUDP <FEC> & CPU", "Process CPU / %");
    assert.equal((svg.match(/<rect [^\n]*><title>/g) || []).length, 54);
    assert.ok(svg.includes("RUDP &lt;FEC&gt; &amp; CPU"));
    assert.ok(svg.includes("1 io_context") && svg.includes("4 io_contexts"));
    assert.ok(svg.endsWith("</g></svg>\n"));
    const zero = { ...report, rows: report.rows.map(row => ({ ...row, cpu_percent: statistics([0, 0, 0]) })) };
    assert.equal((chartSVG(zero, "cpu_percent", "Zero CPU", "CPU / %").match(/All measured values = 0/g) || []).length, 6);
    const base = join(directory, example.backend, example.name);
    writeFileSync(base + ".exit", "1\n");
    assert.throws(() => loadResults(directory), /failed process exit/);
    writeFileSync(base + ".exit", "0\n");
    const bytes = readFileSync(base + ".json");
    rmSync(base + ".json");
    assert.throws(() => loadResults(directory), /incomplete or unexpected matrix/);
    writeFileSync(base + ".json", bytes);
    meta.complete = false;
    writeFileSync(join(directory, "metadata.json"), JSON.stringify(meta));
    assert.throws(() => loadResults(directory), /complete metadata required/);
    checkBuiltPresentation();
    console.log("RUDP FEC report self-check: PASS (synthetic data; unchanged tables/charts/JSON; no network)");
  } finally { rmSync(directory, { recursive: true, force: true }); }
}

function main() {
  const args = process.argv.slice(2);
  if (args.length === 1 && args[0] === "--self-check") {
    selfCheck();
    return;
  }
  const options = {};
  for (let index = 0; index < args.length; index += 2) {
    check(["--results", "--metadata", "--output", "--raw-url"].includes(args[index]) && args[index + 1] &&
      !args[index + 1].startsWith("--"), "Usage: node report_rudp_fec.mjs --results DIRECTORY --output DIRECTORY [--metadata FILE] [--raw-url URL]");
    check(options[args[index]] === undefined, "duplicate option: " + args[index]);
    options[args[index]] = args[index + 1];
  }
  check(options["--results"] && options["--output"], "--results and --output are required");
  const report = loadResults(resolve(options["--results"]),
    options["--metadata"] ? resolve(options["--metadata"]) : undefined);
  writeReport(report, resolve(options["--output"]), options["--raw-url"]);
  console.log("RUDP FEC report: " + report.rows.length + " configurations; " +
    report.rows.reduce((sum, row) => sum + row.rounds, 0) + " measured runs; PASS");
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try { main(); }
  catch (error) { console.error(error.message); process.exitCode = 1; }
}
