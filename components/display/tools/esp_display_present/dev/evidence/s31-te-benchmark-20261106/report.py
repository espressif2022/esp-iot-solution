from pathlib import Path
import json,re
r=Path(__file__).parent;rows=json.loads((r/'results.json').read_text());n=sum(x['admitted'] for x in rows)
s=['# S31 mosaico TE LVGL benchmark — 2026-11-06','', '硬件：ACM0，S31 mosaico / CO5300，480×480 RGB565，QSPI 40MHz×4，TE GPIO43，CPU320MHz，PSRAM200MHz。干净 IDF 7b9cc1ac，LVGL 9.6.0~1，SW draw units=2，refresh period=15ms，draw tile=50 lines/internal。', '', 'C 表示 compose 缓冲数，T 表示 tile 缓冲数。各组合新建 presenter/LVGL display，完整运行 benchmark，汇总后清零场景累加器；每组约69秒、16个有效评分场景。测试源使用链接包装器计数，无逐帧日志，不改组件或 IDF。', '', '| 组合 | 角度 | TE均值 ms | TE最大 ms | DMA均值 ms | LVGL FPS | 提交帧/秒 | 帧数 |','|---|---:|---:|---:|---:|---:|---:|---:|']
for x in rows:s.append(f"| {x['mode']} | {x['angle']}° | {x['wait_avg_us']/1000:.3f} | {x['wait_max_us']/1000:.3f} | {x['tx_avg_us']/1000:.3f} | {x['lvgl_fps']} | {x['transport_fps']:.1f} | {x['admitted']} |")
if n:
 avg=sum(x['wait_avg_us']*x['admitted'] for x in rows)/n/1000
 mx=max(x['wait_max_us'] for x in rows)/1000
 s+=['',f'按提交帧数加权 TE 等待均值约 **{avg:.3f}ms**，最大 **{mx:.3f}ms**，共 **{n}** 次放行。均值由各组微秒整数量化值加权，存在小于1µs舍入误差。', '', '计时口径：第一次调用真实 te_sync_admit 到返回 ESP_OK；重试间隔计入。该段开始时 compose 已可读取且前序传输不再阻挡本次调度，因此只统计 TE gate 放行等待，不包括前序 DMA 排队、LVGL 绘制或 compose 复制。包含 gate 本身的执行和线程唤醒开销。DMA 段从 te_gate.start 到真实传输完成回调，包含少量提交开销。', '', '窗口机制：Tf约16.75ms、active约16.21ms、初始传输估算约23.04ms，slow/end模式初始允许在扫描相位约[0,9.9ms)发送。后续每次放行取max(理论传输时间, 上一次实测传输时间)，实际约24ms时窗口会缩小到约9ms；策略未切换时不会重新打印窗口日志。有效窗口内可立即放行，不是每帧强制等待下一次TE；8.4ms只是随机等待单边沿的粗略估算，不能当作当前机制实测均值。实测DMA约24ms，调度已使用该反馈；当前模型仍依赖板级TE与扫描方向假设，本轮没有修改模型。', '', 'LVGL FPS为有效场景评分均值；提交帧/秒为整轮实际提交数/总耗时，二者分母不同。LVGL日志CPU=100%为其内部忙闲指标，不是双核硬件负载测量；不要据此判断双核100%。', '', '结果边界：本轮覆盖0°与90°，未测180°/270°，不是本轮adapter对比。串口通过不等同于光学无撕裂。GPIO ISR service already installed 是共享服务重复安装提示，测试允许ESP_ERR_INVALID_STATE；原始日志保留。callback overwritten 为解绑回调的驱动提示。', '', '原始整片备份在本机 /tmp/present-s31-te-20261106/original-16MB.bin；测试后的恢复校验见 restore-verify.log.gz。']
(r/'README.md').write_text('\n'.join(s)+'\n')
