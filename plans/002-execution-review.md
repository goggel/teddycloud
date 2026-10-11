# Plan 002 execution review

Current integration status: **Integrated locally at the user's request**, 2026-10-11.
Acceptance verdict remains **BLOCK — browser acceptance unavailable**.
Implementation, scope review and all automated gates pass on the final snapshot.
The direct browser attempt returned `No browser is available`; physical Toniebox
acceptance also remains open. This is an acceptance limitation, not an unresolved
source defect or a request for another implementation revision.
The focused regression and minimal persistence fix were authorized on 2026-10-10.
The previous persistence-scope BLOCK below is retained as history and is resolved.
The user subsequently authorized integration into the existing feature branches
and stopping the preview, then explicitly requested pushing both branches.
Integration and both pushes are complete, as recorded below.

## Isolation and comparison

- Backend worktree: `execution-worktrees/plan-002/backend`.
- Frontend worktree: its `teddycloud_web/` directory.
- Both branches: `feature/plan-002-four-sources-resumed`.
- Compare backend against `deb98225`, frontend against `1955814`; these preserve
  the pre-existing feed-inspection work separately from plan 002.
- Recovery checkpoints: backend `c52a27d3`, frontend `cb700bc`. The original
  temporary worktrees disappeared overnight; the executor recovered their
  verified Step 1–3 sources from the disposable test image into durable worktrees.
- STOP checkpoints: backend `bfeaf04c469c3b900a4b7bf714dd02629ff95f22`,
  frontend `d306b1fa126cfefa598d719d7823f5caa0186d4a` (the final commit only formats
  the last-added test; implementation checkpoint `a5aa488`).
- Focused persistence checkpoint: backend
  `1831505d1677130731e123549c1cdfc17d3d1bbf` (`Preserve source profiles across
  legacy content writes`). The frontend remains at `d306b1fa`; only the expected
  nested gitlink is unstaged in the backend.
- Test-image packaging checkpoint: backend `532bb9a9`; frontend remains `d306b1fa`.
- Review-round-1 checkpoint: backend `8339af0e`; deterministic chunked-header
  fixture, direct stale move regression, pending-metadata preservation, and
  production fixture-string checks. Frontend remains `d306b1fa`.

The improve advisor reads/reviews source and runs verification; a separate
executor made the implementation changes. The original application trees were
untouched during review; the later explicit integration request is recorded below.

## Independent verification — 2026-10-10

Frontend checks in the executor worktree:

| Command | Result |
| --- | --- |
| `npm ci --offline` | Exit 0; cached lockfile installation |
| `npm test` | Exit 0; 17 files, 169 tests, including the final CAS regression |
| `npx tsc --noEmit` | Exit 0 |
| `npm run format:check` | Exit 0 |
| `npm run check:translations` | Exit 0; DE/EN/ES/FR/IT/PL |
| `npm run build` | Exit 0; existing large-chunk advisory |

The final rerun included the newly added editing-CAS integration test. Its only
formatting issue was corrected by the executor in `d306b1fa`; no implementation
changes followed the successful frontend checks. Backend committed changes
remain within the allowed file list; no generated bundle or frontend gitlink
was committed. The backend worktree has only its expected unstaged nested
frontend gitlink difference, and the frontend worktree is clean.

At this historical checkpoint, final backend and production checks were pending
and the image build was interrupted at the STOP boundary (exit 130). Those checks
have since passed on the final snapshot below. Browser and physical acceptance
remain open; earlier Step 1–3 images are not used as final verification evidence.

## Confirmed blocker

`src/handler.c:890` calls `mutex_lock_id`, but `src/mutex_manager.c:46` returns
immediately (line 49). `saveTonieInfo` (`src/handler.c:978`, save at line 982) and
move-to-library (save at line 1210) can write an old Content JSON snapshot after
a profile save. Bulk model reassignment (`src/handler_api.c:4386`, save at line
4399) has the same problem and does not take the shared content mutex.
Copying the profile fields through those snapshots preserves sequential
roundtrips but cannot preserve a newer concurrent profile revision or opaque
future-version profile data. The new CAS tests cover the ordinary content setter,
not these other writers; they therefore cannot establish the full requirement.

The executor correctly stopped rather than editing out-of-scope `src/handler.c`
or redesigning legacy persistence silently. Fixing the old ID-lock no-op alone
would also reintroduce its documented TagIndex deadlock risk. Plan 002 now records
this prerequisite explicitly. No final backend or deployment approval is given.

## Authorized resumption

