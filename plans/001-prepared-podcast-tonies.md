# Plan 001: Prepare RSS and ARD podcasts with episode trimming from the existing UI

> **Executor instructions:** Read this file fully, implement the steps in order, and run each verification gate. Commands marked NEW describe required deliverables from the original baseline; some now exist in the in-progress implementation. Extend those files and tests rather than recreating them. Work in feature branches, not the user's current master checkout. Update `plans/README.md` as milestones complete. Do not publish or deploy to a real household instance during automated tests.
>
> **Drift check:** Backend evidence was read at `78c3b38` (2026-10-09). Run `rtk git diff --stat 78c3b38..HEAD -- src include Makefile DockerfileUbuntu DockerfileDebian DockerfileAlpine dev-sandbox tests` in the implementation checkout. Frontend evidence was read remotely at `636a8d65043f6c6e322f76bcb11e958725159e96`; run `rtk git -C teddycloud_web diff --stat 636a8d65043f6c6e322f76bcb11e958725159e96..HEAD -- src public/translations package.json`. Compare changed integration points with the excerpts below, then record the actual implementation base SHAs in this file. Ordinary upstream drift is reconcilable; stop if an upstream implementation changes the design assumptions or duplicates this feature.

## Historical extension planning baseline and drift check — 2026-10-09

The user explicitly requested that **both ARD Audiothek support and episode trimming extend this existing plan**, not become separate projects. Confirmed choices: search ARD shows and accept pasted ARD show links; podcast-wide trim defaults with individual episode overrides. These are required deliverables of plan 001.

The original upstream evidence below remains historical context. A newer, dirty implementation checkout now exists at `/Users/benjaminkoster/git-privat/teddycloud/teddycloud`, backend HEAD `8b39ac9c`, with its initialized frontend at `teddycloud_web/`, HEAD `636a8d6`. The outer workspace HEAD remains `78c3b38` and holds this `plans/` directory. **Application paths throughout this plan are relative to the implementation backend root**, not the outer workspace. The podcast implementation is largely uncommitted/untracked, so a commit-only drift check cannot see it. Never reset, reclone over, stash, or replace those changes.

Before continuing, run `git status --short` in the backend and frontend, `git diff --stat 8b39ac9c..HEAD -- src include Makefile tests docs DockerfilePodcastTests`, and `git -C teddycloud_web diff --stat 636a8d6..HEAD -- src public/translations package.json`. Also read the live files named below, including untracked files. Reconcile the existing implementation by symbols and behavior; line numbers may move during concurrent work. If source ownership, registry, or API behavior materially differs, report the mismatch before coding the extension. Do not repeat completed original milestones just because they are written in future tense.

Extension evidence read directly in that checkout:

```c
// src/podcast_feed.c:22 — config currently requires an RSS feed URL
bool podcast_config_valid(const cJSON *c, char *err, size_t size)
// ... podcast_url_valid(podcast_string(c, "feedUrl")) ...

// src/podcast.c, discovery() — only Apple discovery exists
"https://itunes.apple.com/search?media=podcast&entity=podcast&limit=20&country=%s&term=%s"
// prepare(): fingerprint profile is "taf-opus-48k-stereo-v1";
// ordered episode identity and URL are the only per-episode fingerprint fields.

// include/podcast_encode.h / src/podcast_encode.c:35 — no trim input
bool podcast_encode(const char *const *inputs, size_t count, const char *output,
                    uint32_t audio_id, const atomic_bool *cancel,
                    char *error, size_t error_size);
// Decoder argv produces s16le stereo at 48 kHz; every complete PCM frame
// currently goes straight into toniefile_encode().
```

```ts
// teddycloud_web/src/types/podcastTypes.ts:1
export interface PodcastConfig {
    feedUrl: string;
    episodeCount: number;
    order: "newest_first" | "oldest_first";
    intervalHours: number;
    refreshOnPlacement: boolean;
    enabled: boolean;
}
// PodcastSearchResult.providerId is currently number, feedUrl required.
// podcasts/podcastValidation.ts: podcastConfig() explicitly copies keys;
// adding a type field alone will lose it during dirty checks and saves.
```

`src/podcast.c` owns registry validation, preview dispatch, discovery, fingerprinting and publication. `src/handler_podcast.c` currently copies only q/country into search requests. `PodcastSubscriptionEditor.tsx` currently has one RSS search/input and episode lists, but no provider selector or trimming. Match existing C cJSON accessors, deep-copy job snapshots and content→service lock order; never hold either lock during ARD lookup. Match Ant Design InputNumber/Form controls, i18next strings, AbortController cancellation and React draft/save behavior in the existing editor/hook.

Read-only baseline verification performed for this extension: frontend `./node_modules/.bin/tsc --noEmit` exited 0; `./node_modules/.bin/vitest run --no-cache src/components/tonies/toniecard/podcasts src/api/apis/TeddyCloudApi.test.ts` passed 4 files / 30 tests. These verify the existing work only. Backend/container/device gates were not run for this planning update. RTK is unavailable in the planning shell; use ordinary equivalents when it is absent. No broad codebase/security audit was performed; investigation was scoped to the requested source and trimming extensions.

## Status and outcome

- Priority: P1; category: direction; effort: L (original foundation roughly 2–3 engineering weeks; allow approximately 5–10 additional engineering days for ARD, trimming, migration and automated verification); implementation risk: MED. Estimates exclude waiting for hardware acceptance.
- Depends on: none. Hardware acceptance requires access to the user's intended Toniebox generation and firmware.
- Planned at: original backend `78c3b38`; extension reviewed against implementation backend `8b39ac9c` plus its existing dirty changes, frontend `636a8d6` plus dirty changes, 2026-10-09. See the extension drift check above.
- Outcome: in **Tonies → Edit Tonie**, search the general podcast directory or ARD Audiothek, enter an RSS feed or paste an ARD show link, select the latest N episodes and their playback order, configure start/end cuts for all episodes with individual overrides, save, and receive a prepared TAF with one chapter per episode. Update periodically, manually, or optionally in the background after placement. Everything runs inside the normal TeddyCloud container after the browser closes.
- Default choices: RSS/general podcast source, zero seconds removed at either end, no episode overrides, latest 5 episodes, newest first, check every 6 hours, refresh-on-placement off, `nocloud=true`, `live=false` for managed prepared audio. Users may select N from 1–50, interval from 1–168 hours, or periodic updates off. These are proposed product defaults, not existing settings.
- Periodic refresh prepares content on the server; the box downloads it on its next suitable online freshness/content request. Placement refresh starts preparation but does not promise that the same placement plays the new episodes. Keep prior audio available and state this briefly in the UI. Validate actual synchronization timing on hardware.

## Implementation record

- Implementation checkout: `teddycloud/` (the recursive clone requested by the user).
- Actual backend base: `8b39ac9c9cbc312ea1fa948f66b0dc014fd096c1`, frontend base: `636a8d65043f6c6e322f76bcb11e958725159e96`; both on `develop` before feature branches.
- Drift reconciled: web auth stays on the existing SERTY_WEB routes; the newer disposable server/test infrastructure is reused. The existing keyed content mutex is a no-op, so a separate working content lock coordinates podcast publication with manual source changes.
- HTTP peer checks use libcurl’s opensocket callback on the actual sockaddr it connects to, preserving async DNS timeout/cancellation while preventing unchecked DNS re-resolution. No production localhost bypass exists.
- Existing RTNL decoding consumes low big-endian 32-bit word before high word. Placement fixtures match that convention and then verify UID-to-rUID byte reversal.
- Target hardware confirmed by user: Toniebox 1, test Tonie available. Physical protocol/playback gates remain unresolved; see implementation `docs/podcast-download-protocol.md`.
- Original RSS foundation software gates completed: backend parser/service/recovery tests, frontend TypeScript/build and 105 tests, feature-disabled build/API, 21 existing API regressions and 9 C utility tests. Ubuntu/Debian/Alpine images build and pass root/PUID=1000 persistence checks. Backend code through `1ed9e958`, frontend `7ee218e`; local Ubuntu image `localhost/teddycloud:podcasts` (`4a1821880606`, Linux/arm64). See implementation `docs/podcast-validation.md` for provenance and logs. Physical Toniebox 1 and visual-browser acceptance remain pending. ARD/trimming extensions above are not implemented by the original RSS foundation work; overall plan remains IN PROGRESS.
- ARD/trimming extensions implemented: backend `d374b7ba` plus `f49e0a84`, frontend `8a30a31`. Source-tagged public ARD show search and numeric/URN links use the existing HTTP/worker pipeline; source-bound defaults/overrides trim measured PCM with bounded two-pass end cuts. Registry v1 migration preserves assignments and pins; older UI/API compatibility remains covered.
- Extension software verification: 119 frontend tests including real rendered controls; 13 native service scenarios; optimized parser/window/transport tests; existing 9 C and 21 API regressions; feature-disabled compile/link/API; clean release metadata; Ubuntu/Debian/Alpine production builds and root/PUID=1000 restart persistence. Local Ubuntu extension image `localhost/teddycloud:podcast-extensions` (`21a730ba7571`), private integration commit `8bcf81c6`. Full provenance is in implementation `docs/podcast-validation.md`. Physical Toniebox 1 and connected-browser review remain pending; overall plan remains IN PROGRESS. No household deployment or publishing occurred.
- RTK was uninstalled during implementation. Commands below are historical plan commands; omit the `rtk` prefix.

