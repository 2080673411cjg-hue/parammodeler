# LHouse辅助量单位一致性

日期：2026-10-21。仅验证输入单位和旧格式兼容，没有启动GPU训练、改写数据或checkpoint、切换默认模型或进行QGIS验收。

## 格式约定

- 新L形训练使用canonical_aux_units=physical。归一化TXT的跨度乘metadata物理scale，物理点云的跨度直接使用；体积为立方场景单位，scale为场景长度，长宽比为无量纲量，bbox_x/y/z_norm为物理跨度除以scale。
- 新格式写入config和best checkpoint，测试/推理读取权重标记；旧v4和axis_trial没有标记，保留legacy行为，不能直接套用新辅助量或只修改config伪装新权重。
- 新checkpoint没有匹配metadata时，调用者必须明确声明input_coordinates=physical或normalized。归一化输入需要有限正物理scale，不能按数据数值大小猜测单位。
- 插件本机及远程回归均传递坐标类型，服务透传。已知数据集TXT依据metadata标记，其他输入默认按物理坐标声明；没有metadata的归一化TXT需要调用者自行明确其语义和尺度。
- 本轮只让L形实验入口自动启用physical，其他已部署权重和默认模型路径不变。主目标、损失、方向处理和数据划分不变。

## 验证

9项辅助量、22项canonical管线和27项训练/服务检查通过，parammodeler_pointnet.cpp仅语法检查通过，未构建DLL或执行真实GPU模型推理。

额外读取现有800份L形训练记录，逐项验证physical.xyz=legacy.xyz*scale、physical.volume=legacy.volume*scale^3，主目标、scale和长宽比保持不变。100对相同测试点云在归一化存储与metadata反变换物理表示之间比较，最大bbox差约0.000003815场景单位，属于浮点舍入级别。

| 辅助量训练均值 | legacy | physical |
|---|---:|---:|
| bbox_x | 1.7842 | 28.7149 |
| bbox_y | 1.1139 | 17.5131 |
| bbox_z | 0.3721 | 5.6732 |
| scale | 16.0133 | 16.0133 |
| bbox_volume | 0.7413 | 3032.6484 |
| aspect_ratio_xy | 0.6287 | 0.6287 |

这些bbox是含增强离群点的实际全点跨度，尤其横向跨度可能大于建筑真实宽度，不应把它当作标签或把均值变化当成预测改善。本轮没有调整极值策略、裁剪离群点或增加拐角损失。

verification.json保留800份汇总、100份逐样本差值和源码SHA256。单元测试包括两种表示的模型输入/辅助向量相同、体积与归一化派生量正确、训练测试一致、旧权重缺少标记时保持原值，以及缺少输入声明或尺度时拒绝。

## 重训与部署

使用新目录ptv3_reg_lhouse_axis_scale_trial_normals训练，旧axis_trial作为对照保留；无需重新生成或增强数据。将E:/pointnet/ptv3_simple里的main_reg.py、train_ptv3_reg.py、ptv3_server.py同步Ubuntu，按scripts/README.md的新命令执行，训练结束仍自动测试metadata和geometry两个版本。

重训得到新权重后才能评估准确率。部署新格式前还需用户编译本轮插件、同步服务并重启，才能确保传递坐标类型；旧模型没有因此自动迁移或切换。
