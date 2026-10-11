# Podcast implementation plans

Prepared and extended by the improve skill on 2026-10-09. The user selected both ARD support and trimming as additions to plan 001. The original RSS foundation and the ARD/trimming extensions are implemented locally; see the implementation validation record. Physical acceptance remains outstanding.

| Plan | Outcome | Priority | Effort | Depends on | Status |
| --- | --- | --- | --- | --- | --- |
| [001](001-prepared-podcast-tonies.md) | Search/subscribe to RSS and ARD Audiothek shows; apply podcast trim defaults and episode overrides; prepare chaptered audio inside the single TeddyCloud container | P1 | L, original foundation approximately 2–3 weeks; ARD/trim extension approximately 5–10 engineering days, plus hardware acceptance | None | IN PROGRESS: software implemented and automated gates passed; hardware/browser acceptance pending |
| [002](002-four-audio-sources.md) | Four separate source editors with persistent settings; immediately retain selected podcast/ARD names; always check enabled podcast/ARD subscriptions on placement | P1 | L, approximately 8–12 engineering days plus hardware acceptance | Implemented software foundation from 001; its pending hardware gate does not block development | IN PROGRESS: integrated and pushed to both origins on `feature/prepared-podcast-tonies` at user request; all automated gates pass, browser/device acceptance open. Preview stopped. |

## Four-source follow-up — 2026-10-09

Execution worktrees: `execution-worktrees/plan-002/backend` and its
`teddycloud_web/` directory, both on `feature/plan-002-four-sources-resumed`.
The original `/private/tmp/teddycloud-plan002.efZXwH` worktree disappeared
overnight. On 2026-10-10 the executor recovered the verified Step 1–3 source
from test image `7dd0a3de8747` into these durable worktrees and committed recovery
checkpoints: backend `c52a27d3`, frontend `cb700bc`. Preserved pre-existing
feed-inspection snapshots are backend `deb98225` and frontend `1955814`;
advisor review compares plan changes against these snapshots.
The original application working trees were not implementation/review targets.
The subsequent user-requested integration and branch pushes are recorded below.

Execution previously stopped at a plan boundary. WIP checkpoints are backend
`bfeaf04c` and frontend `d306b1fa` (including the final test-formatting checkpoint);
see the [advisor review](002-execution-review.md)
and the execution-block note in [plan 002](002-four-audio-sources.md).
The user has now authorized a focused regression and minimal fix. Execution resumes
with the revised step 2a, using short shared read–modify–write operations that
preserve unrelated current data and reject stale intended source replacements.
The change must first reproduce the actual loss, then correct shared persistence:
`saveTonieInfo`, move-to-library, and bulk model reassignment currently use stale
snapshots outside the new shared content lock. The old ID-lock functions are
disabled because of an existing TagIndex deadlock; simply enabling them is not
an acceptable fix. Do not merge the WIP or call plan 002 complete before the
lost-update regression and remaining gates pass.

The lost update was independently reproduced before the fix, and fixed-image
regressions pass, including opaque future-version profiles, newer source flags
and a directly paused move-to-library request. Review round 1 is resolved in
backend `8339af0e`; frontend is `d306b1fa`. All automated gates have now passed
independently: 169 frontend tests plus type/format/translation/build checks;
final test image `13a72fad64c2` with 21 podcast API + 8 profile tests, both build
modes and C/Python suites; clean production integration `8eb39cfc`, image
`04eb430da969`, and the full root/PUID=1000 container restart/persistence harness.
See the review for exact immutable IDs, commands, scope and limitations.

Acceptance-only BLOCK: the actual CUA browser entry point returned
`No browser is available`. Physical Toniebox acceptance is also open. No further
product question or implementation revision is pending. The user subsequently
requested stopping the preview and integrating into the feature branch. Both
repositories are now on `feature/prepared-podcast-tonies`: backend `d372dcd7`,
frontend `0d31514`, with clean working trees. Both feature branches were then
pushed successfully to their configured `goggel` origins at the user's explicit
request, frontend first and backend second; both now match their remote branches.
The complete local
integration includes a separate rebuilt bundle/gitlink commit; source exactly
matches the reviewed snapshots, all 338 bundled files excluding version metadata
match the production-tested bundle, and Git-version validation passes.
Preview container `tc-plan002-browser` is stopped (`exited`); its isolated
volumes remain. See the review for acceptance procedure and resource cleanup.
Do not mark DONE before acceptance; publication has not been requested.

Plan 002 is based on the nested backend at `6fd7f9dbb4c4fca54bfdbb71a35ab4856352163f`
and frontend at `8a30a31e43d1d71a4041c4c4cada676fc58fb918`. These are the current
planning baselines; the older checkout notes below describe plan 001's history.

The user requested four independent source forms (audio files, radio, RSS podcasts,
ARD Audiothek/Sounds), persistent selected feed/show names, and placement refresh
always enabled. Plan 002 includes durable inactive source profiles, source-bound
display names distinct from previously prepared titles, and server-side enforcement
for existing entries and older clients. The existing 15-minute placement throttle
is retained. Execute 002's data/API work before UI/save-flow integration and test
each step. This planning update changes only files under the outer `plans/` folder.

Deferred from this follow-up: a standalone library subscription manager, ARD video,
multiple simultaneously active subscriptions per Tonie, and a cross-API atomic
activation transaction. Partial save failures must be visible and recoverable.

