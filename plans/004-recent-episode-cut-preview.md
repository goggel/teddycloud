# Plan 004: Listen across recent episodes to choose podcast cut defaults

> Follow the steps and verification gates in order. This is a plan, not an
> implementation. Preserve existing user work. Do not push, publish, or change
> household assignments as part of execution. Update this plan's index row with
> actual results and outstanding acceptance checks.

## Status

- **Status:** IN PROGRESS — software APPROVED after independent review on 2026-10-12; browser playback acceptance pending. See [execution review](004-execution-review.md).
- **Implementation:** reviewed isolated frontend `b071f72459e76112764d86aa4c6d59894c2c9e82`, backend documentation `06a08c9d593cc7bbdff5da1f2368aaf2cc74f50c`. Subsequently integrated and pushed at user request on 2026-10-12: frontend `de9446bbeda1600940a6642879111b18fd13d0ac`, backend `6c361acd704854e88fa2eb92ed639b2c241bc99c`, both `feature/prepared-podcast-tonies`; backend includes a separate matching bundle/gitlink commit. Browser acceptance remains pending.
- **Priority:** P1
- **Category:** direction / podcast editing UX
- **Effort:** M, approximately 3–5 engineering days including browser acceptance
- **Risk:** MED; browser media support and asynchronous source changes need care
- **Planned at:** 2026-10-11; outer planning repository `1cbd143d`, backend
  `2d0cc0f1b414cd329b40ce835aabee2e024b020d`, frontend
  `e94f205cf51f8c7733a78d5e70ef320fb1c1490e`. Reconciled 2026-10-12:
  documentation/editor/translations unchanged; the only podcast-directory drift
  is 003's reviewed detach reconciliation (`recordSubscription(undefined)` in
  `usePodcastSubscription.ts`). Both application working trees are clean.
- **Depends on:** implemented podcast and four-source software from 001/002.
  Coordinate with 003 because it also touches the subscription hook/editor
  lifecycle. Its completion is not required for design, but reconcile its
  integrated fixes before implementing shared-file changes. Earlier physical
  acceptance gates remain separate.

## Why this matters

The current preview lists episodes and cut values without playing their audio.
Users must guess where an intro ends or an outro begins. Let them listen to the
latest five episodes, capture those positions as draft cuts, and quickly test
the same defaults across episodes before saving. A varying intro or outro can
still use the existing individual override.

Interpretation of the request: “last few episodes” means the five most recent
eligible audio episodes of the selected RSS or ARD show, independent of the
number and playback order prepared for the Tonie.

## Product behavior

1. Add **Listen to recent episodes** alongside the existing episode-selection
   preview in both Podcast and ARD editors. Clicking it loads metadata for the
   latest five; fewer are fine. Show episode title, publication date, and
   duration when known. Keep the current **selected episodes** preview and
   preparation settings intact. Label the listening list clearly as recent
   episodes rather than the future Tonie playlist.
2. Provide one player for the selected episode, with play/pause, native seeking,
   original-source elapsed time, total duration, and skip ±5 seconds. Audio
   loads only after an explicit episode-listen action. Never autoplay on list
   load, source selection, or episode switching.
3. Add **Beginning**, **End**, **Hear intro boundary**, **Hear outro boundary**,
   and **Try retained audio**. Beginning goes to zero; End goes to the final
   30 seconds. Boundary buttons play a short context window around the proposed
   cut so the user can hear the transition. Try retained audio starts at the
   intro cut and pauses at the outro boundary. All times refer to the original
   episode, so a 30-second intro cut does not reset the displayed clock.
4. An explicit **Edit podcast defaults / Edit this episode** target determines
   what **Set intro cut here** and **Set outro cut here** change. Default target
   is podcast defaults. Intro captures whole seconds from the beginning; outro
   captures whole seconds removed from the end, not an absolute end timestamp.
   Existing numeric fields remain usable for fine adjustments.
5. Default changes affect this editor's draft and can immediately be auditioned
   on another recent episode. Existing episode overrides remain intact. In
   default mode, audition the defaults even if the selected episode has an
   override, and display that distinction. In episode mode, audition its
   effective cuts and create/update its existing source-bound override when
   capturing a boundary. Offer **Use podcast defaults for this episode** as
   an explicit override removal.
