# ParamModeler 深度学习完整流程

## 分类与十三类回归v9部署（2026-11-02）

本机分类目录及“恢复默认值”切到`ptv3_cls_v9`，十三类回归后缀为`_v9_normals`。新配置首次使用会将已有分类目录和回归后缀一次性切到v9，之后用户的手动修改继续生效。用户自行编译`plugin_parammodeler`，重启QGIS后在“PointNet路径设置”确认这两项。

Ubuntu已有本轮训练使用的`main.py`、`main_reg.py`、`canonical_orientation.py`和PCL库，本次只需覆盖服务入口。Windows CMD：

```cmd
scp "E:\pointnet\ptv3_simple\ptv3_server.py" xubo@100.70.215.84:/home/xubo/pointnet/ptv3_simple/
```

在原服务终端按Ctrl+C停止旧服务，再激活训练环境并重启。下面明确指定v9，并清空圆塔/人字形旧的单类环境覆盖；不会重新训练：

```bash
conda activate ptv3_train && cd /home/xubo/pointnet/ptv3_simple && PTV3_CLS_LOG=logs/ptv3_cls_v9 PTV3_REG_SUFFIX=_v9_normals PTV3_TOWER_LOG= PTV3_GABLED_LOG= python -m uvicorn ptv3_server:app --host 0.0.0.0 --port 8008
```

Windows CMD确认服务配置：

```cmd
curl http://100.70.215.84:8008/health
```

`cls_log`应以`ptv3_cls_v9`结尾，`reg_suffix`为`_v9_normals`，`tower_reg_log`和`gabled_reg_log`也应为v9。health只确认配置；实际回归成功响应的`model_log_dir`应指向相应`ptv3_reg_<类别短名>_v9_normals`。新权重按checkpoint使用PCL法向，沿用当前`pcl_normals/libpcl_normals.so`，无需重新增强或补算法向。

37项相关CPU检查及配置文件MSVC语法检查通过；Windows实际服务脚本已同步。本次未SSH或重启Ubuntu服务，未构建插件DLL、未执行远程GPU推理；上述步骤由用户在对应机器执行。历史记录和训练命令保留在下方，当前部署目标以本节为准。

## 分类与参数回归v9完整训练（2026-10-30）

本轮使用已有增强数据，每类1600/200/200，十三类全部训练100轮，排除三棱柱。分类batch=8、回归batch=4、seed=42、2048点，使用PCL永久法向缓存，FP32训练。五轮对照中AMP平均每轮慢约3.6%，本轮关闭。数据、损失和网络保持当前配方；L形沿用完整朝向规范化、物理辅助尺度和稳健包围盒，截顶屋顶沿用尺度平衡损失及选模。

批量入口现在允许全类别训练时按类别启用上述两项配置，并只给L形和截顶屋顶增加geometry朝向测试。三项相关命令生成检查及本地13类1600/200/200、圆塔/人字形标签、截顶尺度分档预检通过。Windows实际`train_ptv3_reg.py`已同步；Ubuntu只需补传这个入口，其他PCL/AMP脚本和SO已在前面步骤部署。

Windows CMD：

```cmd
scp "E:\pointnet\ptv3_simple\train_ptv3_reg.py" xubo@100.70.215.84:/home/xubo/pointnet/ptv3_simple/
```

Ubuntu激活环境并进入目录：

```bash
conda activate ptv3_train && cd /home/xubo/pointnet/ptv3_simple
```

下面一行依次完成十三类回归训练及各类测试、分类训练和分类测试；前一步失败时停止，所有输出使用新v9目录。脚本拒绝覆盖已有非空权重目录。

```bash
nohup sh -c 'python -u train_ptv3_reg.py --data_root /home/xubo/pointnet/datasets_aug --run_tag v9 --epochs 100 --batch_size 4 --seed 42 --num_workers 2 --normal_backend pcl --normal_threads 4 --lhouse_canonical --lhouse_robust_bbox --truncated_scale_balanced --expected_split_counts 1600 200 200 && python -u main.py --mode train --data_root /home/xubo/pointnet/datasets_aug --log_dir logs/ptv3_cls_v9 --epochs 100 --batch_size 8 --num_points 2048 --use_normals --normal_backend pcl --normal_threads 4 --seed 42 --num_workers 2 --no-amp && python -u main.py --mode test --data_root /home/xubo/pointnet/datasets_aug --log_dir logs/ptv3_cls_v9 --split test --batch_size 8 --seed 42 --num_workers 2 --normal_threads 4' > logs/ptv3_v9_train.log 2>&1 &
```

```bash
tail -f /home/xubo/pointnet/ptv3_simple/logs/ptv3_v9_train.log
```

分类权重保存在`logs/ptv3_cls_v9/`，回归保存在`logs/ptv3_reg_<类别简称>_v9_normals/`。需要用户在Ubuntu执行上述命令；本地仅完成脚本与数据预检，未启动远程训练，也未提前切换插件模型。完成后按同一测试集比较v8与v9。

## 分类AMP训练与每轮计时（2026-10-30）

分类入口`main.py`增加可选`--amp`：CUDA训练使用float16 autocast和GradScaler；默认关闭，可用`--no-amp`明确关闭。梯度裁剪前先unscale，再由scaler更新参数和缩放值；遇到非有限训练/验证loss停止本次运行。验证、独立测试和单云预测使用FP32，权重仍保存FP32，选模继续按验证准确率优先、相同则比较验证loss。配置和checkpoint记录AMP设置。

每轮结束新增`epoch_time`、`peak_gpu_allocated`、`peak_gpu_reserved`，AMP开启时同时打印`amp_scale`。耗时包含该轮训练、验证和模型保存，不含点云/法向准备；GPU在计时边界同步。显存为PyTorch分配器统计，allocated表示实际分配、reserved表示保留，不代表整卡占用。CPU运行显示N/A，显式要求AMP但没有CUDA时直接报错。

本次只接入分类，回归仍使用FP32。2048点、batch=8、学习率、Flash开关和数据采样规则沿用原设置。12项分类CPU检查及6项PCL检查通过，1项真实CUDA FP16/GradScaler检查因本机无CUDA跳过；Ubuntu的PTv3/spconv兼容性与提速幅度待实际试跑，不能写成已验证加速。

Windows实际目录的`main.py`已同步。本次Ubuntu只需再覆盖这一份文件；前面PCL相关文件已同步、SO已构建时无需重做。Windows CMD：

```cmd
scp "E:\pointnet\ptv3_simple\main.py" xubo@100.70.215.84:/home/xubo/pointnet/ptv3_simple/
```

Ubuntu进入`/home/xubo/pointnet/ptv3_simple`并激活`ptv3_train`。已有26000份PCL缓存可复用（用户实测首次计算和写入共107.95秒），无需重新增强或补算法向。建议先串行各跑5轮：两边都使用PCL，固定点数、batch和seed，仅改变AMP开关，以免并行争抢GPU影响比较。以下一行先FP32、成功后再AMP，日志保存在同一文件：

```bash
nohup sh -c 'python -u main.py --mode train --data_root /home/xubo/pointnet/datasets_aug --log_dir logs/ptv3_cls_amp_fp32_trial --epochs 5 --batch_size 8 --num_points 2048 --use_normals --normal_backend pcl --normal_threads 4 --seed 42 --num_workers 2 --no-amp && python -u main.py --mode train --data_root /home/xubo/pointnet/datasets_aug --log_dir logs/ptv3_cls_amp_fp16_trial --epochs 5 --batch_size 8 --num_points 2048 --use_normals --normal_backend pcl --normal_threads 4 --seed 42 --num_workers 2 --amp' > logs/ptv3_cls_amp_compare.log 2>&1 &
```

```bash
tail -f /home/xubo/pointnet/ptv3_simple/logs/ptv3_cls_amp_compare.log
```

AMP阶段应显示`Training precision: AMP float16 | validation: FP32`。比较第2～5轮的耗时、显存和loss稳定性；5轮仅用于速度和兼容性试验，最终精度需要完整训练对照。新输出目录不覆盖旧权重；当前插件/服务模型选择保持原配置。后续正式分类训练命令加`--amp`即可，独立测试命令不加该标志。

## PCL法向缓存接入（2026-10-30）

流程为：插件导出XYZ → 原`E:/pointnet/ptv3_simple/augment_dataset.py`完成增强 → PCL计算并保存法向 → 分类/回归读取缓存。已有增强数据只需补算法向，不必重新生成或增强；TXT和参数标签保持原样，三棱柱不参与。增强入口本身未改，新增一次独立的法向预处理。

缓存位于增强数据根目录`.normal_cache/pcl_v1/`，按split/类别/文件名保存NPZ。每项包含法向、归一化源XYZ的SHA256、算法标识和PCL版本；匹配才复用，过期或损坏项重新计算并原子替换。重新运行会打印`hit`和`computed`，以及进度、耗时、ETA。新增/重新增强的样本只会重算相应缓存；完整覆盖整个输出目录会同时移除旧缓存。

PCL采用原16近邻、排除自身、相对质心定向。分类与回归共用归一化源坐标的法向；canonical回归再同步旋转法向，法向不做平移。在线采样和旋转仍使用同一组点索引和旋转矩阵，jitter保持原有XYZ扰动规则。PCL使用单进程加OpenMP线程，`--normal_workers`仍只控制原NumPy算法，避免两层并行叠加。

训练时显式加`--normal_backend pcl --normal_threads 4`，并保留`--use_normals`；配置和checkpoint记录后端。默认仍为NumPy。测试与单云预测读取checkpoint中的后端，旧权重缺少该字段时继续原NumPy算法；新PCL权重推理也计算PCL法向，不会静默降级。缓存与本机PCL版本不一致时重算，并在日志显示版本。

**Ubuntu部署：**Windows实际目录已同步三份入口及`pcl_normals/`。在Windows CMD将它们传到Ubuntu，目录不含训练权重：

```cmd
scp "E:\pointnet\ptv3_simple\main.py" "E:\pointnet\ptv3_simple\main_reg.py" "E:\pointnet\ptv3_simple\train_ptv3_reg.py" xubo@100.70.215.84:/home/xubo/pointnet/ptv3_simple/
scp -r "E:\pointnet\ptv3_simple\pcl_normals" xubo@100.70.215.84:/home/xubo/pointnet/ptv3_simple/
```

在Ubuntu激活`ptv3_train`、进入`/home/xubo/pointnet/ptv3_simple`。系统需有PCL开发库、CMake及C++编译器；若CMake报告缺PCL，可先安装`sudo apt install libpcl-dev cmake build-essential`。构建的是独立法向库，与QGIS插件编译无关，动态库放回脚本目录供后续训练和服务读取：

```bash
cmake -S pcl_normals -B /tmp/ptv3_pcl_normals_build -DCMAKE_BUILD_TYPE=Release -DCMAKE_LIBRARY_OUTPUT_DIRECTORY="$PWD/pcl_normals"
cmake --build /tmp/ptv3_pcl_normals_build --parallel 2
python -u -m pcl_normals --data_root /home/xubo/pointnet/datasets_aug --threads 4
```

以后现有分类或回归**训练**命令加`--normal_backend pcl --normal_threads 4`即可读取这些缓存。不必为了验证缓存立刻重训全部模型，也不会因此切换当前插件权重。Ubuntu需自行同步、构建和执行预处理；本次未运行完整GPU训练或固定v8权重的PCL精度对照。

