"""Check by default. --apply restores only an unchanged close-zoom experiment."""
from pathlib import Path
import argparse
import hashlib
import json

PLUGIN = Path(__file__).resolve().parents[1]
ROOT = PLUGIN.parents[2]
BACKUP = PLUGIN / 'backups/2026-09-17-before-qgis-close-zoom'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    manifest = json.loads((BACKUP / 'manifest.json').read_text(encoding='utf-8'))
    pending = []
    for entry in manifest['files']:
        relative = Path(entry['path'])
        target = (ROOT / relative).resolve()
        if not target.is_relative_to(ROOT):
            raise RuntimeError('Path outside QGIS source tree')
        before = (BACKUP / 'before' / relative).read_bytes()
        after = (BACKUP / 'after' / relative).read_bytes()
        if digest(before) != entry['before_sha256'] or digest(after) != entry['after_sha256']:
            raise RuntimeError(f'Backup checksum mismatch: {relative}')
        current = digest(target.read_bytes())
        if current == entry['before_sha256']:
            continue
        if current != entry['after_sha256']:
            raise RuntimeError(f'Later edits detected; manual merge required, nothing restored: {relative}')
        pending.append((target, before))
    print(f'All backups validated; {len(pending)} source files eligible for restoration.')
    if args.apply:
        for target, before in pending:
            target.write_bytes(before)
        print('Source restoration complete. Rebuild qgis_3d and plugin_parammodeler before running QGIS.')
    else:
        print('Check only: live sources unchanged. Close QGIS and use --apply only when rollback is requested.')


if __name__ == '__main__':
    main()