The user requested a proportionate correction for background overlap with a
single operator. Plan 002 step 2a now explicitly permits the three affected
legacy write paths and a common short update helper. The executor must first
reproduce a real lost update, then preserve current unrelated fields by reloading
under the shared content lock and applying only intended changes. Stale intended
source replacements must preserve the newer assignment and report a conflict.
The disabled ID locks, broad locking architecture and deployment remain outside
this correction. Final backend/production/browser gates still apply.

The advisor independently reproduced the loss in test image
`73ed11bb31577ce4c06b94a2a21064216c004f4f56a24d0e1999a3195e44b591`
with:

```sh
podman run --rm localhost/teddycloud:four-sources-tests env TC_SERVER_BIN=/buildenv/bin/podcast-test/teddycloud TC_LEGACY_RACE_TESTING=1 bash tests/py/with_server.sh 0 0 0 180 -- python3 -m unittest tests.test_source_profiles.SourceProfilesAPI.test_legacy_metadata_save_cannot_erase_new_profiles
```

Exit 1: both deterministic subcases failed. An actual paused `saveTonieInfo`
write erased a profile revision saved through the profile API, and erased opaque
profile data with unknown version 987. This establishes a concrete background
overlap defect without requiring simultaneous editors. The regression uses
bounded, test-build-only file pause points; production builds expose no pause API.

With the focused fix, the advisor independently ran the lost-update regression
and bulk model rename regression in cached test image
`6c14889f9c071858a6f922ba5d47ba47f354705bb9475e0be69de92daf67b1d8`:

```sh
podman run --rm 6c14889f9c071858a6f922ba5d47ba47f354705bb9475e0be69de92daf67b1d8 env TC_SERVER_BIN=/buildenv/bin/podcast-test/teddycloud TC_LEGACY_RACE_TESTING=1 bash tests/py/with_server.sh 0 0 0 180 -- python3 -m unittest tests.test_source_profiles.SourceProfilesAPI.test_legacy_metadata_save_cannot_erase_new_profiles tests.test_source_profiles.SourceProfilesAPI.test_bulk_model_rename_preserves_current_profiles_and_flags
```

Exit 0, 2 tests. The first test now also verifies that a newer source and its
live/no-cloud/cloud-override flags survive, and that a malformed fresh read is
left byte-for-byte untouched. The second verifies model rename with opaque
future-version profiles. This cached image establishes the focused correction;
it does not replace the final clean image, disabled-build or production gates.

The final image rebuild encountered Podman VM storage exhaustion while copying
frontend `node_modules`. The scoped test Dockerfile now retains frontend checks
in its frontend stage and copies only the built UI into the backend-test image,
avoiding that redundant dependency layer. Backend tests remain runnable in the
finished image. This test-packaging adjustment does not change the production
Dockerfile or runtime and does not waive any verification gate.

The first full slim-image attempt reached the backend suite but failed podcast
preparation assertions. The executor's retained retry log now identifies
`lastError='Not enough free podcast storage'` in the protocol-pins and trim tests.
The guard in `src/podcast.c` requires 440 MiB available before a download
(`PODCAST_AUDIO_LIMIT` is 200 MiB, plus a 240 MiB reserve);
the VM had only 338 MiB before further build layers. The same source layer
`90cac356f311`, without an implementation change, passed 20 podcast API tests and
8 profile tests in a standalone diagnostic run. The failure is therefore an
observed storage gate, rather than evidence of a persistence regression. Further
test-image trimming must preserve the storage guard and all tests. Clean-image
and independent final reruns remain required; no integration approval is issued.

## Independent final-image checks — 2026-10-11

The clean build completed successfully in image
`7aafc2009459f8a05cfbecc750b02bdee0f510c5f9fad3a185b8f1809ee5ee97`.
The advisor checked that `src/contentJson.c`, `src/handler.c`, and the Dockerfile
hashes match the execution worktree. All three enabled/disabled test and
production-mode binaries are present; Node is absent from this backend-test
image. The VM has 2.0 GiB free after selective Plan-002 artifact cleanup.

| Independent command in the image | Result |
| --- | --- |
| `make test_podcast` | Exit 0; parser/unit tests, 20 podcast API tests, 8 profile tests, including the legacy regression |
| Disabled T0 `tests/test_podcast_disabled.py` | Exit 0; disabled capabilities/API verified |
| Disabled T0 `tests/test_source_profiles.py` | Exit 0; 8 tests, test-only pause case intentionally skipped |
| Disabled T1 `TC_LEGACY_RACE_TESTING=1 ... tests/test_source_profiles.py` | Exit 0; all 8 tests, no skip |
| Disabled T0 `ldd` check | No curl/XML linkage |
| `make test_c` | Exit 0; 9 tests |
| `make test_py TESTS=custom_json NO_SANITIZERS=1` | Exit 0; 5 tests |
| `make test_py TESTS=box_sim NO_SANITIZERS=1` | Exit 0; 15 tests |
| `make test_py TESTS=taf_length NO_SANITIZERS=1` | Exit 0; 1 test |

