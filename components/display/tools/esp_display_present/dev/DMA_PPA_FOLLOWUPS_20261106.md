# DMA / PPA 后续对齐项 · 2026-11-06

用户要求先记录，暂不实现；当前讨论转向 present 本身的架构。

- 复核已确认：PPA SRM 对 dma2d_get_buffer_alignment_constraint(addr) > 1 的内存必须拒绝，即使窗口满足行对齐。干净 IDF 的 ppa_check_buffer_alignment 明确指出 SRM 内部宏块无法保证 MSPI 严格对齐；不能靠放宽 probe 提高命中率。已补充“窗口完全对齐但仍须回退”的 Host 测试。
- 对照 adapter 的加密/ECC 内存和 MALLOC_CAP_SPIRAM_NO_ENC 分配、校验策略，检查适用芯片和 IDF 能力。
- DMA2D 已检查地址、stride、X 和宽度；后续评估在生产者安全提供完整扩展区域的前提下，对齐 damage 以提升硬件命中率。不能在提交后扩大未绘制区域。
- 动态对齐和真实分配需统一。当前 allocator 固定 64 B；进一步核对发现 plan resolver 对非紧密 stride 或 alignment != 64 明确返回 NOT_SUPPORTED，因此不是所有配置都能靠 executor 回退。以后同时处理规划、分配、容量和运行时 probe。
- adapter 的 LVGL PPA fill/blend 绘制加速尚未在 esp_lv_present 对齐。它与输出 SRM 旋转/拷贝是不同层次，应单独设计。

已核对 RGB565 CPU/PPA 字节序处理、硬件前后 cache 同步和目标容量检查；本次代码对照没有确认普通 RGB565 路径存在漏 swap 或漏 cache 同步。尚未完成加密/ECC + PPA/DMA2D 实板验证。S3 BOX-3 的 GRAM benchmark 不覆盖这些硬件能力。
