# Prepared podcast Tonies

In **Tonies → Edit Tonie**, choose among four source forms: **Audio files**,
**Radio**, **Podcast**, and **ARD Audiothek / Sounds**. Audio files retain the
library/path picker and previous/next-file actions. Radio has its own station
search, URL and selected station name. Podcast searches Apple's RSS directory;
ARD searches audio shows, not video. Paste a public RSS feed URL or supported
ARD show link when needed. Choose the number of recent episodes, their
playback order, and the check interval. Preview shows the requested selection;
**Save** starts preparation. Each episode becomes a chapter in one Tonie audio
file. The preparation continues when the browser closes.

Each source has an independent saved profile, including separate RSS and ARD
cuts. Switching forms only changes a local draft; **Save** stores changed
profiles and activates the selected source. **Cancel** discards draft changes.
Profiles persist in the content JSON across browsers and server restarts, but
only one source is active and at most one podcast subscription exists for a
Tonie. The active assignment and subscription ownership determine what plays;
saved inactive profiles do not start jobs. Legacy unknown HTTP/TAP paths remain
editable in the audio path form. A direct audio/radio assignment uses the
existing source setter, which accepts at most 255 UTF-8 bytes.

Selecting a search result shows its name immediately. The selected name remains
visible after saving and reopening; its URL is available in the labeled detail
field and can be edited. A manual URL change clears the old source-bound name.
The status area separately identifies the title of the last successfully
prepared selection, which can differ while a new feed is preparing. A feed
lookup can update the selected name even when later audio preparation fails.

The editor distinguishes the requested selection from the currently prepared
episodes. A failed refresh leaves the previous complete audio assigned. **Refresh
now** checks immediately. **Remove subscription** restores the previous source
and its live/no-cloud flags when the subscription still owns the assignment.
Choosing another source pauses automatic management. Prepared files are kept
when removing a subscription.

The default is five episodes, newest first, checked every six hours. An interval
of zero disables periodic checks. An enabled RSS or ARD subscription always
checks in the background on recognized placement, at most once every 15 minutes;
repeated placements and duplicate jobs are throttled. The box may play the previous episodes
on that placement; newly prepared content becomes available at its next suitable
online freshness/content request. Use the box's normal sync gesture when needed.
Offline playback requires the box to have downloaded the entire prepared file.
Normal playback resume is retained; listening history is not tracked per episode.

## Intro and outro cuts

Set default start/end cuts in seconds (0–21,600 per side), including fractions
such as 21.5. The editor and player capture use up to three decimal places; the
encoder rounds cuts to the nearest 48 kHz PCM sample. Preview an
episode selection to override both cuts for individual episodes. Zero is an
explicit override; removing an override restores the current defaults. Stored
overrides remain available even when that episode leaves the latest selection.
Up to 50 identities can be stored; remove inactive entries to free space.

Overrides belong to the podcast source. A different source requires explicitly
clearing the old overrides before saving. Numeric and modern URN ARD links for
the same show share a canonical identity after preview resolves them. Edit cuts
locally, then Save; cut edits do not fetch another preview. Cancel discards edits.
The preview's duration is an estimate; prepared episodes display measured audio
duration and the cuts actually applied to the last successful generation.
Durations and cut summaries use readable hours, minutes and seconds (for example,
`1h 30min 15sec` or `33min 10sec`), with fractional seconds preserved for precise cuts.
The currently prepared episodes are collapsed until their heading is clicked.

**Listen to recent episodes** loads up to five newest eligible audio episodes,
independently of the requested Tonie episode count and playback order. Select
**Listen** on an episode to load its player directly below that episode. The
browser player shows readable positions in the original recording with up to
millisecond precision, can skip five seconds,
start a short preview exactly at the configured intro cut, hear a short passage
around the outro cut, and play the retained interval. Choose whether captures
edit the podcast defaults or this episode's override. An episode override is ignored while
auditioning defaults. **Use podcast defaults for this episode** removes its
override. Listening, seeking and switching episodes do not prepare or save audio.