An additional explicit default enabled-T0 profile run failed in the chunked-body
fixture: `urllib` raised `URLError(BrokenPipeError)` while uploading the final
chunk after the server's immediate header rejection. Review verdict REVISE,
round 1: make this request deterministic with a header-only chunked request and
assert the actual HTTP 413 response; do not accept transport errors as success.
The executor is also adding the plan's direct stale move-to-library regression.
A rebuilt image and independent affected-profile/move checks remain required.
During that review, the advisor confirmed a successful-move regression:
`getTonieInfoV2` can set an auto-detected model and `_updated`, but the new move
path cleared `_updated` without saving that intended metadata. The correction
must retain the pending metadata merge through `freeTonieInfo`; it must keep the
move helper source-only and never restore whole-snapshot writes. This feedback
is part of review round 1, not a second revision verdict.
Checkpoint `8339af0e` removes only the successful move's premature `_updated`
clearing. Code review confirms the subsequent intent merge leaves the moved
source intact while applying a pending model delta. The successful-move test
asserts preservation of an existing on-disk model and profiles; it does not
directly construct a newly auto-detected model. The race test asserts the newer
source, flags, profile revision/data, and retention of the original TAF.

### Final reviewed snapshot

Final test image:
`13a72fad64c20e70bbbe36c71521c1b3cdaf1d498e64b76e3a882d2db888f2bc`,
backend `8339af0e`, frontend `d306b1fa`. The advisor independently reran the exact
test-image build (exit 0, cache hits, same immutable image ID) and checked hashes
of the two persistence sources, both API test files, and container harness.

All independent backend gates pass on this final image:

- `make test_podcast`: parser/unit tests, **21 podcast API tests + 8 profile
  tests**, including the direct stale move and successful move assertions.
- Explicit default enabled-T0 profile run: **8 tests**, one intentional T1-only
  skip; the deterministic actual-413 assertion passes.
- Disabled-T0 API and profile checks: verified, **8 profile tests**, same
  intentional skip; no curl/XML linkage.
- Disabled-T1 profile checks with `TC_LEGACY_RACE_TESTING=1`: **all 8**, no skip.
- C **9**, custom JSON **5**, simulated box **15**, TAF length **1**: all pass.

Review round 1 is resolved. A Python ResourceWarning for a closed test socket
appeared in the passing podcast suite; no assertion or command failed.
The standalone production integration checkout is clean at `8eb39cfc`; its
reviewed source/test hashes match the feature checkpoint, with the generated UI
and frontend gitlink confined to that integration checkout. Independent
`make test_git_version` reports valid backend `8eb39cfc` and frontend `d306b1fa`.
The first production build exposed Git alternates in eight integration-only
dependency clones: their objects were available on the host but referenced paths
outside the container. The executor repacked those clones, removed their
alternate references, and verified connectivity. This is a Git-metadata-only
packaging correction; no tracked source changed.
The retry passed and produced production image
`04eb430da969da91c44284fc0f3169a8f33cad156965fa53b504c14851b457c9`.
The advisor independently reran standalone `make test_git_version` with exit 0;
both integration backend and frontend revisions are valid. The advisor also
independently reran the exact production build with exit 0 and the same immutable
image ID, then ran:

```sh
python3 tests/run_podcast_container_tests.py --image localhost/teddycloud:four-sources --test-image localhost/teddycloud:four-sources-tests --engine podman
```

Exit 0: fixture tests (21 podcast API + 8 profile), root smoke/persistence, and
PUID=1000 smoke/persistence all passed. The harness verifies names and profiles
after restart, required capabilities and bundled translations, runtime libraries,
and absence of Node/Python, fixture routes and both legacy pause markers in the
production binary. It used and cleaned up only its own disposable containers and
volumes.

Final scope/status checks pass: backend diff is 22 allowed files, frontend diff
30 allowed files, and both `git diff --check` commands return 0. Feature backend
`8339af0e` has only the expected unstaged nested frontend gitlink; frontend
`d306b1fa` and integration `8eb39cfc` are clean. Original backend `dbed00d8` still
has only its pre-existing frontend gitlink difference, and original frontend
`9cf6bf8` is clean. Generated bundle/gitlink commits exist only in integration.

### Remaining acceptance and reproducible handoff

The advisor attempted the supported browser entry point against the actual
running preview, `cua.getBrowser({url:"http://127.0.0.1:33513/web/"})`.
It returned exactly `No browser is available`. No browser interaction or visual
acceptance is claimed. Rendered component tests pass but do not replace this gate.