## Why this matters

The original upstream UI can encode files and assign audio; the in-progress checkout adds the subscription foundation. This extension makes ARD shows available directly and removes recurring intros/outros with optional episode-specific corrections. Users otherwise maintain external scripts and perform repeat manual work. Prepared audio fits the existing Tonie file and freshness mechanisms, provides chapter skipping, and avoids waiting for full conversion during playback. Failed downloads must never replace working audio.

## Current state and conventions

Backend: C with a hand-written Makefile; normal source files under `src/` are included by wildcard. Cyclone wrappers provide `error_t`, `osAllocMem`/`osFreeMem`, task APIs and filesystem operations. Match surrounding TRACE logging (`\r\n` line endings) and mutex conventions. HTTP handling is multithreaded; do not hold settings/content mutexes across network or encoding work.

Evidence read directly:

```c
// src/server.c:135 — admin routes use REQ_GET/REQ_POST and SERTY_WEB
{REQ_POST, "/api/fileEncode", SERTY_WEB, &handleApiEncodeFile},

// src/server.c:989 — existing service loop
while (!settings_get_bool("internal.exit")) {
    osDelayTask(250);
    mqtt_server_task();
    settings_loop();
    // ...
}

// src/toniebox_state.c:24 — placement already reaches the backend
void tbs_tag_placed(client_ctx_t *client_ctx, uint64_t uid, bool valid)
// publishes TagValid through SSE and MQTT; no preparation barrier exists

// include/toniefile.h:59 — reuse the native TAF encoder
toniefile_t *toniefile_create(const char *fullPath, uint32_t audio_id, bool append, int32_t size);
error_t toniefile_encode(toniefile_t *ctx, int16_t *sample_buffer, size_t samples_available);
error_t toniefile_new_chapter(toniefile_t *ctx);
error_t toniefile_close(toniefile_t *ctx);

// src/handler_cloud.c:977 — freshness works from actual TAF audio IDs
tonieInfo->updated = boxAudioId < serverAudioId;
// later considers updateOnLowerAudioId, custom-content priority, and live

// src/tonie_audio_playlist.c:99 — do NOT depend on TAP regeneration
if (isValidTaf(tap->_filepath_resolved, get_settings()->core.tap_taf_validation)) {
    tap->_cached = true;
    // TODO check audio id if different and check settings.
}
```

Other integration points:

- `src/contentJson.c` / `save_content_json`: writes content JSON through a temporary file and move. Preserve fields unrelated to podcasts; do not bump CONTENT_JSON_VERSION just to store subscription state elsewhere. The existing manual set handler does not itself take a content lock; add the same canonical `<contentPath>.json` keyed lock to it and podcast publication, covering load/modify/save with cleanup on every exit.
- `src/handler.c:843` / `getTonieInfoV2`: resolves `source` to a library TAF. `src/handler_cloud.c:622` serves content and handles range requests; the generation-pinning hook belongs here.
- `src/settings.c:186`: `internal.configdirfull`, `internal.librarydirfull` and `internal.contentdirfull` are resolved storage roots. Use these, not hardcoded `/teddycloud` paths.
- `src/toniefile.c:745`: existing ffmpeg conversion assigns `time(NULL) - TEDDY_BENCH_AUDIO_ID_DEDUCT`; `:791` creates chapters between files. Existing conversion shells out with `osPopen` at `:608`. Do not send feed URLs, titles or arbitrary paths into that shell command.
- `src/toniefile.c:307` already adds the first chapter during create; do not add an extra first chapter. `:361` writes the final header at close. Validate the completed output, not a growing file.
- `src/server.c:1079`: stop and join the new worker before cache/mutex teardown. No background worker may access settings after shutdown.
- `include/mutex_manager.h`: add a podcast state mutex before `MUTEX_ID_START` if using this manager; keep lock ordering documented.
- `tests/test_tonies_custom_json_api.py`: stdlib `unittest`/HTTP request conventions. Its existing tests modify server data, so run only against disposable test data. Do not point them at production.
- Dockerfiles already ship FFmpeg and copy built web assets from `install/pre/data/www/`. Production Dockerfiles do not themselves run `make web`; they consume staged assets. `dev-sandbox/Dockerfile` does run `make web`.

Frontend: React/TypeScript, Ant Design, i18next, Vite, npm, Vitest. The local frontend submodule is currently uninitialized; source was inspected at the frontend SHA above through GitHub.

```tsx
// src/components/tonies/toniecard/modals/EditTonieModal.tsx:321
<RadioStreamSearch
    placeholder={t("tonies.editModal.placeholderSearchForARadioStream")}
    onChange={onSearchRadioChange}
    key={keyRadioStreamSearch}
/>

// src/components/tonies/toniecard/TonieCard.tsx:233
const { handleSaveChanges } = useTonieCardSaveFlow({
    tonieCard, overlay, selectedModel, selectedSource, /* ... */
});

// src/components/tonies/toniecard/hooks/useTonieCardSaveFlow.ts:168
await api.apiPostTeddyCloudContentJson(
    tonieCard.ruid, "source=" + encodeURIComponent(selectedSource), overlay,
);
// :206 derives live from selectedSource.startsWith("http")

// src/api/apis/TeddyCloudApi.test.ts — existing test conventions
const fetchApi = vi.fn().mockResolvedValue(new Response(null, { status: 200 }));
const api = new TeddyCloudApi(new Configuration({ basePath: "", fetchApi }));
```

Use the **tonies/toniecard** EditTonieModal, not the similarly named tonieboxes modal. `TonieCard` already owns rUID and overlay. Centralize API methods in `src/api/apis/TeddyCloudApi.ts`. Keep feature logic in hooks and use existing theme tokens/components. All locales in `public/translations/*.json` must have matching keys; English fallback text is acceptable where a reviewed translation is unavailable, but do not claim it is translated.

## Repositories, branches and boundaries

Obtain backend with recursive submodules; work on the frontend inside `teddycloud_web/`. See `plans/README.md` for exact clone commands. Backend and frontend branches: `feature/prepared-podcast-tonies`, based on each repository's `develop`. Target separate PRs to `develop`, frontend first. Do not commit gitlink changes or generated frontend bundles in the backend feature PR. Local integration staging is separate from feature commits. Do not push, publish images, or create GitHub issues without operator instructions.

Backend files allowed:

- Create or extend `include/podcast.h`, `include/podcast_http.h`, `include/podcast_feed.h`, `include/podcast_encode.h`, `include/handler_podcast.h`; NEW `include/podcast_ard.h`, `include/podcast_trim.h`, `src/podcast_ard.c`, `src/podcast_trim.c`; create or extend `src/podcast.c`, `src/podcast_feed.c`, `src/podcast_http.c`, `src/podcast_encode.c`, `src/handler_podcast.c`.
- Integration edits: `src/server.c`, `src/toniebox_state.c`, `src/handler_cloud.c`, `src/handler_api.c`, `include/mutex_manager.h`, `Makefile`.
- Packaging: `DockerfileUbuntu`, `DockerfileDebian`, `DockerfileAlpine`, `dev-sandbox/Dockerfile`; `tests/py/with_server.sh` only for isolated podcast test harness integration; create or extend `DockerfilePodcastTests` for verification only.
- NEW `tests/test_podcast.c`, `tests/test_podcast_api.py`, `tests/run_podcast_tests.sh`, `tests/run_podcast_container_tests.py`, `tests/fixtures/podcasts/**`; NEW `docs/api/podcasts.md`, `docs/podcasts.md`, `docs/podcast-download-protocol.md`, `.github/workflows/podcast-tests.yml`; this plan/index.

Frontend files allowed (paths relative to the frontend repository):

- `src/api/apis/TeddyCloudApi.ts`, its test; NEW `src/types/podcastTypes.ts`.
- `src/components/tonies/toniecard/TonieCard.tsx`, `modals/EditTonieModal.tsx`, `hooks/useTonieCardSaveFlow.ts`.
- NEW `src/components/tonies/toniecard/podcasts/PodcastSubscriptionEditor.tsx`, `PodcastSubscriptionEditor.test.tsx`, `usePodcastSubscription.ts`, `usePodcastSubscription.test.ts`, `podcastValidation.ts`, `podcastValidation.test.ts`.
- `public/translations/*.json`, `CHANGELOG.md`; `package.json`/lockfile only if a truly necessary test dependency is absent. Prefer existing React DOM test utilities and Vitest; no new application framework.

