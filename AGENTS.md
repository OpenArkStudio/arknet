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
  does not validate that combination. Add eligible PRs to the queue promptly.
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
- Required checks include the configured Boost and Standalone Asio, operating
  system, TLS, sanitizer, documentation and formatting coverage. Do not omit
  checks or fabricate success to make a PR mergeable.
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
- Build and run the affected native targets, then the required suite as needed:
  `cmake --build build --target arknet_check --config Release`.
- Keep third-party licenses and notices intact; do not reformat vendored code.

## Tests, benchmarks and documentation

- Keep correctness tests under `tests/` and permanent native C++ benchmarks under
  `benchmarks/`. Users must be able to compile and run them without Python or Ruby;
  shell scripts may orchestrate internal batches.
- Keep raw measurement JSON and build logs outside the source repository. Use
  release attachments or CI artifacts for detailed evidence.
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