Historical manual preview: <http://127.0.0.1:33513/web/>. Container
`tc-plan002-browser` was stopped at the user's request after review; its status
was independently checked as `exited`. Its owned disposable volumes remain:
`tc-plan002-browser-config`, `tc-plan002-browser-certs`,
`tc-plan002-browser-content`, `tc-plan002-browser-library`.
Fixture rUID `0000000000000e02` has four seeded profiles, revision 4, with
Demo Radio active and Demo Podcast / Demo ARD-Sendung names. This is isolated
acceptance data, not a user's production configuration.

After manual review, these commands remove only the owned preview resources:

```sh
podman rm -f tc-plan002-browser
podman volume rm tc-plan002-browser-config tc-plan002-browser-certs tc-plan002-browser-content tc-plan002-browser-library
```

Remaining browser procedure: inspect all four forms in DE and EN, select RSS
and ARD and check immediate names, save/reopen, cancel edited drafts and verify
no persisted change, reopen the browser, and restart this preview container to
verify persistence through the UI. Record actual results before approval.
Physical Toniebox playback/placement remains untested; record model, firmware,
date and result separately. Simulated placement tests have passed.

No further implementation question or revision is pending. To resume acceptance,
provide a working browser provider or record the manual preview results; physical
acceptance requires an available Toniebox. The preview must first be restarted
and its currently mapped port checked. Do not mark plan 002 DONE until the
required acceptance is complete. The user has authorized local integration
despite these open gates; publication has not been requested.

### User-requested feature-branch integration — 2026-10-11

The user explicitly requested: “stop the preview and integrate this feature in
the feature branch.” This supersedes the earlier instruction to defer local
branch integration. The preview was stopped; volumes were retained.

Both application repositories are now on `feature/prepared-podcast-tonies`:

| Repository | Integrated HEAD | Location |
| --- | --- | --- |
| Backend | `d372dcd749360ae8da10bd274d238f4fac3335d9` | `teddycloud/` |
| Frontend | `0d315144e037ac1a7bee52ee68775548d5f3f8bb` | `teddycloud/teddycloud_web/` |

Frontend commits `cb700bc`, `a5aa488`, `d306b1fa` were cherry-picked onto the
existing feature branch. Backend first fast-forwarded to its already-fetched
remote tip `6072c6bd`, preserving the existing bundled-frontend commit, then
cherry-picked `c52a27d3`, `bfeaf04c`, `1831505d`, `532bb9a9`, `8339af0e`.
The duplicate feed-inspection baseline commits were not replayed: their trees
exactly matched the existing feature-branch changes. No conflicts occurred.
Existing fork publishing workflows and submodule configuration were preserved.

The user-requested complete local integration includes a separate bundle/gitlink
commit `d372dcd7`, generated using `npm_config_offline=true npm_config_audit=false
make web` (exit 0). This is a local fork integration commit, outside the earlier
source-only upstream PR scope. Keep it separate when preparing upstream PRs.
Frontend source exactly matches reviewed `d306b1fa`; backend source, headers,
tests, docs, Makefile and test Dockerfile exactly match reviewed `8339af0e`.
All **338** bundled files excluding `web_version.json` match the production-tested
integration bundle byte-for-byte. Version metadata correctly names frontend
`0d31514`. `make test_git_version` passes for both integrated revisions.
Both application working trees are clean; nothing was pushed. Browser and
physical-device acceptance remain open and were not inferred from integration.
Post-integration source `git diff --check` passes. The all-files check reports
two trailing-whitespace lines inside the generated minified JavaScript bundle;
that bundle is byte-identical to the tested build and was not manually rewritten.

### User-requested branch pushes

At the user's explicit request, `git push origin feature/prepared-podcast-tonies`
passed for both repositories, frontend first (`9cf6bf8..0d31514` to
`goggel/teddycloud_web`), then backend (`6072c6bd..d372dcd7` to
`goggel/teddycloud`). No force push was used. Both working trees are clean and
both branches now match their origin tracking branches. Browser and physical
acceptance remain open; no CI result is claimed by this push verification.

## Review focus

The review covers the plan's source-profile API, ownership, names, placement
policy, four forms, activation sequencing, failure recovery and tests. It does
not constitute a broad audit of the repository or its dependencies.

Review-driven corrections include exact HTTP method enforcement, first-job
ownership while encoding, functional draft updates, source-length preflight,
save lifecycle locking, explicit partial-failure readback, editing-revision
preservation during polling, and retaining legacy source settings when switching.
Automated verification of the completed snapshot is complete; the acceptance
limits above remain explicit.