Out of scope: changes to vendored submodules, a general plugin framework, TAP redesign/PR #462 merge, live-stream mode, accounts, private/paid feeds, Spotify/DRM, ARD category browsing/collections/individual-episode imports, waveform editing/automatic ad detection, playlist shuffle, arbitrary artwork downloads, migration of unrelated API/auth behavior, Windows podcast functionality. Preserve existing Windows builds through feature-disabled stubs; Linux Docker is the initial supported podcast platform.

## Architecture and contract (implement these decisions)

### Native service and dependencies

Use a native C service in the existing server process, one bounded background preparation worker, and a small deduplicated job queue. Add `PODCAST_SUPPORT=1` by default for Linux, `0` for Windows; feature-disabled builds expose capabilities as unsupported and do not require new libraries. No Node/Python worker at runtime.

Use **libcurl** for bounded HTTP downloads and **libxml2** for RSS parsing; do not write a regex XML parser. Add pkg-config compiler/link flags only when podcast support is enabled. Add build packages `pkg-config libcurl4-openssl-dev libxml2-dev` on Debian/Ubuntu, `pkgconf curl-dev libxml2-dev` on Alpine. Runtime images need the matching curl/XML shared libraries (Debian/Ubuntu `libcurl4` and `libxml2`, Alpine `libcurl` and `libxml2`; reconcile distro package renames before building). Keep dependencies image-contained. Do not initialize/cleanup libcurl globally per HTTP request; initialize once before starting worker threads and clean up after they join.

### Persistent model

Store a versioned registry at `<configdirfull>/podcasts.json` using atomic write/rename and backup recovery. Do not rewrite a corrupt registry as empty. Store prepared audio and metadata at `<librarydirfull>/podcasts/<subscription-id>/<generation-id>.taf` plus a generation manifest. Use server-generated lowercase hex IDs as filenames; never feed titles/URLs. Temporary downloads and encodes live under the same owned directory and are removed after success/failure/restart recovery.

Subscription fields: `id`, `revision`, `overlay` (existing overlay identifier, not an array index), normalized 16-hex `ruid`, `sourceType` (`rss`/`ard_audiothek`), `feedUrl` (RSS only), `ardShowUrl` and server-resolved `ardShowId` (ARD only), `sourceKey`, `trimStartSeconds`, `trimEndSeconds`, `episodeTrimSourceKey`, `episodeTrims`, `episodeCount`, `order` (`newest_first`/`oldest_first`), `intervalHours` (0 or 1–168), `refreshOnPlacement`, `enabled`, sanitized display `title`, `createdAt`, `lastCheckedAt`, `lastSuccessAt`, `nextCheckAt`, `lastError`, `state`, published/pending generation IDs and episode manifests. State values: `idle`, `queued`, `checking`, `downloading`, `encoding`, `ready`, `error`, `paused`. Persist configuration and committed results, not every progress tick. At restart, interrupted states become queued, temporary output is discarded, valid published audio remains.

Enforce one subscription per effective content directory + rUID. Two overlays sharing a content directory represent the same assignment and must not race independent subscriptions. Resolve overlay roots at request/job time; reject missing overlays. Placement UID is not rUID: reuse the backend's existing byte-order conversion convention and test it explicitly.

Keep the user's pre-podcast source, `live`, `nocloud` and cloud-override flags as restoration metadata. Do not change their assignment until the first complete TAF exists. On publish, under the content mutex, preserve unrelated content JSON fields and set managed `source=lib://podcasts/<id>/<generation>.taf`, `nocloud=true`, `live=false`, `cloud_override=false`. Subsequent revisions change only managed fields. Detaching restores original fields only when the current source is still owned by this subscription; a manually changed source must never be overwritten. A manual source change through the existing content API pauses/detaches management under the same locking policy. Delete/detach keeps already published audio by default; no automatic deletion of arbitrary user files.

### Proposed HTTP API

All routes below were new in the original baseline and are now partially implemented; extend them under `SERTY_WEB`, use the existing deployment's access controls and overlay resolution. Do not expose them on the device API port. JSON errors: `{ "error": { "code": "...", "message": "..." } }`. Never return feed credentials/internal paths. Mutations use POST to match existing router support.

| Method/path | Input | Result |
| --- | --- | --- |
| GET `/api/podcasts/capabilities` | none | supported boolean, `sourceTypes:["rss","ard_audiothek"]`, `trimSupported:true`, `maxTrimSeconds:21600`, `maxEpisodeTrims:50`, `maxRequestBytes:65536`, and existing limits |
| GET `/api/podcasts/search?q=...&country=DE&provider=apple` | 2–100 character query, country, `provider=apple` (default) or `ard_audiothek` | tagged results: Apple keeps existing numeric providerId/feedUrl and adds sourceType=rss; ARD returns string providerId, sourceType=ard_audiothek, title, author, ardShowUrl |
| POST `/api/podcasts/preview` | source fields, episodeCount/order, optional trim defaults and overrides | sourceKey, title, selected episodes with effective cuts, optional durationSeconds/estimatedTrimmedDurationSeconds, warnings |
| GET `/api/podcasts/subscriptions?ruid=...&overlay=...` | existing identity conventions | `{subscriptions:[...]}`; includes progress and prepared episodes, no secrets |
| POST `/api/podcasts/subscriptions/save?overlay=...` | `{id?,expectedRevision?,ruid,sourceType?,feedUrl?,ardShowUrl?,trimStartSeconds?,trimEndSeconds?,episodeTrimSourceKey?,episodeTrims?,episodeCount,order,intervalHours,refreshOnPlacement,enabled}` | saved subscription + async job ID; 201 create / 200 update |
| POST `/api/podcasts/subscriptions/refresh?overlay=...` | `{id}` | 202 job ID; repeated requests return existing queued/running job |
| POST `/api/podcasts/subscriptions/detach?overlay=...` | `{id,expectedRevision}` | 200; cancel pending work and preserve prepared files |

Use 400 for validation, 404 missing resources/overlay, 409 revision/ownership conflicts, 413 body/feed limits, 429 full queue, 502 upstream failure, 503 disabled feature. For this extension raise the podcast JSON request cap from 16 KiB to **64 KiB**, and publish `maxRequestBytes=65536` in capabilities. Fifty bounded 512-byte episode identities plus trim fields must fit; keep all other input bounds. Use a bounded heap allocation of at most 65,537 bytes for the handler read buffer, with cleanup on every path; do not enlarge the HTTP worker stack buffer. Update the error text and old 17,000-byte rejection test together. Exactly 65,536 bytes is allowed if otherwise valid; larger bodies return 413. No other API body limits change. A successful save means configuration accepted, not audio prepared. New subscriptions queue immediate preparation. Existing prepared audio remains assigned while edits prepare a replacement. Copy configuration/revision into each job; discard results if that revision was edited/detached before publish.

### Discovery, feed selection and network handling

Search through Apple's iTunes Search API with `media=podcast`, `entity=podcast`, encoded query, country and limit 20. Server-side proxy/cache for 10 minutes, debounce UI 400 ms, rate-limit global discovery requests to respect Apple's approximately 20/minute guidance. No API key installation. Manual RSS entry must keep working if discovery is unavailable. Display plain text title/author; no remote HTML or automatically fetched artwork in v1.

Support RSS 2.0 audio enclosures, common iTunes metadata namespaces, CDATA, encoded text, relative URLs resolved against the final feed URL, RFC822 publication dates, GUID identity, and missing dates. Atom/private feeds are explicitly unsupported in v1 with a useful error. Drop non-audio/unpublished future entries, deduplicate by GUID (fallback enclosure URL), sort dated entries by actual publication instant descending, put undated entries afterwards in feed order. Select the latest N first; reverse the selected set for oldest-first playback. Fewer than N available is a visible count warning, not failure. An empty playable set is failure and leaves previous audio intact.

Fingerprint the canonical sourceKey, selected identities/URLs/order, each selected episode’s **effective** start/end cuts, and encoding profile `taf-opus-48k-stereo-trim-v2`. Defaults hidden by overrides and overrides for unselected episodes do not change the audio fingerprint. A changed effective trim must regenerate audio even if the feed and URLs are unchanged. Unchanged selections skip downloads/re-encoding and do not bump audio ID. Conditional ETag/Last-Modified checks may be used, but HTTP 304 only skips generation if configuration/profile also matches. Refresh only new missing downloads; remove unneeded temporary audio after a successful compilation. Initial hard limits: feed 5 MiB decompressed, 1,000 parsed items, 50 selected episodes, 200 MiB per audio download, 1 GiB preparation workspace per subscription, 6 hours decoded duration, 240 MiB completed TAF. Exceeding a limit errors instead of silently truncating the selected list. Check available storage before downloads and continuously enforce streamed byte limits.

