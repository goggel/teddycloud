# Podcast API

All endpoints use the existing web API access controls and web port, not the box
API port. Bodies/responses are JSON. POST bodies must have Content-Length, fit in
16 KiB, and be a single object; chunked request bodies are not supported. Overlay
is the existing unique overlay identifier, never an array index. Unknown overlays
are rejected instead of falling back to the default assignment.

| Method | Path | Input / result |
| --- | --- | --- |
| GET | `/api/podcasts/capabilities` | `supported`, episode/interval/body/duration limits |
| GET | `/api/podcasts/search?q=...&country=DE` | `results: [{providerId,title,author,feedUrl}]` |
| POST | `/api/podcasts/preview` | `{feedUrl,episodeCount,order}` → title, ordered episodes, warnings |
| GET | `/api/podcasts/subscriptions?ruid=...&overlay=...` | `subscriptions: [...]`, progress and prepared episodes |
| POST | `/api/podcasts/subscriptions/save?overlay=...` | config below → subscription and job ID; 201 create/200 update |
| POST | `/api/podcasts/subscriptions/refresh?overlay=...` | `{id}` → 202 job ID; deduplicated |
| POST | `/api/podcasts/subscriptions/detach?overlay=...` | `{id,expectedRevision}` → `{detached:true}` |

```json
{
  "ruid": "0123456789abcdef",
  "feedUrl": "https://example.org/podcast.rss",
  "episodeCount": 5,
  "order": "newest_first",
  "intervalHours": 6,
  "refreshOnPlacement": false,
  "enabled": true
}
```

Edits additionally carry `id` and the revision originally loaded as
`expectedRevision`. Conflicts return 409; polling current progress must not
replace the editing revision. Save accepts configuration and queues preparation;
it does not imply audio is ready. Config changes supersede older jobs. Playback
orders are `newest_first` and `oldest_first`; both choose the latest N first.

One subscription owns an effective content directory/rUID pair. Shared overlays
must also share the library directory. On successful preparation the managed
source is `lib://podcasts/<id>/<audio-id>.taf`, with live off, no-cloud on and
cloud override off. Originals are restored on detach only if management still
owns the source. A manual change pauses the subscription.

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
