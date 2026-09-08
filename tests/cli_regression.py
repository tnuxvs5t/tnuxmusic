"""Exercise actual application processes and the legacy script in private data dirs."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

binary = str(Path(sys.argv[1]).resolve())
script = Path(__file__).resolve().parents[1] / 'scripts/merge_import_library.js'
with tempfile.TemporaryDirectory(prefix='tnuxmusic-cli-test-') as temp:
    root = Path(temp)
    env = dict(os.environ, XDG_DATA_HOME=str(root / 'data'), XDG_CONFIG_HOME=str(root / 'config'),
               QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software', TNUXMUSIC_BIN=binary)
    data = root / 'data/tnux/tnuxmusic'
    data.mkdir(parents=True)
    dest = data / 'library.json'
    original = {'schema': 'tnuxmusic.library.v1', 'tracks': [
        {'id': 'a', 'title': 'First', 'artist': 'A', 'album': 'Original', 'qualities': [{'path': 'a.mp3'}]}]}
    dest.write_text(json.dumps(original))
    incoming_dir = root / 'source'
    incoming_dir.mkdir()
    incoming = incoming_dir / 'incoming.json'
    incoming.write_text(json.dumps({'tracks': [
        {'id': 'b', 'title': 'Second', 'artist': 'B', 'album': 'New', 'qualities': [{'path': 'b.mp3'}]}]}))

    def run(args):
        return subprocess.run(args, env=env, capture_output=True, text=True, timeout=20)

    with (root / 'app.log').open('w') as log:
        first = subprocess.Popen([binary], env=env, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 15
            while not (data / 'instance.lock').exists():
                assert first.poll() is None, 'First instance failed to start'
                assert time.monotonic() < deadline, 'Timed out waiting for instance lock'
                time.sleep(0.05)
            result = run([binary])
            assert result.returncode == 0, result.stderr
            assert first.poll() is None, 'Second instance affected the first process'
            before = dest.read_bytes()
            result = run([binary, '--merge-library', str(incoming), '--library', str(dest)])
            assert result.returncode == 2, result.stderr
            assert dest.read_bytes() == before, 'Rejected merge wrote the library'
        finally:
            first.terminate()
            first.wait(timeout=10)

    # QLockFile must recover a dead process lock; the CLI uses the same backend.
    result = run([binary, '--merge-library', str(incoming), '--library', str(dest)])
    assert result.returncode == 0, result.stderr + result.stdout
    tracks = {t['id']: t for t in json.loads(dest.read_text())['tracks']}
    assert len(tracks) == 2
    assert tracks['a']['qualities'][0]['path'] == str(data / 'a.mp3')
    assert tracks['b']['qualities'][0]['path'] == str(incoming_dir / 'b.mp3')
    before = dest.read_bytes()
    incoming.write_text('{"wrong": true}')
    result = run([binary, '--merge-library', str(incoming), '--library', str(dest)])
    assert result.returncode != 0
    assert dest.read_bytes() == before

    if shutil.which('node'):
        incoming.write_text(json.dumps({'tracks': [tracks['b']]}))
        result = run(['node', str(script), str(incoming), str(dest)])
        assert result.returncode == 0, result.stderr + result.stdout
        assert len(json.loads(dest.read_text())['tracks']) == 2
print('PASS: instance activation, write lock, stale lock, transactional CLI, relative paths, JS wrapper')
