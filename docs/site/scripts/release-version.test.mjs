import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";

function capture(source, pattern, label) {
  const match = source.match(pattern);
  assert.ok(match, `missing ${label}`);
  return match[1];
}

function checkVersions({ cmake, header, manifest }) {
  const project = capture(cmake, /project\(arknet\s+VERSION\s+(\d+\.\d+\.\d+)\b/, "CMake project version");
  const components = ["MAJOR", "MINOR", "PATCH"].map(name =>
    capture(header, new RegExp(`^#define ARKNET_VERSION_${name} (\\d+)$`, "m"), `header ${name}`));
  const string = capture(header, /^#define ARKNET_VERSION_STRING "(\d+\.\d+\.\d+)"$/m, "header version string");
  assert.equal(components.join("."), project, "header components differ from CMake project version");
  assert.equal(string, project, "header string differs from CMake project version");
  assert.equal(JSON.parse(manifest).version, project, "vcpkg manifest differs from CMake project version");
}

test("library release metadata agrees across CMake, public header and vcpkg", async () => {
  const [cmake, header, manifest] = await Promise.all([
    readFile(new URL("../../../CMakeLists.txt", import.meta.url), "utf8"),
    readFile(new URL("../../../include/arknet/version.hpp", import.meta.url), "utf8"),
    readFile(new URL("../../../vcpkg.json", import.meta.url), "utf8"),
  ]);
  checkVersions({ cmake, header, manifest });
});

const consistent = {
  cmake: "project(arknet VERSION 1.2.3 LANGUAGES CXX)",
  header: [
    "#define ARKNET_VERSION_MAJOR 1",
    "#define ARKNET_VERSION_MINOR 2",
    "#define ARKNET_VERSION_PATCH 3",
    '#define ARKNET_VERSION_STRING "1.2.3"',
  ].join("\n"),
  manifest: '{"name":"arknet","version":"1.2.3"}',
};

test("release check rejects stale CMake metadata", () => {
  assert.throws(() => checkVersions({ ...consistent, cmake: consistent.cmake.replace("1.2.3", "1.2.2") }),
    /header components differ/);
});

for (const [name, value] of [["MAJOR", 1], ["MINOR", 2], ["PATCH", 3]]) {
  test(`release check rejects a mismatched header ${name}`, () => {
    const header = consistent.header.replace(`ARKNET_VERSION_${name} ${value}`, `ARKNET_VERSION_${name} ${value + 1}`);
    assert.throws(() => checkVersions({ ...consistent, header }), /header components differ/);
  });
}

test("release check rejects a stale public version string", () => {
  const header = consistent.header.replace('STRING "1.2.3"', 'STRING "1.2.2"');
  assert.throws(() => checkVersions({ ...consistent, header }), /header string differs/);
});

test("release check rejects stale vcpkg metadata", () => {
  assert.throws(() => checkVersions({ ...consistent, manifest: '{"version":"1.2.2"}' }), /vcpkg manifest differs/);
});

test("release check reports a missing public version component", () => {
  const header = consistent.header.replace("#define ARKNET_VERSION_PATCH 3\n", "");
  assert.throws(() => checkVersions({ ...consistent, header }), /missing header PATCH/);
});
