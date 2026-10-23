import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';

const workflow = readFileSync(new URL('../../.github/workflows/ci.yml', import.meta.url), 'utf8');
const defaultActivities = ['opened', 'synchronize', 'reopened'];
const admissionActivities = [...defaultActivities, 'ready_for_review'];

// Read this workflow's inline trigger lists without requiring installed packages.
// The omitted-types case models GitHub's defaults to reproduce the draft-release bug.
function pullRequestTrigger(source) {
  const lines = source.split('\n');
  const start = lines.indexOf('  pull_request:');
  assert.notEqual(start, -1, 'missing pull_request trigger');
  const options = {};
  for (const line of lines.slice(start + 1)) {
    if (!line.trim()) continue;
    if (!line.startsWith('    ')) break;
    const match = line.match(/^    (branches|types): \[([^\]]+)\]$/);
    assert.ok(match, 'review unsupported pull_request trigger option: ' + line);
    assert.equal(Object.hasOwn(options, match[1]), false, 'duplicate trigger option');
    options[match[1]] = match[2].split(',').map(value => value.trim());
  }
  return options;
}

function triggers(trigger, { action, base }) {
  return (!trigger.branches || trigger.branches.includes(base)) &&
    (trigger.types || defaultActivities).includes(action);
}

function checkAdmissionContract(source) {
  const trigger = pullRequestTrigger(source);
  assert.deepEqual(trigger.branches, ['main'], 'source checks must target main');
  for (const action of admissionActivities) {
    assert.equal(triggers(trigger, { action, base: 'main' }), true,
      action + ' must start fresh source checks');
    assert.equal(triggers(trigger, { action, base: 'release' }), false,
      action + ' must retain the main branch filter');
  }
  for (const action of ['edited', 'closed', 'converted_to_draft']) {
    assert.equal(triggers(trigger, { action, base: 'main' }), false,
      action + ' must not add an unrelated workflow run');
  }
}

test('main PR creation, synchronization, reopening and draft release start source validation', () => {
  checkAdmissionContract(workflow);
});

test('the original implicit event defaults reproduce the missing draft-release run and fail admission', () => {
  const regression = workflow.replace(/^    types: \[opened, synchronize, reopened, ready_for_review\]\n/m, '');
  assert.notEqual(regression, workflow);
  const trigger = pullRequestTrigger(regression);
  for (const action of defaultActivities) assert.equal(triggers(trigger, { action, base: 'main' }), true);
  assert.equal(triggers(trigger, { action: 'ready_for_review', base: 'main' }), false);
  assert.throws(() => checkAdmissionContract(regression), /ready_for_review must start fresh source checks/);
});

test('the admission contract rejects losing a default activity, widening events or changing the base filter', () => {
  for (const action of admissionActivities) {
    const activities = admissionActivities.filter(value => value !== action);
    const changed = workflow.replace('types: [opened, synchronize, reopened, ready_for_review]', `types: [${activities.join(', ')}]`);
    assert.notEqual(changed, workflow);
    assert.throws(() => checkAdmissionContract(changed), assert.AssertionError);
  }
  for (const [from, to] of [
    ['branches: [main]', 'branches: [release]'],
    ['    branches: [main]\n', ''],
    ['types: [opened, synchronize, reopened, ready_for_review]', 'types: [opened, synchronize, reopened, ready_for_review, edited]'],
  ]) {
    assert.ok(workflow.includes(from));
    assert.throws(() => checkAdmissionContract(workflow.replace(from, to)), assert.AssertionError);
  }
});
