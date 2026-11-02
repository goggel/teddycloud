# Plan 004 execution review

Verdict: **APPROVE software implementation**, 2026-10-12. Plan status remains
**IN PROGRESS** because real-browser audio acceptance could not run.

Implementation is isolated in `execution-worktrees/plan-004/backend` and its
`teddycloud_web/`, on `feature/recent-episode-cut-preview` in each repository:

| Repository | Base | Reviewed commit |
| --- | --- | --- |
| Backend, documentation only | `2d0cc0f1b414cd329b40ce835aabee2e024b020d` | `06a08c9d593cc7bbdff5da1f2368aaf2cc74f50c` |
| Frontend | `e94f205cf51f8c7733a78d5e70ef320fb1c1490e` | `b071f72459e76112764d86aa4c6d59894c2c9e82` |

Plan 003 was integrated before dispatch. Its only podcast-directory drift was
the reviewed detach reconciliation in `usePodcastSubscription.ts`; this execution
does not modify that hook. At review completion, original application trees were
clean at their base commits and nothing had been merged or pushed. Subsequent
user-authorized integration and pushes are recorded below.

## Reviewed behavior and scope

- The recent list independently requests five newest episodes, or a smaller
  advertised limit, with zero cuts and empty overrides. Abort and generation
  guards reject obsolete responses. Listening does not update the show draft,
  playlist count/order, or persistence. Canonical ARD resolution uses the existing
  source-resolution path.
- One native player mounts after explicit episode selection. Metadata may load
  then; playback requires a gesture. It preserves the original URL and clock,
  validates HTTP(S) without credentials, and explains HTTPS/HTTP restrictions.
  Native duration controls capture math. Seeking failures and play rejections
  remain visible. No backend media endpoint or proxy was added.
- Context and retained audition stop at their boundary. Manual seeking and
  ordinary playback clear the audition stop. Changing cuts or edit target pauses
  audition. Source/episode/URL changes, reload and close unload media; late events
  and promises cannot replace current session state. Other audio elements
  and the preview pause each other without clearing other sources.
- Captures edit only the current draft and relevant default side. Episode
  captures preserve the other effective side, explicit zero, canonical source
  binding and inactive overrides. Source mismatch, locking and the 50-entry cap
  protect mutations. Tests exercise Save and Cancel/reopen through the existing
  subscription hook.

The advisor read the source/test diff and audited paths: all 16 frontend paths
are explicitly permitted by plan 004; the backend commit changes only
`docs/podcasts.md` (16 added lines). No dependency, backend code, bundle, or
gitlink change is committed. The backend worktree reports `M teddycloud_web`
because its nested frontend is at the reviewed feature commit; this expected
local pointer difference was excluded from the documentation commit.

All prior translation values are unchanged. Six catalogs contain 34 new
keys each. English and German copy is localized; French, Spanish, Italian and
Polish retain English fallback for these controls, matching existing podcast
entries. Full localization of those four catalogs is not claimed.

## Review revisions and justified adaptations

Review tightened native session guards, capture validation and functional draft
updates, mutual exclusion with any audio element, unsupported-provider gating,
and the distinction between default and episode audition. Additional rendered
tests independently exercise capacity, locking, source mismatch, canonical ARD
binding, legacy listening and Cancel after capture. Pause spies are reset before
the event whose effect is asserted, including the audition boundary stop.

The recent metadata request also uses a neutral six-hour interval, while source
validation ignores unrelated invalid draft numeric settings. This small
adaptation lets users listen while correcting invalid cuts/count/interval; it
does not modify the draft or weaken Save validation. Metadata preload begins
only after explicit Listen, enabling seeking before playback. Changing the edit
target pauses without remounting or refreshing the media URL.

One intermediate independent formatting run failed on
`usePodcastRecentEpisodes.test.tsx`. The executor applied scoped Prettier
formatting, and the final independent check passes. No existing assertion was
removed or weakened.

## Independent final verification

Commands ran inside the isolated frontend unless stated otherwise:

| Gate | Result |
| --- | --- |
| `npm test -- src/components/tonies/toniecard/podcasts` | 9 files / 83 tests pass, exit 0 |
| `npm test` | 22 files / 202 tests pass, exit 0; final committed source, 11.49 seconds |
| `./node_modules/.bin/tsc --noEmit` | Exit 0 |
| `npm run format:check` | Exit 0, including final committed test correction |
| `npm run check:translations` | Exit 0; six catalogs and statically referenced keys match |
| `npm run build` | Exit 0; existing Vite chunk-size advice remains |
| `git diff --check <base>..HEAD`, each changed repository | Exit 0 |
| Allowed-path and old-translation preservation audit | All 16 frontend paths allowed; existing values unchanged in six catalogs |

The complete suite includes all 177 previous tests and 25 added tests. The jsdom
pseudo-element `getComputedStyle` notices do not fail the run. Build/type/
translation checks cover final application source; subsequent changes were
test formatting and one additional spy reset, followed by final exact suite and
format checks. Backend compilation/tests were not repeated for documentation-only
changes. No general repository audit or parent-feature hardware validation was
performed.

## Browser acceptance remains pending

Both advisor and executor invoked the actual browser-control entry point,
`await cua.getState()`. It failed with
`CUA_REPL_ENABLED_SURFACES is required`. Browser versions and actual codec support
could not be observed. No live sandbox or household assignments were modified.

| Acceptance scenario | Result |
| --- | --- |
| Generated RSS with at least three distinct intro/main/outro fixtures | Pending; browser unavailable |
| Public RSS and ARD via production metadata: actual playback and seeking | Pending; browser unavailable |
| Audible intro/outro context and retained boundaries | Pending; browser unavailable |
| Native no-autoplay, session switching/close and footer coexistence | Pending in real browser; mocked media regressions pass |
| Default/override/zero, Save/reopen and Cancel | Pending in real browser; rendered regressions pass |
| Keyboard and narrow-screen layout | Pending; browser unavailable |
| Unsupported/nonseekable media and HTTPS/HTTP enclosure | Pending in real browser; error-handling regressions pass |

To close acceptance, use the isolated sandbox with this matching frontend build
and follow plan 004 step 5 in available Chromium and Safari. Record browser
versions, actual media formats and pass/fail evidence. Mocked media events prove
control logic, not decoding or audible timing. Plan 004 must remain IN PROGRESS
until that gate is completed. Earlier Toniebox acceptance remains separate.

## User-authorized feature integration and push

On 2026-10-12 the user requested adding the reviewed implementation to the
feature branch and pushing it. The advisor fetched both origins, confirmed
neither branch was behind, and cherry-picked the reviewed changes onto
`feature/prepared-podcast-tonies`. Frontend source is exactly the reviewed tree
at integrated commit `de9446bbeda1600940a6642879111b18fd13d0ac`. Backend documentation
was integrated at `17bbefa6`; its source tree matches the reviewed backend,
excluding the expected frontend pointer difference.

`npm_config_offline=true npm_config_audit=false make web` passed. All 338 bundled
files, excluding `web_version.json`, match the independently reviewed production
build byte for byte. Version metadata identifies the clean integrated frontend.
Separate bundle/gitlink commit `6c361acd704854e88fa2eb92ed639b2c241bc99c` contains only
the generated bundle and frontend pointer. `make test_git_version` and diff
hygiene passed; the frontend lockfile and source remain unchanged by bundling.

Pushes succeeded to the configured `goggel` origins, frontend first and backend
second. Final frontend is `de9446bbeda1600940a6642879111b18fd13d0ac`; final backend is
`6c361acd704854e88fa2eb92ed639b2c241bc99c`. Both application trees are clean, on
`feature/prepared-podcast-tonies`, and exactly match their remote tracking refs
(zero ahead/behind). These pushes also include the previously local plan 003
fixes. No publishing workflow, deployment, or household-data action was performed.
Real-browser acceptance remains pending; integration does not mark plan 004 DONE.