6. Save uses the existing persistence/preparation flow. Cancel discards draft
   edits and stops playback. Listening, seeking, and switching episodes never
   save, queue preparation, or change episodeCount/order. Editing cuts never
   refetches the recent list or audio URL.

## Technical approach and limits

Use the existing metadata preview API and one native HTML audio element pointed
at the returned episode URL. This first version needs no backend endpoint,
FFmpeg job, server media cache, new dependency, or schema migration. The existing
API already returns public enclosure/asset URLs for both providers.

Do not use fetch-to-Blob, Web Audio decoding, or a waveform library. Native
playback avoids introducing a cross-origin fetch requirement solely for a
waveform. It remains subject to browser codec support, upstream availability,
seeking support, and mixed-content restrictions. Handle these visibly: preserve
the numeric editor, explain that playback or seeking is unavailable, and provide
an explicit Retry/Reload recent episodes action. Do not silently switch to an
unrestricted server proxy or automatically retry indefinitely. On HTTPS pages,
report an HTTP enclosure limitation without rewriting its URL. Do not promise
that the browser downloads only a small bounded segment.

Browser duration and seek positions assist editing; the existing backend PCM
decoder remains authoritative for final cuts. Provider ads or changing media
can alter boundaries between preview and preparation. Display concise help that
cuts are per-show defaults and may need episode exceptions. This version is a
listening aid, not automatic intro detection or sample-accurate editing.

## Current state and conventions

Application backend root is the nested `teddycloud/` checkout in the outer
workspace. Frontend root is `teddycloud/teddycloud_web/`. Paths below are relative
to the relevant application repository, not the outer planning repository.

- `src/components/tonies/toniecard/podcasts/PodcastSubscriptionEditor.tsx:74`
  ties the current metadata preview to playlist settings:

  ```ts
  const selectionToken = sourceToken(draft) + ":" + draft.episodeCount + ":" + draft.order;
  ```

- Its `setOverride` at lines 82–92 replaces both cut values and binds entries to
  `episodeTrimSourceKey`. Its preview effect at lines 117–168 uses an
  AbortController and checks cancellation before accepting a response:

  ```ts
  .podcastPreview(
      configForCapabilities(
          { ...draft, episodeTrims: [], episodeTrimSourceKey: "" },
          state.capabilities,
      ),
      controller.signal,
  )
  ```

  Preserve the selected-playlist preview's behavior. Add an independent recent
  episode request, rather than changing its count to five.

- `src/types/podcastTypes.ts:36–46` already provides media and identity:

  ```ts
  export interface PodcastEpisode {
      identity: string;
      title: string;
      url: string;
      publishedAt: number;
      durationSeconds?: number;
      estimatedTrimmedDurationSeconds?: number;
      trimStartSeconds?: number;
      trimEndSeconds?: number;
      trimmedDurationSeconds?: number;
  }
  ```

- `src/api/apis/TeddyCloudApi.ts:169` already exposes
  `podcastPreview(config, signal)`. Use it through the existing API instance.
  No new API method or capability flag is necessary for native playback.
- `podcastValidation.ts` supplies `sourceToken`, `configForCapabilities`,
  `effectiveTrim`, and `validTrim`. Whole-second cuts are 0–21,600 per side.
  Overrides replace both defaults, including explicit zero. At most 50
  source-bound identities are retained, even outside the prepared playlist.
- Backend `src/podcast.c:382` resolves RSS/ARD previews. Both playback orders
  choose the latest N first. Requesting count five and `newest_first` therefore
  already supplies the required recent comparison set.
- Existing `docs/podcasts.md` specifies local cut edits, Save/Cancel semantics,
  source-bound overrides, and measured durations after preparation. Preserve
  those contracts.
- React/TypeScript, Ant Design 6, i18next, four-space Prettier. Follow
  `PodcastSubscriptionEditor.render.test.tsx`: jsdom file directive,
  `createRoot`/`act`, mocked API, actual rendered Ant Design controls.
