"""Run only under tests/py/with_server.sh with PODCAST_SUPPORT=0."""
import json
import os
import urllib.request
import urllib.error
base = os.environ['TEDDYCLOUD_BASE_URL']
with urllib.request.urlopen(base + '/api/podcasts/capabilities', timeout=5) as response:
    assert not json.load(response)['supported']
try:
    urllib.request.urlopen(urllib.request.Request(base + '/api/podcasts/preview', data=b'{}', headers={'Content-Type': 'application/json'}), timeout=5)
except urllib.error.HTTPError as error:
    assert error.code == 503
else:
    raise AssertionError('Disabled preparation endpoint was available')
print('Feature-disabled capabilities/API verified')