The browser reads the public episode URL directly. Browser codec support,
upstream availability, seeking support and HTTPS pages containing HTTP audio can
prevent playback or accurate seeking; use the numeric cut fields when that
happens. Feed durations are estimates, while the player uses the loaded media's
duration. Dynamic ads can shift boundaries, and prepared audio is measured again
by the server. The player is a listening aid, not automatic cut detection.

The decoder trims 48 kHz stereo samples before TAF encoding. End cuts require
two decode passes over the same cached input: one to count frames and one to
encode the retained interval. This costs additional preparation time while using
constant memory. A cut that removes an entire episode fails the whole refresh;
the previous complete Tonie audio remains assigned. Episodes remain separate
chapters, so normal chapter skipping can be tested on the box.

## Deployment

This feature is included in the TeddyCloud server and existing web UI. Deploy one
image with the usual config, content and library volumes. No plugin, cron, script,
extra port, browser session or companion container is required. FFmpeg, libcurl
and libxml2 are image dependencies. Node/Python are development/test tools only.
The source-profile endpoints use the same web access path, container and volumes.
Profile saves, subscription changes, source assignment and model assignment are
separate API operations. If a later operation fails, the editor stays open and
shows which profiles were confirmed and which source the server reports active;
retry after reviewing that state. A successful detach followed by a failed
direct assignment leaves the restored original as the actual source.

Both repositories need matching feature commits. Follow the repository's normal
two-PR contribution workflow; a maintainer bundles the frontend for releases.
For a local development image, `dev-sandbox/Dockerfile` builds the frontend and
backend together. For production Dockerfiles, stage the frontend bundle in an
isolated integration checkout and commit its gitlink/bundle locally before
building, because `make test_git_version` requires clean Git state. Do not include
that integration commit in the backend feature PR.

```sh
podman build -f DockerfilePodcastTests -t localhost/teddycloud:podcast-tests .
podman run --rm localhost/teddycloud:podcast-tests make test_podcast
# Clean integration checkout containing the newly bundled UI:
podman build -f DockerfileUbuntu -t localhost/teddycloud:podcasts .
```

Use Docker instead of Podman where installed. Linux images enable podcasts by
default. `make build PODCAST_SUPPORT=0` builds feature-disabled stubs without
curl/XML linkage; Windows podcast preparation is outside this version's scope.
`PODCAST_TESTING=1` is exclusively for the separate test executable. Production
builds ignore all `TC_PODCAST_*` fixture/clock/failure environment variables.

## Storage, limits and supported feeds

Subscriptions and download-session pins persist in `config/podcasts.json`. The
previous committed state is backed up in `podcasts.json.bak`. A corrupt primary
is preserved as `podcasts.json.corrupt-*` when the backup can recover it. If both
are unusable, automatic management is disabled and the files are preserved for
repair. Registry version 1 migrates to version 2 after pending publications are
recovered. The original file is retained as `podcasts.json.v1.bak`; existing IDs,
revisions, source assignments and download pins survive. Migration does not
re-encode synchronously; the next refresh uses the new encoder profile. Before
downgrading, stop teddyCloud and back up config/content/library together; restore
a consistent pre-upgrade snapshot. Restoring only the old registry can lose later
configuration and desynchronize assignments. Unknown registry versions disable
management without overwriting the newer file.

Existing prepared audio can still be served from the library.

Each subscription owns `library/podcasts/<id>/`. Completed TAF generations and
manifests are immutable. Selected episode downloads are cached by identity and
URL; changed selections download only missing episodes. Unselected cached inputs
are discarded. Interrupted temporary files are removed at startup. Superseded
TAFs are retained for at least 24 hours and while an unexpired download pin
references them, then pruned during preparation. The current published TAF is
always retained. Detached directories remain for explicit manual cleanup.

