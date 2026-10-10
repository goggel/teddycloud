#!/usr/bin/env bash
set -euo pipefail
# Existing upstream helper creates isolated certificates and storage. All podcast
# requests use a fixture map compiled only into the dedicated test binary.
fixture_dir=$(mktemp -d /tmp/tc-podcast-fixtures-XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
export TC_PODCAST_FIXTURES="$fixture_dir/map.json" TC_PODCAST_FIXTURE_DIR="$fixture_dir"
export TC_PODCAST_CLOCK="$fixture_dir/clock" TC_PODCAST_FAILURE="$fixture_dir/failure"
export TC_PODCAST_ENCODER_CONTROL="$fixture_dir/encoder-control"
export TC_PODCAST_REAL_FFMPEG="$(command -v ffmpeg)"
mkdir "$fixture_dir/tools"
ln -s "$PWD/tests/fixtures/podcasts/ffmpeg_stub.py" "$fixture_dir/tools/ffmpeg"
export PATH="$fixture_dir/tools:$PATH"
date +%s > "$TC_PODCAST_CLOCK"
echo '{}' > "$TC_PODCAST_FIXTURES"
bash tests/py/with_server.sh 0 0 0 180 -- python3 tests/test_podcast_api.py "$@"
bash tests/py/with_server.sh 0 0 0 180 -- python3 tests/test_source_profiles.py
