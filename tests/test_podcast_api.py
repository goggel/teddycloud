"""Disposable-server integration tests. Never defaults to a household instance."""
import hashlib
import array
import math
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
from podcast_ard_fixtures import install as install_ard, SHOW_URL, CORE_ID
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
        video_feed = 'https://podcasts.example.org/video.rss'
        mixed_feed = 'https://podcasts.example.org/mixed.rss'
        video_item = '<item><enclosure type="video/mpeg" url="https://podcasts.example.org/maus.mp4"/></item>'
        audio_item = '<item><enclosure type="audio/mpeg" url="https://podcasts.example.org/audio.mp3"/></item>'
        (FIXTURES / 'video.xml').write_text('<rss version="2.0"><channel>' + video_item + '</channel></rss>')
        (FIXTURES / 'mixed.xml').write_text('<rss version="2.0"><channel>' + video_item + audio_item + '</channel></rss>')
        mapping[video_feed] = str(FIXTURES / 'video.xml')
        mapping[mixed_feed] = str(FIXTURES / 'mixed.xml')
        provider.write_text(json.dumps({'results': [
            {'collectionId': 42, 'collectionName': '<b>Example Show</b>', 'artistName': 'Author', 'feedUrl': FEED},
            {'collectionId': 43, 'collectionName': 'Invalid', 'feedUrl': 'file:///etc/passwd'},
            {'collectionId': 44, 'collectionName': 'Video podcast', 'kind': 'podcast', 'feedUrl': video_feed},
            {'collectionId': 45, 'collectionName': 'Mixed podcast', 'kind': 'podcast', 'feedUrl': mixed_feed},
            {'collectionId': 46, 'collectionName': 'Temporarily unavailable',
             'feedUrl': 'https://podcasts.example.org/unavailable.rss'}]}))
        mapping['https://itunes.apple.com/search?media=podcast&entity=podcast&limit=20&country=DE&term=Example%20Show'] = str(provider)
        install_ard(FIXTURES, mapping)
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

    def test_ard_preparation_and_trim_identity(self):
        config = dict(self.config('0000000000000009'), sourceType='ard_audiothek', feedUrl='', ardShowUrl=SHOW_URL,
                      episodeCount=2, episodeTrimSourceKey='ard:' + CORE_ID,
                      episodeTrims=[dict(identity='urn:ard:episode:newer', trimStartSeconds=0, trimEndSeconds=0)])
        saved = request('subscriptions/save', config, status=201)['subscription']
        page = FIXTURES / 'ard-page-50.json'; original = page.read_text()
        try:
            ready = self.wait(saved)
            self.assertEqual([e['identity'] for e in ready['episodes']], ['urn:ard:episode:newer', 'urn:ard:episode:older'])
            # Legacy update fields must preserve an ARD source and its overrides.
            saved = request('subscriptions/save', dict(self.config(saved['ruid']), episodeCount=2, id=saved['id'], expectedRevision=saved['revision']))['subscription']
            self.assertEqual(saved['sourceType'], 'ard_audiothek')
            self.assertEqual(len(saved['episodeTrims']), 1)
            self.wait(saved)
            saved = request('subscriptions/save', dict(saved, expectedRevision=saved['revision'], ardShowUrl='https://ardaudiothek.de/sendung/example/23667030/'))['subscription']
            self.assertEqual(len(saved['episodeTrims']), 1)
            self.wait(saved)
            data = json.loads(page.read_text())
            url = 'https://podcasts.example.org/2.wav?v=2'
            data['data']['programSet']['items']['nodes'][1]['audios'][0]['downloadUrl'] = url
            page.write_text(json.dumps(data))
            mapping_path = Path(os.environ['TC_PODCAST_FIXTURES'])
            mapping = json.loads(mapping_path.read_text()); mapping[url] = str(FIXTURES/'2.wav')
            mapping_path.write_text(json.dumps(mapping))
            request('subscriptions/refresh', {'id':saved['id']}, status=202)
            current = self.wait(saved)
            self.assertGreater(current['audioId'], ready['audioId'])
            self.assertEqual(current['episodes'][0]['identity'], 'urn:ard:episode:newer')
            self.assertEqual(current['episodes'][0]['trimStartSeconds'], 0)
        finally:
            page.write_text(original)
            request('subscriptions/detach', {'id':saved['id'], 'expectedRevision':saved['revision']})

    def test_ard_show_selection_and_source_identity(self):
        from urllib.parse import quote
        result = request('search', query='?q=' + quote('ARD "&ä') + '&country=DE&provider=ard_audiothek')['results']
        self.assertEqual(result[0]['providerId'], '23667030')
        self.assertEqual(result[0]['sourceType'], 'ard_audiothek')
        config = dict(sourceType='ard_audiothek', ardShowUrl=SHOW_URL, episodeCount=3, order='newest_first')
        modern = request('preview', config)
        legacy = request('preview', dict(config, ardShowUrl='https://ardaudiothek.de/sendung/example/23667030/?share=true'))
        self.assertEqual(modern['sourceKey'], 'ard:' + CORE_ID)
        self.assertEqual(modern, legacy)
        self.assertEqual([e['identity'] for e in modern['episodes']], ['urn:ard:episode:newer', 'urn:ard:episode:older'])
        self.assertTrue(modern['warnings'])
        request('search', query='?q=Example&provider=wrong', status=400)
        request('preview', dict(config, ardShowUrl='https://ardsounds.de.evil.org/sendung/x/123'), status=400)
        path = FIXTURES / 'ard-page-50.json'
        original = path.read_text()
        try:
            path.write_text('{"data":null,"errors":[{"message":"failed"}]}')
            request('preview', config, status=502)
            repeated = json.loads((FIXTURES / 'ard-page-0.json').read_text())
            path.write_text(json.dumps(repeated))
            request('preview', config, status=502)  # No candidate progress across pages.
            path.write_text(original)
            request('preview', dict(config, episodeTrims=[dict(identity='urn:ard:episode:newer', trimStartSeconds=0, trimEndSeconds=0)], episodeTrimSourceKey='ard:another-show'), status=409)
            reversed_selection = request('preview', dict(config, episodeCount=1, order='oldest_first'))
            self.assertEqual(reversed_selection['episodes'][0]['identity'], 'urn:ard:episode:newer')
            data = json.loads(original)
            data['data']['programSet']['items']['nodes'][1]['assetId'] = ''
            data['data']['programSet']['items']['nodes'] = data['data']['programSet']['items']['nodes'][:2]
            path.write_text(json.dumps(data))
            self.assertEqual(request('preview', config)['episodes'][0]['identity'], 'ard-item:newer')
        finally:
            path.write_text(original)

    def test_ard_cumulative_and_page_limits(self):
        from podcast_ard_fixtures import query_url, SHOW_ID
        mapping_path = Path(os.environ['TC_PODCAST_FIXTURES'])
        original_mapping = mapping_path.read_text()
        mapping = json.loads(original_mapping)
        original_pages = {p: p.read_text() for p in [FIXTURES/'ard-page-0.json', FIXTURES/'ard-page-50.json']}
        show = json.loads((FIXTURES/'ard-show.json').read_text())['data']['programSet']
        config = dict(sourceType='ard_audiothek', ardShowUrl=SHOW_URL, episodeCount=50, order='newest_first')
        try:
            paths = []
            for offset in range(0, 1000, 50):
                path = FIXTURES / f'ard-limit-{offset}.json'
                nodes = [dict(id=str(offset+i), isPublished=False) for i in range(50)]
                path.write_text(json.dumps({'data':{'programSet':dict(show, items=dict(nodes=nodes, pageInfo=dict(hasNextPage=True)))}}))
                mapping[query_url('Show', {'id':SHOW_ID, 'offset':offset, 'count':50})] = str(path)
                paths.append(path)
            mapping_path.write_text(json.dumps(mapping))
            request('preview', config, status=502)  # Stops at twenty pages, with no playable selection.
            for path in paths:
                data = json.loads(path.read_text())
                data['ignoredPadding'] = 'x' * (300 * 1024)
                path.write_text(json.dumps(data))
            request('preview', config, status=502)  # Aggregate exceeds 5 MiB before page twenty.
            paths[0].write_text('{' + 'x' * (5 * 1024 * 1024))
            request('preview', config, status=502)  # Oversized individual response.
            paths[0].write_text('{malformed')
            request('preview', config, status=502)
        finally:
            mapping_path.write_text(original_mapping)
            for path, data in original_pages.items(): path.write_text(data)

    def test_capabilities_and_validation(self):
        self.assertTrue(request('capabilities')['supported'])
        self.assertTrue(request('capabilities')['displayNameSupported'])
        self.assertTrue(request('capabilities')['placementRefreshAlways'])
        self.assertEqual(request('subscriptions')['subscriptions'], [])
        request('subscriptions', query='?overlay=does-not-exist', status=404)
        request('subscriptions', query='?ruid=../../bad', status=400)
        for n in [0, 51, 1.5, '5']:
            request('subscriptions/save', dict(self.config('0000000000000001'), episodeCount=n), status=400)
        request('subscriptions/save', dict(self.config('0000000000000001'), feedUrl='file:///etc/passwd'), status=400)
        request('subscriptions/save', dict(self.config('0000000000000001'), intervalHours=-1), status=400)
        request('subscriptions/save', dict(self.config('0000000000000001'), intervalHours=1.5), status=400)
        request('subscriptions/save', dict(self.config('0000000000000001'), refreshOnPlacement='true'), status=400)
        request('subscriptions/save', dict(self.config('0000000000000001'), refreshOnPlacement=1), status=400)
        request('subscriptions/save', {'padding': 'x' * 65536}, status=413)

    def test_placement_always_on_with_legacy_false_and_zero_interval(self):
        feed([1, 2, 3])
        ruid = '00000000000000b3'
        config = dict(self.config(ruid), refreshOnPlacement=False, intervalHours=0)
        saved = request('subscriptions/save', config, status=201)['subscription']
        clock = Path(os.environ['TC_PODCAST_CLOCK'])
        previous_clock = int(clock.read_text())
        box = box_module.Box('000000000003', str(SANDBOX), '127.0.0.1', int(os.environ['TEDDYCLOUD_HTTPS_API_PORT']))
        self.addCleanup(box.close)
        try:
            self.assertTrue(saved['refreshOnPlacement'])
            first = self.wait(saved)
            self.assertTrue(first['refreshOnPlacement'])
            missing = self.config(ruid)
            missing.pop('refreshOnPlacement')
            saved = request('subscriptions/save', dict(missing, id=saved['id'],
                            expectedRevision=saved['revision']))['subscription']
            self.assertTrue(saved['refreshOnPlacement'])
            self.wait(saved)
            feed([1, 2, 3, 4])
            clock.write_text(str(previous_clock + 901))
            uid = int.from_bytes(bytes.fromhex(ruid), 'little')
            placement = rtnl.frame(rtnl.log2(1000, 1, rtnl.FUGR_TAG, rtnl.FUNC_TAG_VALID_CC3200,
                struct.pack('>II', uid & 0xffffffff, uid >> 32), field9=rtnl.CRLF))
            box.rtnl(placement)
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                placed = self.wait(saved)
                if placed['audioId'] > first['audioId']: break
                time.sleep(.1)
            self.assertGreater(placed['audioId'], first['audioId'])
            self.assertEqual(placed['intervalHours'], 0)
            feed([1, 2, 3])
            box.rtnl(placement)
            time.sleep(.5)
            self.assertEqual(self.wait(saved)['audioId'], placed['audioId'])
            saved = request('subscriptions/save', dict(missing, id=saved['id'],
                    expectedRevision=saved['revision'], enabled=False))['subscription']
            self.assertFalse(saved['enabled'])
            clock.write_text(str(previous_clock + 1802))
            box.rtnl(placement)
            time.sleep(.5)
            self.assertEqual(request('subscriptions', query='?ruid=' + ruid)['subscriptions'][0]['audioId'], placed['audioId'])
        finally:
            clock.write_text(str(previous_clock))
            feed([1, 2, 3])
            current = request('subscriptions', query='?ruid=' + ruid)['subscriptions'][0]
            request('subscriptions/detach', {'id': current['id'], 'expectedRevision': current['revision']})

    def test_display_name_is_bound_before_preparation_and_survives_legacy_update(self):
        ruid = '00000000000000b1'
        config = dict(self.config(ruid, False), displayName='Émission 🎵',
                      displayNameSourceKey='rss:' + FEED)
        saved = request('subscriptions/save', config, status=201)['subscription']
        try:
            self.assertEqual(saved['displayName'], 'Émission 🎵')
            self.assertEqual(saved['displayNameSourceKey'], 'rss:' + FEED)
            self.assertEqual(saved.get('title', ''), '')
            request('subscriptions/save', dict(config, id=saved['id'],
                    expectedRevision=saved['revision'], displayName='x' * 513), status=400)
            request('subscriptions/save', dict(config, id=saved['id'],
                    expectedRevision=saved['revision'], displayName='bad\nname'), status=400)
            mismatch = request('subscriptions/save', dict(config, id=saved['id'],
                    expectedRevision=saved['revision'], displayNameSourceKey='rss:https://other.example/feed'), status=409)
            self.assertEqual(mismatch['error']['code'], 'conflict')
            legacy = self.config(ruid, False)
            saved = request('subscriptions/save', dict(legacy, id=saved['id'],
                            expectedRevision=saved['revision']))['subscription']
            self.assertEqual(saved['displayName'], 'Émission 🎵')
            changed = request('subscriptions/save', dict(legacy, id=saved['id'],
                    expectedRevision=saved['revision'], feedUrl='https://other.example/feed'))['subscription']
            saved = changed
            self.assertEqual(saved['displayName'], '')
            self.assertEqual(saved['displayNameSourceKey'], '')
        finally:
            request('subscriptions/detach', {'id': saved['id'], 'expectedRevision': saved['revision']})

    def test_ard_resolved_title_is_bound_to_canonical_show(self):
        ruid = '00000000000000b2'
        config = dict(self.config(ruid, False), sourceType='ard_audiothek', feedUrl='',
                      ardShowUrl=SHOW_URL, displayName='Search result title',
                      displayNameSourceKey=SHOW_URL)
        saved = request('subscriptions/save', config, status=201)['subscription']
        try:
            self.assertTrue(saved['displayName'])
            self.assertNotEqual(saved['displayName'], 'Search result title')
            self.assertEqual(saved['displayNameSourceKey'], saved['sourceKey'])
            self.assertEqual(saved.get('title', ''), '')
        finally:
            request('subscriptions/detach', {'id': saved['id'], 'expectedRevision': saved['revision']})

    def test_discovery_and_manual_feed_fallback(self):
        result = request('search', query='?q=Example%20Show&country=DE')['results']
        # Discovery stays fast; the browser inspects feeds asynchronously.
        self.assertEqual([entry['providerId'] for entry in result], [42, 44, 45, 46])
        self.assertEqual(result[0]['providerId'], 42)
        self.assertEqual(result[0]['feedUrl'], FEED)
        self.assertEqual(result[0]['title'], '<b>Example Show</b>')
        (FIXTURES / 'search.json').unlink()
        self.assertEqual(request('search', query='?q=Example%20Show&country=DE')['results'], result)
        request('search', query='?q=x&country=DE', status=400)
        request('search', query='?q=Example&country=D', status=400)
        request('search', query='?q=Unavailable&country=DE', status=502)
        self.assertEqual(len(request('preview', self.config('0000000000000001'))['episodes']), 3)

    def test_feed_info(self):
        self.assertTrue(request('capabilities')['feedInspectionSupported'])
        self.assertEqual(request('feed-info', {'feedUrl': FEED}), {'videoOnly': False})
        self.assertEqual(request('feed-info', {'feedUrl': 'https://podcasts.example.org/video.rss'}), {'videoOnly': True})
        self.assertEqual(request('feed-info', {'feedUrl': 'https://podcasts.example.org/mixed.rss'}), {'videoOnly': False})
        request('feed-info', {'feedUrl': 'https://podcasts.example.org/unavailable.rss'}, status=502)
        request('feed-info', {'feedUrl': 'file:///etc/passwd'}, status=400)
        request('feed-info', {}, status=400)

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
        # A downloaded enclosure that is not decodable must also preserve audio.
        invalid = FIXTURES / 'invalid.audio'
        invalid.write_bytes(b'not an audio file')
        mapping_path = Path(os.environ['TC_PODCAST_FIXTURES'])
        mapping = json.loads(mapping_path.read_text())
        mapping['https://podcasts.example.org/5.wav'] = str(invalid)
        mapping_path.write_text(json.dumps(mapping))
        feed([2, 3, 4, 5])
        request('subscriptions/refresh', {'id': saved['id']}, status=202)
        self.wait(saved, 'error')
        self.assertEqual(json.loads(content.read_text())['source'], updated_source)
        self.assertEqual(taf_header(SANDBOX / 'data/library' / updated_source[6:])[0], newer['audioId'])
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
    def test_trim_validation_and_request_boundaries(self):
        config = self.config('0000000000000008', enabled=False)
        for value in [-1, 0.5, '3', True, None, 21601]:
            request('subscriptions/save', dict(config, trimStartSeconds=value), status=400)
        request('subscriptions/save', dict(config, sourceType='unknown'), status=400)
        override = dict(identity='i' * 512, trimStartSeconds=0, trimEndSeconds=0)
        request('subscriptions/save', dict(config, episodeTrims=[override, override]), status=400)
        for identity in ['a\x00b', 'a\nb', 'a\x7fb']:
            request('subscriptions/save', dict(config, episodeTrims=[dict(override, identity=identity)]), status=400)
        request('subscriptions/save', dict(config, episodeTrims=[dict(override, identity='i'*513)]), status=400)
        request('subscriptions/save', dict(config, episodeTrims=[dict(override, identity=str(i)) for i in range(51)]), status=400)
        request('subscriptions/save', dict(config, episodeTrims=[override], episodeTrimSourceKey='rss:https://other.example/feed'), status=409)
        large = dict(config, trimStartSeconds=3, trimEndSeconds=3, episodeTrimSourceKey='rss:' + FEED,
                     episodeTrims=[dict(override, identity=str(i) + 'x' * 509) for i in range(50)], padding='')
        large['padding'] = 'x' * (65536 - len(json.dumps(large).encode()))
        self.assertEqual(len(json.dumps(large).encode()), 65536)
        saved = request('subscriptions/save', large, status=201)['subscription']
        self.assertEqual(len(saved['episodeTrims']), 50)
        legacy = request('subscriptions/save', dict(config, id=saved['id'], expectedRevision=saved['revision']))['subscription']
        self.assertEqual(legacy['trimStartSeconds'], 3)
        self.assertEqual(len(legacy['episodeTrims']), 50)
        request('subscriptions/save', dict(config, id=legacy['id'], expectedRevision=legacy['revision'], feedUrl='https://other.example/feed'), status=409)
        request('subscriptions/detach', {'id':legacy['id'], 'expectedRevision':legacy['revision']})

    def test_trimming_pcm_signal_and_generation_metadata(self):
        mapping_path = Path(os.environ['TC_PODCAST_FIXTURES'])
        mapping = json.loads(mapping_path.read_text())
        for identity in ['tone-a', 'tone-b']:
            path = FIXTURES / (identity + '.wav')
            with wave.open(str(path), 'wb') as audio:
                audio.setparams((2, 2, 48000, 0, 'NONE', 'not compressed'))
                for frequency, seconds in [(300, 3), (700, 6), (1100, 3)]:
                    values = array.array('h')
                    for i in range(seconds * 48000):
                        value = int(12000 * math.sin(2 * math.pi * frequency * i / 48000))
                        values.extend((value, value))
                    audio.writeframes(values.tobytes())
            mapping['https://podcasts.example.org/' + identity + '.wav'] = str(path)
        mapping_path.write_text(json.dumps(mapping))
        items = ''.join('<item><guid>' + name + '</guid><title>' + name + '</title><duration>1</duration><enclosure type="audio/wav" url="https://podcasts.example.org/' + name + '.wav"/></item>' for name in ['tone-a', 'tone-b'])
        (FIXTURES / 'feed.xml').write_text('<rss><channel><title>Tones</title>' + items + '</channel></rss>')
        config = dict(self.config('0000000000000007'), episodeCount=2, trimStartSeconds=3, trimEndSeconds=3,
                      episodeTrimSourceKey='rss:' + FEED, episodeTrims=[dict(identity='tone-a', trimStartSeconds=0, trimEndSeconds=3)])
        saved = request('subscriptions/save', config, status=201)['subscription']
        try:
            ready = self.wait(saved)
            self.assertEqual([e['trimmedDurationSeconds'] for e in ready['episodes']], [9, 6])
            content = SANDBOX / 'data/content/default/00000000/00000007.json'
            def audio_path():
                return SANDBOX / 'data/library' / json.loads(content.read_text())['source'][6:]
            path = audio_path()
            self.assertEqual(len(taf_header(path)[1]), 2)
            decoded = subprocess.run(['ffmpeg', '-v', 'error', '-f', 'ogg', '-i', 'pipe:0', '-ac', '1', '-ar', '48000', '-f', 'f32le', 'pipe:1'], input=path.read_bytes()[4096:], stdout=subprocess.PIPE, check=True).stdout
            pcm = array.array('f'); pcm.frombytes(decoded)
            self.assertAlmostEqual(len(pcm)/48000, 15, delta=.4)
            def energy(at, frequency):
                values = pcm[int(at*48000):int(at*48000)+4800]
                return sum(v*math.cos(2*math.pi*frequency*i/48000) for i,v in enumerate(values))**2 + sum(v*math.sin(2*math.pi*frequency*i/48000) for i,v in enumerate(values))**2
            for at, expected in [(1, 300), (5, 700), (12, 700)]:
                for unwanted in [300, 700, 1100]:
                    if unwanted != expected: self.assertGreater(energy(at, expected), energy(at, unwanted)*20)
            # An inactive override changes saved configuration, not prepared bytes.
            saved = request('subscriptions/save', dict(config, id=saved['id'], expectedRevision=saved['revision'], episodeTrims=config['episodeTrims']+[dict(identity='inactive',trimStartSeconds=9,trimEndSeconds=0)]))['subscription']
            self.assertEqual(self.wait(saved)['audioId'], ready['audioId'])
            # End-only and start-only paths both use measured PCM, ignoring metadata.
            config['trimEndSeconds'] = 0
            saved = request('subscriptions/save', dict(config, id=saved['id'], expectedRevision=saved['revision']))['subscription']
            changed = self.wait(saved)
            self.assertGreater(changed['audioId'], ready['audioId'])
            self.assertEqual([e['trimmedDurationSeconds'] for e in changed['episodes']], [9, 9])
            previous_source = json.loads(content.read_text())['source']
            previous_hash = hashlib.sha1(audio_path().read_bytes()).hexdigest()
            # Removing an entire episode fails the whole job and preserves applied metadata.
            saved = request('subscriptions/save', dict(config, id=saved['id'], expectedRevision=saved['revision'], episodeTrims=[dict(identity='tone-a',trimStartSeconds=12,trimEndSeconds=0)]))['subscription']
            failed = self.wait(saved, 'error')
            self.assertEqual(json.loads(content.read_text())['source'], previous_source)
            self.assertEqual(hashlib.sha1(audio_path().read_bytes()).hexdigest(), previous_hash)
            self.assertEqual(failed['episodes'][0]['trimStartSeconds'], 0)
            self.assertEqual(failed['episodes'][0]['trimmedDurationSeconds'], 9)
        finally:
            request('subscriptions/detach', {'id':saved['id'], 'expectedRevision':saved['revision']})
            feed([1, 2, 3])

    def test_decoder_partial_frames_failures_and_cancellation(self):
        control = Path(os.environ['TC_PODCAST_ENCODER_CONTROL'])
        config = dict(self.config('000000000000000a'), episodeCount=1, trimEndSeconds=1)
        saved = request('subscriptions/save', dict(config, enabled=False), status=201)['subscription']
        def mode(value):
            control.with_suffix('.count').unlink(missing_ok=True)
            control.write_text(value)
        try:
            mode('chunks')
            saved = request('subscriptions/save', dict(config, id=saved['id'], expectedRevision=saved['revision']))['subscription']
            prepared = self.wait(saved)
            self.assertAlmostEqual(prepared['episodes'][0]['trimmedDurationSeconds'], .2, places=4)
            published = prepared['audioId']
            for failure in ['fail_second', 'inconsistent', 'partial_frame']:
                mode(failure)
                saved = request('subscriptions/save', dict(config, id=saved['id'], expectedRevision=saved['revision'], trimEndSeconds=1, trimStartSeconds=0,
                    episodeCount=2 if failure != 'inconsistent' else 3))['subscription']
                failed = self.wait(saved, 'error')
                self.assertEqual(failed['audioId'], published)
                self.assertTrue(failed['lastError'])
            mode('hang')
            request('subscriptions/refresh', {'id':saved['id']}, status=202)
            deadline = time.monotonic() + 5
            pid_path = control.with_suffix('.pid')
            prior_pid = pid_path.read_text()
            while time.monotonic() < deadline and pid_path.read_text() == prior_pid:
                time.sleep(.02)
            pid = int(pid_path.read_text())
            saved = request('subscriptions/save', dict(config, id=saved['id'], expectedRevision=saved['revision'], enabled=False))['subscription']
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                try: os.kill(pid, 0)
                except ProcessLookupError: break
                time.sleep(.02)
            else: self.fail('Cancelled decoder was not reaped')
            self.assertEqual(request('subscriptions', query='?ruid='+saved['ruid'])['subscriptions'][0]['audioId'], published)
        finally:
            control.unlink(missing_ok=True)
            request('subscriptions/detach', {'id':saved['id'], 'expectedRevision':saved['revision']})

    def test_y_provider_rate_budgets(self):
        # The one earlier ARD search is cached and consumes just one upstream slot.
        for i in range(19):
            request('search', query='?provider=ard_audiothek&q=Unavailable'+str(i), status=502)
        request('search', query='?provider=ard_audiothek&q=UnavailableExtra', status=429)
        request('search', query='?provider=apple&q=StillAvailableBudget', status=502)
        from urllib.parse import quote
        self.assertTrue(request('search', query='?provider=ard_audiothek&q='+quote('ARD "&ä'))['results'])

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
        registry = SANDBOX / 'config/podcasts.json'
        before_policy = json.loads(registry.read_text())
        for subscription in before_policy['subscriptions']:
            subscription['refreshOnPlacement'] = False
        registry.write_text(json.dumps(before_policy))
        start()
        after_policy = json.loads(registry.read_text())
        self.assertTrue(all(s['refreshOnPlacement'] for s in after_policy['subscriptions']))
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
            self.assertTrue(all('trimmedDurationSeconds' in e and e['trimStartSeconds'] == 0 and e['trimEndSeconds'] == 0 for e in recovered['episodes']))
            self.assertGreater(audio_id, first['audioId'])
        # Manual assignment must win against future jobs.
        req = urllib.request.Request(BASE + '/content/json/set/' + saved['ruid'], data=b'source=')
        urllib.request.urlopen(req, timeout=5).read()
        paused = request('subscriptions', query='?ruid=' + saved['ruid'])['subscriptions'][0]
        self.assertFalse(paused['enabled']); self.assertEqual(paused['state'], 'paused')
        request('subscriptions/refresh', {'id': saved['id']}, status=409)
        self.assertEqual(json.loads(content.read_text())['source'], '')
        stop()
        legacy = json.loads(registry.read_text())
        legacy['version'] = 1
        for index, subscription in enumerate(legacy['subscriptions']):
            if index == 0:
                subscription.pop('refreshOnPlacement', None)
            else:
                subscription['refreshOnPlacement'] = False
            for key in ['sourceType', 'sourceKey', 'ardShowUrl', 'ardShowId', 'trimStartSeconds', 'trimEndSeconds', 'episodeTrims', 'episodeTrimSourceKey']:
                subscription.pop(key, None)
        registry.write_text(json.dumps(legacy))
        migration_backup = registry.with_name('podcasts.json.v1.bak')
        migration_backup.unlink(missing_ok=True)
        start()
        migrated = json.loads(registry.read_text())
        self.assertEqual(migrated['version'], 2)
        self.assertEqual(migrated['pins'], legacy['pins'])
        self.assertEqual(json.loads(migration_backup.read_text()), legacy)
        self.assertEqual([s['id'] for s in migrated['subscriptions']], [s['id'] for s in legacy['subscriptions']])
        self.assertTrue(all(s['trimStartSeconds'] == 0 for s in migrated['subscriptions']))
        self.assertTrue(all(s['refreshOnPlacement'] for s in migrated['subscriptions']))
        if box:
            status, _, body = box.request('GET', '/v1/content/' + self.pinned_subscription['ruid'], headers={'Range': 'bytes=4096-'})
            self.assertEqual(status, 206); self.assertEqual(body, self.pinned_audio[4096:])
        stop()
        registry.write_text('{corrupt')
        start()
        self.assertTrue(list(registry.parent.glob('podcasts.json.corrupt-*')))
        self.assertTrue(request('capabilities')['supported'])
        stop()
        unknown = json.loads(registry.read_text()); unknown['version'] = 99
        registry.write_text(json.dumps(unknown))
        # Unsupported future versions must be preserved, even with an older valid backup.
        settings = ','.join(['core.tonies_json_auto_update=false', 'core.server.http_port='+os.environ['TC_HTTP_PORT'], 'core.server.https_web_port='+os.environ['TC_HTTPS_WEB_PORT'], 'core.server.https_api_port='+os.environ['TC_HTTPS_API_PORT']])
        proc = subprocess.Popen([os.environ['TC_SERVER_BIN'], '--base_path', str(SANDBOX), '--config-set', settings], stdout=log, stderr=log)
        deadline = time.monotonic()+10
        while time.monotonic()<deadline:
            try:
                if not request('capabilities')['supported']: break
            except Exception: pass
            time.sleep(.1)
        else: self.fail('Unknown-version service did not report unavailable')
        request('preview', self.config('0000000000000001'), status=503)
        stop()
        self.assertEqual(json.loads(registry.read_text())['version'], 99)



if __name__ == '__main__':
    unittest.main(verbosity=2)
