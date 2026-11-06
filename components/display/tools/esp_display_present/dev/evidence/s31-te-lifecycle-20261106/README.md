# S31 mosaico QSPI TE 硬件验证 2026-11-06

板子 ACM0，MAC 30:ed:a0:f4:0b:34，CO5300 480×480，TE GPIO43，QSPI 40MHz 四线，CPU320MHz、PSRAM200MHz。使用干净 IDF 7b9cc1ac，无 IDF 补丁。

首次创建失败：IDF 支持 IO psram_dma_direct 配置但没有 SPI memory-info 查询 API。保守 staging 上限无法容纳 TE 全屏，创建返回 NOT_SUPPORTED，尚未送帧。新增硬件配置 spi_psram_dma_direct 供应用按真实 IO 配置声明能力；查询成功时驱动结果优先。默认 false 保留保守行为。Host 同时覆盖查询存在/不存在、声明与驱动冲突，共39/39通过。

C1/C2 compose × T1/T2 tile 共4组合，各运行两轮；同一 presenter 连续0/90/180/270/0旋转，完整像素 oracle 加局部更新，8 case/48帧通过。四组合各测试持有lease取消、等待TE时停止、CPU复制未完成时停止、真实DMA在途时停止，共16 lifecycle通过；DMA pending=0、bad=0，内存回到同一基线。用户确认目视正常。

WAIT_TE 场景暂时屏蔽真实GPIO中断；COPY_PENDING延迟真实executor入口，随后执行真实复制；DMA_PENDING观察真实硬件回调，未伪造DMA完成。日志中的共享GPIO ISR service already installed 为重复创建的幂等提示，未作为失败；SPI callback overwritten 是解绑/替换回调时驱动提示，保留在原始日志中。

全屏传输理论23.04ms，屏幕周期约16.75ms（138%），不能将本轮功能与生命周期通过等同于所有负载下无撕裂保证。本轮不是 FPS benchmark，也未完成与 adapter 的 TE 跑分对比。

原始完整16MB固件保存在本机 /tmp/present-s31-te-20261106/original-16MB.bin，未纳入仓库。恢复与完整校验见 restore 日志。

恢复完成：独立 verify-flash 对完整16MB校验 digest matched，随后复位运行原始固件。