HTTP helper requirements: HTTP/HTTPS only, TLS verification on, no URL userinfo, at most 5 manually validated redirects, public unicast destinations only, block loopback/private/link-local/metadata addresses including IPv6/mapped forms, validate and pin resolved addresses for each connection to avoid DNS rebinding. Do not automatically follow proxy environment settings for this new helper; document this scope. Never pass downloaded media to FFmpeg with network protocols enabled. Timeouts: connect 10 s, feed/search total 30 s, audio total 15 min plus low-speed timeout; worker cancellation aborts transfers. Redact query strings from logs/errors. Use fixture transport injection for tests rather than a production localhost bypass.

### ARD Audiothek source contract

Keep ARD in the same native service, scheduler, queue, downloader and TAF publication flow. Do not depend on a public RSS bridge, plugin, PHP script, login, or second container. Product label: **ARD Audiothek / ARD Sounds**, because the old website currently redirects to ARD Sounds. Expose a separate provider/source choice beside general podcast search. RSS entry remains usable when ARD is unavailable, and vice versa.

Accepted pasted URLs: HTTPS, exact host `www.ardaudiothek.de`, `ardaudiothek.de`, `www.ardsounds.de` or `ardsounds.de`; path `/sendung/<slug>/<numeric-id>/` or `/sendung/<slug>/urn:ard:show:<16 hex>/`, optional trailing slash, no credentials, fragment, nonstandard port or extra path components. Ignore/remove ordinary sharing query parameters locally; never fetch the supplied web URL. Parse and percent-decode the identifier once with strict validation; reject encoded slashes, double-encoding and lookalike hosts. Resolve numeric IDs using programSet(id:), URNs using programSetByCoreId(coreId:), through the fixed API endpoint. Both forms must normalize to the returned show coreId as `ardShowId` and `sourceKey="ard:" + coreId`; normalize ardShowUrl to the validated sharingUrl. RSS sourceKey is `"rss:" + feedUrl` after normal URL normalization, without silently changing subscriptions to redirected feed URLs. Never trust a client-provided canonical ID/sourceKey for lookup or file paths.

`src/podcast_ard.c` handles URL parsing, fixed GraphQL query construction and provider normalization. Use GET `https://api.ardaudiothek.de/graphql?query=<encoded fixed query>&variables=<encoded cJSON object>` with `podcast_http_get`. This avoids adding a second transport or POST redirect semantics. All values go in GraphQL variables, never interpolated into query source. Apply existing TLS/public-peer/redirect/cancellation rules and 5 MiB response bound; the generated URL must fit the transport’s 2,048-byte bound. If a minimal query exceeds it, shorten selected fields rather than raising the global user URL limit. HTTP 200 with a nonempty GraphQL errors array, null required data or malformed JSON is upstream failure; never publish an empty successful generation.

The following query shapes were verified against ARD’s live endpoint on 2026-10-09. They are starting contracts, not permanent API stability guarantees:

```graphql
query Search($q:String!) {
  search(query:$q,type:ProgramSets,limit:20) {
    programSets { nodes { id coreId title sharingUrl publicationService { title } } }
  }
}
query Show($id:ID!,$offset:Int!,$count:Int!) {
  programSet(id:$id) {
    id coreId title sharingUrl
    items(first:$count,offset:$offset,orderBy:PUBLISH_DATE_DESC,
      filter:{isPublished:{equalTo:true},itemType:{notEqualTo:EVENT_LIVESTREAM}}) {
      pageInfo { hasNextPage endCursor }
      nodes { id assetId title isPublished itemType publishDate duration
        audios { url downloadUrl allowDownload } }
    }
  }
}
query Resolve($coreId:String!) {
  programSetByCoreId(coreId:$coreId) { id coreId title sharingUrl }
}
```

Search returns shows only (no categories, collections or single episodes). Namespace cache keys by provider/country/query; retain 10-minute caching, 400 ms debounce, bounded results and rate limits, with independent provider budgets so ARD traffic cannot exhaust Apple’s allowance. Clear/abort pending search when provider changes. Missing provider means Apple for old clients. Unknown provider/sourceType is 400. Preserve old Apple result fields; define a TypeScript discriminated union so ARD string IDs are never coerced into numbers or assumed to be feed URLs.

For show preview/refresh, fetch published on-demand candidates newest-first in pages of 50, at most 20 pages / 1,000 candidates total / 30 seconds for the whole lookup. Apply limits cumulatively across pages, stop after N eligible unique episodes, and detect repeated pages/no progress. Exclude future dates, invalid dates, live/event streams and entries without a usable public downloadUrl with allowDownload=true. Do not fall back to a stream explicitly marked non-downloadable. Use `assetId` (`urn:ard:episode:...`) as stable episode identity, fallback to `"ard-item:" + id` only when assetId is absent; deduplicate identities and chosen URLs. This avoids binding overrides to a rotating CDN URL or publication coreId. Parse ISO8601 timestamps with UTC offsets explicitly (curl_getdate is not an ISO8601 contract). At equal dates use deterministic ID ordering. Select latest N before reversing for oldest-first, as RSS does. Duration is optional advisory seconds; decoded PCM remains authoritative. Fewer than N at actual end of results produces a warning; hitting a hard candidate/time/byte limit before a sufficient set/end is an error, not silent truncation. Download failures preserve the previous generation; automatic refresh must re-resolve current media URLs.

Provide deterministic fixtures derived from minimal, sanitized ARD responses, including HTTP-200 GraphQL errors and changing media URLs. Do not make internet access a CI gate. Optional live smoke: search `Sandmännchen`, resolve `23667030` and `urn:ard:show:0e3f60ed4ba198df`, preview a small set without saving a household Tonie. The primary API response for this sample has programSet.id=23667030, coreId=urn:ard:show:0e3f60ed4ba198df, episode assetId, ISO8601 publishDate, duration and audios[].allowDownload/downloadUrl.

### Trim configuration, persistence and compatibility

Start/end are **whole seconds removed** from each episode’s beginning/end, not absolute timestamps in the combined TAF. Technical defaults: each side 0, allowed integers 0–21,600, no overrides. Same behavior for RSS and ARD; new episodes inherit podcast defaults. One explicit override replaces both defaults for that episode, and 0 is a real override. Removing the override restores inheritance. No fades, silence/ad detection, waveform editor or modification of upstream/downloaded originals.

Contract example (other normal subscription fields omitted here only):

```json
{
  "sourceType": "ard_audiothek",
  "ardShowUrl": "https://www.ardsounds.de/sendung/unser-sandmaennchen/urn:ard:show:0e3f60ed4ba198df/",
  "trimStartSeconds": 20,
  "trimEndSeconds": 15,
  "episodeTrimSourceKey": "ard:urn:ard:show:0e3f60ed4ba198df",
  "episodeTrims": [
    {"identity": "urn:ard:episode:2be7265fda224f44", "trimStartSeconds": 0, "trimEndSeconds": 10}
  ]
}
```

`episodeTrims` contains at most 50 unique nonempty identities (at most 512 UTF-8 bytes each, no control characters). Require both numeric, finite, whole, bounded cut values for every override; reject null/string/bool/fraction/negative/overflow and duplicate identities. Validate defaults identically. Missing whole extension fields in legacy requests mean RSS/zero/empty; explicitly malformed fields are 400. For editing an existing record, omitted extension fields preserve its current values, so an old client cannot silently erase trimming. An ARD subscription cannot be converted by a legacy request omitting sourceType; require an explicit sourceType to change source kind. The initial preview input may omit scheduler fields as before. Preview and save share source/trim validation and return 400 for malformed config, not a generic 502.

Nonempty overrides require episodeTrimSourceKey to match the backend-resolved sourceKey. Preserve overrides when episodes leave the latest-N selection and across media URL changes/restarts; show stored overrides with a remove action so the 50-entry limit is manageable. Never silently evict them. A canonical source change clears overrides only through an explicit UI warning/clear action or explicit empty array in the request; otherwise return 409 `trim_source_mismatch`. Podcast defaults survive changing the source. Equivalent legacy/modern ARD URLs resolve to the same sourceKey and retain overrides. Retain RSS GUID/URL-fallback semantics, documenting that URL-only identities cannot survive URL changes reliably.

Migrate registry v1 to v2, supplying RSS sourceType, zero cuts and empty override lists without changing IDs, revisions, published source, pins, original assignment or pending recovery intent. Validate v1 using its original schema before adding defaults; never treat arbitrary malformed v1 as valid. Keep a migration-specific `.v1.bak`, use existing durable temporary-write/rename mechanics, and recover old pending publication before/alongside migration. A failed migration must preserve the valid original and playable audio, expose a recoverable service error and avoid accepting mutations. Load/validate v2 explicitly; unknown versions are errors, not empty registries. Do not regenerate synchronously at startup; changed fingerprint profile takes effect on the next requested/due preparation. Document that downgrading to the previous podcast implementation requires restoring the v1 backup and cannot represent ARD/trim settings.

