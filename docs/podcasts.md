# Prepared podcast Tonies

In **Tonies → Edit Tonie**, choose **Podcast** in the audio selector. Search by
name or paste a public RSS feed URL. Choose the number of recent episodes, their
playback order, and the check interval. Preview shows the requested selection;
**Save** starts preparation. Each episode becomes a chapter in one Tonie audio
file. The preparation continues when the browser closes.

The editor distinguishes the requested selection from the currently prepared
episodes. A failed refresh leaves the previous complete audio assigned. **Refresh
now** checks immediately. **Remove subscription** restores the previous source
and its live/no-cloud flags when the subscription still owns the assignment.
Choosing another source pauses automatic management. Prepared files are kept
when removing a subscription.

The default is five episodes, newest first, checked every six hours. An interval
of zero disables periodic checks. Optional placement refresh checks in the
background, at most once every 15 minutes. The box may play the previous episodes
on that placement; newly prepared content becomes available at its next suitable
online freshness/content request. Use the box's normal sync gesture when needed.
Offline playback requires the box to have downloaded the entire prepared file.
Normal playback resume is retained; listening history is not tracked per episode.

## Deployment

This feature is included in the TeddyCloud server and existing web UI. Deploy one
image with the usual config, content and library volumes. No plugin, cron, script,
extra port, browser session or companion container is required. FFmpeg, libcurl
and libxml2 are image dependencies. Node/Python are development/test tools only.

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
repair. Existing prepared audio can still be served from the library.

Each subscription owns `library/podcasts/<id>/`. Completed TAF generations and
manifests are immutable. Selected episode downloads are cached by identity and
URL; changed selections download only missing episodes. Unselected cached inputs
are discarded. Interrupted temporary files are removed at startup. Superseded
TAFs are retained for at least 24 hours and while an unexpired download pin
references them, then pruned during preparation. The current published TAF is
always retained. Detached directories remain for explicit manual cleanup.

Supported: public RSS 2.0 feeds with audio enclosures; MP3, M4A/MP4 audio, Ogg/Opus,
FLAC, WAV and AAC. No Atom, private/paid feeds, credentials, Spotify or DRM.
Search uses Apple's podcast directory, with manual RSS entry available when the
provider is unavailable. Search is cached for ten minutes and limited to twenty
upstream requests per minute. English and German labels are supplied; other
locales currently use English fallback text for the new controls.

Limits: 1–50 episodes; 0–168 whole hours between checks; 5 MiB decompressed feed;
1,000 RSS items; 200 MiB per episode; 1 GiB selected raw audio; six hours decoded
audio; 240 MiB per completed TAF; 128 subscriptions; 32 queued jobs; one encoder
worker. Retained TAF generations require additional library capacity. Exceeding
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

See [validation results](podcast-validation.md) for verified commits and local
images, [download protocol](podcast-download-protocol.md) for the device-specific
recovery gate and [API](api/podcasts.md) for integration details.
