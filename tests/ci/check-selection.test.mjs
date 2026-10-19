import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { existsSync, mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { test } from "node:test";

const sourceRoot = fileURLToPath(new URL("../../", import.meta.url));
const source = readFileSync(join(sourceRoot, "tests/CMakeLists.txt"), "utf8");
const start = source.indexOf("function(arknet_add_check");
const end = source.indexOf("set(arknet_test_names");
assert.ok(start >= 0 && end > start, "locate the actual check-registration functions");
const functions = source.slice(start, end);
const cmakeCommand = "${CMAKE_COMMAND}";

test("selected check targets run real commands, propagate failures and reject missing checks", () => {
  const root = mkdtempSync(join(tmpdir(), "arknet-check-selection-"));
  const run = args => spawnSync("cmake", args, { encoding: "utf8", timeout: 30_000 });
  try {
    mkdirSync(join(root, "tests"));
    writeFileSync(join(root, "tests/run.cmake"), readFileSync(join(sourceRoot, "tests/run.cmake")));
    writeFileSync(join(root, "CMakeLists.txt"), `cmake_minimum_required(VERSION 3.21)
project(check_selection LANGUAGES NONE)
set(ARKNET_CHECK_TESTS "" CACHE STRING "Selected checks")
${functions}
add_executable(pass_command IMPORTED GLOBAL)
add_executable(fail_command IMPORTED GLOBAL)
set_target_properties(pass_command fail_command PROPERTIES IMPORTED_LOCATION "${cmakeCommand}")
arknet_add_check(pass 5 pass_command -E true)
arknet_add_check(fail 5 fail_command -E false)
arknet_configure_check_target()
`);
    for (const [selection, succeeds, names] of [["pass", true, ["pass"]], ["fail", false, ["fail"]],
      ["pass;fail", false, ["pass", "fail"]], ["", false, ["pass", "fail"]]]) {
      const build = join(root, `build-${selection.replaceAll(";", "-") || "all"}`);
      const configure = run(["-S", root, "-B", build, `-DARKNET_CHECK_TESTS=${selection}`]);
      assert.equal(configure.status, 0, configure.stdout + configure.stderr);
      const compile = run(["--build", build, "--target", "arknet_check_build"]);
      assert.equal(compile.status, 0, compile.stdout + compile.stderr);
      assert.equal(existsSync(join(build, "tests/logs/pass.log")), false, "build must not execute checks");
      const check = run(["--build", build, "--target", "arknet_check"]);
      assert.equal(check.status === 0, succeeds, check.stdout + check.stderr);
      for (const name of ["pass", "fail"]) {
        const path = join(build, "tests/logs", `${name}.log`);
        assert.equal(existsSync(path), names.includes(name), `${selection}: ${name}`);
        if (existsSync(path)) assert.match(readFileSync(path, "utf8"), name === "pass" ? /result: 0/ : /result: 1/);
      }
    }
    const unavailable = run(["-S", root, "-B", join(root, "missing"), "-DARKNET_CHECK_TESTS=pass;missing"]);
    assert.notEqual(unavailable.status, 0);
    assert.match(unavailable.stdout + unavailable.stderr, /Unknown or unavailable arknet check: missing/);
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});
