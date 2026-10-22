import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';

const workflow = readFileSync(new URL('../../.github/workflows/ci.yml', import.meta.url), 'utf8');
const runner = readFileSync(new URL('../../.github/scripts/native-profile.mjs', import.meta.url), 'utf8');

function checkCacheContract(source) {
  for (const value of [
    'uses: Mozilla-Actions/sccache-action@v0.0.11', 'version: v0.16.0', 'disable_annotations: true',
    'VCPKG_CACHE_FEATURES: boost;ssl', 'VCPKG_CACHE_DEFAULT_FEATURES: \'true\'',
    'VCPKG_CACHE_PROFILE: grouped-v2-${{ matrix.boost }}-${{ matrix.sanitize }}-',
    'VCPKG_DEFAULT_TRIPLET: ${{ runner.arch', 'VCPKG_DEFAULT_HOST_TRIPLET: ${{ runner.arch',
    'vcpkgGitCommitId: ${{ env.VCPKG_CACHE_REVISION }}',
    'key: ${{ steps.compiler-cache.outputs.key }}-${{ github.run_id }}-${{ github.run_attempt }}',
    'restore-keys: ${{ steps.compiler-cache.outputs.key }}-',
    "if: ${{ env.ARKNET_RUN_NATIVE == 'true' && always() }}",
  ]) assert.ok(source.includes(value), value);
  const dependencyRestore = source.slice(source.indexOf('      - name: Restore dependency archives'), source.indexOf('      - name: Install compiler cache'));
  assert.match(dependencyRestore, /continue-on-error: true/);
  assert.match(dependencyRestore, /key: \$\{\{ steps.vcpkg-cache.outputs.key \}\}/);
  assert.doesNotMatch(dependencyRestore, /restore-keys/);
  assert.ok(source.indexOf('      - name: Restore compiler cache') < source.indexOf('      - name: Start compiler cache after archive restore'));
  assert.ok(source.indexOf('node .github/scripts/vcpkg-cache.mjs') < source.indexOf('uses: lukka/run-vcpkg@v11'));
  for (const name of ['Configure - Boost - TLS', 'Build planned native targets - Boost - TLS', 'Test - Boost - TLS']) {
    const start = source.indexOf(`      - name: ${name}\n`);
    assert.ok(start > 0, name);
    const end = source.indexOf('      - ', start + 8);
    assert.doesNotMatch(source.slice(start, end < 0 ? undefined : end), /continue-on-error/);
  }
}

test('optional caches preserve compiler launchers, pinned dependencies, profile isolation and native errors', () => {
  checkCacheContract(workflow);
});

test('workflow cache contract rejects unpinned tools, partial keys, broad dependency restores and swallowed build errors', () => {
  for (const [from, to] of [
    ['version: v0.16.0', 'version: latest'],
    ['sccache-action@v0.0.11', 'sccache-action@main'],
    ['disable_annotations: true', 'disable_annotations: false'],
    ['VCPKG_CACHE_PROFILE: grouped-v2-${{ matrix.boost }}-${{ matrix.sanitize }}-', 'VCPKG_CACHE_PROFILE: shared-'],
    ['VCPKG_DEFAULT_HOST_TRIPLET:', 'UNTRACKED_TRIPLET:'],
    ['key: ${{ steps.vcpkg-cache.outputs.key }}', 'restore-keys: arknet-vcpkg-'],
    ['      - name: Build planned native targets - Boost - TLS\n', '      - name: Build planned native targets - Boost - TLS\n        continue-on-error: true\n'],
  ]) {
    assert.ok(workflow.includes(from));
    assert.throws(() => checkCacheContract(workflow.replace(from, to)), assert.AssertionError);
  }
});

test('the grouped runner preserves dependency features and both compiler launcher arguments', () => {
  for (const option of ['VCPKG_MANIFEST_FEATURES', 'CMAKE_C_COMPILER_LAUNCHER', 'CMAKE_CXX_COMPILER_LAUNCHER']) {
    assert.ok(runner.includes(`-D${option}=`));
  }
});