Execute the numbered steps in plan 001 in order, including extension steps 2a (ARD), 3a (model/migration) and 4a (PCM trimming). Source normalization in 2a precedes source-bound overrides in 3a; 3a precedes 4a. Extend the existing dirty foundation rather than restarting it. Frontend work can start against the documented API after the backend contract tests exist. Update this table to IN PROGRESS, DONE, or BLOCKED with a concrete reason. Do not mark DONE until the device gates pass; passing container tests alone is insufficient.

## Current implementation and extension scope

The outer workspace holds these plans at backend HEAD `78c3b38`. An in-progress implementation exists in the nested `teddycloud/` checkout at backend HEAD `8b39ac9c` with uncommitted/untracked podcast work; its initialized `teddycloud_web/` submodule is at `636a8d6` with frontend changes. Application paths in plan 001 refer to that backend root. Inspect and preserve both working trees; a HEAD diff alone does not describe their implementation. The original base plan’s TODO status is retained because complete container/publication/device acceptance has not been established in this planning session.

Confirmed feature choices:

- ARD show search and pasted show links; accept legacy Audiothek and current ARD Sounds links, including numeric/URN identifiers. No category browser or individual-episode import.
- Whole seconds removed from each episode’s beginning/end, with podcast defaults and individual overrides. Zero defaults preserve existing behavior; explicit zero overrides disable inherited cuts for that episode.
- Both features use the same native service and single-container deployment. Invalid cuts or upstream failures retain previous prepared audio.

Technical extension decisions and verification live in plan 001: canonical source-bound override identities, v1→v2 registry migration, a bounded 64 KiB podcast request cap, effective-cut fingerprints, constant-memory two-pass decoding for end cuts, capability fallback, and fixture-backed audio/provider tests. Frontend baseline TypeScript checking and 30 focused tests passed; backend/build/hardware checks were not run for this planning update. No application source was changed by the advisor.

## Repositories to obtain

1. **Backend and Docker packaging:** https://github.com/toniebox-reverse-engineering/teddycloud
2. **Existing web UI:** https://github.com/toniebox-reverse-engineering/teddycloud_web — already declared as the backend's `teddycloud_web/` submodule. Develop it there; a separate sibling clone is unnecessary.

For a new development checkout, run these commands separately:

```sh
rtk git clone --branch develop --recurse-submodules https://github.com/toniebox-reverse-engineering/teddycloud.git
cd teddycloud
rtk git switch -c feature/prepared-podcast-tonies
rtk git -C teddycloud_web fetch origin develop
rtk git -C teddycloud_web switch -c feature/prepared-podcast-tonies origin/develop
```

For this existing checkout, do not reclone or switch away from user changes. Create an implementation checkout/worktree based on `develop`, initialize its recursive submodules, and create the frontend feature branch there. Inspect `git status` in both repositories first. A parent worktree does not initialize its submodules automatically.

Recursive submodule initialization also obtains cJSON, Cyclone/Common/TCP/SSL/Crypto, Opus, Ogg and Emscripten as declared in `.gitmodules`. Use the pinned revisions. Do not separately clone or edit those projects for this feature.

**Not required:** audio2tonie, TonieToolbox, tonie-podcast-sync, TeddyCloudPlugins, a scheduler repository, or a companion service. Existing solutions are references, not runtime dependencies.

## Deployment and contribution

The deliverable is one image containing the updated backend, bundled frontend, FFmpeg and any added runtime libraries. Existing config/library/cache/content volumes hold all persistent state. Users must not install scripts, plugins, cron, Node, Python, a broker, or a second container to use podcasts.

Follow the backend README's two-repository contribution workflow: separate backend and frontend PRs targeting `develop`; frontend first, then backend. Do not commit a frontend gitlink change or generated web bundle in the feature backend PR. For local image verification, stage the freshly built frontend assets in an isolated integration checkout and make an integration-only local commit recording the frontend gitlink and generated bundle. This commit must never enter the backend feature PR; it is needed because the production Dockerfiles reject dirty Git/version state. A maintainer runs the existing frontend bundling/release workflow for the final combined image. Do not invoke the publishing workflow or push images as part of this plan without operator instructions.

## Dependencies and rejected alternatives

- Plan 001 has a first milestone for dependencies/test infrastructure, followed by backend preparation, UI integration, and final packaging/device tests.
- Direct live podcast streaming was considered and deferred: the user chose prepared chaptered audio and reliable skipping.
- Editing TAP files alone was rejected: the inspected cached TAP path can reuse old output. The plan generates complete TAFs directly and does not depend on TAP PR #462.
- A plugin/sidecar deployment was rejected: the required product is the existing UI and one container.
- Original upstream inspection was backend `78c3b38` / frontend `636a8d6`; the extension uses the newer dirty implementation checkout recorded above. Reconcile by symbols and behavior without overwriting existing work.
- A separate ARD/trimming plan was rejected because the user explicitly selected extension of plan 001. External RSS bridges, metadata-only end cuts and large in-memory tail/reverse buffers were rejected: the required product uses the same service, measured PCM boundaries and bounded memory.
- Full repository auditing, unrelated fixes, ARD browsing/collections/single-episode imports and an audio waveform editor were not audited or included.
