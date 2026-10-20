# Initial Alignment Evaluation

known class and metadata yaw; no classification or geometric-yaw inference; pre-widget geometry

All groups use metadata yaw. Translation error is measured against the generated local origin (zero), not the algorithm's own chosen target.
This is a standalone geometry evaluation, not a QGIS GUI or live-service acceptance test.
Classification, unknown-yaw estimation, widget rounding/limits, and actual checkpoint inference are not exercised.
Existing test predictions are reused; matching labels do not prove unchanged cloud contents since training.
RMSE/P95 are unsigned cloud-to-triangle distances over all displayed points (up to 2048). Outliers are retained.
0.2m and 1% of true mesh diagonal are descriptive thresholds, not validated acceptance limits.
Recess auto groups also reposition the inner opening; baseline/shape groups do not.

Summary CSV includes world-space inner-opening corner XY and top-Z errors for IndentedCuboid, including outer translation.

BuildMesh and dlutils functions are linked directly. Display-bbox/translation orchestration mirrors the plugin; LHouse uses an equivalent six-wall mesh because its declared data overload has no definition.

| Class | Group | Valid | Median XY (m) | Median Z (m) | Median RMSE (m) | Median P95 (m) | Mean within 0.2m |
|---|---|---:|---:|---:|---:|---:|---:|
| AsymmetricGableHouse | alignment | 100/100 | 0.142 | 0.196 | 0.214 | 0.299 | 67.2% |
| AsymmetricGableHouse | baseline | 100/100 | 0.000 | 0.000 | 0.141 | 0.135 | 99.1% |
| AsymmetricGableHouse | combined | 100/100 | 0.142 | 0.232 | 0.265 | 0.424 | 57.1% |
| AsymmetricGableHouse | shape | 100/100 | 0.000 | 0.000 | 0.245 | 0.358 | 66.6% |
| ConeCylinder | alignment | 100/100 | 0.362 | 0.009 | 0.202 | 0.352 | 59.8% |
| ConeCylinder | baseline | 100/100 | 0.000 | 0.000 | 0.082 | 0.091 | 99.7% |
| ConeCylinder | combined | 100/100 | 0.362 | 0.009 | 0.215 | 0.380 | 56.2% |
| ConeCylinder | shape | 100/100 | 0.000 | 0.000 | 0.120 | 0.151 | 91.4% |
| Cuboid | alignment | 100/100 | 0.116 | 0.001 | 0.096 | 0.134 | 98.5% |
| Cuboid | baseline | 100/100 | 0.000 | 0.000 | 0.084 | 0.105 | 99.7% |
| Cuboid | combined | 100/100 | 0.116 | 0.114 | 0.122 | 0.174 | 94.5% |
| Cuboid | shape | 100/100 | 0.000 | 0.000 | 0.169 | 0.254 | 77.2% |
| Cylinder | alignment | 100/100 | 0.222 | 0.000 | 0.132 | 0.248 | 86.7% |
| Cylinder | baseline | 100/100 | 0.000 | 0.000 | 0.067 | 0.105 | 99.6% |
| Cylinder | combined | 100/100 | 0.222 | 0.000 | 0.137 | 0.255 | 85.4% |
| Cylinder | shape | 100/100 | 0.000 | 0.000 | 0.086 | 0.138 | 97.2% |
| CylinderDome | alignment | 100/100 | 0.501 | 0.024 | 0.251 | 0.461 | 56.0% |
| CylinderDome | baseline | 100/100 | 0.000 | 0.000 | 0.085 | 0.108 | 99.7% |
| CylinderDome | combined | 100/100 | 0.501 | 0.024 | 0.269 | 0.484 | 50.6% |
| CylinderDome | shape | 100/100 | 0.000 | 0.000 | 0.123 | 0.179 | 91.0% |
| FourStageRoundTower | alignment | 100/100 | 0.304 | 0.000 | 0.180 | 0.297 | 83.9% |
| FourStageRoundTower | baseline | 100/100 | 0.000 | 0.000 | 0.146 | 0.157 | 96.1% |
| FourStageRoundTower | combined | 100/100 | 0.304 | 0.000 | 0.279 | 0.462 | 63.4% |
| FourStageRoundTower | shape | 100/100 | 0.000 | 0.000 | 0.249 | 0.428 | 67.0% |
| GabledRoof | alignment | 100/100 | 0.112 | 0.174 | 0.180 | 0.258 | 80.6% |
| GabledRoof | baseline | 100/100 | 0.000 | 0.000 | 0.128 | 0.131 | 99.2% |
| GabledRoof | combined | 100/100 | 0.112 | 0.178 | 0.278 | 0.463 | 60.9% |
| GabledRoof | shape | 100/100 | 0.000 | 0.000 | 0.264 | 0.426 | 66.4% |
| HalfCylinderRoof | alignment | 100/100 | 0.556 | 0.000 | 0.341 | 0.565 | 56.4% |
| HalfCylinderRoof | baseline | 100/100 | 0.000 | 0.000 | 0.131 | 0.144 | 98.9% |
| HalfCylinderRoof | combined | 100/100 | 0.556 | 0.000 | 0.415 | 0.738 | 47.2% |
| HalfCylinderRoof | shape | 100/100 | 0.000 | 0.000 | 0.242 | 0.350 | 65.6% |
| IndentedCuboid | alignment | 100/100 | 0.342 | 0.485 | 0.370 | 0.569 | 47.8% |
| IndentedCuboid | baseline | 100/100 | 0.000 | 0.000 | 0.134 | 0.152 | 98.5% |
| IndentedCuboid | combined | 100/100 | 0.342 | 0.485 | 0.411 | 0.664 | 39.6% |
| IndentedCuboid | shape | 100/100 | 0.000 | 0.000 | 0.299 | 0.509 | 50.7% |
| LHouse | alignment | 100/100 | 0.220 | 0.004 | 0.224 | 0.280 | 83.2% |
| LHouse | baseline | 100/100 | 0.000 | 0.000 | 0.195 | 0.179 | 95.2% |
| LHouse | combined | 100/100 | 0.220 | 0.081 | 0.429 | 0.777 | 64.7% |
| LHouse | shape | 100/100 | 0.000 | 0.000 | 0.437 | 0.828 | 59.6% |
| PyramidRoof | alignment | 100/100 | 0.164 | 0.272 | 0.255 | 0.364 | 46.4% |
| PyramidRoof | baseline | 100/100 | 0.000 | 0.000 | 0.119 | 0.120 | 99.5% |
| PyramidRoof | combined | 100/100 | 0.164 | 0.243 | 0.277 | 0.424 | 45.2% |
| PyramidRoof | shape | 100/100 | 0.000 | 0.000 | 0.190 | 0.293 | 73.1% |
| TruncatedPyramidRoof | alignment | 100/100 | 0.107 | 0.467 | 0.338 | 0.503 | 44.1% |
| TruncatedPyramidRoof | baseline | 100/100 | 0.000 | 0.000 | 0.139 | 0.149 | 98.8% |
| TruncatedPyramidRoof | combined | 100/100 | 0.107 | 0.578 | 0.408 | 0.659 | 33.8% |
| TruncatedPyramidRoof | shape | 100/100 | 0.000 | 0.000 | 0.245 | 0.402 | 66.0% |
| TwoGableHouses | alignment | 100/100 | 0.210 | 0.069 | 0.274 | 0.296 | 74.2% |
| TwoGableHouses | baseline | 100/100 | 0.000 | 0.000 | 0.234 | 0.202 | 94.5% |
| TwoGableHouses | combined | 100/100 | 0.210 | 0.125 | 0.641 | 1.204 | 37.9% |
| TwoGableHouses | shape | 100/100 | 0.000 | 0.000 | 0.606 | 1.162 | 41.2% |

