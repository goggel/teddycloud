# Podcast API

All endpoints use the existing web API access controls and web port, not the box
API port. Bodies/responses are JSON. POST bodies must have Content-Length, fit in
65,536 bytes (64 KiB), and be a single object; chunked request bodies are not supported. Overlay
is the existing unique overlay identifier, never an array index. Unknown overlays
are rejected instead of falling back to the default assignment.

| Method | Path | Input / result |
| --- | --- | --- |
| GET | `/api/podcasts/capabilities` | `supported`, episode/interval/body/duration limits |
| GET | `/api/podcasts/search?q=...&country=DE&provider=apple` | `results` tagged by `sourceType`; provider is `apple` (default) or `ard_audiothek` |
| POST | `/api/podcasts/feed-info` | `{feedUrl}` → `{videoOnly}`; bounded RSS metadata inspection, no media download or persistence |
| POST | `/api/podcasts/preview` | `{sourceType,feedUrl/ardShowUrl,episodeCount,order,...cuts}` → canonical `sourceKey`, title, ordered episodes, warnings |
| GET | `/api/podcasts/subscriptions?ruid=...&overlay=...` | `subscriptions: [...]`, progress and prepared episodes |
| POST | `/api/podcasts/subscriptions/save?overlay=...` | config below → subscription and job ID; 201 create/200 update |
| POST | `/api/podcasts/subscriptions/refresh?overlay=...` | `{id}` → 202 job ID; deduplicated |
| POST | `/api/podcasts/subscriptions/detach?overlay=...` | `{id,expectedRevision}` → `{detached:true}` |

```json
{
  "ruid": "0123456789abcdef",
  "sourceType": "rss",
  "feedUrl": "https://example.org/podcast.rss",
  "trimStartSeconds": 0,
  "trimEndSeconds": 0,
  "episodeTrimSourceKey": "",
  "episodeTrims": [],
  "episodeCount": 5,
  "order": "newest_first",
  "intervalHours": 6,
  "refreshOnPlacement": true,
  "enabled": true
}
```

Edits additionally carry `id` and the revision originally loaded as
`expectedRevision`. Conflicts return 409; polling current progress must not
replace the editing revision. Save accepts configuration and queues preparation;
it does not imply audio is ready. Config changes supersede older jobs. Playback
orders are `newest_first` and `oldest_first`; both choose the latest N first.
`refreshOnPlacement` remains in the schema for older clients, but missing or
explicit `false` values normalize to `true`. Wrong types return 400. Enabled
subscriptions check on recognized placement even when `intervalHours` is zero;
the 900-second throttle, queue limit and job deduplication still apply. This
starts a background check and does not guarantee fresh audio for that placement.

For ARD use `sourceType:"ard_audiothek"`, `ardShowUrl` and an empty `feedUrl`.
The server resolves numeric/URN show links to `sourceKey:"ard:<coreId>"` and
`ardShowId`. RSS keys use `rss:<normalized configured feed URL>`; redirects do
not change the configured identity. Apple results contain numeric `providerId`
and `feedUrl`; ARD results contain string `providerId`, `ardShowUrl`, `ardShowId`
and `sourceKey`.
`displayName` and `displayNameSourceKey` are optional bound selection metadata.
Names are at most 512 UTF-8 bytes without controls, and never affect source
identity, ownership or the TAF fingerprint. A selected name is returned in the
save response before preparation; successful feed resolution may refine it for
the current revision/source, including when a later download fails. Missing
titles or failed lookups preserve the existing bound name. On a source change
an old name is cleared. `title` remains the last successfully prepared title.
Old clients may omit the new fields. Capabilities advertise
`displayNameSupported` and `placementRefreshAlways`.

Cuts are integer seconds, 0–21,600. Each `episodeTrims` entry contains a unique,
nonempty `identity` (up to 512 UTF-8 bytes, no controls) and both
`trimStartSeconds` and `trimEndSeconds`. At most 50 entries are retained,
including inactive episodes. Overrides replace both defaults; zero is explicit.
Nonempty overrides require `episodeTrimSourceKey` equal to the resolved source
key. A different source returns 409 with `error.code:"trim_source_mismatch"`;
send `episodeTrims:[]` to explicitly clear. Omitted extension fields on updates
preserve stored values; omitting `sourceType` cannot convert an ARD subscription
to RSS. New legacy requests default to RSS and zero cuts.

Preview episodes contain effective cuts, optional upstream `durationSeconds`
and `estimatedTrimmedDurationSeconds`. Prepared episodes/manifests contain
applied cuts and measured `trimmedDurationSeconds`. The manifest identifies
encoder profile `taf-opus-48k-stereo-trim-v2`. The selected identities, URLs,
effective cuts and canonical source participate in generation fingerprints;
inactive overrides alone do not regenerate audio. Failed/full-episode cuts
leave previous prepared metadata intact.

Capabilities include `sourceTypes`, `trimSupported`, `maxTrimSeconds`,
`maxEpisodeTrims`, `maxRequestBytes` and `feedInspectionSupported`. Feed inspection
shares the preview HTTP/XML safety limits and has a five-second transfer timeout.
`videoOnly:false` also covers unclassifiable XML; only confirmed video-only RSS
feeds are hidden. Transfer failures return 502 and leave the result visible.
Older backends omit extension flags;
clients must hide unsupported controls and omit their fields from requests.

One subscription owns an effective content directory/rUID pair. Shared overlays
must also share the library directory. On successful preparation the managed
source is `lib://podcasts/<id>/<audio-id>.taf`, with live off, no-cloud on and
cloud override off. Originals are restored on detach only if management still
owns the source. A manual change pauses the subscription.

`ownsAssignment` in a rUID-scoped subscription response reports whether the
subscription still controls the actual content source. A queued or running
first generation can own its original assignment before its first TAF exists.
Paused subscriptions with a published TAF may still own that assignment;
inactive subscriptions without a queued/running job do not claim the original.
Older servers may omit this field; clients can infer ownership only from an
exact generated podcast source path, and must avoid ambiguous direct switches.

States: idle, queued, checking, downloading, encoding, ready, error, paused.
`lastSuccessAt` means the last successful check; `episodes` and `audioId` describe
the currently prepared audio. Errors preserve previous completed audio. Times
are Unix seconds. Internal storage roots/restoration metadata/pins are omitted
from responses.

Errors use `{"error":{"code":"...","message":"..."}}`: 400 invalid input,
404 unknown resource/overlay, 409 revision/ownership conflict, 413 body limit,
429 queue/search/subscription capacity, 502 failed upstream feed/search,
503 disabled/unavailable service or failed persistence. Capabilities return
`supported:false` in disabled builds or when the registry needs repair. An older
backend may return 404; the frontend retains normal audio editing.
