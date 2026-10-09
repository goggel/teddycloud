"""Disposable-server integration tests. Never defaults to a household instance."""
import hashlib
import json
import os
import signal
import subprocess
import sys
import tempfile
import shutil
from pathlib import Path
import struct
import time
import unittest
import urllib.error
import urllib.request
import wave

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sim import box as box_module, rtnl
box_module.BIN = Path(os.environ['TC_SERVER_BIN'])
BASE = os.environ['TEDDYCLOUD_BASE_URL']
SANDBOX = Path(os.environ['TC_SANDBOX'])
FIXTURES = Path(os.environ['TC_PODCAST_FIXTURE_DIR'])
FEED = 'https://podcasts.example.org/feed.rss'


def request(action, body=None, status=200, query=''):
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(BASE + '/api/podcasts/' + action + query, data=data,
                                 headers={'Content-Type': 'application/json'})
    try:
        response = urllib.request.urlopen(req, timeout=35)
    except urllib.error.HTTPError as exc:
        response = exc
    raw = response.read()
    assert response.status == status, (response.status, status, raw)
    return json.loads(raw)


def feed(episodes):
    items = ''.join(f'<item><title>Episode {i}</title><guid>{i}</guid>'
                    f'<pubDate>Thu, 01 Jan 2026 {i:02}:00:00 +0000</pubDate>'
                    f'<enclosure type="audio/wav" url="https://podcasts.example.org/{i}.wav"/></item>'
                    for i in episodes)
    (FIXTURES / 'feed.xml').write_text('<rss version="2.0"><channel><title>Test podcast</title>' + items + '</channel></rss>')


def varint(data, offset):
    value = shift = 0
    while True:
        byte = data[offset]; offset += 1
        value |= (byte & 127) << shift
        if byte < 128:
            return value, offset
        shift += 7


def taf_header(path):
    raw = path.read_bytes()
    length = struct.unpack('>I', raw[:4])[0]
    data = raw[4:4+length]
    fields = {}; offset = 0
    while offset < len(data):
        tag, offset = varint(data, offset)
        if tag & 7 == 0:
            value, offset = varint(data, offset)
        elif tag & 7 == 2:
            length, offset = varint(data, offset)
            value = data[offset:offset+length]; offset += length
        else:
            raise AssertionError(f'Unknown protobuf wire type {tag & 7}')
        fields[tag >> 3] = value
    tracks = []; offset = 0
    while offset < len(fields[4]):
        value, offset = varint(fields[4], offset); tracks.append(value)
    assert hashlib.sha1(raw[4096:]).digest() == fields[1]
    assert fields[2] == len(raw) - 4096
    return fields[3], tracks


