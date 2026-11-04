"""Building class names and legacy checkpoint aliases, in stable class order."""

import re
from pathlib import Path

PRIMITIVES = (
    ('Cuboid', 'FlatRoofHouse', 'cuboid', 'flatroof', '矩形平顶房屋'),
    ('Cylinder', 'RoundFlatRoofHouse', 'cylinder', 'roundflat', '圆形平顶房屋'),
    ('LHouse', 'LShapedHouse', 'lhouse', 'lshaped', 'L形平顶房屋'),
    ('ConeCylinder', 'ConicalRoofHouse', 'conecylinder', 'conicalroof', '圆锥顶圆屋'),
    ('GabledRoof', 'GableRoofHouse', 'gabledroof', 'gableroof', '双坡顶房屋'),
    ('PyramidRoof', 'PyramidalRoofHouse', 'pyramidroof', 'pyramidalroof', '四坡尖顶房屋'),
    ('TruncatedPyramidRoof', 'FlatTopHipRoofHouse', 'truncatedpyramid', 'flattophip', '平顶四坡房屋'),
    ('HalfCylinderRoof', 'BarrelRoofHouse', 'halfcylinder', 'barrelroof', '筒拱顶房屋'),
    ('CylinderDome', 'DomeRoofHouse', 'cylinderdome', 'domeroof', '穹顶圆屋'),
    ('IndentedCuboid', 'RecessedRoofHouse', 'indentedcuboid', 'recessedroof', '内凹式平顶房屋'),
    ('AsymmetricGableHouse', 'ShortRidgeHipRoofHouse', 'asymgable', 'shortridgehip', '短脊四坡顶房屋'),
    ('FourStageRoundTower', 'CurvedRoofRoundHouse', 'fourstage', 'curvedround', '带檐曲面顶圆屋'),
    ('TwoGableHouses', 'AngledGableHouse', 'twogable', 'angledgable', '转折式双坡顶房屋'),
)

CLASS_NAMES = tuple(row[1] for row in PRIMITIVES)
_ALIASES = {name: row[1] for row in PRIMITIVES for name in (row[0], row[1])}
_ALIASES['CylinderHemisphere'] = 'DomeRoofHouse'

def canonical_class_name(name):
    return _ALIASES.get(name, name)

def legacy_class_name(name):
    canonical = canonical_class_name(name)
    return next((row[0] for row in PRIMITIVES if row[1] == canonical), name)

def regression_slug(name, suffix=None):
    canonical = canonical_class_name(name)
    match = re.match(r"_?v(\d+)(?:[_.]|$)", suffix or '')
    legacy = match is not None and int(match.group(1)) <= 9
    return next(row[2 if legacy else 3] for row in PRIMITIVES if row[1] == canonical)

def class_directory(data_root, split, name):
    root = Path(data_root) / split
    names = (canonical_class_name(name), legacy_class_name(name))
    existing = [root / n for n in dict.fromkeys(names) if (root / n).is_dir()]
    if len(existing) > 1:
        raise ValueError(f'Mixed old and renamed class folders in {root}: {names}; regenerate one consistent dataset')
    return existing[0] if existing else root / names[0]
