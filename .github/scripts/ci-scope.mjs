import { spawnSync } from "node:child_process";
import { appendFileSync, readFileSync } from "node:fs";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";

const commit = /^[0-9a-f]{40}(?:[0-9a-f]{24})?$/;
const relativePath = file => typeof file === "string" && file.length > 0 && !file.includes("\\") &&
  !file.startsWith("/") && !file.split("/").some(part => part === "." || part === "..");

export const moduleTests = {
  rudp: ["arq_core", "rudp", "rudp_stability", "fec", "rudp_fec", "arq_interop", "fec_interop", "allocation_failure"],
  udp: ["udp", "ipv6", "socket_metadata", "arq_core", "rudp", "rudp_stability", "fec", "rudp_fec", "arq_interop", "fec_interop", "allocation_failure"],
  http: ["tcp", "http", "router", "httprequest", "websocket", "tls", "ipv6", "allocation_failure"],
  mysql: ["mysql_wire", "mysql_handshake", "mysql_command", "mysql_auth", "mysql_connection", "mysql_shutdown", "mysql_pool", "mysql_pool_return_failure", "mysql_pool_shutdown"],
  redis: ["redis_resp", "redis_client", "redis_pool", "redis_shutdown", "redis_pool_shutdown", "redis_close_failure", "redis_pool_return_failure", "redis_child_close_failure"],
};

export function sourceTests(files) {
  const selected = new Set();
  for (const file of files) {
    if (isDocumentationPath(file)) continue;
    let group;
    if (/^include\/arknet\/(?:rudp|arq|fec)\//.test(file) || /^tests\/(?:rudp|arq|fec)[\w.\/]*\.(?:cpp|hpp|c)$/.test(file)) group = "rudp";
    else if (/^include\/arknet\/udp\//.test(file) || file === "tests/udp.cpp") group = "udp";
    else if (/^include\/arknet\/(?:http|websocket)\//.test(file) || /^tests\/(?:http|router|websocket)\w*\.cpp$/.test(file)) group = "http";
    else if (/^include\/arknet\/mysql\//.test(file) || /^tests\/mysql_\w+\.cpp$/.test(file)) group = "mysql";
    else if (/^include\/arknet\/redis\//.test(file) || /^tests\/redis_\w+\.cpp$/.test(file)) group = "redis";
    // Shared transports, build files, CI and unknown paths need the complete
    // Linux/Boost suite. Only explicitly mapped modules narrow that suite.
    if (!group) return [];
    for (const name of moduleTests[group]) selected.add(name);
  }
  return [...selected].sort();
}

export function isDocumentationPath(file) {
  if (!relativePath(file)) return false;
  return file === "README.md" || file === "README_CN.md" ||
    (file.startsWith("docs/") && /\.(?:md|mdx|svg|css|png|jpe?g|webp|gif)$/.test(file));
}

export function planChecks({ eventName, known, files }) {
  const knownFiles = known === true && Array.isArray(files) && files.length > 0 && files.every(relativePath);
  const docsOnly = ["pull_request", "merge_group"].includes(eventName) && knownFiles && files.every(isDocumentationPath);
  const tier = docsOnly ? "documentation" : eventName === "pull_request" && knownFiles ? "modules" : "full";
  return {
    full_cpp: !docsOnly,
    tier,
    source_tests: tier === "modules" ? sourceTests(files).join(";") : "",
    changed_files: known === true && Array.isArray(files) ? files.length : null,
    reason: docsOnly ? "documentation-only" : tier === "modules" ? "source-module-regression" : "full-validation",
  };
}

export function changedFiles(eventName, event, cwd = process.cwd()) {
  const base = eventName === "pull_request" ? event?.pull_request?.base?.sha : event?.merge_group?.base_sha;
  const head = eventName === "pull_request" ? event?.pull_request?.head?.sha : event?.merge_group?.head_sha;
  if (!["pull_request", "merge_group"].includes(eventName) || typeof base !== "string" ||
      typeof head !== "string" || !commit.test(base) || !commit.test(head)) return { known: false };
  // Disable rename detection: both the removed and added paths must qualify.
  const result = spawnSync("git", ["diff", "--no-renames", "--name-status", "-z", base, head, "--"],
    { cwd, encoding: "utf8", maxBuffer: 8 * 1024 * 1024 });
  if (result.error || result.status !== 0 || !result.stdout.endsWith("\0")) return { known: false };
  const fields = result.stdout.slice(0, -1).split("\0");
  if (fields.length % 2 || fields.some((value, index) => index % 2 === 0 && !/^[AMD]$/.test(value))) {
    return { known: false };
  }
  return { known: true, files: fields.filter((_, index) => index % 2 === 1) };
}

export function main(env = process.env) {
  let changes = { known: false };
  try {
    const event = JSON.parse(readFileSync(env.GITHUB_EVENT_PATH, "utf8"));
    changes = changedFiles(env.GITHUB_EVENT_NAME, event);
  } catch {
    // An unavailable or malformed event cannot qualify for reduced checks.
  }
  const plan = planChecks({ eventName: env.GITHUB_EVENT_NAME, ...changes });
  if (env.GITHUB_OUTPUT) appendFileSync(env.GITHUB_OUTPUT,
    `full_cpp=${plan.full_cpp}\ntier=${plan.tier}\nsource_tests=${plan.source_tests}\n`);
  process.stdout.write(`${JSON.stringify(plan)}\n`);
  return plan;
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) main();
