# Plan 003: Fix the four source-editor review findings

## Status and intent

- DONE for the four software fixes; implemented, independently reviewed and
  integrated locally on 2026-10-11. See [verification record](003-execution-review.md).
- Planned at backend `d372dcd749360ae8da10bd274d238f4fac3335d9` and frontend
  `0d315144e037ac1a7bee52ee68775548d5f3f8bb`, 2026-10-11.
- Backend root: `/Users/benjaminkoster/git-privat/teddycloud/teddycloud`;
  frontend: its `teddycloud_web/` submodule. Both currently clean and pushed on
  `feature/prepared-podcast-tonies` at planning time. Final local heads are backend
  `2d0cc0f1` and frontend `e94f205`; both clean and ahead of their pushed baselines.
- Depends on the implemented software of 002, not its open browser/device gate.
- Category bugs/UX; effort M overall, risk MED because one additive assignment
  field crosses backend/frontend. No new multiuser or transaction architecture.

Four verified review findings: profile POST repairs damaged existing metadata
before checking revision; bootstrap resets audio edits accepted during loading;
inline podcast detach leaves a deleted editing subscription and stale source
baseline; explicitly choosing Radio reopens as Audio if both profiles use the
same URL. Fix these without changing the four-source product or adding services.

## Current state and conventions

- `src/source_profiles.c:169`: `load_content_json(path, &content, save, settings)`.
  With save=true, `src/contentJson.c:172–181` rewrites on any load error, not just
  a missing file. Revision checks happen afterward.
- `TonieCard.tsx:102` gates `useAudioSourceEditor` on `podcast.initialized`.
  Its opening effect clears touched kinds and resets drafts. The modal's
  fieldset disables only for `busy`; audio input/file actions remain editable
  during initialization.
- `usePodcastSubscription.ts:152–160` inline detach clears subscription state
  but not `editingBase`. `TonieCard.tsx:763–765` only refreshes the card. New
  save flow uses `podcast.editingSubscription`; backend deletes detached IDs.
- `sources/sourceProfiles.ts:40–41` classifies matching radio URL as Radio only
  if the Audio profile has a different URL. No explicit active kind is stored.
- C/cJSON with explicit ownership/freeing and common Content lock; lock order
  Content → Service. React/TS, Ant Design 6, i18next, four spaces. Follow existing
  profile API tests and rendered source-editor tests; do not weaken old assertions.
- One actual active assignment; inactive profiles are settings only. Legacy HTTP
  audio/TAP stays Audio when ambiguous. Source-bound metadata must not override
  actual assignment or owning podcast. Sequential saves with visible partial
  failures remain intentional; cross-API atomic transactions are out of scope.

## Scope

Backend allowed:
`include/contentJson.h`, `src/contentJson.c`, `src/source_profiles.c`,
`src/handler_api.c`, `src/podcast.c` (only assignment-marker preservation/reset
where necessary), `tests/test_source_profiles.py`, `tests/test_podcast_api.py`,
`tests/run_podcast_container_tests.py`, `docs/api/source-profiles.md`.

Frontend allowed:
`src/types/{audioSourceTypes,tonieTypes}.ts`, `src/api/apis/TeddyCloudApi.ts`,
`src/components/tonies/toniecard/TonieCard.tsx`,
`.../hooks/useTonieCardSaveFlow.ts`, `.../modals/EditTonieModal.tsx`,
`.../sources/{AudioFileSourceEditor,RadioSourceEditor}.tsx`,
`.../sources/{useAudioSourceEditor,sourceProfiles,saveAudioSource}.ts`,
`.../podcasts/usePodcastSubscription.ts`, existing corresponding tests, and new
`.../sources/TonieCard.integration.test.tsx` if useful. Here `...` means
`src/components/tonies/toniecard`. Six existing translations may change only if
these fixes need user-facing text. API tests may be extended in their existing file.

Out of scope: dependency changes, generated bundle/gitlink in implementation
commits, broad mutex refactoring, authentication, publishing workflows, new
podcast provider behavior, physical playback, unrelated UX improvements.

## Git workflow

Use isolated disposable backend/frontend worktrees under
`execution-worktrees/plan-003/`, branches `fix/four-source-review` based on the
two planned-at commits. Preserve the original feature trees. Initialize pinned
backend submodules from available local clones/caches; do not edit dependencies.
The existing plan-002 integration checkout has self-contained dependency Git
metadata if needed. Use the existing lockfile (`npm ci --offline` if cached).
Commit logical fixes separately. No push or PR. Advisor maintains the index,
reviews the full diff and reruns checks before integrating into the already
authorized feature branches. Any later bundle/gitlink update is a separate local
integration commit, not part of the source-fix diff.

Before editing, run `git diff --stat <planned-at>..HEAD` in each worktree;
expect no differences. Stop on unrelated drift or dirty user data.

## Implementation steps and regressions

