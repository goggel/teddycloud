# Prepared podcast implementation validation

Validated locally on 2026-10-09 using Linux/arm64 containers in Podman on macOS.
This records the original RSS feature. ARD providers and episode trimming remain
additional planned work; they are not included in these images.

## Source and image provenance

- Backend implementation: `1ed9e958` on `feature/prepared-podcast-tonies`, based
  on `8b39ac9c9cbc312ea1fa948f66b0dc014fd096c1`.
- Frontend: `7ee218e818a4cb520e2ef0f13f71cf9a6669136c` on the same branch name,
  based on `636a8d65043f6c6e322f76bcb11e958725159e96`.
- Integration checkout: `/private/tmp/teddycloud-podcast-integration`, branch
  `integration/prepared-podcast-image`. Its local bundle commit `3fd8301c`
  contains the frontend gitlink and generated assets. Packaging follow-ups lead
  to `11564df9`. These integration commits must stay out of the backend PR.
- The backend feature branch intentionally retains its original frontend gitlink
  and generated web bundle. The locally changed submodule pointer is expected.

All production images below contain the matching UI, FFmpeg, curl and XML runtime
libraries. Node/Python and fixture transport switches are absent. Images were
built with `NO_SANITIZERS=1`; sanitizer and Linux/amd64 runs are not claimed.

| Local image | Image ID | Integration source |
| --- | --- | --- |
| `localhost/teddycloud:podcasts` (Ubuntu) | `4a1821880606` | `11564df9` |
| `localhost/teddycloud:podcasts-debian` | `f23d42a31dcd` | `3fd8301c` |
| `localhost/teddycloud:podcasts-alpine` | `0e142d6c8a85` | `3fd8301c` |
| `localhost/teddycloud:podcast-tests-final` | `ae8133277c77` | reviewed feature sources; test-only image |

The Debian/Alpine images predate the later Ubuntu-only runtime package adjustment
and occupied-UID entrypoint fix. Their root and PUID=1000 checks passed. Their
podcast code and bundled frontend match the Ubuntu image.

## Checks and evidence

| Gate | Result |
| --- | --- |
| Frontend complete suite | 11 files, 105 tests passed |
| Frontend TypeScript/Vite build | Passed |
| Formatting and all translation key checks | Passed |
| Parser, selection, URL and actual-peer address policy | Passed, Ubuntu 24.04 and 26.04 |
| Deterministic native service tests | 6 scenarios passed, root and UID/GID 1000 |
| Feature-disabled compile/link and API | Passed; no curl/XML linkage, capabilities false and preview unavailable |
| Existing C utility tests | 9 passed |
| Existing content JSON, box simulator and TAF length tests | 21 passed |
| Clean release Git/version check | Passed |
| Ubuntu/Debian/Alpine production image builds | Passed |
| Debian/Alpine startup, bundled UI and restart persistence | Passed as root and PUID/PGID 1000 |
| Ubuntu startup, bundled UI and restart persistence | Passed as root and PUID/PGID 1000 |
| Visual browser review | Pending; no browser available through the connected UI tool |
| Physical Toniebox 1 playback/protocol gates | Pending; test Tonie confirmed available |

The service tests prepare real chaptered TAFs from generated WAV audio, check
header chapter counts and complete-file SHA-1, verify changed/unchanged updates,
and preserve previous audio after bad XML and undecodable enclosure failures.
They cover paused drafts/revisions, shared-overlay ownership, simulated placement
and throttling, periodic refresh, clock rollback, certificate-separated range
pins, persisted restart pins, crash recovery before/after source publication,
and corrupt-registry backup recovery. Fixture substitution is compiled into the
separate test transport only; production HTTP policy tests use the production transport.

Production smoke tests use fresh owned volumes and generated certificates. They
check the served JS/translation bundle, disabled test hooks, runtime libraries,
subscription storage permissions and restart persistence. The test harness uses
shared SELinux labels only on its disposable volumes and removes its containers
and volumes afterwards. No household instance, certificates or Tonie assignment
has been changed.

Reproduce from the backend checkout:

```sh
podman build -f DockerfilePodcastTests -t localhost/teddycloud:podcast-tests-final .
podman run --rm localhost/teddycloud:podcast-tests-final make test_podcast
python3 tests/run_podcast_container_tests.py \
  --image localhost/teddycloud:podcasts \
  --test-image localhost/teddycloud:podcast-tests-final --engine podman
```

Production build commands, run from the clean integration checkout after bundling
the matching frontend with `make web` and committing the integration-only assets:

```sh
make test_git_version
podman build --build-arg NO_SANITIZERS=1 -f DockerfileUbuntu -t localhost/teddycloud:podcasts .
podman build --build-arg NO_SANITIZERS=1 -f DockerfileDebian -t localhost/teddycloud:podcasts-debian .
podman build --build-arg NO_SANITIZERS=1 -f DockerfileAlpine -t localhost/teddycloud:podcasts-alpine .
```

Local logs are `/private/tmp/teddycloud-podcast-*.log`; notable logs are
`final-tests-image`, `nonroot-final`, `regressions`, `release-check`,
`container-ubuntu`, `container-debian`, `container-alpine` and `ubuntu26-unit`.
No images or branches have been pushed. See [podcasts](podcasts.md) for operation
and the outstanding device matrix, and [download protocol](podcast-download-protocol.md)
for the interrupted-download/416 recovery acceptance procedure.
