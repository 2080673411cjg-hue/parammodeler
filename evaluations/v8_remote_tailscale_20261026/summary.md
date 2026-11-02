# v8 接入及真实远程补测（2026-10-26）

十三类均已核对真实 /regress 响应的 model_log_dir 为对应 v8 目录。请求显式传入 _v8_normals；远程服务当时的默认后缀仍为 v4，默认值不代表本次实际模型。

样本为每类小、中、大各10份，共390份；另外加入全部CPU朝向异常和5份既有L形样本，去重后401份。两类结果分开统计，额外异常不会混入分尺度抽样均值。此处不冒充完整2600份网络测试。

全部2600份CPU朝向补查另见 ../v8_orientation_cpu_20261026/summary.md；HTTP成功返回也不能代表全部形状贴合通过。

|类别|分尺度样本成功/计划|MAE：参数名与实际远程误差|
|---|---:|---|
|Cuboid|30/30|geom_footprint_long=0.45370；geom_footprint_short=0.36274；geom_total_height=0.14960|
|Cylinder|30/30|radius=0.04717；height=0.02563|
|LHouse|30/30|outerLength=0.28086；outerWidth=0.25859；cutoutLengthRatio=0.00673；cutoutWidthRatio=0.00900；height=0.13756|
|ConeCylinder|30/30|radius=0.11023；totalHeight=0.05857；cylinderRatio=0.00731|
|GabledRoof|30/30|length=0.49322；width=0.41246；totalHeight=0.18842；wallRatio=0.01031；ridgeRatio=0.00984|
|PyramidRoof|30/30|geom_footprint_long=0.43348；geom_footprint_short=0.40756；geom_total_height=0.15191；wallRatio=0.01300|
|TruncatedPyramidRoof|30/30|geom_footprint_long=0.41824；geom_footprint_short=0.30933；geom_top_long_ratio=0.01257；geom_top_short_ratio=0.01279；geom_total_height=0.21662；wallRatio=0.01279|
|HalfCylinderRoof|30/30|length=0.48141；width=0.23211；wallHeight=0.11528|
|CylinderDome|30/30|radius=0.10146；totalHeight=0.05243；cylinderRatio=0.01084；bulge=0.03742|
|IndentedCuboid|30/30|geom_footprint_long=0.48121；geom_footprint_short=0.39647；geom_total_height=0.16161；innerHeight=0.16191；geom_long_start_margin_ratio=0.00719；geom_long_end_margin_ratio=0.01042；geom_short_start_margin_ratio=0.01001；geom_short_end_margin_ratio=0.00926|
|AsymmetricGableHouse|30/30|length=0.46673；width=0.28883；totalHeight=0.15829；wallRatio=0.01133；ridgeLength=0.60348；ridgeRatio=0.01496|
|FourStageRoundTower|30/30|baseRadius=0.29009；baseHeight=0.20289；middleHeight=0.13276；middleTopRadius=0.22351；middleRise=0.03780；coneHeight=0.20805；eaveRadius=0.27893；eaveHeight=0.12506|
|TwoGableHouses|30/30|length1=1.38852；length2=1.17253；width=0.24930；totalHeight=0.09725；wallRatio=0.00946；angle=2.93662；ridgeRatio=0.02727|

全部HTTP补测：401份，400份成功返回，1份失败；分尺度390份之外另有11份。
耗时 19.6 分钟，单请求中位数 5.84 秒，两并发。

尺寸单位为米，比例和曲线系数无量纲，角度为度。stratified_summary.csv 将同一批文件在已知metadata朝向下的v8误差与真实远程几何朝向误差并列；两者使用各自相同坐标约定的真值，不能把不同坐标的参数直接相减。单云API与原批量测试的批次及随机状态不同，小幅差异不单独归因于朝向处理。

**额外异常与失败**

- `test/AsymmetricGableHouse/sample_00907.txt`：CPU轴误差90.000°；HTTP成功=True；ridgeRatio: true=0.74000, pred=0.39031; ridgeLength: true=9.70000, pred=2.52508; length: true=13.30000, pred=10.71002; width: true=10.87000, pred=8.69114
- `test/AsymmetricGableHouse/sample_00972.txt`：CPU轴误差89.543°；HTTP成功=True；ridgeRatio: true=0.62000, pred=0.42307; ridgeLength: true=14.26000, pred=7.97002; length: true=29.90000, pred=24.40318; width: true=15.09000, pred=23.57209
- `test/AsymmetricGableHouse/sample_01086.txt`：CPU轴误差90.000°；HTTP成功=True；ridgeRatio: true=0.29000, pred=0.30666; ridgeLength: true=21.46000, pred=13.91132; length: true=29.30000, pred=33.98624; width: true=28.61000, pred=28.61482
- `test/Cuboid/sample_01015.txt`：CPU轴误差89.853°；HTTP成功=True；
- `test/Cuboid/sample_01061.txt`：CPU轴误差89.378°；HTTP成功=True；
- `test/HalfCylinderRoof/sample_00992.txt`：CPU轴误差90.000°；HTTP成功=True；length: true=13.77000, pred=10.60716; width: true=16.50000, pred=14.28305; wallHeight: true=2.90000, pred=6.12259
- `test/IndentedCuboid/sample_01068.txt`：CPU轴误差89.000°；HTTP成功=True；
- `test/PyramidRoof/sample_01087.txt`：CPU轴误差41.500°；HTTP成功=True；
- `test/LHouse/sample_00978.txt`：ValueError: Canonical orientation is uncertain for this cloud: LHouse needs three supported roof corners and one distinct missing corner


所有成功响应均已验证实际v8目录；canonical_alignment.applied只代表流程实施了规范旋转，不保证旋转角正确。近正方形长宽轴交换可能几何等价，屋顶的90°错轴则须结合屋脊方向判定。

本轮只接入版本并测评，没有修改朝向算法、参数预测、几何校正、训练数据或旧权重，也没有编译插件DLL或重启Ubuntu服务。

后续优先处理：非对称人字形/半圆柱的少数错轴，L形sample_00978的判向失败；双人字形未参与canonical判向，不能把SKIPPED_CLASS误认为方向正确。