1. **Protect existing metadata.** Profile GET and POST first load without repair.
   Only a genuinely absent file may initialize defaults. Check expected revision
   before creating a missing file (a stale revision must not materialize it).
   Return 503 on malformed/truncated/unreadable existing content and preserve its
   bytes. Probe filesystem status locally: the portable existence helper also
   reports directories and permission/stat errors as missing. Reject those with
   503; only actual ENOENT permits defaults. Retain version conflicts and both podcast build modes. Do not globally
   change legacy repair policy. Add POST tests for malformed/truncated data,
   mismatched expected revision on an absent file, and successful first save.
   Run targeted source-profile API tests; all pass, corrupt file byte-identical.

2. **Lock editing through bootstrap.** Prefer disabling source inputs and file/
   model-audio/sibling actions until initialization finishes; include Ant Design
   portal/action controls, not just a native fieldset. Existing late profile-GET
   touched-draft preservation remains. Keep Cancel available during loading.
   Add a rendered regression with podcast bootstrap deliberately pending: source
   input and actions cannot change drafts until initialized. Verify normal edits
   after readiness, Cancel/reopen, and late GET behavior still pass.

3. **Reconcile inline detach.** After confirmed detach, clear editing subscription
   through the common record operation and reconcile actual assignment/baseline
   in the integrated source editor. Preserve all four local drafts and selected
   target where useful, but do not claim detached Podcast remains active. Handle
   failed readback explicitly rather than inventing an assignment. Add integrated
   detach→Audio/Radio save and detach→new Podcast save tests; successful save on
   first attempt, no second detach or obsolete ID. Retain CAS polling semantics.

4. **Persist explicit Audio/Radio selection with assignment.** Add one optional
   additive `sourceAssignment` object to Content JSON, shaped
   `{kind:"audio"|"radio",source:<exact assigned URL/path>}`. Load/copy/free/save
   it consistently. In the existing direct Content setter accept an optional
   `sourceKind` field only alongside `source`, validate it before writes, and
   save assignment plus marker under the same existing lock, even when the URL
   is unchanged. A legacy explicit source assignment without `sourceKind` must
   clear the marker, even at the same URL. A source change by another writer
   must not reuse a stale marker: expose/use it only when its exact source still
   matches actual assignment. Metadata-only writes retain a valid marker.
   Keep Content JSON version 5. An owning podcast takes precedence over marker;
   profile save never sets it. Return valid `sourceKind` in TagInfo/TagIndex,
   carry it through TonieCard and readback, and use it before legacy profile
   inference. Frontend sends selected kind during direct activation, including
   same-URL switches. Old records/servers retain existing conservative fallback.
   Add API and frontend regressions for identical-URL Audio→Radio→reopen→Audio,
   marker persistence, metadata updates, legacy assignment invalidation, manual
   replacement and podcast ownership precedence. Run them in both build modes.
   Document the additive marker and sourceKind setter contract.

## Verification commands and done criteria

Frontend (its isolated root): `npm test`, `npx tsc --noEmit`,
`npm run format:check`, `npm run check:translations`, `npm run build`.
All exit 0; previous 169 tests retained and meaningful new regressions present.
The existing large-chunk advisory is not a gate failure.

Backend (isolated root):

```sh
podman build -f DockerfilePodcastTests -t localhost/teddycloud:review-fixes-tests .
podman run --rm localhost/teddycloud:review-fixes-tests make test_podcast
podman run --rm localhost/teddycloud:review-fixes-tests make test_c
podman run --rm localhost/teddycloud:review-fixes-tests bash tests/py/with_server.sh 0 0 0 180 -- python3 tests/test_source_profiles.py
podman run --rm localhost/teddycloud:review-fixes-tests make test_py TESTS=custom_json NO_SANITIZERS=1
podman run --rm localhost/teddycloud:review-fixes-tests make test_py TESTS=box_sim NO_SANITIZERS=1
```

All exit 0. Dockerfile also runs frontend checks and disabled build/API tests;
ensure new profile regressions execute with disabled support. Inspect disabled
binary with `ldd`: no curl/XML. Preserve all old race/ownership tests and the
440 MiB podcast storage guard. Check VM free space first; remove only provably
owned disposable artifacts if necessary, never broad prune or live dev volumes.
Use actual fixture wrappers/variables; test-only pause markers stay out of
production. Finish with `git diff --check` and allowed-file/status audit in both
repos. No claim of live browser/device acceptance; preview stays stopped.

## STOP and maintenance notes

Stop for scope expansion, a repeated verification failure after a focused fix,
or an assignment marker that would require a second playback authority. Report
actual evidence and partial work; do not skip tests or lower storage guards.
Marker validity is tied to the actual source, not inactive saved profiles.
Future source writers must invalidate or correctly rebind it; legacy requests
remain supported. Newly missing files and unreadable existing files must remain
distinct. Source 002 browser/device acceptance stays open after these fixes.