## Label Audit

- Cuboid: matched; max target difference 9.536743e-07; E:\pointnet\ptv3_simple\logs\ptv3_reg_cuboid_v4_canonical_normals
- Cylinder: matched; max target difference 0; E:\pointnet\ptv3_simple\logs\ptv3_reg_cylinder_v4_canonical_normals
- LHouse: matched; max target difference 0; E:\pointnet\ptv3_simple\logs\ptv3_reg_lhouse_v4_canonical_normals
- ConeCylinder: matched; max target difference 0; E:\pointnet\ptv3_simple\logs\ptv3_reg_conecylinder_v4_canonical_normals
- GabledRoof: matched; max target difference 0; E:\pointnet\ptv3_simple\logs\ptv3_reg_gabledroof_v4_canonical_normals
- PyramidRoof: matched; max target difference 9.536743e-07; E:\pointnet\ptv3_simple\logs\ptv3_reg_pyramidroof_v4_canonical_normals
- TruncatedPyramidRoof: matched; max target difference 1.907349e-06; E:\pointnet\ptv3_simple\logs\ptv3_reg_truncatedpyramid_v4_canonical_normals
- HalfCylinderRoof: matched; max target difference 0; E:\pointnet\ptv3_simple\logs\ptv3_reg_halfcylinder_v4_canonical_normals
- CylinderDome: matched; max target difference 0; E:\pointnet\ptv3_simple\logs\ptv3_reg_cylinderdome_v4_canonical_normals
- IndentedCuboid: matched; max target difference 1.907349e-06; E:\pointnet\ptv3_simple\logs\ptv3_reg_indentedcuboid_v4_canonical_normals
- AsymmetricGableHouse: matched; max target difference 0; E:\pointnet\ptv3_simple\logs\ptv3_reg_asymgable_v4_canonical_normals
- FourStageRoundTower: matched; max target difference 0; E:\pointnet\ptv3_simple\logs\ptv3_reg_fourstage_v5_normals
- TwoGableHouses: matched; max target difference 0; E:\pointnet\ptv3_simple\logs\ptv3_reg_twogable_v4_canonical_normals

## Raw Geometry Validation

All raw ground-truth meshes must reproduce the exported clouds with RMSE below 0.0001m before this report is accepted.
- Cuboid: 100/100 valid; median RMSE 0.00000025m; max RMSE 0.00000047m
- Cylinder: 100/100 valid; median RMSE 0.00000029m; max RMSE 0.00000059m
- LHouse: 100/100 valid; median RMSE 0.00000041m; max RMSE 0.00000091m
- ConeCylinder: 100/100 valid; median RMSE 0.00000027m; max RMSE 0.00000046m
- GabledRoof: 100/100 valid; median RMSE 0.00000039m; max RMSE 0.00000067m
- PyramidRoof: 100/100 valid; median RMSE 0.00000034m; max RMSE 0.00000059m
- TruncatedPyramidRoof: 100/100 valid; median RMSE 0.00000045m; max RMSE 0.00000075m
- HalfCylinderRoof: 100/100 valid; median RMSE 0.00000041m; max RMSE 0.00000063m
- CylinderDome: 100/100 valid; median RMSE 0.00000034m; max RMSE 0.00000051m
- IndentedCuboid: 100/100 valid; median RMSE 0.00000037m; max RMSE 0.00000059m
- AsymmetricGableHouse: 100/100 valid; median RMSE 0.00000039m; max RMSE 0.00000061m
- FourStageRoundTower: 100/100 valid; median RMSE 0.00000039m; max RMSE 0.00000078m
- TwoGableHouses: 100/100 valid; median RMSE 0.00000059m; max RMSE 0.00000090m