**Windows本机预处理可选：**已将实测DLL放在`E:/pointnet/ptv3_simple/pcl_normals/`。在Windows CMD、`pointnet_train`环境和该PTv3目录内执行：

```cmd
set "PTV3_PCL_DLL_DIR=E:\mambaforge\envs\polyfit_env\Library\bin"
python -u -m pcl_normals --data_root E:\pointnet\datasets_aug --threads 4
```

两端PCL版本不同时缓存会重算，建议在训练用的Ubuntu端生成缓存，避免额外传输。6项新增检查通过，含真实PCL库、缓存命中/失效、canonical法向旋转、旧checkpoint兼容，以及六通道小模型训练与推理；原有20项加载和法向准备检查通过。此验证不等于完整网络精度已验证。

## PCL法向独立试验（2026-10-30）

以下保留接入前的独立试验记录，用于验证PCL计算速度和与现有法向的数值差异。通过ctypes在内存中传递点坐标，无需额外Python绑定包；当时尚未修改生产入口。计算采用PCL KD-tree、computePointNormal和OpenMP，显式排除查询点自身，并保留原有相对质心定向规则。

本机PCL 1.8.1、十三类共130份点云、三次中位数：原NumPy23.356秒，PCL单线程0.601秒、2线程0.404秒、4线程0.270秒。近邻几何集合相同，无无效法向和符号翻转；夹角P95约0.004682度，最大1.371501度，3个点超过1度。完整设置和限制见 `evaluations/pcl_normals_20261030/summary.md`，不能把此处倍数当作完整训练加速比例。

在Ubuntu将本目录的`pcl_normals`文件夹复制到`/home/xubo/pointnet/ptv3_simple/`，与`main_reg.py`同级。需要系统已有PCL开发包、CMake和支持OpenMP的C++编译器。在该PTv3目录、激活`ptv3_train`后构建独立动态库：

```bash
cmake -S pcl_normals -B /tmp/ptv3_pcl_normals_build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/ptv3_pcl_normals_build --parallel 2
python -u pcl_normals/benchmark.py --library /tmp/ptv3_pcl_normals_build/libpcl_normals.so --data-root /home/xubo/pointnet/datasets_aug --output evaluations/pcl_normals_cpu_trial --per-class 10 --repeats 3 --threads 1 2 4
```

脚本从同级生产`main_reg.py`提取原读取、归一化和法向函数；保留固定输入哈希，轮换方法执行顺序。结果目录非空时拒绝覆盖。线程数比较针对单Python进程，未叠加外层进程池。缓存接口现已接入，固定已有分类v8权重的精度对照仍待Ubuntu运行。

## 分类 v7 部署（2026-10-29）

分类v7已训练并取回Windows：最佳第87轮，验证2598/2600正确（99.92%），测试2587/2600正确（99.50%）。测试主要错例为长方体→L形8份、L形→长方体1份、金字塔→非对称人字形2份、穹顶→半圆柱1份、圆塔→圆锥圆柱1份；两类人字形各200份全对，L形00978分类正确。v6报告的1300份与本轮2600份没有同类别/文件名交集，不直接用准确率差判断版本优劣。

当前部署状态：用户已同步下列四份脚本，服务health确认分类v7、回归v8；实际POST `/classify`使用L形00978返回LHouse，概率95.52%，单次请求总耗时约5.70秒。该耗时含服务流程，不作为纯GPU推理速度；QGIS显示和新一轮参数回归仍需单独验收。以下命令保留用于后续重新部署。

插件本机默认目录与恢复默认值更新为 `logs/ptv3_cls_v7`，旧v2/v5/v6默认目录只迁移一次；自定义目录及迁移后的手动改动保留。服务默认分类同步改为v7，显式 `PTV3_CLS_LOG` 仍优先。回归继续v8，分类推理从checkpoint读取类别顺序。本次不编译DLL；远程分类切换在服务同步重启后生效，本机默认迁移由用户编译插件后生效，也可先在路径窗口手动选择v7。

用户编译时遇到的C2664已在源码修复：v7迁移路径先保存成QString变量，再交给setValue，避免QStringBuilder隐式转换失败。此修复仅涉及`parammodeler_config.cpp`，待用户重新生成插件确认，不需要因此重新传Ubuntu文件。

在Windows CMD同步上一步法向优化的三份脚本和本次服务脚本：

```cmd
scp "E:\pointnet\ptv3_simple\main.py" "E:\pointnet\ptv3_simple\main_reg.py" "E:\pointnet\ptv3_simple\train_ptv3_reg.py" "E:\pointnet\ptv3_simple\ptv3_server.py" xubo@100.70.215.84:/home/xubo/pointnet/ptv3_simple/
```

Ubuntu训练就在该机完成时，保留 `logs/ptv3_cls_v7/best_model.pth` 和 `classification_config.json`，无需重传权重。在原服务终端Ctrl+C后，运行以下一行，显式指定v7以免旧分类环境变量覆盖：

```bash
cd /home/xubo/pointnet/ptv3_simple && PTV3_CLS_LOG=logs/ptv3_cls_v7 /home/xubo/anaconda3/envs/ptv3_train/bin/python -m uvicorn ptv3_server:app --host 0.0.0.0 --port 8008
```

另一个终端用 `curl -s http://127.0.0.1:8008/health` 核对 `cls_log` 结尾为 `/logs/ptv3_cls_v7`。health确认配置，随后在插件加载 `test/LHouse/sample_00978.txt` 验证界面中的分类与叠加。此前判向修复的 `canonical_orientation.py` 若尚未同步，也须补传后再检查参数/对齐；本次四文件同步不包含它，不能据此认定判向修复已部署。下方旧部署记录按日期保留，当前分类以本节为准。

```cmd
scp "E:\pointnet\ptv3_simple\canonical_orientation.py" xubo@100.70.215.84:/home/xubo/pointnet/ptv3_simple/
```

插件复测优先顺序：先看非对称人字形907/972/1086、半圆柱992、L形978/1023、金字塔1087；再抽查双人字形两段长度和转角、圆塔檐口与中段细部、凹陷长方体内槽和普通人字形屋脊偏移。L形1023在v7测试报告中仍被分类成长方体，应先确认类别再判断回归。每份样本分开记录分类、姿态位置和形状贴合，初次检查关闭几何校正，并观察RMSE/P95。v8报告及测试朝向说明见仓库`evaluations/v8_training_review_20261026.md`，已知朝向的参数MAE不等于插件最终贴合误差。

## 法向并行预处理与计时（2026-10-29）

分类 `main.py`、回归 `main_reg.py` 及批量入口 `train_ptv3_reg.py` 新增独立的 `--normal_workers`。Ubuntu默认2个法向计算进程，Windows默认0；0或1为串行，可显式设置4测试多核收益。它与训练数据加载的 `--num_workers` 分别控制两个阶段。单云推理仍直接计算，不创建进程池。

每份点云沿用原16邻居PCA法向算法、归一化与朝向约定；按原样本顺序收集，只预提交至多两倍进程数的任务，避免把整套数据同时排入进程池。仍在每次训练/测试启动时重新计算，结果仅在当次RAM缓存；在线采样和jitter继续生效，未加入AMP或磁盘缓存。

法向阶段日志新增进程数、完成比例、elapsed、ETA及clouds/s，结束打印总耗时（时分秒与秒数）。计时包含该阶段的进程启动、未缓存坐标读取/归一化及结果收集；回归在此之前完成的坐标/标签准备不计入法向阶段。ETA由当前平均速度估算，开头受进程启动影响可能偏大。

同步Windows实际目录下三份文件到Ubuntu `/home/xubo/pointnet/ptv3_simple/`：`main.py`、`main_reg.py`、`train_ptv3_reg.py`。下次原有训练命令默认生效；`--normal_workers 0`可对比串行耗时，显式`--normal_workers 2`可在两边使用相同设置。无需重新生成/增强数据或编译插件；本次不切换分类/回归权重。

19项相关检查通过，含真实spawn对照与计时。13类各10份共130份真实点云的本机单次CPU小测，串行36.09秒、双进程32.12秒，法向哈希全部一致；含进程启动，且受执行顺序和文件缓存影响，不外推Ubuntu或完整训练收益。记录在仓库 `evaluations/normal_preparation_20261029/`。

## 推理判向修复与本轮同步（2026-10-26）

本轮仅修改Python，无需重新编译插件，也不需要重训回归或重新增强数据。已将分类提速入口和判向修复同步到Windows实际目录；从Windows CMD把以下三份同步到Ubuntu：

```cmd
scp "E:\pointnet\ptv3_simple\main.py" "E:\pointnet\ptv3_simple\main_reg.py" "E:\pointnet\ptv3_simple\canonical_orientation.py" xubo@100.70.215.84:/home/xubo/pointnet/ptv3_simple/
```

随后在原8008服务窗口Ctrl+C，激活 `ptv3_train` 后按本页v8服务命令重新启动，再复测之前六份点云。回归继续使用v8；分类v7尚未训练，当前v6分类保持原状。L形978判向修复的前提是指定/识别为LHouse，不能把它当作旧分类器已修好。

屋顶判向改为比较两个垂直轴在上表面的相对覆盖，裁去少数Z/XY极值，证据不足明确返回不确定。L形原缺角检测失败时，用上层40%点的外框重试，保留原角点支持条件；金字塔的上层屋面与全云外框明显冲突时，用屋面方向修正斜向外框。公共坐标和标签约定不改。

六份指定点云已在本地修正：非对称907/972/1086分别约0°/0.46°/0°，半圆柱992为0°，L形978由失败变为2°，金字塔1087由41.5°变为0.5°。五类测试/验证共2000份复测无新增判向失败、已接受样本无角误差退步，另80份其他类别接口保持原结果；105项相关CPU检查通过。最终逐样本及限制见仓库 `evaluations/2026-10-26-roof-axis-fix/integrated/summary.md`。尚未部署后重新运行网络，不能把角度改善写成新的参数精度；金字塔验证集仍有3份90°轴差，其中一份为正方形，报告已单列。

## 分类 v7 训练与数据加载提速（2026-10-26）

分类实际入口为 `E:/pointnet/ptv3_simple/main.py`，对应仓库 `scripts/main.py`。点云固定归一化后的XYZ默认缓存在RAM，法向从同一份坐标估计并缓存；每次取样仍重新执行采样、旋转开关和jitter，增强不写回缓存。Ubuntu默认2个持久加载进程、spawn、每进程预取2批，CUDA启用锁页内存；复用 `main_reg.py` 的加载器和worker种子。缓存开关在同一加载配置下已验证输入逐元素一致；多进程随机序列与旧单进程不保证相同。

仅新增数据准备优化，不改分类网络、batch=8、2048点、100轮、学习率、损失、标签平滑、原旋转开关或选模标准。最佳模型仍按验证准确率优先，准确率相同则比较验证loss。配置和checkpoint记录data_loading；日志新增训练总耗时（含点云/法向准备、验证和保存，不含结束后测试）。可用 `--num_workers 0 --no-cache_points` 回退。

训练发现类别时明确排除TriPrismPyramid，包括清单和文件夹两条发现路径，不修改原清单或数据。当前实际预检为13类，每类1600/200/200，总计20800训练、2600验证、2600测试。8项新增CPU检查通过，包括缓存一致性与只读、在线增强、真实spawn双进程两轮、排除三棱柱和真实小模型训练；未运行完整GPU分类训练，不承诺GPU提速比例。

