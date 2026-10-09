#!/usr/bin/env python3
"""Production smoke and separately labeled fixture-mode tests. Owned volumes only."""
import argparse
import json
import re
import shutil
import subprocess
import time
import urllib.error
import urllib.request
import uuid


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--image', required=True)
    parser.add_argument('--test-image', required=True)
    parser.add_argument('--engine', default=shutil.which('docker') or shutil.which('podman'))
    args = parser.parse_args()
    if not args.engine:
        parser.error('Docker or Podman is required')

    def run(*command, capture=False):
        result = subprocess.run([args.engine, *command], check=True,
                                stdout=subprocess.PIPE if capture else None, text=True)
        return result.stdout.strip() if capture else ''

    print('Deterministic service/restart tests: fixture-enabled test image', flush=True)
    run('run', '--rm', args.test_image, 'make', 'test_podcast')

    for nonroot in (False, True):
        prefix = 'tc-podcast-test-' + uuid.uuid4().hex[:12]
        volumes = []
        container = prefix + '-server'
        try:
            flags = []
            for suffix, target in [('config', '/teddycloud/config'), ('certs', '/teddycloud/certs'),
                                   ('content', '/teddycloud/data/content'), ('library', '/teddycloud/data/library')]:
                volume = prefix + '-' + suffix
                run('volume', 'create', volume, capture=True)
                volumes.append(volume)
                flags += ['-v', volume + ':' + target]
            run('run', '--rm', '-v', volumes[1] + ':/out', '--entrypoint', 'sh', args.test_image,
                '-c', 'cp -a /buildenv/bin/test-template/certs/. /out/')
            environment = ['-e', 'TC_PODCAST_FIXTURES=/must-be-ignored', '-e', 'TC_PODCAST_CLOCK=/must-be-ignored']
            if nonroot:
                environment += ['-e', 'PUID=1000', '-e', 'PGID=1000']
            run('run', '-d', '--name', container, '-p', '127.0.0.1::80', *flags, *environment,
                args.image, '--config-set', 'core.tonies_json_auto_update=false', capture=True)
            port = run('port', container, '80/tcp', capture=True).splitlines()[0].rsplit(':', 1)[1]
            base = 'http://127.0.0.1:' + port

            def get(path):
                with urllib.request.urlopen(base + path, timeout=5) as response:
                    return response.read()

            deadline = time.monotonic() + 60
            while True:
                try:
                    assert json.loads(get('/api/podcasts/capabilities'))['supported']
                    break
                except Exception:
                    if time.monotonic() > deadline:
                        run('logs', container)
                        raise
                    time.sleep(.2)
            index = get('/web/').decode()
            scripts = re.findall(r'src="([^"]+\.js)"', index)
            assert scripts
            assert any(b'/api/podcasts/' in get(src if src.startswith('/') else '/web/' + src) for src in scripts)
            assert 'podcasts' in json.loads(get('/web/translations/en.json'))
            run('exec', container, 'sh', '-c',
                '! command -v node && ! command -v python3 && '
                '! grep -a -q TC_PODCAST_FIXTURES /usr/local/bin/teddycloud && '
                'ldd /usr/local/bin/teddycloud | grep libcurl && '
                'ldd /usr/local/bin/teddycloud | grep libxml2')
            try:
                get('/api/podcasts/test-hook')
            except urllib.error.HTTPError as error:
                assert error.code == 404
            else:
                raise AssertionError('Unexpected test route')
            config = dict(ruid='0123456789abcdef', feedUrl='https://example.org/feed.rss',
                          episodeCount=5, order='newest_first', intervalHours=0,
                          enabled=False, refreshOnPlacement=False)
            req = urllib.request.Request(base + '/api/podcasts/subscriptions/save',
                                         data=json.dumps(config).encode(), headers={'Content-Type': 'application/json'})
            with urllib.request.urlopen(req, timeout=5) as response:
                assert response.status == 201
                saved = json.load(response)['subscription']
            assert saved['state'] == 'paused'
            run('restart', '--time', '10', container, capture=True)
            deadline = time.monotonic() + 30
            while True:
                try:
                    subscriptions = json.loads(get('/api/podcasts/subscriptions?ruid=' + config['ruid']))['subscriptions']
                    assert subscriptions[0]['id'] == saved['id']
                    break
                except Exception:
                    if time.monotonic() > deadline:
                        raise
                    time.sleep(.2)
            print(f'Production image smoke/persistence: {"PUID=1000" if nonroot else "root"} passed', flush=True)
        finally:
            subprocess.run([args.engine, 'rm', '-fv', container], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            for volume in volumes:
                subprocess.run([args.engine, 'volume', 'rm', volume], check=True, stdout=subprocess.DEVNULL)
    print('Container verification passed')


if __name__ == '__main__':
    main()