Persist requested defaults/overrides in subscription config and copy them into the worker revision snapshot. For every completed generation, its manifest and public prepared-episode list contain the **applied** trimStartSeconds/trimEndSeconds and measured trimmedDurationSeconds, not values from a newer draft. Preview includes effective cuts and, when metadata permits, durationSeconds/estimatedTrimmedDurationSeconds. Label the latter as estimates. If known metadata duration is too short, show a warning; never rely on it as the final encoder gate. Source lookup/normalization happens outside content/service locks, followed by the existing revision/ownership check under lock.

### Encoding and publication

Download all selected media before encoding. In `podcast_encode.c`, spawn FFmpeg with an argv API (POSIX spawn/exec, no shell) on owned local filenames; restrict decoder protocols to file/pipe and demuxers to the supported actual audio containers (MP3, MP4/M4A, Ogg/Opus, FLAC, WAV). Reject playlist/manifest formats rather than allowing a downloaded enclosure to reference other local files or network sources. Decode to PCM s16le/48 kHz/stereo and call the existing native `toniefile_*` functions. Use the first automatically created chapter, then `toniefile_new_chapter` exactly once between episodes. Handle partial PCM reads; cap decoded frames, wait on/reap children, capture bounded diagnostic stderr without deadlocking, and implement cancellation/overall deadline. Use nonblocking/pollable pipes so shutdown cannot hang on fread. Do not reuse the existing global box `stream_ctx`, change radio settings, or modify `encode.use_frontend`.

Apply cuts **before** native TAF encoding, per episode. Change podcast_encode’s input to an array of owned-local-path plus effective start/end cut structs, and return per-episode measured duration metadata. Build it in prepare() in selected playback order. Put checked sample-window calculations in pure `podcast_trim.c/.h` so assert-based unit tests can exercise boundary cases without linking the server.

Use constant-memory decoding rather than trusting RSS/ARD/container durations: for episodes with a nonzero end cut, first run the same bounded local FFmpeg decoder to count exact complete 48 kHz stereo PCM frames, discarding output; then decode that immutable input again and send only frames in `[startSeconds*48000, totalFrames-endSeconds*48000)` to toniefile_encode. With an end cut of zero, one pass may discard the start and encode the rest, checking retained-frame count at EOF. Check uint64 arithmetic, 4-byte frame alignment across arbitrary pipe chunks, nonzero retained duration and consistency of the second-pass count. Do not allocate a tail buffer proportional to cut length, write whole decoded PCM to disk, use reverse filters, or add ffprobe as a duration authority. The two-pass cost for end cuts is deliberate; keep the existing overall one-hour encoding deadline shared across passes and count source decoded duration once against the six-hour job limit. Enforce the same memory/workspace/TAF limits. Cancellation, child wait/exit checks, partial-frame handling and stderr bounds apply to both passes.

If cuts leave zero/negative duration, any episode decoder fails, measured passes disagree or any limit is exceeded, fail the whole preparation, identify the affected episode in a bounded plain-text error, discard temporary output and leave the old assignment/generation unchanged. Do not skip the episode, clamp cuts or publish fewer chapters. Add a chapter only for an episode with retained audio; final count must still equal the selected count. Keep one chapter per episode and account for normal Opus frame padding at boundaries in audio-duration assertions. Never trim an already published TAF in place. Successful changes use the usual immutable-generation/pinning/publication path; stale trim edits cannot publish after a new revision.

Allocate a strictly increasing custom audio ID for each changed successful generation: max(current custom timestamp ID, last allocated ID + 1), with checked 32-bit arithmetic and TEDDY_BENCH_AUDIO_ID_DEDUCT semantics. Persist last allocated ID; burned IDs after failures are harmless. Clock rollback/same-second rebuilds must not reuse IDs. Pass this ID to `toniefile_create`; do not patch a finished header or mutate an Ogg serial afterwards.

After encoding closes, require `isValidTaf(..., true)` and exactly the selected episode chapter count. Write the generation manifest and durable registry intent; atomically rename completed output on the same filesystem, then update the content source under its existing mutex. Reconcile a crash between these operations by examining the durable generation metadata and actual content source. Never expose `.tmp` output to a box; never remove the last successful generation on error.

Pin immutable generations per box/download session in `handler_cloud.c` before serving a managed subscription. There is no generation token in current device requests; do not invent one and expect the box to send it. Use `(stable authenticated box identity, effective content scope, rUID)` as the pin key, never source IP alone. A request starting at byte zero durably establishes/replaces the pin before serving the header. A nonzero Range reuses the persisted pin across server restart. Keep references while serving. Do not apply an old byte offset to a newly published TAF. Keep superseded generations for at least a 24-hour retry window and while any live/persisted session references them; prune only owned, unreferenced files. Expired pins become explicit tombstones, not a license to reinterpret offsets. Devices lacking stable identity must not share or overwrite another box's pin.

**Protocol decision requiring the early spike:** proposed response for an orphaned nonzero Range is HTTP 416 with a valid `Content-Range: bytes */<current-length>`, no audio bytes. Verify that the target box then requests full content and actually accepts the new audio. This is not established by current code. If 416 does not recover correctly, test the device's documented manual freshness-sync/full-download path and record the exact supported behavior before changing publication logic. First adoption of a previously unmanaged source, process restart, and sessions predating pin creation are orphan cases too. Do not mark this contract implemented based only on a generic HTTP client test. If no safe recovery works, stop publication integration for a protocol redesign; independent feed/API/UI work can continue.

### Scheduling and placement

Initialize/load service before accepting podcast jobs; run nonblocking due-job enqueueing from the server loop and preparation in one worker. Queue at most 32 jobs; at most one pending/running job per subscription; configuration changes coalesce to the latest revision. Failed jobs back off 15 min, 1 h, then up to the configured interval (use 6 h maximum when periodic updates are off); manual refresh can retry immediately. No subscriptions means no network activity.

At `tbs_tag_placed`, valid tags with a subscription and `refreshOnPlacement=true` enqueue a check, throttled to once per 15 minutes. Invalid tags/removal do not enqueue. Normalize UID/rUID and overlay identically to ordinary content lookup. The notification must return promptly without fetching or encoding. Browser polling is not the scheduler. Startup queues due jobs without marking existing audio invalid. Worker shutdown cancels network/FFmpeg, joins, then releases registry/mutex resources.

## Commands and verification environment

Commands read from the original repo and the in-progress implementation. Only the read-only frontend baseline checks listed above were run for this extension; builds, installs and backend/device tests remain executor gates:

| Context | Command | Expected |
| --- | --- | --- |
| Backend, Linux dependencies installed | `rtk make build` | server builds; Linux default sanitizer configuration |
| Frontend | `rtk npm ci` | install from lockfile |
| Frontend | `rtk npm run build` | TypeScript check and Vite production build pass |
| Frontend | `rtk npm test` | Vitest suite passes |
| Frontend, installed dependencies | `./node_modules/.bin/tsc --noEmit` | exit 0, no output; extension baseline passed |
| Frontend, installed dependencies | `./node_modules/.bin/vitest run --no-cache src/components/tonies/toniecard/podcasts src/api/apis/TeddyCloudApi.test.ts` | all focused tests pass; extension baseline was 4 files / 30 tests |
| Frontend | `rtk npm run format:check` | Prettier check passes |
| Frontend | `rtk npm run check:translations` | locale key validation passes |
| Disposable integration checkout with Node installed | `rtk make web` | copies frontend dist to backend staged web directory |
| Backend | `rtk docker build -f DockerfileUbuntu -t teddycloud:podcasts .` | combined production image builds from staged assets |

Use Docker on macOS; do not run the Linux Makefile directly on the host. Add a NEW `DockerfilePodcastTests` based on the production Ubuntu build dependencies, plus Python 3, pkg-config, curl/XML dev libraries, FFmpeg and Node 26 in the test/build stage. Copy the initialized repo, run frontend `npm ci`, check/test/build it, stage assets with `make web`, build backend, and run isolated backend tests. Keep the resulting test image usable for compiling/running tests with a working directory and shell, not only a build layer whose files disappear. Unlike production Dockerfiles this test harness need not run the clean-Git release check. The final production runtime remains unchanged in process layout and contains no Python/Node test dependencies. Add equivalent package dependencies to all production Docker variants and the sandbox.