将Windows下 `main.py` 和 `main_reg.py` 同步到Ubuntu `/home/xubo/pointnet/ptv3_simple/`；无需重新生成或增强点云。进入该目录并激活 `ptv3_train` 后，用新目录保存为分类v7，保留当前v6：

```bash
nohup sh -c 'python -u main.py --mode train --data_root /home/xubo/pointnet/datasets_aug --log_dir logs/ptv3_cls_v7 --use_normals --seed 42 --num_workers 2 && python -u main.py --mode test --data_root /home/xubo/pointnet/datasets_aug --log_dir logs/ptv3_cls_v7 --split test --seed 42 --num_workers 2' > logs/ptv3_cls_v7_train.log 2>&1 &
```

```bash
tail -f /home/xubo/pointnet/ptv3_simple/logs/ptv3_cls_v7_train.log
```

启动日志应显示 `classes=13`、`Train samples=20800 Val samples=2600` 和缓存/加载配置。训练失败时不继续测试，已有v7目录非空时拒绝覆盖。当前插件和服务分类仍为v6；待v7训练及新测试集评估完成再切换，不能用旧1300份测试的99.85%替代当前数据精度。L形00978保留为新分类重点复测样本，不修改测试标签或移动到训练集。

## 十三类回归 v8 接入与补测（2026-10-26）

插件首次使用本次更新时，将已有 PTv3 回归后缀迁移为 `_v8_normals`；之后在窗口中手动更改后缀会保留。十三类本机目录和远程请求统一使用该后缀，不再给 LHouse 固定实验版本，也不再让插件端 `PTV3_TOWER_LOG` 绕过窗口设置。服务的显式 `model_suffix` 优先于旧的圆塔/人字形环境覆盖。分类保持 v6；旧权重、数据、训练和测试报告均保留。需要用户自行编译插件 DLL。

Windows 实际推理目录已同步 `ptv3_server.py` 和新入口 `evaluate_ptv3_reg.py`。将这两个文件覆盖到 Ubuntu 的同名目录；批量入口还复用该目录已有的 `main_reg.py`、`train_ptv3_reg.py` 和 `compare_ptv3_reg.py`。后者来自先前同集比较，若 Ubuntu 缺少它，也从 Windows 同步。

Windows CMD 同步两份更新：

```cmd
scp "E:\pointnet\ptv3_simple\ptv3_server.py" "E:\pointnet\ptv3_simple\evaluate_ptv3_reg.py" xubo@100.70.215.84:/home/xubo/pointnet/ptv3_simple/
```

Ubuntu 原服务窗口按 Ctrl+C 停止旧服务，再启动；不要在其他训练环境中安装或运行服务：

```bash
conda activate ptv3_train && cd /home/xubo/pointnet/ptv3_simple && PTV3_REG_SUFFIX=_v8_normals PTV3_TOWER_LOG= PTV3_GABLED_LOG= python -m uvicorn ptv3_server:app --host 0.0.0.0 --port 8008
```

`/health` 的 `reg_suffix` 应显示 `_v8_normals`。每次真实回归响应的 `model_log_dir` 才是该次实际使用的目录，应以 `_v8_normals` 结尾；旧服务默认仍为 v4 时，明确请求 v8 也能选中新权重，不能只看默认健康状态判断本次模型。

全量 GPU 补测不需要服务进程，直接在 Ubuntu 的 `ptv3_train` 环境下、上述工作目录执行：

```bash
nohup python -u evaluate_ptv3_reg.py --data_root /home/xubo/pointnet/datasets_aug --run_tag v8 --output_root evaluations/ptv3_v8_geometry > logs/ptv3_reg_v8_geometry.log 2>&1 &
```

查看进度：

```bash
tail -f /home/xubo/pointnet/ptv3_simple/logs/ptv3_reg_v8_geometry.log
```

入口预检查十三类各200份测试点云、v8 checkpoint 和配置，再串行运行13次 `main_reg.py --mode test --test_orientation geometry`，不训练、不修改权重、不覆盖原报告。新结果保存到 `evaluations/ptv3_v8_geometry/<类别>/`，汇总在 `summary.csv`，完成后日志出现 `ALL_V8_GEOMETRY_DONE`、manifest 状态为 `complete`。结果目录非空时拒绝覆盖；再次执行请换新的 `--output_root` 和日志名。失败时 manifest 记录失败及已完成类别，原始错误在对应类别日志。

CPU 几何朝向检查已实际读取全部2600份点云，结果在仓库 `evaluations/v8_orientation_cpu_20261026/`。1599份被接受、1份 L形无法判向、1000份明确跳过旋转；其中圆形基元因旋转对称跳过，双人字形因不在朝向白名单跳过，不计为判向成功。非对称人字形3份和半圆柱1份有接近90°错轴，即使算法接受，也不代表朝向正确。该检查未运行网络，不能代替上述完整 GPU 参数测试。

另通过真实 Tailscale `/regress` 接口抽取每类每尺度10份，加上全部朝向异常和5份既有 L形复测样本，去重后401份已全部完成，耗时19.6分钟：400份成功返回、L形 `sample_00978` 无法判向。常规分尺度390份全部成功，额外11份单独统计；每份保留原始响应并验证实际权重目录为 v8，记录在仓库 `evaluations/v8_remote_tailscale_20261026/`，报告见 `summary.md`，逐参数及同文件metadata比较见 `stratified_summary.csv`。这是分尺度抽样与异常复现，不能当作完整2600份网络测试，也不能把小幅单云/批量差异全部归因于朝向。

少数异常已在真实服务复现：非对称 `sample_00907` 真值屋脊长9.7米，错轴后预测约2.53米；另两份非对称和一份半圆柱也有90°错轴。`canonical_alignment.applied=true` 只代表实施了旋转，不保证旋转角正确。当前只接入权重并测评，未修改朝向规则；下一步应先处理这些少数异常，不能据成功返回数宣称全部贴合正确。

## 回归点云缓存与并行加载（2026-10-25）

仅优化回归训练/批量测试的数据准备，不改batch、2048点、轮数、网络、精度、目标、损失或选模，分类和插件不变。`main_reg.py` 默认将固定朝向处理与归一化后的XYZ缓存在RAM，原有法线缓存复用同一份固定点云；每次取样仍重新做随机采样、旋转和jitter，缓存设为只读，增强不会写回。无磁盘缓存，也不改数据文件。可用 `--no-cache_points` 回退原按需读取方式。

`train_ptv3_reg.py` 在Ubuntu默认使用2个数据加载进程，Windows默认0；参数传给训练及后续两种测试。加载进程采用spawn隔离CUDA父进程、持久保留并预取2批；CUDA路径启用pin_memory和non_blocking传输，CPU不启用pin_memory。`main_reg.py` 单独调用时仍默认0进程。可用 `--num_workers 0` 保留缓存但关闭多进程；进程数并非越大越快，需要按实际GPU/CPU负载选择。

多进程使用显式Torch生成器，并按worker的Torch初始种子设置NumPy/Python种子；相同加载配置和seed下的数据采样可复现，但其随机序列不承诺与旧单进程完全一致，也不保证GPU算子的逐位确定性。配置和checkpoint新增 `data_loading` 记录运行设置。缓存开关在相同加载配置和随机状态下已通过逐元素一致性测试；`--num_workers 0 --no-cache_points` 可回退原读取和主进程随机路径。

将 `E:/pointnet/ptv3_simple/main_reg.py`、`E:/pointnet/ptv3_simple/train_ptv3_reg.py` 同步到Ubuntu对应目录即可，不需重新增强、同步数据或重启服务。若已有训练在跑，应等它完成，不要为应用这次优化中断或覆盖运行目录。下一轮可用独立输出名，例如：

```bash
nohup python -u train_ptv3_reg.py --classes TruncatedPyramidRoof --data_root /home/xubo/pointnet/datasets_aug --run_tag v7_scale_data1600_fast --truncated_scale_balanced --num_workers 2 --expected_split_counts 1600 200 200 > logs/ptv3_reg_truncatedpyramid_v7_scale_data1600_fast_train.log 2>&1 &
```

日志将显示 `Cached points ...` 和 `Data loading ... pin_memory=True`。回退只需将进程数设为0，并使用新的空输出目录；已有非空权重目录仍拒绝覆盖。训练脚本不自动中断或替换旧任务。

新增7项缓存/加载测试，包括朝向、归一化与法线组合下的逐元素对照、读取次数、缓存不可变与在线增强、参数回退、worker种子和真实2个spawn进程连续两轮复现；连同原有104项检查共111项通过。4份真实截顶点云的40次CPU取样微测，未缓存约5.32毫秒/份、缓存约0.09毫秒/份，固定输入逐元素相同；这只测数据准备，不代表整轮GPU训练加速倍数。当前本机无CUDA/spconv，实际吞吐需Ubuntu测量。

## 截顶增量增强恢复与1600/200/200训练（2026-10-25）

用户本次实际追加800训练、100验证、100测试，最终使用1600/200/200，按最新确认不再限定本轮验证/测试各100份。仍不生成或增强三棱柱。原增量脚本在1000份新点云增强完成后，误检查未选中的三棱柱旧标签对应文件，导致标签同步中断；不是截顶点云增强失败。

`augment_added_samples.py` 已改为只验证/合并所选类别；未选类别的旧记录保留，不要求补齐其点云，也不把未选类别新标签直接拷入增强清单。默认类别排除TriPrismPyramid。新增 `--splits train` 可用于以后仅扩训练集，本轮使用默认全部split，纳入用户新增的验证/测试样本。已有选中类别标签若被改写会停止，避免旧增强点云匹配新标签。

本次已复用用户增强好的1000份点云恢复标签同步，未重做、删除或改写点云，其他类别标签未变；原100份截顶测试点云SHA256和标签与v7快照仍相同。最终metadata和单类清单均为1600训练/200验证/200测试，训练预检查及46项相关测试通过。以后继续追加本类时仍可运行已有命令：

```cmd
python "E:\QGIS_Source\QGIS-final-3_44_2\QGIS-final-3_44_2\src\plugins\parammodeler\scripts\augment_added_samples.py" --input E:\pointnet\datasets --output E:\pointnet\datasets_aug --classes TruncatedPyramidRoof --augment-module E:\pointnet\ptv3_simple\augment_dataset.py --holes
```

此次已经恢复成功，无需再运行增强。将 `datasets_aug/train/TruncatedPyramidRoof/`、`val/TruncatedPyramidRoof/`、`test/TruncatedPyramidRoof/` 与增强目录下 `metadata/sample_params.json`、`metadata/sample_params_TruncatedPyramidRoof.json` 同步至Ubuntu对应位置；不要用原始datasets的metadata代替增强清单。沿用上一轮训练脚本，无需重启推理服务。

Ubuntu激活 `ptv3_train`、进入 `/home/xubo/pointnet/ptv3_simple` 后运行：

```bash
nohup python -u train_ptv3_reg.py --classes TruncatedPyramidRoof --data_root /home/xubo/pointnet/datasets_aug --run_tag v7_scale_data1600 --truncated_scale_balanced --expected_split_counts 1600 200 200 > logs/ptv3_reg_truncatedpyramid_v7_scale_data1600_train.log 2>&1 &
```

