"""Reconstruct the pre-experiment sources without changing live source files."""
from pathlib import Path
import hashlib
import json
import subprocess

PLUGIN = Path(__file__).resolve().parents[1]
ROOT = PLUGIN.parents[2]
DEST = PLUGIN / 'backups' / '2026-09-17-before-qgis-close-zoom'


def normalize(data):
    return data.decode('utf-8').replace('\r\n', '\n')


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Expected exactly one matching block')
    return text.replace(old, new, 1)


def main():
    if DEST.exists():
        raise RuntimeError('Backup already exists; refusing to overwrite')
    patch = (PLUGIN / 'patches/qgis-close-zoom.applypatch').read_text(encoding='utf-8')
    sections = patch.split('*** Update File: ')[1:]
    pairs = {}
    for section in sections:
        name, body = section.split('\n', 1)
        path = (PLUGIN / name).resolve()
        current = normalize(path.read_bytes())
        before = current
        cursor = 0
        edits = []
        for hunk in body.split('@@\n')[1:]:
            lines = [line for line in hunk.splitlines(True) if not line.startswith('***')]
            old = ''.join(line[1:] for line in lines if line.startswith((' ', '-')))
            new = ''.join(line[1:] for line in lines if line.startswith((' ', '+')))
            position = before.find(new, cursor)
            if not new or position < 0:
                raise RuntimeError(f'Cannot reverse hunk in {name}')
            before = before[:position] + old + before[position + len(new):]
            edits.append((position, old, new))
            cursor = position + len(old)
        forward = before
        for position, old, new in reversed(edits):
            if forward[position:position + len(old)] != old:
                raise RuntimeError('Round-trip context mismatch')
            forward = forward[:position] + new + forward[position + len(old):]
        if forward != current:
            raise RuntimeError('Round-trip validation failed')
        pairs[path] = (before, 'reverse recorded core patch; forward round-trip verified')

    for name in ('parammodeler_scene3d.cpp', 'parammodeler_scene3d.h'):
        old = subprocess.check_output(['git', 'show', f'HEAD:{name}'], cwd=PLUGIN)
        pairs[PLUGIN / name] = (normalize(old), 'HEAD baseline; diff reviewed as close-zoom-only')

    path = PLUGIN / 'parammodeler_dock.cpp'
    before = normalize(path.read_bytes())
    start = before.index('  auto *viewActions = new QHBoxLayout();\n')
    end = before.index('  connect( mWireframeModeCheckBox, &QCheckBox::toggled', start)
    before = replace_once(before, before[start:end], '  workflowLayout->addWidget( mWireframeModeCheckBox );\n')
    before = replace_once(before, '#include <QToolButton>\n#include <QToolTip>\n', '')
    pairs[path] = (before, 'only focus button/includes reversed; earlier uncommitted features retained')

    manifest = {'description': 'Reconstructed pre-close-zoom source backup, not a historical binary backup',
                'files': []}
    for path, (before, method) in pairs.items():
        relative = path.relative_to(ROOT).as_posix()
        after_bytes = path.read_bytes()
        newline = '\r\n' if b'\r\n' in after_bytes else '\n'
        before_bytes = before.replace('\n', newline).encode('utf-8')
        for folder, data in (('before', before_bytes), ('after', after_bytes)):
            target = DEST / folder / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        manifest['files'].append({'path': relative, 'method': method,
                                 'before_sha256': hashlib.sha256(before_bytes).hexdigest(),
                                 'after_sha256': hashlib.sha256(after_bytes).hexdigest()})
    (DEST / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(f'Backed up and verified {len(pairs)} source files: {DEST}')


if __name__ == '__main__':
    main()
