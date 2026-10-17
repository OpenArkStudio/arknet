import { spawnSync } from "node:child_process";
import { appendFileSync, readFileSync } from "node:fs";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";

const commit = /^[0-9a-f]{40}(?:[0-9a-f]{24})?$/;

export function isDocumentationPath(file) {
  if (typeof file !== "string" || !file || file.includes("\\") ||
      file.startsWith("/") || file.split("/").some(part => part === "." || part === "..")) return false;
  return file === "README.md" || file === "README_CN.md" ||
    (file.startsWith("docs/") && /\.(?:md|mdx|svg|css|png|jpe?g|webp|gif)$/.test(file));
}

export function planChecks({ eventName, known, files }) {
  const docsOnly = ["pull_request", "merge_group"].includes(eventName) && known === true &&
    Array.isArray(files) && files.length > 0 && files.every(isDocumentationPath);
  return {
    full_cpp: !docsOnly,
    changed_files: known === true && Array.isArray(files) ? files.length : null,
    reason: docsOnly ? "documentation-only" : "full-validation",
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
  if (env.GITHUB_OUTPUT) appendFileSync(env.GITHUB_OUTPUT, `full_cpp=${plan.full_cpp}\n`);
  process.stdout.write(`${JSON.stringify(plan)}\n`);
  return plan;
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) main();