```bash
tail -f /home/xubo/pointnet/ptv3_simple/logs/ptv3_reg_truncatedpyramid_v7_scale_data1600_train.log
```

仍训练100轮，结束后自动做两种朝向测试。训练集与验证集均翻倍，耗时可能接近上一轮两倍，具体取决于GPU负载等因素。由于验证集也增加了，本轮不是“只改训练样本数”的严格单变量实验；后续既报告完整200份测试表现，也按原文件路径筛出旧100份子集与v7配对比较，不能直接比较200份和旧100份的均值。

## 截顶金字塔分尺度训练实验（2026-10-25）

只重训 `TruncatedPyramidRoof`，沿用v7的800/100/100数据、六个目标、网络、辅助特征、参数权重和100轮训练设置，不重新生成或增强。新增 `--truncated_scale_balanced`，默认关闭，仅允许单独选择截顶类并启用canonical与bbox_residual；不影响其他类别，也不改插件选模。新目录使用 `ptv3_reg_truncatedpyramid_v7_scale_trial_normals`，不覆盖原v7。

训练保留原加权标准化SmoothL1，在还原实际尺寸后，对 `geom_footprint_long`、`geom_footprint_short`、`geom_total_height` 加入权重0.05的相对L1误差；三项按原尺寸参数权重汇总，分母为各样本自身尺寸（下限使用 `relative_loss_eps=0.05`）。不是对bbox残差或全局标准差计算相对误差；三个比例目标不进入新增项，仍参与原有损失。0.05是本轮固定实验系数，不声明为最优取值。

最佳权重只看验证集：各样本尺寸使用相对绝对误差，比例使用绝对误差，按原六参数权重汇总；先在小/中/大三档内取均值，再三档等权平均。train与val必须都有 `horizontalScaleBand` 0/1/2，不允许缺档后静默跳过。配置和checkpoint记录配方、系数、分母下限及选模指标；history增加尺寸相对损失和 `val_scale_small/medium/large`。保留原标准化MAE供观察，但不再用它挑选本实验的best。测试集不参与选模。

将Windows `E:/pointnet/ptv3_simple/main_reg.py` 与 `E:/pointnet/ptv3_simple/train_ptv3_reg.py` 同步到Ubuntu `/home/xubo/pointnet/ptv3_simple/`。保留已同步的数据和其他依赖脚本，不需要修改或重启推理服务。激活 `ptv3_train`、进入该目录后运行：

```bash
nohup python -u train_ptv3_reg.py --classes TruncatedPyramidRoof --data_root /home/xubo/pointnet/datasets_aug --run_tag v7_scale_trial --truncated_scale_balanced --expected_split_counts 800 100 100 > logs/ptv3_reg_truncatedpyramid_v7_scale_trial_train.log 2>&1 &
```

```bash
tail -f /home/xubo/pointnet/ptv3_simple/logs/ptv3_reg_truncatedpyramid_v7_scale_trial_train.log
```

训练结束自动执行metadata真朝向和geometry估计朝向两种测试。将 `/home/xubo/pointnet/ptv3_simple/logs/ptv3_reg_truncatedpyramid_v7_scale_trial_normals/` 与总日志拿回；对照固定v7测试，重点观察中尺度下底长度能否改善，同时保住大尺度表现，不因总体均值变好就宣称成功。已通过96项相关测试和真实数据预检查，100份测试点云哈希与标签均与v7复测快照相同；本机缺少CUDA/spconv，尚未运行真实训练，不保证实验一定提升。

## v7同集复测（2026-10-25）

新增 `compare_ptv3_reg.py`，只复测长方体、截顶金字塔、四段式圆塔，各取当前 v7 数据的100份测试点云。旧权重分别用 v4、v4、v5，与对应 v7 比较。仅测试，不重新训练、不改插件选模，不处理第十四类。`main_reg.py` 新增测试专用 `--eval_output_dir`，保留原权重目录的全部报告；新输出目录须为空且不能是权重目录。

将 `E:/pointnet/ptv3_simple/main_reg.py` 和 `E:/pointnet/ptv3_simple/compare_ptv3_reg.py` 同步到 Ubuntu `/home/xubo/pointnet/ptv3_simple/`。保持现有配套模块，尤其 `canonical_orientation.py`、`model_ptv3.py` 可导入。Ubuntu 需保留本轮增强数据的 `test` 与 `metadata`，以及六个模型目录的 `best_model.pth`、`regression_config.json`。本次不需要重新同步点云、增强或重启推理服务，前提是这些数据和训练权重仍在 Ubuntu。

激活 `ptv3_train` 并进入 `/home/xubo/pointnet/ptv3_simple` 后执行：

```bash
nohup python -u compare_ptv3_reg.py --data_root /home/xubo/pointnet/datasets_aug --log_root /home/xubo/pointnet/ptv3_simple/logs --output_root /home/xubo/pointnet/ptv3_simple/evaluations/v7_same_set > logs/ptv3_v7_same_set_compare.log 2>&1 &
```

```bash
tail -f /home/xubo/pointnet/ptv3_simple/logs/ptv3_v7_same_set_compare.log
```

脚本先复制300份测试点云和对应标签到独立结果目录的 `dataset/`，记录点云、原标签、checkpoint及配置SHA256；旧模型和v7使用同一快照、2048点、batch size 4、seed 42，各自沿用checkpoint的归一化统计和输入处理。先用metadata真朝向，再用点云估计朝向，共12次串行测试，最多1200份预测。圆塔绕Z轴旋转对形状无影响，几何朝向阶段不额外判别圆塔方向。`geometry` 不是QGIS中的完整平移对齐评测。

完成后将 `/home/xubo/pointnet/ptv3_simple/evaluations/v7_same_set/` 拿回分析，主要看 `comparison.csv`、`failures.csv` 和 `manifest.json`。`comparison.csv` 按模型、朝向来源、整体/小/中/大尺度、单个参数记录两套模型的MAE、RMSE、MAPE、绝对误差P95、v7胜率、平局率和失败数。几何阶段只对双方均成功的同文件配对算误差，另列失败，不能把配对误差单独当完整成功率；没有配对样本时指标为空，不写成零误差。原始朝向诊断及每次测试日志保留在类别/版本目录。

结果目录非空时拒绝复用；中途失败后换新 `--output_root`，不要删除或覆盖旧训练报告。`--dry_run` 只核对文件、配置、数量及列出命令，不创建结果或执行推理。本机只有CPU版PyTorch、无CUDA/spconv，已通过88项相关测试和真实数据只读预检查，未完成GPU复测，不能据此宣称v7胜负。

## L形稳健权重接入与Ubuntu部署（2026-10-22）

本轮训练已完成，插件本机/远程选模统一改为 `ptv3_reg_lhouse_robust_bbox_trial_normals`。只改 LHouse，其他类别继续原配置，全局回归后缀仍为 `_v4_normals`，不要在路径设置里把所有类别改成实验后缀。旧目录保留；不重新训练、不移动数据或重新增强，插件 DLL 由用户自行编译并重启 QGIS。

在 Windows CMD 同步配套推理脚本；需要正常的 SSH 登录认证，密码在终端输入，不发到聊天里：

```cmd
scp "E:\pointnet\ptv3_simple\main_reg.py" "E:\pointnet\ptv3_simple\canonical_orientation.py" "E:\pointnet\ptv3_simple\ptv3_server.py" xubo@100.70.215.84:/home/xubo/pointnet/ptv3_simple/
```

Ubuntu 保留本轮训练生成的 `/home/xubo/pointnet/ptv3_simple/logs/ptv3_reg_lhouse_robust_bbox_trial_normals/`，至少需有 `best_model.pth`，建议同时保留 `regression_config.json` 与评测报告，无需从 Windows 重传权重。核对该配置中 `canonical_bbox_mode` 为 `lhouse_xy_quantile_005_995_v1`、`canonical_aux_units` 为 `physical`。新 main_reg.py 会按 checkpoint 重算 XY 分位数特征，不能使用忽略该字段的旧脚本。

在原来运行 uvicorn 的 Ubuntu 终端按 Ctrl+C，仅停止本次推理服务；保留此前的 `PTV3_CLS_LOG`、`PTV3_REG_SUFFIX`、`PTV3_TOWER_LOG`、`PTV3_GABLED_LOG` 自定义配置。切回 `/home/xubo/pointnet/ptv3_simple` 后运行：

```bash
/home/xubo/anaconda3/envs/ptv3_train/bin/python -m uvicorn ptv3_server:app --host 0.0.0.0 --port 8008
```

使用上述环境的 Python 避免未激活环境导致找不到 uvicorn。若 8008 被占用，应先确认仍运行的是哪个旧服务，不要重复启动或批量停止其他 Python/训练进程。另一个 Ubuntu 终端检查：

```bash
curl -sS http://127.0.0.1:8008/health
```

`/health` 的 `reg_suffix` 仍是全局默认值，不能用它判断 L 形实际选模。重新执行插件 LHouse 回归后，原始响应中的 `model_log_dir` 应以 `ptv3_reg_lhouse_robust_bbox_trial_normals` 结尾，`canonical_alignment.applied` 应为 true，并返回数值型 `poseRotateZ`。先关闭几何校正复测第 4、67、88、44、93 份，避免几何参数校正掩盖网络本身的变化。

本轮 best 为第 99 轮、验证分数 0.06713、训练耗时约 30 分 28 秒。几何朝向测试 100 份，相对 axis_scale_trial：外长 MAE 0.540 → 0.343 米、外宽 0.242 → 0.107 米；缺角宽比略退化，不能声称所有参数或所有拐角都改善。本次自动 SSH 认证被拒绝且 /health 超时，未在远程执行同步或重启；后续需按实际回归响应确认部署。下方为按日期保留的实验过程和旧部署记录，当前选模以本节为准。

## L形稳健尺寸特征实验（2026-10-22）

本轮仅修改LHouse的水平尺寸辅助特征：先按原规则转正，再用XY各轴的0.5%-99.5%分位数跨度代替全点极值跨度。Z仍采用全点跨度，物理scale、点坐标、法线、原五目标、原损失、seed、数据划分和验证选模标准均不改。体积、宽长比等派生辅助量按同一个新bbox重算。既不删除增强点云里的杂点，也不改变插件显示、模型参数或定位逻辑。

开关为 `--lhouse_robust_bbox`，仅允许L形canonical物理辅助量实验，与非零拐角损失组合时明确拒绝，避免同时改两件事。config和best checkpoint保存 `canonical_bbox_mode=lhouse_xy_quantile_005_995_v1`。测试和推理读取权重中的量法，不靠临时CLI标记覆盖；旧权重没有该字段时保持full，未知版本或不兼容类别明确报错。

把Windows `E:/pointnet/ptv3_simple/` 下已更新的 `main_reg.py`、`train_ptv3_reg.py` 同步到 `/home/xubo/pointnet/ptv3_simple/`。其他脚本沿用已部署的新版本；不用重新生成/增强数据，不需要为本轮训练编译插件或重启推理服务。激活ptv3_train，在上述Ubuntu目录运行：

```bash
nohup python -u train_ptv3_reg.py --classes LHouse --data_root /home/xubo/pointnet/datasets_aug --run_tag robust_bbox_trial --lhouse_canonical --lhouse_robust_bbox --expected_split_counts 800 100 100 > logs/ptv3_reg_lhouse_robust_bbox_trial_train.log 2>&1 &
```

