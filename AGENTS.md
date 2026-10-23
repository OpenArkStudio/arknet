# Contributor and agent instructions

These rules apply to this repository. Preserve unrelated work and use an isolated
branch or worktree when the existing checkout contains uncommitted changes.

## Commits and pull requests

- Write all new commit subjects and PR titles in English using Conventional
  Commits: `type(scope): description`. Use a short, lowercase scope and an
  imperative description of the resulting change.
- Types: `feat`, `fix`, `perf`, `refactor`, `docs`, `test`, `build`, `ci`, `style`,
  `chore` and `revert`. Choose the type that describes the actual change; use
  `style` only for formatting changes without behavior changes.
- Examples: `feat(redis): support ordered command pipelines`,
  `fix(benchmarks): include Winsock 2 before Windows headers`,
  `docs(releases): summarize user-visible changes`.
- Name new topic branches `type/short-kebab-case`, matching the change, for example
  `feat/database-coroutines`, `fix/windows-includes` or `docs/readme-maintainers`.
  Do not use the `codex/` prefix or generic `xxx-branch` names. Existing published
  branches do not need renaming solely to adopt this rule. Use lowercase names
  for new GitHub repositories.
- Mark incompatible public changes with `!` and explain them in a
  `BREAKING CHANGE:` footer. Describe migration steps in the PR and release notes.
- Keep each PR focused on one functional module or related change. Publish
  independently reviewable work instead of accumulating unrelated modifications.
- All changes to `main` go through a PR. Do not push directly, force-push or delete
  `main`, bypass branch protection, or rewrite already merged history.
- Use squash only after every required CI check passes and review conversations
  are resolved. Outside the merge queue, synchronize the PR head with `main`
  before acceptance. With the merge queue enabled, validate the queue candidate
  containing the latest `main` and preceding queued changes; a green PR head alone
  does not validate that combination. Admit eligible PRs in the creation order
  defined below.
  Another maintainer's approval is not currently mandatory. Merge eligible PRs
  without asking for confirmation again.
- Explicitly set the squash commit subject to the Conventional Commit PR title.
  Replace GitHub's generated commit-list body with a concise change description;
  do not copy intermediate commit subjects into release notes.

## Verification and delivery

- When runner capacity is shared by several PRs, prioritize the oldest unblocked
  PRs and their required queue candidates. Prepare newer work locally and defer
  its hosted runs until older ready PRs can finish; do not repeatedly push newer
  branches ahead of the backlog. Preserve active acceptance runs. A cancelled
  scheduling run still needs fresh successful checks before acceptance.
- Run the checks relevant to the change before publishing. Report the exact
  tested revision, commands, results and material limitations.
- Investigate failed CI logs before changing code or retrying. Do not repeatedly
  rerun an unexplained failure, weaken tests or protection, or extend timeouts to
  hide a defect.
- Pending, skipped, cancelled and successful checks are distinct states. A
  cancelled run is not validation. If a run is cancelled for scheduling, record
  the reason and resume its acceptance checks; do not leave it indefinitely red.
- Source PRs run related native modules in Linux Boost/TLS Release, plus formatting
  and documentation checks. Shared headers, TCP, build/dependency/CI changes and
  unmapped paths run the complete Linux Boost suite. Module mappings must fail
  conservatively; missing or malformed change ranges select the complete matrix.
- Code-bearing merge-group candidates and manual runs require the full configured
  operating-system, Boost/Standalone, TLS and sanitizer matrix. Source-only green
  checks do not establish that acceptance. Strictly classified documentation-only
  changes require documentation and formatting at both stages. Grouped native
  jobs retain all twelve configuration profiles and explicitly record scope-only
  coverage when no native tests are planned. Required context changes need a
  protected, staged migration after hosted acceptance. Failed/missing scope
  validation must fail required checks; never describe a scope-only cell as an executed native test.
- Include what changed, why, validation and remaining limits in PR descriptions.
  Verify the merge result before saying that a PR has merged.

## Library and code style

- Keep arknet C++20 and header-only. The `arknet::arknet` CMake interface target
  propagates headers, settings and dependencies; it is not an arknet binary.