- `src/components/tonies/teddyaudioplayer/elements/AudioPlayer.tsx` is coupled
  to Tonie content and autoplays after metadata. Do not reuse it directly for
  this explicit-play preview. Existing footer audio uses `#globalAudioPlayer`;
  prevent simultaneous playback without clearing its current source.

Planning verification: the existing five podcast test files / 58 tests pass,
and `tsc --noEmit` exits 0. No new feature tests or browser acceptance have run.

## Scope

Frontend files permitted:

- Existing `src/components/tonies/toniecard/podcasts/PodcastSubscriptionEditor.tsx`
  and `PodcastSubscriptionEditor.render.test.tsx`.
- New files in that same directory:
  `PodcastRecentEpisodes.tsx`, `PodcastRecentEpisodes.test.tsx`,
  `usePodcastRecentEpisodes.ts`, `usePodcastRecentEpisodes.test.tsx`,
  `PodcastEpisodePlayer.tsx`, `PodcastEpisodePlayer.test.tsx`,
  `podcastPreviewTiming.ts`, `podcastPreviewTiming.test.ts`.
- Existing `public/translations/{en,de,fr,es,it,pl}.json`, new keys only.

Backend documentation permitted: `docs/podcasts.md`, explanation of the new
listening aid and browser limitations only. Outer repository: this plan and its
index status/result notes.

Out of scope: backend C code and endpoints, subscription storage/encoder,
source-profile persistence, subscription hook lifecycle changes, global player
refactoring, waveform/transcription/automatic detection, multi-episode bulk
overrides, recommendation algorithms, dependencies, household data, vendored
submodules, generated bundles or frontend gitlink changes in feature commits.
Do not fold outstanding plan 003 fixes into this work.

## Commands and workflow

Before implementation, inspect status in both application repositories and
create isolated implementation worktrees with initialized pinned dependencies.
Use branches `feature/recent-episode-cut-preview`. Base them on the current
integrated feature state after reconciling 003. Preserve the original trees.
No commits, branch changes, or source edits are performed by this planning task.

Drift checks in the respective implementation repositories:

```sh
git diff --stat 2d0cc0f1b414cd329b40ce835aabee2e024b020d..HEAD -- docs/podcasts.md
git diff --stat e94f205cf51f8c7733a78d5e70ef320fb1c1490e..HEAD -- src/components/tonies/toniecard/podcasts public/translations
git status --short
```

Compare shared-file drift by behavior and symbols, particularly changes from
003. Stop on unexpected source/lifecycle changes or dirty user work. Dependency
installation, if needed in the isolated frontend, is `npm ci` using its existing
lockfile; no upgrades. Commit logical units with descriptive subjects, matching
the existing history, for example `Add recent episode listening controls`.
Do not push or open a PR without an operator instruction. Backend documentation
and frontend contributions belong in their respective repositories.

All following commands run from the frontend root unless specified:

| Gate | Command | Expected |
| --- | --- | --- |
| Focused tests | `npm test -- src/components/tonies/toniecard/podcasts` | All existing and new tests pass |
| Typecheck | `./node_modules/.bin/tsc --noEmit` | Exit 0 |
| Complete tests | `npm test` | All pass |
| Formatting | `npm run format:check` | Exit 0 |
| Translations | `npm run check:translations` | Exit 0, matching keys in six locales |
| Production frontend | `npm run build` | Exit 0 |
| Diff hygiene, each changed repo | `git diff --check` | Exit 0 |

## Steps

### 1. Implement and test timing calculations

Create pure helpers in `podcastPreviewTiming.ts`. Use `Math.round(currentTime)`
for intro capture and `Math.round(duration - currentTime)` for outro capture.
Clamp seek positions to `[0, duration]`; validate captured cuts rather than
silently clamping to 21,600. Require finite positive media duration and finite
currentTime in range before enabling capture. Reject resulting pairs with
start + end >= duration. Zero remains valid. Format display times as mm:ss or
h:mm:ss, and show unknown duration without NaN/Infinity text.

Boundary context is `[max(0, boundary - 5), min(duration, boundary + 5)]`.
Intro boundary is start; outro boundary is duration - end. Retained playback
uses `[start, duration - end]`; End seeks to `max(0, duration - 30)`.

Test zero cuts, rounding, fractional duration, unknown/NaN/Infinity durations,
short episodes, out-of-range positions, six-hour limit, and complete removal.