查看日志：

```bash
tail -f ~/pointnet/ptv3_simple/logs/ptv3_reg_lhouse_robust_bbox_trial_train.log
```

结果单独保存为 `logs/ptv3_reg_lhouse_robust_bbox_trial_normals/`，训练后自动执行metadata和geometry两套测试，不覆盖旧目录或自动切换插件权重。日志应出现 `Canonical bbox features: lhouse_xy_quantile_005_995_v1 | XY quantiles=0.005,0.995 | Z=full span`；config中 `lhouse_corner_loss_weight` 应为0。

70项CPU检查及800训练/100验证特征预检通过；100对等价物理/归一化输入的bbox最大差约0.000004场景单位。验证集的长/宽辅助量MAE从0.7766/7.2525变为0.1138/0.1274米，这只是输入特征误差；现已完成GPU训练，网络精度与部署状态见上方接入记录。分位数仍可能削短真实边界，不能只看第67份。预检明细见 `evaluations/2026-10-22-lhouse-robust-bbox-preflight/`，输入诊断保留在 `evaluations/2026-10-22-lhouse-scale-diagnosis/`。

## L形拐角约束实验（2026-10-22）

该轮已经完成，部分困难样本改善但整批结果未全面变好，插件未切换到corner_trial。以下命令保留作实验记录；当前新增对照使用上方独立的稳健尺寸特征开关。

第67份样本保持姿态、只修正总长和翼部长比后明显贴近点云，说明该样本主要需要改善参数组合，而不是继续修改定位。新增独立实验开关 `--lhouse_corner_loss_weight`，默认0；不更改插件五参数定义、定位规则或正在使用的axis_scale_trial权重，也不重新生成、增强或修改测试集。

新损失直接从主输出计算局部内拐角 `(L*(1-cutoutLengthRatio), W*(1-cutoutWidthRatio))`，按训练集总长/总宽的标准差缩放后加SmoothL1；不是新增独立预测头。原五参数损失和几何辅助头继续保留，以免只靠长与比例互相抵消来对准一个角。权重0.2只是首轮对照的起点，不是已验证最优值。选模仍使用原验证集加权标准化MAE，不用第4、67、88份测试结果调权重或选epoch。

将Windows `E:/pointnet/ptv3_simple/` 下的 `main_reg.py`、`train_ptv3_reg.py` 同步到 `/home/xubo/pointnet/ptv3_simple/`。其他文件沿用已部署版本；本轮不要求重启推理服务或编译插件。在Ubuntu的ptv3_train环境、`~/pointnet/ptv3_simple` 目录运行：

```bash
nohup python -u train_ptv3_reg.py --classes LHouse --data_root /home/xubo/pointnet/datasets_aug --run_tag corner_trial --lhouse_canonical --lhouse_corner_loss_weight 0.2 --expected_split_counts 800 100 100 > logs/ptv3_reg_lhouse_corner_trial_train.log 2>&1 &
```

查看日志：

```bash
tail -f ~/pointnet/ptv3_simple/logs/ptv3_reg_lhouse_corner_trial_train.log
```

结果保存在 `logs/ptv3_reg_lhouse_corner_trial_normals/`，已有非空目录拒绝覆盖。config及best checkpoint记录损失权重和配方。启用时，逐轮CSV和日志新增 `val_local_notch_mean`、`val_local_notch_p95`：它们是在同一外部左下角和局部坐标系下解码得到的内角位置误差，单位为场景单位，不包含朝向估计或插件平移误差，也不是点到面残差。

结束后沿用metadata和点云几何朝向两套100份测试，与axis_scale_trial比较全部五参数、拐角均值/P95和逐样本变化；高度、外框不能明显退步，不能只凭第67份或单项平均误差决定替换。该轮第67份固定原点拐角误差由约1.47降到0.80米，但100份均值从0.530变为0.541米，验证选模分数从0.08761变为0.08865，因此未替换当前权重。

## L形新版权重接入（2026-10-21）

插件PTv3本机回归与局域网/Tailscale远程请求均单独选择 `ptv3_reg_lhouse_axis_scale_trial_normals`，不再给LHouse请求旧v4。其他类别仍使用原全局后缀和圆塔独立配置；路径设置中的PTv3回归后缀保持 `_v4_normals`，不要改成L形实验后缀。分类模型、L形五参数定义和定位规则不变。

用户自行编译插件并重启QGIS后生效。Ubuntu保留本次训练生成的 `logs/ptv3_reg_lhouse_axis_scale_trial_normals/`，同步Windows `E:/pointnet/ptv3_simple/` 下的 `ptv3_server.py`、`main_reg.py`、`canonical_orientation.py` 到 `/home/xubo/pointnet/ptv3_simple/`，在ptv3_train环境中重启服务。已有相同文件则不必重复覆盖；本轮未修改这三个Python脚本。

服务通过现有 `model_suffix` 按请求选模，不需要新增环境变量或重训。`/health` 的 `reg_suffix` 仍为全局默认值，不能用它判断L形实际权重；应检查该次回归响应的 `model_log_dir`，末尾须为 `ptv3_reg_lhouse_axis_scale_trial_normals`。新目录缺少 `best_model.pth` 时会明确报错，不回退旧v4。先在几何校正关闭时复测第4、67、88个样本；离线报告不代替QGIS现场验收。

## L形辅助尺度统一（2026-10-21）

后续L形实验统一辅助量为物理场景单位，目标、损失、方向规则、数据划分及seed不变。原axis_trial仍保留，不能把新单位强行套到旧checkpoint上；需要另训 `axis_scale_trial`，无需重新生成或增强数据。

把Windows `E:/pointnet/ptv3_simple/` 下的 `main_reg.py`、`train_ptv3_reg.py`、`ptv3_server.py` 同步到Ubuntu对应目录。`canonical_orientation.py` 本轮未改，沿用上轮。激活ptv3_train，在 `~/pointnet/ptv3_simple` 运行：

```bash
nohup python -u train_ptv3_reg.py --classes LHouse --data_root /home/xubo/pointnet/datasets_aug --run_tag axis_scale_trial --lhouse_canonical --expected_split_counts 800 100 100 > logs/ptv3_reg_lhouse_axis_scale_trial_train.log 2>&1 &
```

查看日志：

```bash
tail -f ~/pointnet/ptv3_simple/logs/ptv3_reg_lhouse_axis_scale_trial_train.log
```

新的训练命令自动添加 `--canonical_aux_units physical`，并在config及best checkpoint保存此设置。结果目录为 `logs/ptv3_reg_lhouse_axis_scale_trial_normals/`，仍在结束后自动跑metadata与geometry两套测试。其他类别的默认训练/推理格式暂不迁移，旧checkpoint缺少该字段时使用legacy逻辑。

物理bbox仍采用实际点云全点跨度，包含增强噪声和离群点，不能当成真值尺寸，也不承诺仅改单位就一定提高预测精度。scale保持原物理归一化尺度，比例无量纲，体积使用立方场景单位；bbox_x/y/z_norm等特征由物理bbox除以同一scale得到。

新权重部署时，还须使用本轮插件源码自行编译DLL，并重启已同步的推理服务。插件按metadata识别数据集归一化TXT，其他输入按物理坐标声明；本机CLI与远程JSON都携带input_coordinates。CLI直接预测新权重、没有匹配metadata时，须显式传 `--input_coordinates physical` 或 `--input_coordinates normalized`；归一化输入还必须有有效物理scale。不要通过点云数值大小猜单位；没有metadata的归一化TXT必须由调用者正确声明和提供尺度。

旧axis_trial和v4仍按原格式解释，不自动改变其预测。新格式的训练、测试和同一云的物理/归一化输入一致性验证见 `evaluations/2026-10-21-lhouse-aux-units/`；当前没有重新训练或切换默认模型。

## L形单类方向对照（2026-10-21）

此段为首次axis_trial的历史操作记录。后续分析发现其bbox辅助量尺度也随canonical路径变化，不能作为严格的方向单变量消融；后续重训使用上方尺度修正版，不在已完成的axis_trial目录重复执行。该轮维持 `outerLength outerWidth cutoutLengthRatio cutoutWidthRatio height` 五个训练目标、原损失、seed=42、100轮及800/100/100划分，没有加入拐角辅助损失或替换线上v4，沿用已有增强数据。

将Windows `E:/pointnet/ptv3_simple/` 下三个已同步文件复制到Ubuntu的 `/home/xubo/pointnet/ptv3_simple/`：`train_ptv3_reg.py`、`main_reg.py`、`canonical_orientation.py`。本轮不要求修改服务脚本、插件默认后缀或其他权重。

在Ubuntu的 `ptv3_train` 环境、`~/pointnet/ptv3_simple` 目录运行：

```bash
nohup python -u train_ptv3_reg.py --classes LHouse --data_root /home/xubo/pointnet/datasets_aug --run_tag axis_trial --lhouse_canonical --expected_split_counts 800 100 100 > logs/ptv3_reg_lhouse_axis_trial_train.log 2>&1 &
```

查看日志：

```bash
tail -f ~/pointnet/ptv3_simple/logs/ptv3_reg_lhouse_axis_trial_train.log
```

checkpoint与结果保存到 `logs/ptv3_reg_lhouse_axis_trial_normals/`，已有非空目录时拒绝覆盖。训练按原验证集选模，结束后自动先做metadata朝向测试，再做 `--test_orientation geometry` 测试，后者只从点云估计朝向，metadata仅用于核验标签和方向误差。

重点取回该完整模型目录及训练日志。`regression_report_test.csv` 是metadata朝向结果，`regression_report_test_geometry.csv` 是几何朝向结果；`orientation_test.json`、`orientation_test_geometry.json` 另记 `notch_corner_error_mean/p95`（米）和逐样本结果。这些角点指标先对齐外包络中心，反映形状与朝向，不包含插件平移定位误差。

几何报告还记录接受/失败数，`axis_error_deg` 对本实验使用完整360°方向差，不把180°翻转隐藏掉。若出现失败，参数指标仅覆盖成功样本，必须同时报告覆盖率，不能直接拿成功子集与旧v4全部100份比较。方向不确定时推理明确失败，不回退到与新权重不匹配的旧输入坐标。

旧LHouse checkpoint没有 `lhouse_canonical` 标记，默认仍不旋转；新实验checkpoint保存 `lhouse_canonical=true` 和 `canonical_frame_version=lhouse_full_yaw_v1`。其他类别的方向行为不变。现有原始/增强验证与测试共400份点云方向预检保存在 `evaluations/2026-10-21-lhouse-axis-preflight/report.json`，不是回归训练结果、真实点云泛化精度或QGIS验收。

## v4 回归目录改名（2026-10-21）

Windows `E:/pointnet/ptv3_simple/logs` 的13个v4回归目录已去掉 `_canonical`，例如 `ptv3_reg_conecylinder_v4_normals`。训练配置和权重不变，规范化仍按checkpoint设置执行；圆塔v5和分类目录不改。历史评测文件保留当时路径，不批量改写已有结果。

先把本机 `E:/pointnet/ptv3_simple/ptv3_server.py` 同步到Ubuntu的 `~/pointnet/ptv3_simple/ptv3_server.py`，在Ubuntu改名已有目录（先检查所有目标，冲突时不会覆盖）：