Make targets required in step 1 (already present in the inspected dirty backend; extend them): `test_podcast_unit` (C fixture tests), `test_podcast_api_with_server` (starts/stops a test server against unique temporary config/data and fixture HTTP transport), `test_podcast` (both). Extend `test_podcast_unit` to compile the new pure ARD/trim modules as well as existing parser/HTTP tests. Add `PODCAST_TESTING=1` only to a separate test binary/build directory, with fixture transport/clock/failure hooks compiled into the podcast service; use test-process environment/input files, not admin HTTP test controls. Production must compile with `PODCAST_TESTING=0`, must not honor fixture environment overrides, and must not contain test-only routes. The test runner must create/copy required config and test certs into temporary roots, select free ports, clean up on signals, and never read/mutate a developer's installed service or live volumes. Match the existing Python unittest style, but avoid its production-data assumptions. Feature flags are not tracked by the existing object prerequisites: isolate object/output directories by PODCAST_SUPPORT/PODCAST_TESTING values, or explicitly clean before switching modes.

## Ordered implementation steps

### 0. Resolve interrupted-download behavior before committing to publication

Using an authorized test Tonie and the intended box generation/firmware, record content requests for a normal prepared TAF, an interrupted/resumed download, an ear-triggered freshness check and a revision change. Confirm stable box identity and URI/Range semantics in `getTonieInfoForRequest` and `handleCloudContentExt`. In a disposable prototype build, test the orphaned-Range 416 response described above without replacing household content. Record the selected recovery behavior, pin identity, retention/restart policy, actual HTTP trace with auth tokens redacted, and device results in NEW `docs/podcast-download-protocol.md`.

**Verify:** the document contains the tested model/firmware and passing results for zero-offset start, nonzero resume, orphan recovery, restart and first adoption; a fresh full download must follow orphan recovery and play correctly. After test infrastructure exists in step 1, encode the selected server response contract in `test_podcast_api.py` and run `test_podcast_api_with_server`. If hardware is unavailable, label this gate unresolved and continue independent steps 1–3 and frontend work; do not present step 4 publication or the overall feature as complete.

### 1. Reconcile bases; establish dependencies and test harness

Read README/CONTRIBUTING in both repositories, inspect the existing dirty implementation without discarding it, initialize only missing submodules, record base SHAs, compare excerpts. Inventory completed original milestones and extend them in place. Preserve original tests and outstanding publication/device gates. Check upstream #363, #417 and PR #462 status; do not merge unrelated work. Add feature flag, dependency discovery, feature-disabled stubs and worker/API headers; add Linux/Docker dependencies and test image/targets above. Add one passing fixture test for capabilities/disabled mode so the harness actually runs code.

**Verify:** `rtk docker build -f DockerfilePodcastTests -t teddycloud:podcast-tests .` → exit 0, frontend baseline gates and backend harness run. `rtk docker run --rm teddycloud:podcast-tests make build PODCAST_SUPPORT=0` → disabled build passes without linking curl/XML, using a separate object directory or explicit clean first. Test unsupported capabilities and verify `ldd` of the disabled binary has no curl/XML entries. Windows scope is compile-preserving stubs, not a new Windows runtime feature. Prefix host shell commands with RTK, but do not require RTK inside images: run `rtk docker run --rm teddycloud:podcast-tests make test_podcast_unit`, likewise for `test_podcast_api_with_server`/`test_podcast` and frontend gates via the image runner. Bare make/npm commands mentioned as “in test image” are executed by its test runner without an RTK dependency.

### 2. Implement bounded HTTP, RSS parsing and episode selection

Implement `podcast_http.c` and `podcast_feed.c` against fixture-driven dependency injection. Add XML/entity rejection, URL/address validation, limits, redirects, feed identity/date ordering and fingerprint rules from this contract. Do not allow the XML parser to fetch external DTDs/entities; reject documents declaring DTD/entity definitions. Support namespace metadata without replacing libxml2's safe defaults with recovery/huge/entity expansion modes.

**Verify:** NEW `rtk make test_podcast_unit` in test image → parsing/selection/network-policy tests pass for dated/undated/duplicate/future/malformed/oversized/relative/empty fixtures, both orders, changed count with 304, redirect/address validation, and cancellation. No network access required for these tests.

### 2a. Add the ARD provider and normalize source selection

Implement `src/podcast_ard.c` / `include/podcast_ard.h` using the ARD contract above, including strict link parsing, canonical-ID resolution, fixed variable-based GET queries, ISO8601 parsing, bounded pagination and downloadable episode selection. `preview()` dispatches RSS or ARD into the same normalized episode shape; `discovery()` dispatches Apple or ARD while retaining old Apple defaults. Add provider query parsing in `src/handler_podcast.c`, namespace caches/budgets, and add a discriminated search-result type on the frontend. Add fixtures under `tests/fixtures/podcasts/ard/` and assertions in `tests/test_podcast.c`; the service/API fixture URL map must include deterministic generated ARD query URLs. Requests/responses are minimal fixtures, never live-network CI dependencies.

**Verify, in test image:** `make test_podcast_unit` → URL hosts/numeric/URN IDs, both date offsets, future/live/non-downloadable filtering, stable asset identity, multiple pages/duplicates/no-progress, GraphQL errors/null/malformed/oversized responses all pass. `make test_podcast_api_with_server` → Apple legacy search unchanged, ARD search/preview normalizes numeric and modern links to the same show, unknown provider is 400, upstream ARD errors are 502 and do not break RSS. Do not proceed to publication until source identity tests pass.

### 3. Implement persistent subscriptions, API and discovery

Implement `podcast.c` registry/validation and `handler_podcast.c` JSON handlers; register explicit routes before generic prefixes in `server.c`. Add the documented API shapes/statuses, overlay resolution, optimistic revision/ownership rules and Apple discovery mapping/cache. Create `docs/api/podcasts.md` with request/response examples. Do not let successful JSON saves imply that audio is ready.

**Verify:** NEW `rtk make test_podcast_api_with_server` → create/edit/list/preview/search fixture/refresh/detach and malformed body/unknown overlay/conflict tests pass; restart preserves subscriptions; corrupt registry retains backup and reports error; isolated server process stops cleanly. Search tests use provider fixtures, not Apple or ARD network access.

### 3a. Persist trim settings and migrate existing podcast records

In `src/podcast_feed.c` extend shared validation or extract bounded reusable config helpers without duplicating rules. In `src/podcast.c` normalize create/update/preview inputs, migrate v1→v2, copy all source/trim fields into job snapshots, and include effective cuts/sourceKey/profile-v2 in fingerprints. Return sourceType/trim capabilities, normalized config, and generation-applied metadata. Preserve pending recovery and revisions. In `src/handler_podcast.c` implement the bounded 64 KiB body cap and corresponding 413 message; update `tests/test_podcast_api.py` so its former 17,000-byte body rejection is replaced by exact new boundary tests. Document complete RSS, ARD, default-cut and override request/response examples in `docs/api/podcasts.md`, including legacy omitted-field semantics and mismatch conflicts.

**Verify, in test image:** `make test_podcast_unit` → invalid/missing/explicit-zero cut values, duplicate/overlong identities and source mismatch rules pass. `make test_podcast_api_with_server` → old RSS requests remain valid; v1 fixture migrates with pins/assignment intact and a recoverable backup; v2 restart round-trips defaults/overrides; malformed/unknown-version state is not overwritten; omitted update fields preserve saved settings; explicit source switch with old overrides returns 409; equivalent ARD links retain overrides; 64 KiB boundary/50-override requests pass and oversized body returns 413. Preparation fingerprint tests show an effective cut change invalidates the generation, while an inactive override or fully overridden default change does not.

### 4. Prepare and publish complete chaptered TAF generations

Implement `podcast_encode.c`, worker preparation and immutable publication. Add generated local paths, safe FFmpeg spawn, bounded I/O, chapter count/TAF validation, increasing IDs and durable manifests. Hook content source ownership in existing `handleApiContentJsonSet` without changing unrelated fields. Add generation session pinning to managed-content requests in `handler_cloud.c`; preserve ordinary content routing unchanged. Add injected failure points around rename/source/registry writes and startup reconciliation.

**Verify:** NEW `rtk make test_podcast` → a three-episode audio fixture creates a valid TAF with three chapters in requested order; unchanged refresh retains file/audio ID; changed selection increases ID even within one second/clock rollback. Failed download/decoder/full disk/cancellation/invalid TAF cannot replace old content. Concurrent downloads and Range retries stay on their original generation. Manual source changes and stale job results cannot be overwritten by the worker.

### 4a. Apply accurate per-episode cuts during PCM preparation

Implement pure checked frame-window helpers in `src/podcast_trim.c` / `include/podcast_trim.h` and test them in `tests/test_podcast.c`. Extend `include/podcast_encode.h`, `src/podcast_encode.c` and `prepare()` in `src/podcast.c` with ordered path/effective-cut inputs, shared bounded decoding/counting logic, measured per-episode result metadata and the one-/two-pass behavior above. Keep local decoder restrictions and native TAF APIs. Carry applied-cut metadata into pending intent, generation manifests and the committed prepared list, including startup recovery; a newer requested draft must not relabel an older prepared generation. Review allocation/cleanup/child lifecycle on every failure path.

