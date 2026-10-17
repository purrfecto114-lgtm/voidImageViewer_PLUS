# 验证证据索引

- `verification-logs.zip`：第一轮 44 份原始日志，元数据在 `../verification-2026-10-16.json`。当时变异为 112 行，历史结果在 `mutation-results-2026-10-16.json`。
- `verification-2026-10-17.zip`：第二轮原始失败/最终通过、构建/PE、变异与独立矩阵日志。故意失败不表示最终树失败；其中最初 policy 失败来自 fixture DPI 未复位，未计为生产问题。
- `mutation-results.json`：当前 121 行、四个隔离副本（31/30/30/30）的结果与源码还原/哈希记录。
- `replay_zoom_matrix.py`：可在源码根目录运行的真实 C 1024 组边界复跑器。默认使用临时输出；`--output DIR` 保留实际 C、命令与日志。不依赖前任工作区路径。

原始日志按字节保留，包括编译器自然输出的行末空格；没有为源码空白门改写它们。ZIP 中不含编译器、项目可执行文件、对象或账号资料。当前构建日志是使用既有 Zig cache 的可见诊断，不能据其 warning 数宣称全量告警清零。详细范围与未执行项在 `../verification.json`。