```bash
cd ~/pointnet/ptv3_simple
python -c 'from pathlib import Path; root=Path("logs").resolve(); pairs=[(p,p.with_name(p.name.replace("_v4_canonical_normals","_v4_normals"))) for p in root.glob("ptv3_reg_*_v4_canonical_normals") if p.is_dir()]; conflicts=[str(b) for a,b in pairs if b.exists()]; assert not conflicts, conflicts; [a.rename(b) for a,b in pairs]; print("Renamed:",len(pairs))'
```

旧服务用Ctrl+C停止后，在 `ptv3_train` 环境中启动；已有圆塔/人字形专用路径只替换旧v4后缀，v5或其他自定义路径保留：

```bash
conda activate ptv3_train
export PTV3_REG_SUFFIX=_v4_normals
export PTV3_TOWER_LOG="${PTV3_TOWER_LOG//_v4_canonical_normals/_v4_normals}"
export PTV3_GABLED_LOG="${PTV3_GABLED_LOG//_v4_canonical_normals/_v4_normals}"
uvicorn ptv3_server:app --host 0.0.0.0 --port 8008
```

另一个终端检查 `curl -s http://127.0.0.1:8008/health` 的 `reg_suffix` 为 `_v4_normals`，并核对 `tower_reg_log`、`gabled_reg_log` 是否存在。插件新编译后自动迁移旧默认后缀；未编译时可在路径设置中手动将PTv3回归后缀改为 `_v4_normals`。不需要重训或重新增强数据。

## 未发布：对齐改动与基线对比（2026-10-20 至 2026-10-21）

本轮只修改插件定位，不重训、不更换服务权重。PowerShell 运行 `scripts/evaluate_alignment.ps1`，默认将新结果保存到 `evaluations/2026-10-21-alignment-lhouse/`，拒绝覆盖 v2.4.0 的 `evaluations/2026-10-20-alignment/` 及上一阶段的 `evaluations/2026-10-20-alignment-refined/`、`evaluations/2026-10-20-alignment-horizontal/`、`evaluations/2026-10-20-alignment-first-gable/`、`evaluations/2026-10-20-alignment-indented-outer/`。四组评测和原始点云回建校验后，自动检查与基线的预测/配置/metadata 哈希及2600条 baseline/shape 结果一致性，再输出 `comparison.csv`，记录中位数、P95、逐样本改善/变差数量和最大增量。

单独重算对比可用 `python scripts/evaluate_alignment.py compare --output evaluations/2026-10-21-alignment-lhouse`。沿用同一13类日志（圆塔v5，其余v4 canonical）与真实 metadata 朝向，不包含未知朝向或实际QGIS窗口验收。凹陷外框与内槽仍独立定位；L形仅在缺可靠外墙的轴向用分段顶面外缘补XY，不修改高度或预测尺寸。新增量可传 `--baseline evaluations/2026-10-20-alignment-indented-outer` 对比上一轮；默认仍对比v2.4.0累计改进。插件需用户编译，Ubuntu服务无须因本轮定位改动而重启。

## v2.4.0：对齐评测基线（2026-10-20）

本版不重新训练。圆塔 v5 和其他十二类 v4 canonical 的测试日志已用于固定真实类别、metadata 朝向的四组评测，结果保存在 `evaluations/2026-10-20-alignment/`；这是下一轮对齐修改前的基线，不是线上服务或未知朝向完整推理验收。

PowerShell 运行 `scripts/evaluate_alignment.ps1` 可复现：校验当前标签与 CSV 真值，构建独立评测程序，计算真值形状/预测形状与真值位姿/自动平移的四组残差，再用未增强点云核验真值网格还原。原始回建不通过时拒绝生成汇总结论。不会构建插件 DLL、重训、改动数据或上传权重；临时独立编译文件在成功完成后清除。脚本默认读取 `E:/pointnet/datasets`、`E:/pointnet/datasets_aug` 和 `E:/pointnet/ptv3_simple/logs`。

## 2026-10-19：圆塔 middleRise 新曲线

本节优先于下方历史圆塔命令。仅修改 FourStageRoundTower，穹顶和其他十二类回归配方不变。输入/导出/训练统一使用 `middleRise`（米），替换旧系数 `middleBulge`；不能通过给旧标签改名来转换数据。隆起高度范围 `0～middleHeight/4`，随机生成采用 `0.03～0.20` 倍中段高度。

顶部半径取消原有 2%～35% 比例截断，仅要求大于零且小于檐口外半径；输入端联动边界，训练入口检查同一关系。随机生成先采样原有造型，再将八个尺寸共同乘以 `0.5～2.0` 的随机倍率：底座半径理论覆盖约 2.5～24、中段高约 0.5～6、顶部半径约 0.25～3.2 米。倍率均匀抽样不等于最终各尺寸均匀分布；造型比例不刻意改变，控件会按原精度舍入。

扩大尺度后仍需用户编译并替换生成圆塔数据，再增强和同步；已有数据不会自动放大。参数字段、增强和 v5 训练命令不变，无须为缩放修改训练/推理脚本。

用户编译并重启插件后，选中 FourStageRoundTower，使用“生成当前基元数据集”的替换模式，在原 `E:/pointnet/datasets` 目录重新生成 1000 份（800/100/100）。不要追加或补齐旧塔样本；其他类别保留。仍用 `E:/pointnet/ptv3_simple/augment_dataset.py`，按下方圆塔 CMD 增强命令只覆盖该类。

Ubuntu 需要替换增强集三个 `FourStageRoundTower` 子目录及更新后的 metadata，并同步新版 `train_ptv3_reg.py` 和 `ptv3_server.py`。Windows 实际使用目录 `E:/pointnet/ptv3_simple` 的这两个脚本已同步；`main_reg.py` 已支持按目标名透传实际尺寸，无须改动，增强脚本和既有权重未修改。训练入口会在启动 GPU 前拒绝旧/混合标签及隆起越界数据。使用新的 run_tag，仅训练圆塔回归；尚未生成数据或启动训练。

新版插件与服务明确拒绝缺少 `middleRise` 或仍含 `middleBulge` 的圆塔预测。圆塔新权重训练验证后再用 `PTV3_TOWER_LOG` 单独指定，分类/其他类别回归默认目录不变；部署前的旧塔推理暂不可用，不会静默套用新定义。分类先复测新塔数据，再决定是否重训，不能只用单类训练分类模型。

本地剖面候选预览：`tower_bulge_profile.html`。实际网格使用相同公式，仍以三次贝塞尔控制点构造。界面值为米数，不要求手工换算比例。

## 2026-10-16：分类 v6 部署

分类 v6 已训练并取回 Windows，测试集 1298/1300 正确（99.85%）。插件本机默认值和旧默认配置迁移均指向 `logs/ptv3_cls_v6`；服务脚本默认分类也改为 v6，回归后缀为 `_v4_normals`（原名含 `_canonical`，算法不变）。

将新版 `ptv3_server.py` 同步到 Ubuntu `/home/xubo/pointnet/ptv3_simple/`。确认该机 `logs/ptv3_cls_v6/best_model.pth` 和 `classification_config.json` 存在；训练就在该机完成时不必重新传权重。停止旧推理服务后，在已激活 `ptv3_train` 的环境中运行：

```bash
PTV3_CLS_LOG=logs/ptv3_cls_v6 python -m uvicorn ptv3_server:app --host 0.0.0.0 --port 8008
```

显式指定分类目录可避免已有 v5 环境变量覆盖新默认值；不改变已有回归环境变量。用 `curl -s http://127.0.0.1:8008/health` 确认 `cls_log` 结尾为 `/logs/ptv3_cls_v6`，再从插件复测 `test/GabledRoof/sample_00007.txt`。只换远程分类不要求重编译插件；要启用本机默认目录及其迁移，则需用户编译新插件。尚未在远程 GPU 或 QGIS 中执行此轮验证。

## 2026-10-15：v4 推理修复与实际方向测试

无需重新生成数据或重新训练。将 Windows `E:/pointnet/ptv3_simple/` 下的 `canonical_orientation.py`、`main_reg.py` 和 `ptv3_server.py` 同步到 Ubuntu `/home/xubo/pointnet/ptv3_simple/`，保留现有 v4 checkpoint；激活 `ptv3_train`，在该目录停止旧 uvicorn 进程后重新启动服务。插件需要用户重新编译。

几何方向使用稳健外接矩形，顶部点云用于判断屋脊对应的轴。插件有 metadata 时把规范参数一同变回原始坐标，而不是仅覆盖姿态角；无 metadata 则保留配套的规范参数和几何角度。近正方形方向标记 WEAK，无法读取有效二维轮廓时明确报错；LHouse、TwoGableHouses 与旋转对称类仍跳过。

在 Ubuntu 已激活的环境中检查两类人字屋和凹陷长方体的实际推理方向表现（只测试，不训练）：

```bash
for stem in gabledroof asymgable indentedcuboid; do python -u main_reg.py --mode test --data_root /home/xubo/pointnet/datasets_aug --log_dir logs/ptv3_reg_${stem}_v4_normals --test_orientation geometry --seed 42 || break; done
```

测试使用和推理相同的几何方向函数。新报告为 `regression_*_test_geometry.csv` 与 `orientation_test_geometry.json`，不会覆盖原来的 `*_test.csv`。JSON 包含方向处理失败数、成功样本的角度误差、中心对齐后的水平轮廓角点误差和屋脊端点误差；这些几何误差不含插件的全局平移/自动角点对齐误差。原有默认测试仍使用 metadata 真值方向，两种结果需分别报告。

插件验证建议先关闭几何校正，用 `test/GabledRoof/sample_00003.txt`、`test/AsymmetricGableHouse/sample_00005.txt` 检查屋脊侧，再检查凹陷长方体内槽；随后用没有 metadata 的点云核对方向。连续加载文件和完成后重新打开估计窗口，都应从上次文件夹开始。

## 2026-10-13 屋脊偏移诊断与独立实验

## 2026-10-14：canonical 方向规范训练

`train_ptv3_reg.py` 默认开启 canonical frame。训练/验证/测试根据增强 metadata 的 `params.rz` 反旋点云，并在旋转后的点云上重算 bbox 辅助特征；GabledRoof 与 AsymmetricGableHouse 同步把 `ridgeRatio` 映射到规范方向。推理时按 checkpoint 中的 `canonical_align` 标记自动应用几何方向估计和同一 bbox 重算。新权重需要与 `canonical_orientation.py`、`main_reg.py` 一起同步到服务端；旧权重不受影响。

当前规范轴支持 Cuboid、IndentedCuboid、GabledRoof、PyramidRoof、TruncatedPyramidRoof、AsymmetricGableHouse、HalfCylinderRoof。LHouse 与 TwoGableHouses 保留原坐标，避免把缺角位置或双房关系错误地旋转成另一组参数；轴向对称基元也不需要旋转规范化。规范训练不与 `*_ridge_symmetry_180` / `--indented_symmetry_180` 同时使用，因为新标签保留明确方向。

### v3.3 两类屋顶实验结果

普通人字形与非对称人字形按800/100/100训练，实验权重位于 `ptv3_reg_gabledroof_ridge180_center_trial_normals` 和 `ptv3_reg_asymgable_ridge180_center_trial_normals`。普通人字形新增第五个目标 `ridgeRatio`；非对称屋顶保留六目标，生成数据采用中心偏重分布。