**Verify, in test image:** `make test_podcast` → generated intro/main/outro WAV fixtures demonstrate correct measured duration and retained signal, not just a shorter header. Default cuts affect every episode; an explicit zero override preserves its intro; removing an override restores defaults; unknown/stale metadata durations do not affect actual cuts; exact/full-length and over-length cuts fail without replacing old content. A trim-only edit yields a higher audio ID/new source; a second unchanged refresh keeps it. Chapters still match selected count/order. Cancellation/revision supersession during either decoder pass retains the old generation and reaps all children. The precise automated cases are listed in the regression matrix below.

### 5. Wire persistent scheduling, placement and shutdown

Integrate initialization, loop tick and shutdown in `server.c`; add the lightweight placement enqueue hook in `toniebox_state.c`. Implement queue bounds, deduplication, revision coalescing, retry schedule and cache pruning. Callbacks may copy identity/state but must never retain borrowed `client_ctx` or settings pointers into an asynchronous job. Use explicit snapshots and re-resolve scope safely before publishing.

**Verify:** NEW `rtk make test_podcast` → injected-clock periodic/disabled/backoff/restart tests pass; placement UID-to-rUID/shared-overlay tests pass; repeated placements enqueue once; placement hook contains no blocking HTTP or encoding calls; shutdown during download/encode exits within 10 seconds, reaps children, and retains prior audio. Check idle subscriptions generate no unsolicited requests.

### 6. Add the podcast controls to the existing Tonie editor

Add typed API methods/types first, then `usePodcastSubscription` and `PodcastSubscriptionEditor` under the paths listed in Scope. Add a source mode choice in the existing edit modal: normal audio/radio vs podcast. Podcast mode exposes source choices **Podcasts (RSS)** and **ARD Audiothek / ARD Sounds**. RSS keeps general search and manual feed URL; ARD offers show-only search and a pasted show URL. Selecting a result fills the correct source fields without treating a string ARD ID as a feed URL. Both share N/order/interval/placement controls, preview, prepared episodes, progress, last successful update, refresh and detach. Provider changes abort/clear stale search and preview results. Capability fallback: a backend without sourceTypes only offers RSS; without trimSupported it hides trim inputs and does not send unknown extension fields.

Add start/end seconds InputNumber fields (0–21,600, precision=0) for podcast defaults, with clear “seconds removed” help text. Preview rows show inherited effective cuts and an “Override for this episode” switch; enabling it copies current defaults into two editable fields, disabling it removes that identity from episodeTrims. Explicit zero overrides remain zero. A saved override list allows removing entries no longer in the latest-N preview. Editing cuts changes only the draft and re-evaluates effective values locally; do not refetch ARD/RSS for every numeric keystroke. Preview fetch/cancellation depends on source/count/order, while fetched episode metadata is combined with current draft cuts for display. Recompute warnings/estimated durations locally; actual prepared durations remain distinct. Cut changes participate in dirty checking, modal Save, Cancel/reopen and optimistic revision checks. Source changes with stored overrides show a clear warning requiring explicit clearing; do not discard them on every pasted-link keystroke or when equivalent links resolve to the same sourceKey. Keep podcast defaults across source changes.

Extend `src/types/podcastTypes.ts`, `podcastConfig()`/`defaultPodcastConfig()`/`validPodcastConfig()` in `podcastValidation.ts`, the API serializer, and the hook’s baseline/draft snapshots together. Retain the full PodcastCapabilities object in usePodcastSubscription and expose it to the editor; supported alone cannot drive per-feature fallback. Normalize missing legacy response fields before dirty comparison. Preserve episodeTrimSourceKey and full stored override list during polling/saves; background status polling must not overwrite an active draft. Do not mutate the prepared list to display requested cuts before a generation succeeds. Hide/disable unsupported controls using capabilities; an older backend returning 404 must leave normal editing usable. Disable edits in read-only cards. Render titles as text.

Keep edits as a local draft until the existing modal Save action; Cancel has no server mutations. Extend `useTonieCardSaveFlow` so a podcast save calls subscription/save and does not immediately write the old selectedSource over the worker's future result. Normal model editing remains supported; handle partial save failure visibly/idempotently. Include podcast dirty state in `TonieCard.hasPendingChanges`. Only explicit Refresh now/Detach buttons mutate independently, with clear button text and existing confirmation conventions. On detach synchronize source UI with backend restoration. Poll subscription progress every 2 seconds only while the modal is open; stop/cancel polling on unmount and ignore stale search responses. Preparation continues server-side after closing the modal/browser.

Show these user-facing semantics without backend implementation jargon: selected versus currently prepared episodes, preparation/error status, previous episodes available on failure, and placement update may become available on the next play/sync. Do not expose shell commands, internal paths or Docker setup in the normal feature flow. Add translation keys across all locales and a frontend changelog entry.

**Verify, in frontend repository:** `rtk npm test` → existing tests plus podcast API, draft/cancel, N validation, debounce/stale-search, overlay, polling cleanup, save-source ownership and failure-state tests pass. Add actual rendered editor tests for ARD provider/link selection, provider-switch cancellation, trim defaults/override inheritance/explicit zero/remove, source-change clear warning, invalid cuts blocking Save, cancel/reopen, capability fallback, and requested versus applied cuts. The existing `PodcastSubscriptionEditor.test.tsx` currently tests the save-flow hook only; extend it with rendered editor coverage rather than assuming its name proves controls are tested. `rtk npm run build`, `rtk npm run format:check`, `rtk npm run check:translations` → all exit 0. Use `createRoot`/React `act` with jsdom for component tests if no existing rendering helper is installed; pure validation/API tests follow current Vitest conventions.

### 7. Verify the single-image product, recovery and supported image variants

Add `tests/run_podcast_container_tests.py` (NEW) using stdlib unittest/subprocess to create throwaway volumes and run two explicitly separate modes: production-image smoke tests against `--image`, and deterministic API/worker/restart tests against the fixture-enabled binary in `--test-image`. Start only one TeddyCloud container at a time, not a fixture sidecar, and clean up owned containers/volumes. Do not add fixture services to the user's runtime compose. Production-image smoke tests verify bundled UI, runtime shared libraries, enabled capabilities, no test-control routes/environment behavior and no Node/Python requirement; do not claim fixture worker results exercised the production HTTP transport. Unit tests cover that transport with an injected connection layer, and a real public-feed smoke test is optional and not a CI gate.

Add `.github/workflows/podcast-tests.yml` for the test image and Linux build gates. Update `docs/podcasts.md` with the existing-UI flow, one-image deployment, volume persistence, supported RSS and ARD scope, podcast-default/episode-override cut semantics, v1 migration/downgrade backup, two-pass end-trim preparation cost, synchronization semantics and resource limits. Do not add extra runtime volumes/ports/services. For local final integration, first commit implementation changes in each feature repository. In a private integration-only checkout, select those exact frontend/backend commits, run `make web`, and create a LOCAL integration commit recording the frontend gitlink and generated `contrib/data/www/web` bundle. Keep this commit out of the backend feature PR. Production `make test_git_version` rejects dirty parent/frontend state, so staging assets without this commit is insufficient. Run that release check before image builds; record the two feature source SHAs, integration SHA and image ID. Maintainer release bundling replaces this private staging step after both PRs merge.

**Verify:** `rtk make test_git_version` in the private integration checkout → exit 0. Build production image as in the commands table. NEW `rtk python3 tests/run_podcast_container_tests.py --image teddycloud:podcasts --test-image teddycloud:podcast-tests` → production smoke and separately labeled deterministic worker/restart/root and PUID-PGID ownership tests pass against disposable volumes. `rtk docker build -f DockerfileDebian -t teddycloud:podcasts-debian .` and equivalent Alpine build → exit 0 from the clean integration checkout. `rtk docker compose -f docker/docker-compose.yaml config --services` → only `teddycloud`. Run frontend and backend complete gates once after final changes; repeat only for new failures/changes.

### 8. Run device acceptance before marking complete

On an authorized test box, record generation/firmware and run two successful prepared revisions on a test Tonie, using known short episode fixtures, one real public RSS feed and one ARD show with a permitted download. Include a default trim, an individual override and a subsequent trim-only revision. Verify selected ordering, skip next/previous by the box's usual gestures, normal resume behavior, ear-triggered freshness sync, automatic update timing, removal/replacement, offline playback after full download, restart/reconnect, interrupted downloads and refresh during playback. Repeat placement mode with a deliberately slow refresh: prior prepared content remains playable; the UI does not promise immediate replacement. Check shared versus independent overlay assignments if using multiple boxes.