**Verify:** `npm test -- src/components/tonies/toniecard/podcasts/podcastPreviewTiming.test.ts`
and typecheck; all pass.

### 2. Load an independent recent-episode comparison list

Create `usePodcastRecentEpisodes.ts`, accepting the existing API instance,
current draft/capabilities, and a source-resolution callback. Load only on a
user action, using `podcastPreview` with count five (or the advertised smaller
maxEpisodes), `newest_first`, zero defaults, and empty overrides/source binding,
passed through `configForCapabilities`. Request a snapshot of the current
source; never write these fixed preview settings back to the draft.

Key the session by `sourceToken(draft)`. Changing source/provider or unmounting
aborts the request, clears results/error/active selection, and stops the player.
Also use a request-generation guard so a late response cannot replace newer
results even if a mock/transport ignores abort. Reload supersedes earlier
loads. Trim changes, playlist count/order changes, and progress polling do not
refetch the list. Retain response order and canonical sourceKey. Empty results,
warnings, network errors, and retry each have explicit states.

On a current successful response, forward canonical resolution through the
existing `state.setResolvedSource({token, key})` path. Do not update displayName,
source URLs, or persisted config merely because the user listened. If overrides
are source-mismatched, keep their existing explicit-clear behavior; never
rebind or discard them automatically.

**Verify:** focused tests and typecheck; assert correct fixed metadata request,
unchanged draft/count/order, stale-response rejection, cancellation, retry,
empty results, both providers, and no cut-edit/polling-triggered fetch.

### 3. Build the single-episode player

Create `PodcastEpisodePlayer.tsx`, taking episode identity/URL, the cuts being
tested, read-only/locked state, and cut-capture callbacks. Use one native audio
element with controls and no src until an explicit Listen action. Do not set
crossOrigin or route the public media URL through TeddyCloud API credentials.
Accept only credential-free HTTP(S) URLs; present a playback error for invalid
URLs. Do not rewrite signed URL query strings.

After activation use metadata loading and native duration. Ignore feed duration
for end-capture calculations; distinguish it as estimated list metadata.
Provide the product controls above, loading/seeking/error status, and a visible
original-source clock. Seek-dependent actions require a known duration and a
seekable target; report unsupported seeking without claiming success. Handle
play() rejections and native errors. Validate every cut-capture action using
step 1; retain the previous draft on failure.

Seek and start playback only on an explicit play/audition gesture. If a browser
needs a second gesture after loading/seeking, leave an ordinary Play control
available. Boundary and retained audition use an explicit stop position; pause
on timeupdate at that position, allow normal playback after the audition ends,
and clear the stop position on manual seeking or an ordinary Play action.
If tested cuts change mid-audition, pause and require a fresh audition.

Replacing episode/source/URL, reloading metadata, closing the modal, or
unmounting pauses playback, removes src, calls load(), and clears listeners and
pending stop positions. Ignore late events from the previous audio session.
Pausing alone must not restart playback. While mounted, coordinate audio play
events so playing this preview pauses other audio elements, and playing another
audio element pauses this one. Do not discard another player's source or state.

**Verify:** focused tests and typecheck. Mock HTMLMediaElement methods/properties
and dispatch media events; test lifecycle and calculations, not real decoding.
Assert one audio element, no eager src/download, no autoplay, seek/stop behavior,
duration precedence, failed playback/seeking, capture rejection, new-URL reset,
unmount cleanup, and mutual exclusion with another audio element.

### 4. Integrate listening and draft cut editing

Create `PodcastRecentEpisodes.tsx` for the list, target selector, errors, and
single player. Integrate it into `PodcastSubscriptionEditor.tsx` for both source
kinds. Gate listening on supported podcasts and a valid selected source. For
backends without trimSupported, allow listening but hide capture/audition-cut
editing controls. Disable all draft-changing controls while the existing
`locked` condition or source-mismatch protection applies.

Podcast-default mode passes draft defaults to the player and capture callbacks
use functional setDraft to update only the relevant side. Episode mode passes
effectiveTrim(draft, episode) and capture callbacks replace both values through
the existing override semantics, using the current other-side value. Resolve
against the accepted recent-list sourceKey; never use a stale selected-playlist
preview from another source. Respect the 50-entry cap while allowing edits to an
existing entry. Explicit zero must remain an override. Validate callback source
token again at mutation time to reject obsolete player events.