- Boost.Asio and Boost.Beast are the default dependencies. Standalone Asio covers
  TCP, UDP, RUDP, TLS and database clients; HTTP and WebSocket require Boost.
  Call these dependencies, not backends. Preserve machine-readable identifiers
  unless an explicit API or data-format migration is part of the task.
- Follow `.clang-format` and `scripts/format_spacing.sh`, using clang-format 18.
  Keep function opening braces on the declaration line, expand short function
  bodies, use braces for control statements and keep names in `snake_case`.
- Group includes as system, third-party, then project headers. Keep required
  platform-header dependencies in a valid order, including Winsock 2 before
  `windows.h` when both are needed. Separate include blocks, namespaces and
  adjacent definitions with the prescribed blank lines.
- Run `CLANG_FORMAT=/path/to/clang-format bash scripts/test_format.sh` and
  `CLANG_FORMAT=/path/to/clang-format bash scripts/format.sh --check`.
- Before using a tracked-only formatter or formatting check, stage new C++ files
  or format and check their explicit paths so untracked sources are not omitted.
  After committing, rerun the full project formatting check against the committed
  tree; a check of tracked files before adding new sources is not acceptance.
- Build and run the affected native targets, then the required suite as needed:
  `cmake --build build --target arknet_check --config Release`.
- Keep third-party licenses and notices intact; do not reformat vendored code.

## Tests, benchmarks and documentation

- Keep correctness tests under `tests/` and permanent native C++ benchmarks under
  `benchmarks/`. Users must be able to compile and run them without Python or Ruby;
  shell scripts may orchestrate internal batches.
- Keep raw measurement JSON and build logs outside the source repository. Raw
  evidence remains local unless publication of the exact files is separately authorized.
- Write performance reports from actual local measurements. Identify the machine,
  source revision, workload, repeated runs and limits. Use CPU utilization percent
  (100% is one logical core), throughput and response-time units consistently.
  Keep interval CPU and whole-run CPU scopes distinct. Use SVG or Mermaid charts.
- Keep English and Chinese documentation aligned. Keep the README concise and
  detailed usage, client/server examples and reports under `docs/`. Follow the
  current Nimbus site and its checks; do not restore retired documentation tools.

## Release notes

- Lead with the version's core user-visible capabilities and purpose. Group
  changes under Features, Improvements, Fixes and Upgrade notes, using only the
  sections that contain meaningful changes.
- Combine related internal changes into their effect on protocol support,
  reliability, performance or integration. State what users gain or need to do.
- Do not use the raw commit list, a chronological work diary, file-by-file changes,
  formatting edits, test assertion inventories or internal validation checkpoints
  as the main release narrative. Link detailed testing/performance material.
- Preserve compatibility changes, dependency requirements, security requirements,
  material known limitations and relevant measured regressions. Do not label a
  feature production-ready or claim capacity without corresponding acceptance.
- Keep English and Chinese release notes equivalent. Include examples and
  migration steps only when they help users adopt the release.
- Only describe changes contained in the release tag. Verify the release commit,
  assets and checks before publishing. Never move a published tag or attribute
  later `main` fixes, measurements or pending PRs to an earlier version.

## Task execution and CI admission

- Use `gh` CLI for GitHub operations; do not replace it with direct REST/GraphQL
  requests or `gh api`. Normal Git fetch/push commands remain appropriate.
- The root agent alone observes CI and controls PR readiness and queue admission.
  Order admission by PR creation time: finish the oldest unfinished PR before
  admitting newer ones. A newer PR becoming green first is not permission to
  overtake an older PR. Account for shared runner capacity across ark and arknet.
- Continue independent implementation and local validation in parallel. Publish
  focused modules without starting newer native matrices ahead of the backlog.
  Use drafts only when the deployed workflow actually defers draft native jobs;
  otherwise retain a reviewable local branch until admission is available.
- Do not enable automatic merge for held PRs. Release them in creation order;
  after required checks pass, squash through the protected merge queue promptly.
  Do not repeatedly ask for already-authorized merges or bypass any check.
- When a draft-preview workflow is deployed, verify fresh ready-for-review checks
  at the intended head before enabling merge or queue admission. Preview success,
  an absent required context, or older green checks on the same SHA do not
  establish fresh admission. Verify the hosted behavior before claiming it works.
