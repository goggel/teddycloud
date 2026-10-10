# Source-profile API

The four source forms save independent configuration in each Tonie's effective
content JSON. These web-only endpoints use the existing web access controls,
container and volumes. They do not activate a source or start a podcast job.
`PODCAST_SUPPORT=0` still supports audio/radio profile storage.

| Method | Path | Result |
| --- | --- | --- |
| GET | `/api/source-profiles/capabilities` | `{supported:true,version:1,maxRequestBytes:65536}` |
| GET | `/api/source-profiles?ruid=<16 hex>&overlay=<name>` | `{revision,profiles}` |
| POST | `/api/source-profiles/save?overlay=<name>` | `{ruid,kind,expectedRevision,profile}` → `{revision,profiles}` |

`kind` is `audio`, `radio`, `podcast`, or `ard`; the latter two map to RSS and
`ard_audiothek` respectively. One request replaces exactly one profile. Send
the revision returned by the previous successful save for each subsequent
profile update. Two concurrent writers using the same revision yield one
success and one 409. Do not retry a conflict silently; reload server state.
The overlay must exist and resolves to the same effective content scope as
other Content JSON operations. Overlays sharing a content directory share the
same profiles and revision; separate directories are isolated.

```json
{
  "ruid": "0123456789abcdef",
  "kind": "radio",
  "expectedRevision": 0,
  "profile": {
    "source": "https://example.org/live",
    "displayName": "Station"
  }
}
```

Profiles contain configuration only, never subscription/job IDs, prepared
episodes, pins or internal paths. Incomplete inactive podcast/ARD profiles can
be saved without network access; activation separately requires a valid source
and subscription save. Names and trim identities are byte/control bounded.
POST bodies must have Content-Length, be a single JSON object and fit within
65,536 bytes; chunked bodies and embedded NULs are rejected. Invalid input is
400, an unknown effective scope is 404, a revision or future-version conflict
is 409, oversize is 413, and a persistence failure is 503. A GET never
materializes missing profiles. Unknown `sourceProfilesVersion` and its data are
preserved by ordinary content mutations, while profile POST is refused.

The content JSON stores `sourceProfilesVersion:1`, a shared
`sourceProfilesRevision`, and `sourceProfiles`. The content JSON `_version`
remains unchanged. Old content without profiles is read as empty profiles;
the editor fills drafts from actual assignment/subscription until the user
explicitly saves. Profiles are not evidence of active playback.

Saving the Tonie can involve multiple existing APIs: profile POSTs, podcast
save or detach, direct content source assignment, and model assignment. This
sequence is not an atomic transaction. After a partial failure, reload the
actual assignment and profiles before a deliberate retry. A direct source
activation currently uses the legacy Content JSON setter and is limited to
255 UTF-8 bytes even though an inactive stored profile may hold a longer URL.