Provide explicit override removal and identify when default-mode audition
differs from this episode's persisted override. Recent episodes outside the
prepared playlist may receive an override, but they do not enter that playlist.
Preserve inactive overrides. Keep listening loading/error state separate from
the existing selected-episode preview and Save validation.

Add accessible translated labels, error/help messages, publication dates using
the app's locale, and compact responsive layout. Document the workflow and
browser-format/seek limitations in backend `docs/podcasts.md`.

**Verify:** focused tests, typecheck, translation and format checks. Add rendered
tests showing that count one / oldest-first can still compare five newest;
default capture survives episode switching; explicit-zero episode capture and
removal work; source mismatch/cap/locked/legacy conditions are honored; Save
contains only intentional cut changes; and Cancel/reopen restores stored cuts
and unloads media. Existing selected-preview and source-editor tests must pass.

### 5. Complete verification and real browser acceptance

Run the complete test, typecheck, formatting, translation, production build,
and diff-hygiene gates once against the final sources. Review changed paths in
both repositories against Scope. No backend code/build changes are expected.

Use an isolated existing sandbox and matching newly built frontend for browser
acceptance; do not alter a household instance. With generated short audio in a
fixture RSS feed, place distinctive intro/main/outro sounds and compare at
least three episodes of different duration. Also try a public RSS show and an
ARD show through the production metadata path. Test the current available
Chromium and Safari on this macOS environment; record versions and which media
formats actually played rather than assuming universal codec support.

Acceptance checks: media starts only on Play; intro/outro context and retained
audio are audible at the intended positions; defaults can be compared across
episodes; override and zero behavior match the labels; Save/reopen retains cuts;
Cancel discards them; closing/switching stops audio; no concurrent footer audio;
keyboard and narrow-screen controls work; non-seekable/unsupported media show
an actionable error while numeric editing remains available. Check the HTTPS
page / HTTP enclosure case. Browser decoding cannot be proved with jsdom.

Record each check with pass/fail and evidence. If no browser is available, mark
browser acceptance pending and leave this plan IN PROGRESS; do not claim DONE.
Physical Toniebox playback acceptance inherited from earlier plans is not
repeated merely for this frontend listening feature.

**Verify:** all commands in the table exit 0; acceptance record identifies each
passed scenario and any remaining limitation. `git status --short` and diffs in
both application repositories show only allowed feature/documentation paths.

## Done criteria

- [x] The latest five eligible episodes can be requested independently of the
  prepared count/order for both provider kinds; tests prove no settings mutation.
- [x] One lazily activated player supports context listening and capturing
  whole-second intro/outro cuts with correct end-offset math.
- [x] Tests cover cancellation/stale results, cleanup, failed media/seeking,
  unknown duration, defaults/overrides/zero/cap/source binding and Save/Cancel.
- [x] Complete frontend tests, typecheck, format, translations, build, and
  git diff --check all exit 0 without weakening existing assertions.
- [ ] Browser acceptance is recorded; unsupported formats have visible fallback
  behavior and supported RSS/ARD samples actually play and seek.
- [x] Only scoped paths changed; index status reflects remaining work honestly.

## STOP conditions and maintenance

Stop and report if required ARD/RSS previews no longer include usable public
media URLs or latest-N selection semantics; if a core supported browser cannot
play/seek representative provider audio; if implementation requires an
authenticated media proxy, backend download/transcode/cache, persistence change,
or global player refactor; or if shared-file drift cannot be reconciled with
003. Report the failed example and propose a separate scoped follow-up rather
than growing this feature silently. A blocked browser check does not undo the
completed automated work.

Review especially source identity guards, end-offset conversion, draft-only
updates, and media teardown. Keep source URL refresh independent of cut edits;
rotating ARD URLs require fresh playback sessions. Future waveform analysis or
server-assisted previews must address CORS, source validation, cancellation,
resource limits and format conversion in their own plan. Do not infer automatic
cut recommendations from merely having a recent-episode list.
