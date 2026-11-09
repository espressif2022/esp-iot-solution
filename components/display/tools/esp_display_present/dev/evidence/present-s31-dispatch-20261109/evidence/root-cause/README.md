最终复现输入位于 images/*/test-inputs；sdkconfig、manifest、额外 .S/.lf 均逐镜像归档。PC采样仅为诊断：pc_address.h必须从该次最终ELF重新提取并验证，不能跨固件使用。源模板迭代中失败的采样与对齐尝试不参与通过表；这里只保留最终源码、采集控制器、数据处理和核验脚本。