Supported: public RSS 2.0 feeds with audio enclosures; MP3, M4A/MP4 audio, Ogg/Opus,
FLAC, WAV and AAC. No Atom, private/paid feeds, credentials, Spotify or DRM.
Video podcasts are not supported. Search results appear immediately; the editor
checks RSS enclosure types asynchronously and hides confirmed video-only feeds
from Apple's directory. If such a feed is already selected, the editor clears
the draft feed URL and preview and shows a notice; saved subscriptions are not
changed until explicitly saved. Mixed audio/video feeds remain available.
Checks download metadata only, run at most three search probes at once, have a
five-second server timeout and cache successful classifications in the editor
for ten minutes. Unavailable or unclassifiable feeds remain visible so a temporary
lookup failure does not hide an audio podcast; preview still validates the feed.
For example, WDR's "Die Sendung mit der Maus" feed contains video enclosures;
use the separate "Die Maus zum Hören" audio podcast instead.
Search uses Apple's podcast directory or ARD's public GraphQL API, with manual
links available when search is unavailable. ARD supports public, published,
downloadable episodes; livestreams, future/invalid dates and non-downloadable
items are excluded. Show links must use HTTPS on `ardaudiothek.de` or
`ardsounds.de` (with optional `www`), in `/sendung/<slug>/<numeric-id-or-show-URN>/`
form. Sharing query parameters are ignored. ARD asset IDs bind overrides across
rotating audio URLs. Search is cached for ten minutes and limited to twenty
upstream requests per minute independently for each provider. English and German
labels are supplied; other locales currently use English fallback text for the new controls.

Limits: 1–50 episodes; 0–168 whole hours between checks; 5 MiB decompressed feed;
1,000 RSS items or 20 ARD pages of 50 candidates; 200 MiB per episode; 1 GiB
selected raw audio; six hours decoded audio; 240 MiB per completed TAF; 128 subscriptions; 32 queued jobs; one encoder
worker. ARD lookups share a 30-second/5 MiB budget across resolution and pages;
encoding shares a one-hour deadline. JSON request bodies are capped at 65,536
bytes. Retained TAF generations require additional library capacity. Exceeding
limits errors instead of silently dropping selected episodes. HTTP destinations
must be public unicast; local/private/link-local addresses and URL credentials
are rejected. The podcast HTTP helper intentionally ignores proxy environment
variables. TLS verification remains enabled. FFmpeg receives only owned local
files and cannot use network protocols or playlist demuxers.

## Validation and device coverage

Implementation baseline: backend `8b39ac9c9cbc312ea1fa948f66b0dc014fd096c1`,
frontend `636a8d65043f6c6e322f76bcb11e958725159e96` (`develop`).
Automated tests use disposable storage and generated WAV inputs. They validate
chaptered TAF hashes, updates, failure preservation, source ownership, scheduling,
placement events, version monotonicity, certificate-separated range pins,
restart and publication crash recovery. Simulated HTTP clients cannot establish
physical playback behavior.

Target hardware: **Toniebox 1**, test Tonie available. Physical device acceptance
is pending; do not claim Toniebox 2 support. Record model, firmware, date and result
for each row before calling the feature device-validated:

| Check | Physical Toniebox 1 result |
| --- | --- |
| First prepared download and playback | Pending |
| Next/previous chapter gestures and chosen order | Pending |
| Normal resume and complete-file offline playback | Pending |
| Second revision and freshness sync | Pending |
| Periodic update with browser closed | Pending |
| Slow placement refresh keeps previous audio playable | Pending |
| Interrupted download resumes its original generation | Pending |
| Orphaned-range 416 recovers with a full download | Pending |
| Container restart/reconnect during playback/download | Pending |
| Prepared ARD episodes and trim-specific chapter transitions | Pending |

See [source-profile API](api/source-profiles.md), [validation results](podcast-validation.md) for verified commits and local
images, [download protocol](podcast-download-protocol.md) for the device-specific
recovery gate and [API](api/podcasts.md) for integration details.