- Give jobs unique, descriptive names. Consolidate compatible jobs/steps to reduce
  setup time without dropping required platforms, protocols or verification.
- Preserve active acceptance runs. If correcting scheduling requires cancellation,
  record its reason and arrange fresh acceptance; cancellation never counts as
  a successful test. GitHub Actions concurrency groups do not by themselves
  enforce PR-creation order.
- Keep one observer per PR batch. Batch status reads, back off unchanged polling,
  and let scripts collect logs and exit codes. Read summaries and relevant failure
  excerpts, not entire repeated logs. Communicate completion, failures, interface
  changes and decisions; do not repeatedly ask agents for unchanged progress.
- Delegate only concrete independent deliveries with a scope and acceptance
  criteria. Do not wake finished agents just to keep them busy. Record a short
  handoff at module completion and preserve unrelated work in dirty checkouts.
- Current-session monitoring does not authorize recurring/background automation.
  Stop owned observers before ending the session unless separately authorized.

## Keep documentation synchronized

- Resolve legacy page aliases to their canonical source before editing. Nimbus
  may skip retired Markdown files even when they remain in Git. Check the emitted
  HTML and Markdown for the changed content; a successful build alone does not
  prove that an edited source is published.
- Treat documentation as part of each functional change. Review both languages,
  README, feature/support tables, API and client/server examples, configuration,
  installation, lifecycle, navigation and affected performance/validation reports.
  Update the relevant material in the same PR; do not routinely defer it.
- Describe the final supported behavior. Remove stale TODO/experimental/private
  development-layer claims when accepted code supersedes them, and avoid
  redundant "implemented", "retest" or "latest test" labels. Keep actual future
  capabilities clearly marked TODO; do not imply they already work.
- Keep ark and arknet boundaries clear. Cross-link the other project's accepted
  public APIs rather than copying them or claiming pending integration is merged.
  Never attribute later main changes or unpublished results to an older release.
- Run the relevant Nimbus/link checks. After publication, verify the Pages run
  succeeded for the intended source revision and inspect affected published pages.
  A local preview, merged PR and deployed site are distinct evidence states.
- Use the full names AddressSanitizer, UndefinedBehaviorSanitizer and
  ThreadSanitizer in explanatory prose. Preserve command flags, paths, workflow
  identifiers and machine-readable fields. Call Asio/Beast dependencies rather
  than backends; capitalize Standalone Asio in prose.
- Prefer SVG or Mermaid for editable diagrams and charts. Show labeled units and
  readable legends, retain useful error bars, and avoid repeating chart contents
  in long prose. Keep the README concise, with properly formatted dependency lists;
  detailed guidance belongs in the bilingual documentation site.

## Evidence and performance boundaries

- Keep raw measurement JSON, packet captures, logs and credentials local, outside
  version control. Do not upload them as release assets, CI artifacts or public
  downloads without separate explicit authorization for those exact files.
- Extend existing doctest correctness tests rather than adding a Boost test
  dependency. Prefer standard C++20 facilities over obsolete wrappers/macros when
  behavior and ownership can be preserved; keep necessary platform/ABI boundaries.
- Maintain compile-and-run C++ tests and benchmarks in their project directories;
  do not require Python or Ruby as the user's test entry point. Shell scripts may
  orchestrate internal batches. Do not replace permanent tests with temporary ones.
- Measure performance locally before writing reports. Identify machine, OS,
  compiler, dependency versions, source/binary identity, duration and units.
  Compare payload sizes, in-flight batches, repeated runs, coroutine/callback
  interfaces and IO/thread models, including one context/one thread and one
  context/multiple threads. Report throughput, latency and CPU utilization;
  100% CPU denotes one logical core, with the measurement scope stated.
- Retain the final valid results and material limitations, not a retesting diary.
  Functional checks, sanitizer checks, performance measurements and deployment
  capacity are separate claims. Loopback tests do not prove production PCU or
  cross-host capacity; successful controls do not establish an unresolved root
  cause. Do not extend timeouts or rerun to green to conceal failures.
