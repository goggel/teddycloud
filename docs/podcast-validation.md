# Prepared podcast implementation validation

Validated locally on 2026-10-09 using Linux/arm64 containers in Podman on macOS.
The original RSS validation is retained below. ARD and trimming extension
validation and new image provenance are recorded in the extension section at
the end; the original image IDs do not include those extensions.

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

## ARD and trimming extension — 2026-10-09

Implementation commits: backend `d374b7ba`, bounds/regression follow-up
`f49e0a84`; frontend `8a30a31e43d1d71a4041c4c4cada676fc58fb918`.
The private integration checkout merges these feature commits and bundles the
matching UI; its release source is `8bcf81c621bad6284fc076bb67ad39ae5f6eade7`.
Feature commits retain the original frontend gitlink and generated assets.

Verified extension gates:

- Frontend: 12 files / 119 tests, TypeScript/Vite build, formatting and six-locale
  key checks. Six rendered editor tests use real Ant Design controls: provider
  search/selection/cancellation, defaults and zero overrides, removal/inheritance,
  inactive source-bound overrides, Save/Cancel, polling/revision preservation,
  requested/applied cuts, read-only controls and legacy capability fallback.
- Optimized C parser/URL/date/trim-window and production transport policy tests.
- Thirteen native service scenarios: both ARD link forms and canonical identity,
  asset identity across rotating URLs, pagination/exclusions/fallback identities,
  malformed/oversized/aggregate response limits, repeated pages and provider
  cache/rate isolation; 50 overrides and exact 65,536-byte requests, malformed
  cuts/identities and source mismatch; migration, pins and publication recovery.
- Generated 12-second intro/main/outro tones prove sample removal in decoded
  Opus output, with measured durations and distinct chapters. Start/end/default
  and explicit-zero overrides work. Trim-only updates change the audio ID;
  inactive overrides alone do not. Full-episode cuts preserve previous audio,
  hash and applied metadata. Test-only FFmpeg wrappers cover 1–3-byte PCM chunks,
  large stderr, incomplete frames, second-pass failure, inconsistent counts,
  cancellation and child reaping; no wrapper is installed in runtime images.
- Existing 9 C utility tests and 21 content/box-simulator/TAF regressions pass
  after the socket line-boundary fix needed for 64 KiB bodies. Both Linux and
  Windows copies of that reader were corrected; Windows builds were not run.

The public ARD GraphQL query shape was checked directly on 2026-10-09; automated
regressions use minimal synthetic fixtures. No credentials or live episode audio
are checked in. Time budgets are bounded by the implementation; simulated
fixture responses do not model all live-network timeout behavior.

All production variants pass root and PUID/PGID=1000 startup, matching served
JS/translation checks, absence of Node/Python/fixture hooks, runtime curl/XML
linkage, persisted trim settings/overrides and restart recovery. The exact
integration Git/version release check passes. The compose file still defines
only the existing `teddycloud` service and its standard volumes.

| Extension image | Image ID | Source |
| --- | --- | --- |
| `localhost/teddycloud:podcast-extensions` (Ubuntu) | `21a730ba7571` | integration `8bcf81c6` |
| `localhost/teddycloud:podcast-extensions-debian` | `325ed1c484db` | integration `8bcf81c6` |
| `localhost/teddycloud:podcast-extensions-alpine` | `98d218b9b243` | integration `8bcf81c6` |
| `localhost/teddycloud:podcast-extensions-tests-final` | `c95e01e93a75` | final implementation and test sources; test-only |

The final test image layers the bounded-query follow-up and final tests onto the
fully checked extension test image, reusing generated test certificates. Its
frontend suite passes 119 tests; its optimized unit tests and 13 native scenarios
pass. All 13 native service scenarios also pass as UID/GID 1000, including actual
ARD fixture preparation and PCM trimming. The underlying extension test image also builds the feature-disabled
executable, verifies absent curl/XML linkage, and checks disabled capabilities/API.
Production images use `NO_SANITIZERS=1` and Linux/arm64.

Logs are `/private/tmp/teddycloud-extension-*.log`: `web-tests`, `web-build`,
`web-format`, `web-translations`, `limits-tests-2`, `regressions`,
`release-final`, `*-final`, `runtime-ubuntu`, `runtime-debian`, `runtime-alpine`
`final-test-image` and `nonroot`. To check another production image without repeating the
same service suite, pass `--skip-service-tests` to the container test runner.

Reproduce the test image from the feature checkout with its matching frontend,
and production images from the clean private integration checkout:

```sh
podman build -f DockerfilePodcastTests -t localhost/teddycloud:podcast-extensions-tests-final .
podman run --rm localhost/teddycloud:podcast-extensions-tests-final make test_podcast
# In the clean integration checkout:
make test_git_version
podman build --build-arg NO_SANITIZERS=1 -f DockerfileUbuntu -t localhost/teddycloud:podcast-extensions .
python3 tests/run_podcast_container_tests.py \
  --image localhost/teddycloud:podcast-extensions \
  --test-image localhost/teddycloud:podcast-extensions-tests-final --engine podman
```

Physical Toniebox 1 playback, chapter gestures, offline/resume, sync timing and
interrupted-download/orphaned-range recovery remain pending. No Toniebox 2,
Linux/amd64 or sanitizer coverage is claimed. Visual browser review also remains
pending because no browser is connected; rendered component tests passed.
