"""Exercise restore safeguards on temporary copies, never on QGIS sources."""
import importlib.util
import json
from pathlib import Path
import shutil
import sys
import tempfile

plugin = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('restore', plugin / 'patches/restore_close_zoom_backup.py')
restore = importlib.util.module_from_spec(spec)
spec.loader.exec_module(restore)
manifest = json.loads((restore.BACKUP / 'manifest.json').read_text(encoding='utf-8'))
with tempfile.TemporaryDirectory(prefix='close-zoom-restore-') as directory:
    restore.ROOT = Path(directory)
    for entry in manifest['files']:
        target = restore.ROOT / entry['path']
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(restore.BACKUP / 'after' / entry['path'], target)
    sys.argv = ['restore']
    restore.main()
    for entry in manifest['files']:
        assert restore.digest((restore.ROOT / entry['path']).read_bytes()) == entry['after_sha256']
    last = restore.ROOT / manifest['files'][-1]['path']
    last.write_bytes(last.read_bytes() + b'\n// later change\n')
    sys.argv = ['restore', '--apply']
    try:
        restore.main()
        raise AssertionError('Should reject later edits')
    except RuntimeError as error:
        assert 'Later edits detected' in str(error)
    for entry in manifest['files'][:-1]:
        assert restore.digest((restore.ROOT / entry['path']).read_bytes()) == entry['after_sha256']
    shutil.copyfile(restore.BACKUP / 'after' / manifest['files'][-1]['path'], last)
    restore.main()
    for entry in manifest['files']:
        assert restore.digest((restore.ROOT / entry['path']).read_bytes()) == entry['before_sha256']
    restore.main()
print('PASS: dry run, no partial writes on conflict, exact restore, repeat restore')