class PodcastAPI(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        mapping = {FEED: str(FIXTURES / 'feed.xml')}
        for i in range(1, 5):
            path = FIXTURES / f'{i}.wav'
            with wave.open(str(path), 'wb') as audio:
                audio.setparams((2, 2, 48000, 0, 'NONE', 'not compressed'))
                audio.writeframes(struct.pack('<hh', 1000 * i, -1000 * i) * 48000)
            mapping[f'https://podcasts.example.org/{i}.wav'] = str(path)
        provider = FIXTURES / 'search.json'
        provider.write_text(json.dumps({'results': [
            {'collectionId': 42, 'collectionName': '<b>Example Show</b>', 'artistName': 'Author', 'feedUrl': FEED},
            {'collectionId': 43, 'collectionName': 'Invalid', 'feedUrl': 'file:///etc/passwd'}]}))
        mapping['https://itunes.apple.com/search?media=podcast&entity=podcast&limit=20&country=DE&term=Example%20Show'] = str(provider)
        Path(os.environ['TC_PODCAST_FIXTURES']).write_text(json.dumps(mapping))
        feed([1, 2, 3])

    def config(self, ruid, enabled=True):
        return dict(ruid=ruid, feedUrl=FEED, episodeCount=3, order='newest_first',
                    intervalHours=0, refreshOnPlacement=True, enabled=enabled)

    def wait(self, subscription, target='ready'):
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            current = request('subscriptions', query='?ruid=' + subscription['ruid'])['subscriptions'][0]
            if current['state'] == target:
                return current
            if current['state'] == 'error' and target != 'error':
                self.fail(str(current))
            time.sleep(.1)
        self.fail(f'Timeout: {current}')

    def test_capabilities_and_validation(self):
        self.assertTrue(request('capabilities')['supported'])
        self.assertEqual(request('subscriptions')['subscriptions'], [])
        request('subscriptions', query='?overlay=does-not-exist', status=404)
        request('subscriptions', query='?ruid=../../bad', status=400)
        for n in [0, 51, 1.5, '5']:
            request('subscriptions/save', dict(self.config('0000000000000001'), episodeCount=n), status=400)
        request('subscriptions/save', dict(self.config('0000000000000001'), feedUrl='file:///etc/passwd'), status=400)
        request('subscriptions/save', dict(self.config('0000000000000001'), intervalHours=-1), status=400)
        request('subscriptions/save', dict(self.config('0000000000000001'), intervalHours=1.5), status=400)
        request('subscriptions/save', dict(self.config('0000000000000001'), refreshOnPlacement='true'), status=400)
        request('subscriptions/save', {'padding': 'x' * 17000}, status=413)

    def test_discovery_and_manual_feed_fallback(self):
        result = request('search', query='?q=Example%20Show&country=DE')['results']
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0]['providerId'], 42)
        self.assertEqual(result[0]['feedUrl'], FEED)
        self.assertEqual(result[0]['title'], '<b>Example Show</b>')
        (FIXTURES / 'search.json').unlink()
        self.assertEqual(request('search', query='?q=Example%20Show&country=DE')['results'], result)
        request('search', query='?q=x&country=DE', status=400)
        request('search', query='?q=Example&country=D', status=400)
        request('search', query='?q=Unavailable&country=DE', status=502)
        self.assertEqual(len(request('preview', self.config('0000000000000001'))['episodes']), 3)

    def test_prepared_audio_refresh_failure_and_detach(self):
        config = self.config('0000000000000002')
        preview = request('preview', config)
        self.assertEqual([e['identity'] for e in preview['episodes']], ['3', '2', '1'])
        saved = request('subscriptions/save', config, status=201)['subscription']
        ready = self.wait(saved)
        self.assertEqual([e['identity'] for e in ready['episodes']], ['3', '2', '1'])
        content = SANDBOX / 'data/content/default/00000000/00000002.json'
        assigned = json.loads(content.read_text())
        self.assertFalse(assigned['live']); self.assertTrue(assigned['nocloud'])
        self.assertFalse(assigned['cloud_override'])
        audio = SANDBOX / 'data/library' / assigned['source'][6:]
        audio_id, tracks = taf_header(audio)
        self.assertEqual(len(tracks), 3)
        self.assertEqual(tracks[0], 0)
        self.assertEqual(audio_id, ready['audioId'])
        request('subscriptions/refresh', {'id': saved['id']}, status=202)
        time.sleep(.4); same = self.wait(saved)
        self.assertEqual(audio_id, same['audioId'])
        feed([1, 2, 3, 4])
        request('subscriptions/refresh', {'id': saved['id']}, status=202)
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            newer = self.wait(saved)
            if newer['audioId'] > audio_id: break
            time.sleep(.1)
        self.assertGreater(newer['audioId'], audio_id)
        self.assertEqual([e['identity'] for e in newer['episodes']], ['4', '3', '2'])
        updated_source = json.loads(content.read_text())['source']
        # Failed XML fetch must leave the completed assignment playable.
        (FIXTURES / 'feed.xml').write_text('<broken')
        request('subscriptions/refresh', {'id': saved['id']}, status=202)
        self.wait(saved, 'error')
        self.assertEqual(json.loads(content.read_text())['source'], updated_source)
        self.assertTrue(audio.exists())  # previous generation retained
        request('subscriptions/detach', {'id': saved['id'], 'expectedRevision': saved['revision']})
        self.assertEqual(json.loads(content.read_text())['source'], '')
        self.assertTrue(audio.exists())
        feed([1, 2, 3])

    def test_protocol_pins_and_periodic_refresh(self):
        feed([1, 2, 3])
        config = self.config('0000000000000004')
        config['intervalHours'] = 1
        saved = request('subscriptions/save', config, status=201)['subscription']
        ready = self.wait(saved)
        box = box_module.Box('000000000001', str(SANDBOX), '127.0.0.1', int(os.environ['TEDDYCLOUD_HTTPS_API_PORT']))
        self.addCleanup(box.close)
        status, headers, original = box.request('GET', '/v1/content/' + saved['ruid'])
        self.assertEqual(status, 200)
        self.assertGreater(len(original), 4096)
        feed([1, 2, 3, 4])
        clock = Path(os.environ['TC_PODCAST_CLOCK'])
        previous_clock = int(clock.read_text())
        clock.write_text(str(previous_clock + 3601))
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            current = self.wait(saved)
            if current['audioId'] > ready['audioId']: break
            time.sleep(.1)
        self.assertGreater(current['audioId'], ready['audioId'])
        status, headers, resumed = box.request('GET', '/v1/content/' + saved['ruid'], headers={'Range': 'bytes=4096-'})
        self.assertEqual(status, 206)
        self.assertEqual(resumed, original[4096:])
        status, headers, newer = box.request('GET', '/v1/content/' + saved['ruid'])
        self.assertEqual(status, 200)
        self.assertNotEqual(original, newer)
        # Another certificate must never inherit the first box's partial file.
        second = box_module.Box('000000000002', str(SANDBOX), '127.0.0.1', int(os.environ['TEDDYCLOUD_HTTPS_API_PORT']))
        self.addCleanup(second.close)
        status, headers, body = second.request('GET', '/v1/content/' + saved['ruid'], headers={'Range': 'bytes=4096-'})
        self.assertEqual(status, 416)
        self.assertEqual(body, b'')
        self.assertEqual(headers['Content-Range'], f'bytes */{len(newer)}')
        # A placement must enqueue independently of the hourly scheduler and
        # reverse the RTNL UID bytes into the same rUID used by the editor.
        feed([1, 2, 3])
        clock.write_text(str(previous_clock + 4502))
        uid = int.from_bytes(bytes.fromhex(saved['ruid']), 'little')
        # Existing RTNL decoder reads the low big-endian 32-bit word first.
        placement = rtnl.frame(rtnl.log2(1000, 1, rtnl.FUGR_TAG, rtnl.FUNC_TAG_VALID_CC3200,
            struct.pack('>II', uid & 0xffffffff, uid >> 32), field9=rtnl.CRLF))
        box.rtnl(placement)
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            placed = self.wait(saved)
            if placed['audioId'] > current['audioId']: break
            time.sleep(.1)
        self.assertGreater(placed['audioId'], current['audioId'])
        feed([1, 2, 3, 4])
        box.rtnl(placement)
        time.sleep(.5)
        self.assertEqual(self.wait(saved)['audioId'], placed['audioId'])  # throttle
        # Backward clock movement must not reuse audio IDs.
        clock.write_text(str(previous_clock))
        request('subscriptions/refresh', {'id': saved['id']}, status=202)
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            rollback = self.wait(saved)
            if rollback['audioId'] > placed['audioId']: break
            time.sleep(.1)
        self.assertGreater(rollback['audioId'], placed['audioId'])
        # Keep one persisted pin for the restart test.
        self.__class__.pinned_box = box
        self.__class__.pinned_subscription = saved
        self.__class__.pinned_audio = newer
        # Do not close its temporary certificate directory until the class ends.
        self._cleanups.pop(0)
        self.__class__.addClassCleanup(box.close)
        feed([1, 2, 3])

    def test_revision_overlay_and_manual_source_ownership(self):
        config = self.config('0000000000000003', False)
        saved = request('subscriptions/save', config, status=201)['subscription']
        self.assertEqual(saved['state'], 'paused')
        request('subscriptions/save', config, status=409)
        request('subscriptions/save', dict(config, id=saved['id'], expectedRevision=99), status=409)
        request('subscriptions/refresh', {'id': saved['id']}, status=409)
        updated = request('subscriptions/save', dict(config, id=saved['id'], expectedRevision=saved['revision'], episodeCount=2))['subscription']
        self.assertEqual(updated['revision'], 2)
        # Manual assignment pauses management, even before first generation.
        url = BASE + '/content/json/set/' + saved['ruid']
        req = urllib.request.Request(url, data=b'source=', method='POST')
        urllib.request.urlopen(req, timeout=5).read()
        request('subscriptions/detach', {'id': saved['id'], 'expectedRevision': updated['revision']})
    def test_z_restart_and_publication_recovery(self):
        # Replace the helper's server on its own disposable ports/storage.
        os.kill(int(os.environ['TC_SERVER_PID']), signal.SIGTERM)
        time.sleep(.7)
        proc = None
        log = tempfile.TemporaryFile()
        def stop():
            nonlocal proc
            if proc and proc.poll() is None:
                proc.terminate(); proc.wait(timeout=10)
            proc = None
        self.addCleanup(stop); self.addCleanup(log.close)
        def start():
            nonlocal proc
            settings = ','.join(['core.tonies_json_auto_update=false',
                'core.server.http_port=' + os.environ['TC_HTTP_PORT'],
                'core.server.https_web_port=' + os.environ['TC_HTTPS_WEB_PORT'],
                'core.server.https_api_port=' + os.environ['TC_HTTPS_API_PORT']])
            proc = subprocess.Popen([os.environ['TC_SERVER_BIN'], '--base_path', str(SANDBOX), '--config-set', settings], stdout=log, stderr=log)
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                if proc.poll() is not None:
                    log.seek(0); self.fail(log.read().decode(errors='replace'))
                try:
                    if request('capabilities')['supported']: return
                except Exception: pass
                time.sleep(.1)
            self.fail('Restart did not become ready')
        start()
        box = getattr(self, 'pinned_box', None)
        if box:
            status, _, body = box.request('GET', '/v1/content/' + self.pinned_subscription['ruid'], headers={'Range': 'bytes=4096-'})
            self.assertEqual(status, 206); self.assertEqual(body, self.pinned_audio[4096:])
        feed([1, 2, 3])
        config = self.config('0000000000000005')
        saved = request('subscriptions/save', config, status=201)['subscription']
        first = self.wait(saved)
        content = SANDBOX / 'data/content/default/00000000/00000005.json'
        for boundary, count in [('after_intent', 2), ('after_source', 1)]:
            Path(os.environ['TC_PODCAST_FAILURE']).write_text(boundary)
            saved = request('subscriptions/save', dict(config, episodeCount=count, id=saved['id'], expectedRevision=saved['revision']))['subscription']
            self.assertEqual(proc.wait(timeout=15), 90)
            Path(os.environ['TC_PODCAST_FAILURE']).unlink()
            start()
            recovered = self.wait(saved)
            current_source = json.loads(content.read_text())['source']
            audio = SANDBOX / 'data/library' / current_source[6:]
            audio_id, chapters = taf_header(audio)
            self.assertEqual(len(chapters), count)
            self.assertEqual(audio_id, recovered['audioId'])
            self.assertGreater(audio_id, first['audioId'])
        # Manual assignment must win against future jobs.
        req = urllib.request.Request(BASE + '/content/json/set/' + saved['ruid'], data=b'source=')
        urllib.request.urlopen(req, timeout=5).read()
        paused = request('subscriptions', query='?ruid=' + saved['ruid'])['subscriptions'][0]
        self.assertFalse(paused['enabled']); self.assertEqual(paused['state'], 'paused')
        request('subscriptions/refresh', {'id': saved['id']}, status=409)
        self.assertEqual(json.loads(content.read_text())['source'], '')
        stop()
        registry = SANDBOX / 'config/podcasts.json'
        registry.write_text('{corrupt')
        start()
        self.assertTrue(list(registry.parent.glob('podcasts.json.corrupt-*')))
        self.assertTrue(request('capabilities')['supported'])
        stop()



if __name__ == '__main__':
    unittest.main(verbosity=2)
