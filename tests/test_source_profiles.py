"""Source profile API checks against the disposable with_server.sh instance."""
import concurrent.futures
import json
import os
from pathlib import Path
import signal
import subprocess
import threading
import tempfile
import time
import unittest
import urllib.error
import urllib.request

BASE = os.environ['TEDDYCLOUD_BASE_URL']
SANDBOX = Path(os.environ['TC_SANDBOX'])


def request(path, body=None, status=200, raw=None):
    data = raw if raw is not None else None if body is None else json.dumps(body, ensure_ascii=False).encode()
    req = urllib.request.Request(BASE + path, data=data,
                                 headers={'Content-Type': 'application/json'} if data is not None else {})
    try:
        response = urllib.request.urlopen(req, timeout=15)
    except urllib.error.HTTPError as exc:
        response = exc
    payload = response.read()
    assert response.status == status, (response.status, status, payload)
    return json.loads(payload)


class SourceProfilesAPI(unittest.TestCase):
    def path(self, ruid):
        return SANDBOX / 'data/content/default' / ruid[:8].upper() / (ruid[8:].upper() + '.json')

    def save(self, ruid, kind, revision, profile, status=200):
        return request('/api/source-profiles/save',
                       dict(ruid=ruid, kind=kind, expectedRevision=revision, profile=profile), status)

    def test_capability_and_inactive_audio_radio_roundtrip(self):
        ruid = '00000000000000a1'
        capability = request('/api/source-profiles/capabilities')
        self.assertEqual(capability, dict(supported=True, version=1, maxRequestBytes=65536))
        empty = request('/api/source-profiles?ruid=' + ruid)
        self.assertEqual(empty['revision'], 0)
        self.assertEqual(empty['profiles'], {})
        self.assertFalse(self.path(ruid).exists())  # GET does not materialize profiles.
        audio = self.save(ruid, 'audio', 0, {'source': 'lib://record.taf'})
        self.assertEqual(audio['revision'], 1)
        radio = self.save(ruid, 'radio', 1,
                          {'source': 'https://example.org/live', 'displayName': 'Rádio 🎵'})
        self.assertEqual(radio['revision'], 2)
        self.assertEqual(radio['profiles']['audio']['source'], 'lib://record.taf')
        self.assertEqual(request('/api/source-profiles?ruid=' + ruid), radio)
        stored = json.loads(self.path(ruid).read_text())
        self.assertEqual(stored['sourceProfilesVersion'], 1)
        self.assertEqual(stored['sourceProfilesRevision'], 2)
        self.assertEqual(stored['sourceProfiles']['radio']['displayName'], 'Rádio 🎵')
        url = BASE + '/content/json/set/' + ruid
        urllib.request.urlopen(urllib.request.Request(url, data=b'hide=true', method='POST'), timeout=15).read()
        self.assertEqual(request('/api/source-profiles?ruid=' + ruid), radio)
        self.assertTrue(json.loads(self.path(ruid).read_text())['hide'])

    def test_get_save_cannot_mutate_a_profile(self):
        ruid = '00000000000000a6'
        body = json.dumps(dict(ruid=ruid, kind='audio', expectedRevision=0,
                               profile={'source': 'lib://must-not-write.taf'})).encode()
        req = urllib.request.Request(BASE + '/api/source-profiles/save', data=body,
                                     method='GET', headers={'Content-Type': 'application/json'})
        with self.assertRaises(urllib.error.HTTPError) as caught:
            urllib.request.urlopen(req, timeout=15)
        self.assertEqual(caught.exception.status, 405)
        self.assertEqual(json.loads(caught.exception.read())['error']['code'], 'method_not_allowed')
        self.assertEqual(request('/api/source-profiles?ruid=' + ruid)['revision'], 0)
        self.assertFalse(self.path(ruid).exists())

    def test_one_profile_cas_and_parallel_writers(self):
        ruid = '00000000000000a2'
        self.save(ruid, 'audio', 0, {'source': 'a'})
        self.save(ruid, 'radio', 0, {'source': 'b'}, status=409)
        barrier = threading.Barrier(2)

        def writer(source):
            barrier.wait()
            req = urllib.request.Request(BASE + '/api/source-profiles/save',
                data=json.dumps(dict(ruid=ruid, kind='radio', expectedRevision=1,
                                     profile={'source': source})).encode(),
                headers={'Content-Type': 'application/json'})
            try:
                response = urllib.request.urlopen(req, timeout=15)
            except urllib.error.HTTPError as exc:
                response = exc
            response.read()
            return response.status

        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            self.assertEqual(sorted(pool.map(writer, ['first', 'second'])), [200, 409])
        self.assertEqual(request('/api/source-profiles?ruid=' + ruid)['revision'], 2)
        # A normal Content JSON mutation uses the same lock, so neither update
        # can erase the other even when they arrive at the same time.
        barrier = threading.Barrier(2)

        def save_profile():
            barrier.wait()
            return self.save(ruid, 'audio', 2, {'source': 'parallel'})['revision']

        def save_content():
            barrier.wait()
            req = urllib.request.Request(BASE + '/content/json/set/' + ruid,
                                         data=b'hide=true', method='POST')
            return urllib.request.urlopen(req, timeout=15).status

        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            a = pool.submit(save_profile)
            b = pool.submit(save_content)
            self.assertEqual(a.result(), 3)
            self.assertEqual(b.result(), 200)
        stored = json.loads(self.path(ruid).read_text())
        self.assertTrue(stored['hide'])
        self.assertEqual(stored['sourceProfiles']['audio']['source'], 'parallel')

    def test_incomplete_podcast_ard_profiles_and_invalid_fields(self):
        ruid = '00000000000000a3'
        name = '🎵' * 128  # exactly 512 UTF-8 bytes
        podcast = self.save(ruid, 'podcast', 0,
                            {'sourceType': 'rss', 'feedUrl': '', 'displayName': name,
                             'displayNameSourceKey': 'rss:https://example.org/feed'})
        self.assertEqual(podcast['profiles']['podcast']['displayName'], name)
        ard = self.save(ruid, 'ard', 1,
                        {'sourceType': 'ard_audiothek', 'episodeTrims': [
                            {'identity': 'episode-1', 'trimStartSeconds': 2, 'trimEndSeconds': 0}]})
        self.assertEqual(ard['profiles']['ard']['episodeTrims'][0]['trimStartSeconds'], 2)
        for invalid in [{'subscriptionId': 'internal'}, {'displayName': name + 'x'},
                        {'displayName': 'bad\nname'}, {'episodeCount': '5'},
                        {'sourceType': 'ard_audiothek'}, {'episodeTrims': [{'identity': '',
                         'trimStartSeconds': 0, 'trimEndSeconds': 0}]}]:
            self.save(ruid, 'podcast', 2, invalid, status=400)
        self.assertEqual(request('/api/source-profiles?ruid=' + ruid)['revision'], 2)

    def test_limits_scope_and_unknown_version_survive_content_mutation(self):
        ruid = '00000000000000a4'
        request('/api/source-profiles?ruid=bad', status=400)
        request('/api/source-profiles?ruid=' + ruid + '&overlay=unknown', status=404)
        request('/api/source-profiles/save', raw=b'{' + b' ' * 65536, status=413)
        request('/api/source-profiles/save', raw=b'{}' + b' ' * (65536 - 2), status=400)
        request('/api/source-profiles/save', raw=b'{"ruid":"bad\x00id"}', status=400)
        request('/api/source-profiles/save', raw=b'{"ruid":"a\\u0000b"}', status=400)
        chunked = urllib.request.Request(BASE + '/api/source-profiles/save',
            data=iter([b'{', b'}']), headers={'Content-Type': 'application/json'})
        try:
            urllib.request.urlopen(chunked, timeout=15)
        except urllib.error.HTTPError as exc:
            self.assertEqual(exc.status, 413)
        else:
            self.fail('Chunked profile POST was accepted')
        self.save(ruid, 'audio', 0, {'source': 'lib://unchanged.taf'})
        path = self.path(ruid)
        stored = json.loads(path.read_text())
        stored['sourceProfilesVersion'] = 987
        stored['sourceProfiles']['future'] = {'opaque': [1, {'value': 'retained'}]}
        path.write_text(json.dumps(stored))
        loaded = request('/api/source-profiles?ruid=' + ruid)
        self.assertEqual(loaded['version'], 987)
        self.assertEqual(loaded['profiles']['future'], stored['sourceProfiles']['future'])
        self.save(ruid, 'audio', 1, {'source': 'replacement'}, status=409)
        url = BASE + '/content/json/set/' + ruid
        urllib.request.urlopen(urllib.request.Request(url, data=b'hide=true', method='POST'), timeout=15).read()
        after = json.loads(path.read_text())
        self.assertEqual(after['sourceProfilesVersion'], 987)
        self.assertEqual(after['sourceProfiles']['future'], stored['sourceProfiles']['future'])
        self.assertEqual(after['sourceProfiles']['audio']['source'], 'lib://unchanged.taf')

    def test_z_restart_and_shared_overlay_scope(self):
        ruid = '00000000000000a5'
        self.save(ruid, 'audio', 0, {'source': 'lib://persisted.taf'})
        os.kill(int(os.environ['TC_SERVER_PID']), signal.SIGTERM)
        time.sleep(.7)
        overlay_config = SANDBOX / 'config/config.overlay.ini'
        overlay_config.write_text('overlay.shared-one.boxName=Shared One\n'
                                  'overlay.shared-one.core.contentdir=default\n'
                                  'overlay.separate.boxName=Separate\n'
                                  'overlay.separate.core.contentdir=separate\n')
        settings = ','.join(['core.tonies_json_auto_update=false',
            'core.server.http_port=' + os.environ['TC_HTTP_PORT'],
            'core.server.https_web_port=' + os.environ['TC_HTTPS_WEB_PORT'],
            'core.server.https_api_port=' + os.environ['TC_HTTPS_API_PORT']])
        log = tempfile.TemporaryFile()
        server_bin = os.environ.get('TC_SERVER_BIN',
                                    str(Path(__file__).resolve().parent.parent / 'bin/teddycloud'))
        proc = subprocess.Popen([server_bin, '--base_path', str(SANDBOX),
                                 '--config-set', settings], stdout=log, stderr=log)
        self.addCleanup(log.close)
        self.addCleanup(lambda: proc.terminate() if proc.poll() is None else None)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            try:
                if request('/api/source-profiles/capabilities')['supported']:
                    break
            except Exception:
                time.sleep(.1)
        else:
            log.seek(0)
            self.fail('Restart did not become ready: ' + log.read().decode(errors='replace'))
        self.assertEqual(request('/api/source-profiles?ruid=' + ruid)['revision'], 1)
        shared = request('/api/source-profiles?ruid=' + ruid + '&overlay=shared-one')
        self.assertEqual(shared['profiles']['audio']['source'], 'lib://persisted.taf')
        self.save(ruid, 'radio', 0, {'source': 'different'}, status=409)
        self.save(ruid, 'radio', 1, {'source': 'https://example.org/live'})
        self.assertEqual(request('/api/source-profiles?ruid=' + ruid + '&overlay=shared-one')['revision'], 2)
        separate = request('/api/source-profiles?ruid=' + ruid + '&overlay=separate')
        self.assertEqual(separate['revision'], 0)
        request('/api/source-profiles/save?overlay=separate',
                dict(ruid=ruid, kind='audio', expectedRevision=0,
                     profile={'source': 'lib://other.taf'}))
        self.assertEqual(request('/api/source-profiles?ruid=' + ruid)['revision'], 2)
        self.assertEqual(request('/api/source-profiles?ruid=' + ruid + '&overlay=separate')['revision'], 1)
        proc.terminate()
        proc.wait(timeout=10)


if __name__ == '__main__':
    unittest.main()