两类都启用了180°等价损失。测试屋脊等价 MAE 分别为0.0334和0.0301，直接方向 MAE 为0.1123和0.1200；测试集上镜像方向更接近标签的比例约为46%和52%。这证明偏移幅度改善，但方向仍未确定。测试集与旧v3标签基本不同，不能把差值视作严格的同样本对照；模型尚未部署。

### 普通人字形五参数训练

GabledRoof 已增加 `ridgeRatio`，旧权重在插件中按0.50回填。新训练前须用编译后的插件重新生成该类原始数据（800/100/100），仅“补齐”已有1000份数据不会更新旧标签。再次增强该类，然后将增强后的 GabledRoof 三个split及元数据、`main_reg.py`、`train_ptv3_reg.py` 同步到Ubuntu。

```cmd
python E:\pointnet\ptv3_simple\augment_dataset.py --input E:\pointnet\datasets --output E:\pointnet\datasets_aug --classes GabledRoof --overwrite --no-drop_bottom --noise_sigma 0.006 --outlier_ratio 0.003 --outlier_extent 0.03
```

```bash
nohup python -u train_ptv3_reg.py --classes GabledRoof --data_root /home/xubo/pointnet/datasets_aug --run_tag gabled_ridge_trial --gabled_ridge_symmetry_180 --expected_split_counts 800 100 100 > logs/ptv3_reg_gabledroof_gabled_ridge_trial_train.log 2>&1 &
```

新对称模型仍需选定朝向才可安全部署。服务脚本已支持 `PTV3_GABLED_LOG=logs/ptv3_reg_gabledroof_gabled_ridge_trial_normals` 单独选择该类，`/health` 返回 `gabled_reg_log`；默认继续使用旧v3权重。若重新生成非对称屋顶的新分布数据，也必须重新增强并保持元数据和点云一致。分类v5仍能运行，但其真实分类表现需要在新分布上重新评估，再决定是否更新分类权重。

以下是已完成的非对称屋顶 `ridge180_trial` 实验的复现说明，使用当时的旧增强数据，区别于需要重生成两类数据的下一轮v3.3。仓库 `main_reg.py` 和 `train_ptv3_reg.py` 是 PTv3 训练部署副本。

```bash
nohup python -u train_ptv3_reg.py --classes AsymmetricGableHouse --data_root /home/xubo/pointnet/datasets_aug --run_tag ridge180_trial --asym_ridge_symmetry_180 --expected_split_counts 800 100 100 > logs/ptv3_reg_asymgable_ridge180_trial_train.log 2>&1 &
```

该开关默认关闭，批量运行时只作用于 AsymmetricGableHouse，不改变双人字屋的损失。实验保持六个原参数与普通误差 CSV，选模使用带权标准化的180°等价误差，测试另存 `ridge_symmetry_test.json`。在两种损失相等时选择确定的分支，避免预测0.5时梯度抵消。

这项实验用于检验方向标签冲突能否解释偏移收缩；等价误差降低不代表原方向已经预测正确。权重部署前必须结合朝向确定 `q` 或 `1-q`。当前权重和服务配置不自动切换。双人字屋包含分支身份及角度，不能直接套用该变换。

已有权重的 CPU 诊断不需要 CUDA：

```cmd
python review_ridge_predictions.py --log_root E:\pointnet\ptv3_simple\logs --run_tag v3 --output ridge-review.json
```

脚本分别报告有方向误差、偏离中线幅度和预测靠近中线的数量；为普通及非对称人字形报告180°等价误差，不把双人字屋套用为同一种对称规则。旧四参数人字形输出会跳过屋脊诊断。可用 `--run_tag ridge180_trial` 检查已完成实验的结果，或在v3.3完成后检查该版本。本机基准和实验报告分别为插件 `evaluations` 目录下的 `ridge-review-20261013.json` 与 `ridge-review-trial-20261013.json`。

## v3：全部 13 类补齐到 800/100/100

在 QGIS 的 **Dataset → Complete selected primitive datasets...** 中点 **Select all trainable primitives**，保持 Train 800、Validation 100、Test 100，再选择原始数据集文件夹 `E:/pointnet/datasets`。三棱柱未纳入这 13 类训练。确认后只生成九类各 500 个缺失样本，合计 4500 个，不覆盖旧点云。

本次 v3 在 Windows CMD 中运行 `E:/pointnet/ptv3_simple/augment_dataset.py`，对全部 13 类重建增强集：

```cmd
python E:\pointnet\ptv3_simple\augment_dataset.py --input E:\pointnet\datasets --output E:\pointnet\datasets_aug --overwrite --no-drop_bottom --noise_sigma 0.006 --outlier_ratio 0.003 --outlier_extent 0.03
```

不写 `--classes` 即处理全部类别；`--overwrite` 会重建整个 `datasets_aug`，旧增强点云也会重新生成，孔洞沿用脚本默认设置。本次输出 success=13000、failed=0，bbox-min 漂移最大 0.0465。若以后只想增强新追加的样本，仓库另有 `scripts/augment_added_samples.py` 可先用 `--dry-run` 检查；它不是本次 v3 使用的命令。

增强集补齐后同步到 Ubuntu，在 `ptv3_train` 环境下从 `ptv3_simple` 目录启动 v3（普通损失，不加 `--indented_symmetry_180`）：

```bash
nohup python -u train_ptv3_reg.py --data_root /home/xubo/pointnet/datasets_aug --run_tag v3 --expected_split_counts 800 100 100 > logs/ptv3_reg_v3_train.log 2>&1 &
```

回归完成后再训练分类 v5，避免两项任务在同一 GPU 上抢显存；分类成功后自动跑测试集：

```bash
nohup sh -c 'python -u main.py --mode train --data_root /home/xubo/pointnet/datasets_aug --log_dir logs/ptv3_cls_v5 --use_normals --seed 42 && python -u main.py --mode test --data_root /home/xubo/pointnet/datasets_aug --log_dir logs/ptv3_cls_v5 --split test' > logs/ptv3_cls_v5_train.log 2>&1 &
```

服务端副本在 `scripts/ptv3_server.py`，canonical v4 部署还需同步 `scripts/main_reg.py` 与 `scripts/canonical_orientation.py` 到 Ubuntu 的 `ptv3_simple/`。部署前先比较文件，避免覆盖其未同步的改动；按本页目录改名步骤处理后重启 `ptv3_train` 环境中的 uvicorn，用 `curl -s http://127.0.0.1:8008/health` 核对 `cls_log` 为v6、`reg_suffix` 为 `_v4_normals`。Windows 端再通过插件选定的 LAN/Tailscale 地址检查 `/health`；实际分类/回归仍需在 QGIS 中验证。

## 圆塔檐口独立参数（2026-10-12）

新生成的圆塔标签和训练目标均为实际尺寸：`eaveRadius` 是檐口外半径，`eaveHeight` 是檐口厚度。只在随机生成时采用温和的造型先验：外挑量为底座半径的 5%–20%，厚度在实际尺寸 0.1 到 `min(底座高度×0.25, 0.9)` 内采样；生成底座高度仍为 1.2–3.5。手动输入、网格和训练检查只要求外半径大于底座半径、厚度为正且小于底座高度，不再硬性施加随机采样范围。底座高度保留原 0–50 输入范围；手动尺寸输入保留技术上限（外半径 60、厚度 50），同时按几何关系更新有效边界。

初次有效底座默认采用 1.12 倍外半径和 `min(底座高度×0.20, 0.9)` 厚度；之后手改底座时保留仍合法的实际檐口尺寸。底座为零时清零并禁用对应控件。应单独生成并增强新版圆塔数据，避免把缺少尺寸标签的旧样本或过渡比例标签直接混入新训练。

`train_ptv3_reg.py` 的圆塔配方已更新为八个实际尺寸目标，并在启动 GPU 前检查新标签及檐口尺寸范围；其他类别的配方不变。当前 v3 权重仍是六输出。仓库内 `scripts/train_ptv3_reg.py` 与 `scripts/ptv3_server.py` 是部署副本，已同步至 Windows 的 `E:/pointnet/ptv3_simple`；Ubuntu 训练前需同步这两个脚本。

数据必须重新生成：按用户要求沿用旧目录。在主面板选 FourStageRoundTower，再选“数据集 → 生成当前基元数据集... → 替换”，数量填 1000，目录选择 `E:/pointnet/datasets`。仅重建圆塔的 train/val/test = 800/100/100 并更新 metadata，其他类别保留。不要使用“生成全部基元”或在旧圆塔目录里“补齐到 1000”。

生成后在 Windows CMD 中增强新圆塔数据（尚未执行）：

```cmd
python E:\pointnet\ptv3_simple\augment_dataset.py --input E:\pointnet\datasets --output E:\pointnet\datasets_aug --classes FourStageRoundTower --overwrite --no-drop_bottom --noise_sigma 0.006 --outlier_ratio 0.003 --outlier_extent 0.03
```

向 Ubuntu 的现有 datasets_aug 复制并替换三个圆塔子目录 `train/FourStageRoundTower`、`val/FourStageRoundTower`、`test/FourStageRoundTower` 以及整个 `metadata/`，Windows 副本保留。同步 ptv3_simple 下的 `train_ptv3_reg.py`、`ptv3_server.py`。其他类别已经在 Ubuntu 时无需再次传输；`main.py`、`main_reg.py`、`ptv3_model.py`、`check_ptv3.py` 等现有依赖保持原样。

在 Ubuntu 的 ptv3_train 环境、ptv3_simple 目录只训练圆塔回归；自动跑 test，权重保存到独立的 `ptv3_reg_fourstage_v4_eaves_normals`，不会覆盖 v3（尚未执行）：

```bash
nohup python -u train_ptv3_reg.py --classes FourStageRoundTower --data_root /home/xubo/pointnet/datasets_aug --run_tag v4_eaves --expected_split_counts 800 100 100 > logs/ptv3_reg_fourstage_v4_eaves_train.log 2>&1 &
```

回归结束后再训练分类，避免同一 GPU 同时跑两项。分类是统一的 13 类模型，仍使用全部类别的数据，只有圆塔数据被更新；不应只用圆塔一个类别训练分类。新分类保存为 v6 并自动跑 test（尚未执行）：

```bash
nohup sh -c 'python -u main.py --mode train --data_root /home/xubo/pointnet/datasets_aug --log_dir logs/ptv3_cls_v6 --use_normals --seed 42 && python -u main.py --mode test --data_root /home/xubo/pointnet/datasets_aug --log_dir logs/ptv3_cls_v6 --split test' > logs/ptv3_cls_v6_train.log 2>&1 &
```

确认训练和测试完成后，关闭旧服务并通过专用环境变量选择分类 v6 与圆塔新目录：

```bash
PTV3_CLS_LOG=logs/ptv3_cls_v6 PTV3_TOWER_LOG=logs/ptv3_reg_fourstage_v4_eaves_normals python -m uvicorn ptv3_server:app --host 0.0.0.0 --port 8008
```

`/health` 的 `cls_log` 与 `tower_reg_log` 会显示实际路径；`reg_suffix` 仍为 `_v3_normals`，其他类别回归保持原权重。不设置变量则分类继续使用 v5、圆塔继续使用 v3；显式设置的圆塔目录缺少 checkpoint 时会报错，不静默回退。