**Verify:** record the matrix with observed results and timestamps in `docs/podcasts.md`; all required rows pass for the tested device. If hardware is unavailable, leave plan status BLOCKED for hardware acceptance with automated implementation complete; do not claim same-placement refresh or skipping was verified. Toniebox 2 requires its own matrix before claiming support.

## Required regression cases

- C fixture suite: valid namespace/CDATA RSS; duplicate GUID/URL; dates with offsets; undated fallback; future/non-audio filtering; fewer than N; count/order/profile changes; 304 handling; DTD/entity rejection; oversized feed/download/workspace/TAF and duration limits; IPv4/IPv6/private redirects and DNS pinning; monotonic IDs; partial PCM chunks; chapter count; safe process arguments; child cancellation/reaping.
- Service/API suite: invalid counts/intervals/body/IDs/overlay; create/edit/detach with expectedRevision; shared-directory identity; invalid UID order; no subscription/no network; dedup/backoff; corrupted state; crash at each publication boundary; old jobs after edit/delete; manual source override; permissions/full disk; active session pruning/range retries; browser closed and container restart.
- Frontend: search provider failure leaves manual RSS usable; N and order persisted; cancel no mutation; stale async results ignored; old backend/read-only behavior; overlay carried on all calls; status polling cleaned up; prepared versus requested episode lists; safe text rendering; normal source save unaffected; podcast save never overwrites output assignment.

Additional extension regression matrix (required; fixtures/offline assertions are CI gates):

- ARD unit/API: search escaping (quotes, ampersands, umlauts); missing/unknown provider; cache separation; old/new exact hosts and numeric/URN equivalence; lookalike host, encoded-slash and malformed URL rejection; ISO8601 timezone ordering; future/live/non-downloadable/no-audio filtering; assetId override identity stable while publication/CDN URL changes; null/GraphQL errors/empty/malformed/oversized/timeouts; pagination with skipped and duplicate entries; repeated page and cumulative limits; fewer-than-N warning. Search/provider failure leaves the other source usable.
- Config/migration: omitted legacy values and update preservation; 0 overrides versus inheritance; negative/fraction/string/bool/null/non-finite/over-limit cuts; duplicates and 51 overrides; 512-byte identity boundary; large valid override request and 65,536-byte request cap; sourceKey binding/explicit clearing/equivalent ARD links; v1 and pending-publication recovery; pins/original source preserved; malformed/unsupported registry version; restart retains inactive overrides without erasing them.
- PCM correctness: generate 12-second WAV episodes with 3 seconds of 300 Hz intro, 6 seconds of 700 Hz main and 3 seconds of 1,100 Hz outro. Cuts 3/3 yield six seconds of main; a 0/3 override yields nine seconds including intro; cut-only updates change the output. Decode the Ogg/Opus payload after the 4,096-byte TAF header with local FFmpeg, check duration within codec-padding tolerance (at most 0.2 seconds per episode), and estimate tone energy in central windows to prove removed signal is absent. Test start-only, end-only, both, zero cuts, different overrides/order, unknown/wrong metadata duration, exact-full/over-full/very short episodes, partial 1–3-byte PCM chunks, checked frame arithmetic, second-pass decoder failure and no leaked child. Pure window tests assert exact retained frame counts; lossy tone assertions use thresholds, not byte equality.
- Service/publication: trim-only regeneration; unchanged effective cuts keep audio ID; inactive override changes do not regenerate; old jobs cannot publish after trim/source edit; errors preserve source/TAF/hash/chapter list; published applied cuts remain old while new requested cuts prepare; restart recovery preserves applied metadata; cancelled two-pass decoder exits within the existing shutdown bound. Existing Range/pin and device gates remain required after trim-only publication.
- Frontend: real rendered controls, default and override persistence, independent zero values, remove/restored inheritance, inactive override removal, capacity error, Cancel no mutation, read-only/old capability fallback, source mismatch and explicit clear, draft survives polling, stale provider/preview response ignored, cut-only edit marks dirty, current prepared versus requested cuts accurately displayed, all locale keys present.

## Done criteria

- [ ] Both repositories pass their commands above; test runner output is saved in review notes with source SHAs.
- [ ] One runtime image serves updated UI and prepares audio with the browser closed, with no external process/service deployment.
- [ ] Early device protocol spike is recorded; failed preparation, restart and interrupted transfers preserve a valid playable generation, and orphaned ranges recover through the tested device behavior.
- [ ] Prepared chapter count/order and audio version semantics pass automated checks and real-device skipping passes.
- [ ] Fixture-backed ARD show search and both legacy/modern pasted links normalize correctly; prepared ARD playback is accepted on the tested device; no RSS bridge/runtime service was added.
- [ ] Default cuts plus per-episode overrides work for RSS and ARD; generated signal/duration assertions pass; trim-only changes regenerate, and invalid full-episode cuts preserve playable previous output.
- [ ] Registry v1→v2/restart/pending recovery tests pass, retaining published source and pins; old RSS clients remain usable and capability fallback is covered.
- [ ] The UI’s rendered control tests pass, including Save/Cancel, explicit zero inheritance, provider cancellation and draft/published separation; all translation keys validate.
- [ ] Existing source/model/radio editing and non-podcast content delivery remain functional.
- [ ] Standard persistent volumes suffice; non-root image mode can write only the existing owned storage locations.
- [ ] Feature-disabled build remains supported; all supported Linux image variants contain required runtime libraries.
- [ ] No vendored source, frontend gitlink, unrelated generated bundle, credentials or out-of-scope files are committed.
- [ ] Backend/frontend PR descriptions state device coverage and remaining limitations; plan index status reflects actual completion.

## STOP conditions

Stop and report a concrete decision needed if upstream independently duplicates this design (the known dirty podcast foundation is expected and should be extended), content session/range semantics cannot safely retain a generation, any change requires vendor edits, safe FFmpeg cancellation cannot be achieved, or image packaging would require a second service/user script. Do not silently replace the native service with a Python/Node sidecar. Reconcile minor upstream drift, but stop for changed ownership/auth/overlay contracts. After two failed reasonable attempts at a gate, report evidence rather than weakening tests. Also stop for ARD schema/identifier/download-permission drift that the fixed-query fixtures cannot reconcile, a required second service/auth workaround, loss of existing registry/pin recovery data, or trim implementation needing unbounded PCM memory/disk. Do not replace safe trimming with duration metadata guesses. Missing hardware blocks only final hardware acceptance; finish independent implementation and automated tests first.

## Maintenance and external references

Review concurrency, publication/recovery, session retention, cancellation and overlay ownership more closely than UI polish. Keep actual published audio IDs, registry metadata and manifests consistent. Updating codecs/bitrate, selection, source normalization or trimming semantics invalidates fingerprints. Preserve applied-cut metadata during pending generation recovery; do not reuse a published manifest to describe newer requested cuts. ARD is a provider adapter with external schema risk: keep query/normalization fixtures small and review schema changes there rather than branching the scheduler/publication pipeline. Stable asset identities and canonical show keys keep overrides attached across link/CDN changes; URL-only RSS identities retain their documented limitation. Adding Atom/private feeds or artwork needs separate scoped work. Do not import the official cloud's duration restrictions into TeddyCloud; this plan's limits are local resource policy. Review RSS/HTTP parser dependencies through normal distro updates.

- [ARD public GraphQL endpoint](https://api.ardaudiothek.de/graphql): live introspection, Search/Show/Resolve query shapes and sample responses verified 2026-10-09; this is observed behavior, not a promise of a supported/stable third-party API.
- [ARD Sounds](https://www.ardsounds.de/): current public website; legacy ardaudiothek.de redirects here.
- Existing upstream direction: [ARD Audiothek #363](https://github.com/toniebox-reverse-engineering/teddycloud/issues/363), [pre-encoding request #417](https://github.com/toniebox-reverse-engineering/teddycloud/issues/417).
- [TAP PR #462](https://github.com/toniebox-reverse-engineering/teddycloud/pull/462) proposes related streaming/version changes; it is not a dependency of this direct-TAF plan.
- [Apple Search API](https://performance-partners.apple.com/search-api): provider request/response and rate-limit guidance.
- [libcurl CURLOPT_RESOLVE](https://curl.se/libcurl/c/CURLOPT_RESOLVE.html): pin validated addresses while preserving HTTP host/TLS identity.
- [libxml2 parser API](https://gnome.pages.gitlab.gnome.org/libxml2/html/parser_8h.html): safe XML options; check installed distro version, not just newest online APIs.
- Existing solutions such as [Podcast2Tonie](https://forum.revvox.de/t/automating-podcast2tonie/1767) and [audio2tonie](https://github.com/marco79cgn/audio2tonie) demonstrate the pipeline, but do not clone/vendor them for runtime.
