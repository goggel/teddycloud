# Prepared podcast download protocol

**Physical-device gate: unresolved.** The user has a Toniebox 1 and test Tonie.
No firmware/model-specific captures or playback results have been collected.
Server behavior is implemented and tested with simulated clients; 416 recovery
must be confirmed on the intended box before household deployment.

The request URI carries only rUID, not the generation being resumed. Therefore a
newly published TAF must never receive an old nonzero byte offset. The server
pins a generation when a byte-zero content request arrives, keyed by TLS client
certificate subject, effective content directory and rUID. Certificate identity
comes from the existing authenticated connection context, independently of the
request's source IP or query parameters. Pins persist across process restarts.
A nonzero request reuses that immutable generation for up to 24 hours. A new
byte-zero request replaces the pin. A different box receives an independent pin.

An orphaned/expired nonzero Range receives HTTP **416**, a
`Content-Range: bytes */<current-length>` header and no audio bytes. A request
without stable authenticated identity can begin a full download but cannot
resume it. Expired pins remain tombstones, so an old offset is never silently
applied to the current generation. Retention protects pinned generations and
keeps superseded files for at least 24 hours. Existing ordinary non-podcast
content follows its original delivery path.

Prepared publication writes and validates the complete TAF, fsyncs and atomically
renames it, writes a generation manifest, persists publication intent, updates
the tag source under the same working lock as manual source edits, and commits
the registry result. Startup compares the actual source with any durable intent.
Incomplete files are never assigned. A stale job cannot publish after a revision,
pause or detach. Manual source changes pause subscription management.

Automated tests cover zero-offset start, certificate-separated pins, nonzero
resume after another generation publishes, 416 for an unpinned second box,
restart with a persisted pin, and forced process death before/after source
publication. They verify byte identity and valid completed TAF hashes/chapters.

## Required physical test

1. Record the box generation, hardware model and firmware.
2. Use a spare Tonie and short, identifiable episodes; preserve its previous
   assignment. Capture content URI/Range and freshness requests with auth tokens
   and certificate details redacted.
3. Verify a full byte-zero download and chapter skipping/resume.
4. Interrupt a download, publish a new revision, then reconnect. A nonzero retry
   must finish the original file; a fresh request must receive the new file.
5. Restart the container between interruption and resume; repeat.
6. Exercise first adoption of previously unmanaged content and an expired/missing
   pin. Confirm the 416 causes a full download and playable audio. If it does not,
   document the precise manual sync/full-download recovery and redesign this gate
   before calling seamless orphan recovery supported.
7. Update the device matrix in podcasts.md with observed results and redacted
   traces. Do not substitute generic HTTP tests for this gate.
