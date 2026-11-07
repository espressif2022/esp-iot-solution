# S31 QSPI：补齐 adapter 原生 TE 基线

这是对原四板报告中 adapter TE 漏测的补充，不修改旧证据，也不将独立原生配置伪装成 present C1/C2、T1/T2。

## 硬件与测试口径

S31 mosaico / CO5300，MAC 30:ed:a0:f4:0b:34，本轮ACM1，480×480 RGB565、QSPI40MHz四线、TE GPIO43。IDF、LVGL综合场景、共同sdkconfig/defaults、生产组件指纹以及面板初始化源码均与此前present C/T测试一致；验证信息见validation.json。只修改测试桥接、用例生成和单独的adapter_te_pipeline测试字段。

## 配置结构

- SYNC_FULL：一个全屏LVGL绘图缓冲，同步等TE后发送；只有0°使用软件配置，其余角度明确SKIP。
- ASYNC_FULL：一个全屏LVGL DIRECT绘图缓冲加一个全屏staging，dirty合并后worker整帧发送；四角度。
- ASYNC_BOUNCE：同上，另开两个16行传输bounce；四角度。

TE模式的require_double_buffer不能选择present式T1/T2；secondary在异步模式作为staging，并未交给LVGL当第二绘图缓冲。因此既不能称adapter没有TE，也不能把不同配置直接填入同一C/T格。网页以原生基线独立列行。

## LVGL FPS

| 配置 | 0° | 90° | 180° | 270° |
|---|---:|---:|---:|---:|
| ADAPTER_TE_SYNC_FULL | 17 | − | − | − |
| ADAPTER_TE_ASYNC_FULL | 24 | 22 | 23 | 22 |
| ADAPTER_TE_ASYNC_BOUNCE | 20 | 19 | 19 | 19 |

9组有效成绩，3组同步软件旋转明确跳过。每格单次16个场景的LVGL平均FPS，不是物理刷新率，也不代表统计显著性。adapter的transport frame计数不可用，不能把日志frames=0解释成无输出。

## 时序与限制

串口测得整帧传输约23.9ms，TE周期约16.7ms，存在Full-frame transfer exceeds one TE period提示。完成跑分和启用TE不能证明所有场景无撕裂；用户反馈本轮显示正常，记录此反馈；它不等同长期压力或仪器测得无撕裂证明。GPIO isr service already installed日志为重复打开时复用已安装服务，不作为测试失败；其他错误、断言、超时仍会阻止有效通过判定。

初测复用了compose字段选择异步模式，造成日志te_buffers=3/4易误读，采集被主动停止。保留initial-selector-trial，其分数不进入最终表；正式测试改用独立adapter_te_pipeline字段并从头重跑。

四时点堆快照保留在results.json中：显示实例打开前、打开后、运行期间最小剩余、关闭后；不包含全框架反初始化，也不单独统计静态区。

共用工具16项unittest及格式检查通过。原16MiB Flash已先核对备份并持久保存。恢复先后因USB连接中断、整片ROM擦除失败而重试；改用ROM 1MiB分块后已完成1至9块，第10块写入期间用户明确要求保持停止，写入进程已终止。原固件恢复尚未完成，未做全片校验，不保证板子能正常启动。备份保留；未经用户新授权不得继续操作板子。状态见restoration-status.json，失败及分块日志均保留。