插件本地 PTv3 推理也识别 `PTV3_TOWER_LOG`。Windows CMD 可设置 `set PTV3_TOWER_LOG=E:\pointnet\ptv3_simple\logs\ptv3_reg_fourstage_v4_eaves_normals`，再从该终端启动 QGIS；本地必须已有新权重目录。此变量只影响圆塔。使用远程推理时，以 Ubuntu 服务的环境变量为准。

## 历史 PointNeXt 流程（归档）

以下步骤保留旧实验记录，涉及 500 样本/类、`pointnext_simple` 和 `_aux`。它们不是当前 PTv3 v3/v5 的操作说明；当前命令见上文。

### 旧流程总览

```
Windows (QGIS)                    Windows (Python)               Ubuntu (GPU)
─────────────────                 ─────────────────              ─────────────────
QGIS插件生成纯净数据集              augment_dataset.py 增强        分类训练 + 测试
  ↓                                  ↓                           参数回归训练 + 测试
datasets/  ─────────────────→  datasets_aug/  ─────────────→  logs/ 拉回 Windows
  train/val/test/                  train/val/test/              更新 C++ 路径
  metadata/sample_params.json      metadata/sample_params.json
```

---

## 第 1 步：Windows — QGIS 插件生成数据集

在 QGIS 中打开 ParamModeler 面板 → 点击 **"Export DL Dataset (All Classes)"**
→ 选择输出目录 → 当时标准: 500 样本/类（train 400 + val 50 + test 50） → 等待完成

输出:
```
E:/pointnet/datasets/
  train/{Cuboid, Cylinder, ...}/sample_00001.txt ... sample_00400.txt
  val/{...}/（每类 50）
  test/{...}/（每类 50）
  metadata/
    class_names.txt
    sample_params.json       <-- 每条 params 含形状参数 + rz（不含 rx/ry/tx/ty/tz）
```

验证:
```bash
python -c "
import json
data = json.load(open('E:/pointnet/datasets/metadata/sample_params.json'))
r = data[0]
print('type:', r['type'], 'params keys:', list(r['params'].keys()))
print('rx:', r['params'].get('rx'), 'ry:', r['params'].get('ry'), 'rz:', r['params'].get('rz'))
"
```

---

## 第 2 步：Windows — 数据增强

```bash
cd E:/pointnet/pointnet_simple

# 全量增强（所有 13 类）
python augment_dataset.py \
  --input E:/pointnet/datasets \
  --output E:/pointnet/datasets_aug \
  --num_points 2048 \
  --seed 42 \
  --overwrite

# 如果只想增强特定类（比如增量添加）：
# python augment_dataset.py --input E:/pointnet/datasets --output E:/pointnet/datasets_aug --classes Cylinder Cuboid --overwrite
```

增强内容包括：底部点丢弃、侧面遮挡（模拟拍照盲区）、随机挖洞、高斯噪声、离群点。

> **历史说明**：当时增强基于原始 500 样本/类；当前 PTv3 v3 使用顶部记录的 1000 样本/类和完整重建命令。

---

## 第 3 步：搬运到 Ubuntu

```bash
# === 在 Windows Git Bash 上执行 ===
# 搬运增强后的数据
scp -r E:/pointnet/datasets_aug xubo@<ubuntu-ip>:/home/xubo/pointnet/datasets_aug

# 搬运训练脚本（首次）
scp E:/pointnet/pointnext_simple/main.py E:/pointnet/pointnext_simple/main_reg.py E:/pointnet/pointnext_simple/model.py \
    xubo@<ubuntu-ip>:/home/xubo/pointnet/pointnext_simple/

# 搬运本次新增的脚本
scp scripts/train_reg_with_rot.sh scripts/train_cls.sh scripts/test_all.sh \
    xubo@<ubuntu-ip>:/home/xubo/pointnet/pointnext_simple/
```

---

## 第 4 步：Ubuntu — 分类训练

```bash
ssh xubo@<ubuntu-ip>
cd /home/xubo/pointnet/pointnext_simple
source /home/xubo/pointnet_envs/pointnet_gpu/bin/activate

# 分类训练（PointNeXt）
python main.py --mode train \
  --data_root /home/xubo/pointnet/datasets_aug \
  --log_dir logs/pointnext_cls_v2 \
  --num_points 1024 \
  --epochs 100 \
  --batch_size 32

# 分类测试
python main.py --mode test \
  --data_root /home/xubo/pointnet/datasets_aug \
  --log_dir logs/pointnext_cls_v2 \
  --split test \
  --num_points 1024 \
  --batch_size 32

# 输出: logs/pointnext_cls_v2/ 下有 best_model.pth, cls_report_test.csv 等
```

---

## 第 5 步：Ubuntu — 参数回归训练

当时的 PointNeXt 主力模型为 `_aux`（纯形状参数，不含 pose）。

```bash
# 先快速验证（可选）
bash quick_sanity_check.sh

# 全量训练 13 类（历史 _aux 版本）
bash train_reg_aux.sh

# 如果需要含旋转标签版本（实验性，形状参数精度更低）：
# bash train_reg_with_rot.sh
```

每类输出:
```
logs/pointnext_reg_cuboid_aux/
  best_model.pth
  regression_config.json
  regression_report_test.csv
  regression_errors_test.csv
  train_history.csv
```

### 模型版本说明

| 版本 | 后缀 | 回归目标 | 状态 |
|------|------|---------|------|
| `_aux` | 当时主力 | 纯形状参数（length, width, height, ratio, bulge, wallRatio...） | 历史对照 |
| `_rot` | 已弃用 | 形状参数 + rz | ❌ 形状参数精度明显更差，不建议使用 |

> `_rot` 版本虽然能预测旋转角，但形状参数精度下降严重。**根因不是数据量**：rz 这个标签在
> 当前数据形态下是**不可辨识**的，加数据也修不好。
>
> - **圆柱类**（Cylinder / ConeCylinder / CylinderDome / FourStageRoundTower）：底面是圆，
>   绕自身轴旋转点云**完全不变** → rz 根本没有定义，标签就是纯噪声。模型学不到，
>   那一维的梯度还会污染共享 backbone。
> - **矩形/多边形底面类**（Cuboid / GabledRoof / PyramidRoof / TruncatedPyramidRoof /
>   IndentedCuboid / LHouse / HalfCylinderRoof / TwoGableHouses / AsymmetricGableHouse）：
>   底面是矩形 → rz 只确定到 **mod 180°**；底面接近正方形时（PyramidRoof /
>   TruncatedPyramidRoof）只确定到 **mod 90°**。目标本身多值，MSE 把它拉向"平均"。
>
> 注意**归一化不背这个锅**：`normalizeForDL`（`exportpointcloud.cpp`）是质心平移 +
> 单一标量 maxRadius 各向同性缩放，两者都与旋转可交换 → 归一化后 rz 在 TXT 里完整保留。
> 真正会碾平朝向的是**逐轴**归一化（分别除以 bboxSize.x/y/z），当前没有做。
>
> 所以当前策略是：回归只用形状参数，**朝向不从模型来** —— 直接回填 metadata 里的
> 导出真值 `params.rz`（见 `dl-pipeline-log.md` 的 rz 回填一节）。要让模型预测 rz，
> 必须先做对称性改造（canonical pose / 把 rz 折叠到 [0°, 90°) 再回归），否则重蹈 `_rot` 覆辙。

---

## 第 6 步：Ubuntu — 批量测试

```bash
# 回归模型批量测试（使用 test 集，_aux 版本）
DATA_ROOT=/home/xubo/pointnet/datasets_aug \
SPLIT=test \
  bash test_all_reg.sh

# 手动逐类测试：
for cls in Cuboid Cylinder LHouse ConeCylinder GabledRoof PyramidRoof \
           TruncatedPyramidRoof HalfCylinderRoof CylinderDome IndentedCuboid \
           AsymmetricGableHouse FourStageRoundTower TwoGableHouses; do
  python main_reg.py --mode test \
    --data_root /home/xubo/pointnet/datasets_aug \
    --metadata /home/xubo/pointnet/datasets_aug/metadata/sample_params.json \
    --split test \
    --class_name "$cls" \
    --log_dir "logs/pointnext_reg_${cls,,}_aux" \
    --num_points 2048 --batch_size 32
done
```

---

## 第 7 步：拉回 logs 到 Windows

```bash
# === 在 Windows Git Bash 上执行 ===
scp -r xubo@<ubuntu-ip>:/home/xubo/pointnet/pointnext_simple/logs/pointnext_cls_v2 \
    E:/pointnet/pointnext_simple/logs/

scp -r xubo@<ubuntu-ip>:/home/xubo/pointnet/pointnext_simple/logs/pointnext_reg_*_aux \
    E:/pointnet/pointnext_simple/logs/
```

---

## 第 8 步：更新 C++ 插件推理路径

通过 QGIS 插件设置对话框修改（**推荐**，无需改源码）：
- 插件面板 → PointNet 路径设置
- 回归模型后缀：`_aux`（或新训练的版本如 `_v3`）

或直接改 QgsSettings 注册表 / 配置文件。详细配置项见 `parammodeler_config.cpp`。

---

## 附录 A：各脚本用途

| 脚本 | 在哪运行 | 用途 |
|------|---------|------|
| `augment_dataset.py` | Windows | 纯净数据 → 增强数据 |
| `main.py --mode train` | Ubuntu | 分类训练 |
| `main.py --mode test` | Ubuntu | 分类测试 |
| `main_reg.py --mode train` | Ubuntu | 回归训练（每类单独） |
| `main_reg.py --mode test` | Ubuntu | 回归测试 |
| `train_reg_aux.sh` | Ubuntu | 13 类 PointNeXt 回归一键训练（`_aux`，历史） |
| `train_reg_with_rot.sh` | Ubuntu | 13 类回归训练（`_rot`，已弃用） |
| `test_all_reg.sh` | Ubuntu | 13 类回归批量测试 |
| `quick_sanity_check.sh` | Ubuntu | 1 类 5 epoch 快速验证 |

## 附录 B：关键参数一览

| 参数 | 分类 | 回归 (`_aux`) |
|------|------|------|
| `--num_points` | 1024 | 2048 |
| `--epochs` | 100 | 100 |
| `--batch_size` | 32 | 32 |
| `--targets` | — | 纯形状参数（length, width, height, ratio, bulge, wallRatio...，各基元不同） |
| `--random_rotate` | — | **不传**（`_aux` 不预测旋转） |
| `--aux_features` | — | `bbox_x bbox_y bbox_z scale` |
| 数据集规模 | 500/类 | 500/类（train 400 + val 50 + test 50） |

### v2.3.10 target 重参数化

以下 4 类的回归 target 已改成更适合数据驱动学习的形式；旧模型输出仍由插件兼容映射。

| 类 | 新 target |
|---|---|
| `HalfCylinderRoof` | `length width wallHeight`（`radius = width/2` 派生） |
| `TruncatedPyramidRoof` | `bottomLength bottomWidth topLengthRatio topWidthRatio totalHeight wallRatio` |
| `LHouse` | `outerLength outerWidth cutoutLengthRatio cutoutWidthRatio height` |
| `IndentedCuboid` | `outerLength outerWidth outerHeight innerLengthRatio innerWidthRatio innerHeight innerMinXRatio innerMinYRatio` |

### 未来模型升级参考

当前 PointNeXt 对局部几何参数精度不足，建议下一版升级到 Swin3D 或 PTv3。详见主项目 `dl-pipeline-log.md` 第九章。
