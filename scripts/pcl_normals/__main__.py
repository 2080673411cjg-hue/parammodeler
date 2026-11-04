"""Compute persistent normals after augmentation, without rewriting point clouds."""

from primitive_names import canonical_class_name

import argparse
from pathlib import Path

from main_reg import load_txt_points, normalize_points, precompute_normals


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data_root', type=Path, required=True)
    parser.add_argument('--classes', nargs='+')
    parser.add_argument('--threads', type=int, default=4)
    args = parser.parse_args()
    if args.threads < 1:
        parser.error('threads must be positive')
    root = args.data_root.resolve()
    selected = {canonical_class_name(n) for n in args.classes} if args.classes else None
    if selected and 'TriPrismPyramid' in selected:
        parser.error('TriPrismPyramid is excluded from training and normal preparation')
    paths = [p for split in ('train', 'val', 'test') for p in sorted((root / split).glob('*/*.txt'))
             if p.parent.name != 'TriPrismPyramid' and (not selected or canonical_class_name(p.parent.name) in selected)]
    if not paths:
        parser.error(f'No selected point clouds found under {root}')
    if selected and selected - {canonical_class_name(p.parent.name) for p in paths}:
        parser.error(f'Classes absent from dataset: {sorted(selected - {canonical_class_name(p.parent.name) for p in paths})}')
    # Read each TXT only once; this is the exact normalization used in training.
    clouds = ((path, normalize_points(load_txt_points(path))) for path in paths)
    for _ in precompute_normals(clouds, len(paths), 'offline', normal_backend='pcl',
                                normal_threads=args.threads, data_root=root, source_points=True):
        pass


if __name__ == '__main__':
    main()
