# Plan 003 execution review

Verdict: **APPROVE**, 2026-10-11. All four software fixes are complete. Implementation is isolated under
`execution-worktrees/plan-003/backend` and its `teddycloud_web/`, on
`fix/four-source-review`, based on backend `d372dcd7` and frontend `0d31514`.
The user requested all four reviewed fixes. Source scope excludes dependencies,
publishing workflows and generated bundle/gitlink changes.

## Reviewed behavior

- Profile GET/POST load without repair, check revision before first creation,
  and reject corrupted content. A narrow filesystem probe rejects directories
  and stat errors before loading; only ENOENT permits an empty record.
- Source editing and portal controls are disabled throughout initialization;
  Cancel remains available. Real TonieCard integration tests exercise input,
  clear/reset, file, sibling and model-audio actions during pending bootstrap.
- Inline detach clears the editing subscription and reconciles the confirmed
  source/profile baseline while preserving local drafts. Save and close stay
  locked through readback; failed readback visibly blocks Save. Tests exercise
  first-attempt Audio/Radio saves and a new Podcast without the deleted ID.
- An additive, source-bound `sourceAssignment` marker records explicit Audio or
  Radio without changing Content JSON version 5. Same-URL switches persist;
  metadata/profile edits preserve valid markers, legacy assignments clear them,
  stale markers are ignored, owning podcasts take precedence, and detach restores
  the original explicit kind. API and frontend tests cover these transitions.

The advisor read the entire source/test diff and checked every changed path
against the plan. No dependency or workflow change was introduced.

## Review round 1

An initial independent exact frontend run failed under resource pressure with
worker startup errors and an unchanged render-test timeout. A bounded rerun
passed all 177 assertions but exited 1 because Ant Design delayed state callbacks
outlived jsdom in the modal render tests. The executor added scoped fake timers
and drained pending callbacks inside `act` after unmount, without suppressing
errors, skipping assertions or raising global timeouts. Independent exact
`npm test` then passed all 18 files / 177 tests in 5.13 seconds, exit 0.

The same round tightened the metadata filesystem probe to reject nonregular
paths before opening them and fail closed if an existing record disappears
between probes. A directory/sentinel regression verifies preservation.
A TagIndex test initially collided with other fixtures sharing its directory
prefix; assigning a unique prefix retained every assertion. The new UI fixture
also isolates unrelated live radio search; actual source-editor hooks/controls
remain under test.

Independent type checking, formatting, all six translations and production
frontend build passed. Existing Vite chunk-size advice is unchanged.

## Final independent verification

Final reviewed backend is `a5e904a579769f400d303f1e04d597cb1581e0cd`;
frontend is `3def7e24a88864fb629dde98291580789a2ff2eb`. Root reran final exact
`npm test`: 18 files / 177 tests, exit 0, 10.15 seconds. Root also passed
`npx tsc --noEmit`, `npm run format:check`, `npm run check:translations` and
`npm run build`. All old 169 tests remain; eight new regressions are present.

The executor's full `podman build -f DockerfilePodcastTests` exited 0 and produced
`3041bc116d8936a3f5460de30178892d8f2f8da2c45fea0265c6123c6130fdae`
(`localhost/teddycloud:review-fixes-tests`). Root checked source/test SHA-256 values
inside this immutable image against the reviewed backend; all match. Root reran:

| Command inside image | Result |
| --- | --- |
| `make test_podcast` | Parser/selection/network-policy checks, 22 API tests, all 10 profile tests pass |
| `make test_c` | 9 pass |
| `bash tests/py/with_server.sh 0 0 0 180 -- python3 tests/test_source_profiles.py` | 10 run, one existing fixture-only race test skipped in production binary |
| `make test_py TESTS=custom_json NO_SANITIZERS=1` | 5 pass |
| `make test_py TESTS=box_sim NO_SANITIZERS=1` | 15 pass |
| Disabled production binary: disabled API and profile scripts with same wrapper | API contract passes; 10 profiles run, same intentional skip |
| Disabled-testing binary: profile script with `TC_LEGACY_RACE_TESTING=1` | All 10 pass, including race regression |
| `ldd bin/podcast-disabled/teddycloud` | Only libc/libm/loader; no curl or XML dependency |

The backend image captured frontend `c14083e`; the subsequent `3def7e2` changes
only the unrelated provider mock in a test. Application source and bundle are
identical. Rather than rebuild all backend modes and certificates for a test-only
change, root independently ran the final Docker `--target frontend` stage, exit 0:
`b961fa622d2828a1a9406de82f3156bc27d64ad5a5064c5d642fc0677f1b0678`
(`localhost/teddycloud:review-fixes-frontend`). It runs the complete final frontend
suite, formatting, translations and production build. This documented adaptation
preserves all gates and avoids unnecessary expensive backend re-verification.

Both final diffs pass whitespace checks and allowed-path audits. No source changes
followed the successful final checks. VM free space was 1.1 GiB; the 440 MiB guard
was retained. Only verified orphan build processes from this plan were terminated;
no user container, volume, broad cache prune or unrelated source was changed.

## Authorized local feature integration

Following the user's feature-branch integration instruction, root cherry-picked
the approved source commits onto both `feature/prepared-podcast-tonies` branches.
Frontend head is `e94f205cf51f8c7733a78d5e70ef320fb1c1490e`; backend source checkpoint
is `b32a455439921ef63f5ac945800db6453aabc5a3`. Both source trees exactly match the
reviewed worktrees (zero tree diff).

Root rebuilt the bundle with `npm_config_offline=true npm_config_audit=false make web`.
All 338 files except `web_version.json` match the independently verified frontend
build byte for byte. Frontend metadata records its clean current SHA. A separate
local integration commit, `242dadad3e7e5b9b8827daeaeebe4b7b918508b0`, contains only
the bundle and gitlink update. `make test_git_version` passes after that commit;
its precommit rejection of dirty generated files was expected. Both final feature
trees are clean. A concurrent fetch revealed remote automatic commit `1c2f5a72`,
which only reordered the old `web_version.json` keys. Root merged that commit,
resolving its generated metadata conflict by retaining the verified current
frontend metadata. Final backend head is `2d0cc0f1b414cd329b40ce835aabee2e024b020d`;
its tree is identical to `242dadad`, and Git-version validation passes again.
Backend is six commits and frontend three commits ahead of their tracking refs,
with neither behind. No follow-up push was performed.

## Acceptance boundary

No live browser or physical Toniebox acceptance is claimed. Those parent-feature
gates remain open in plans 001/002. The preview stays stopped. No follow-up branch
push, image publication or deployment is authorized by this implementation task.
