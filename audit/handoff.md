# HANDOFF：build 114 两轮累计修复

当前入口：[2026-10-17 续查](../AUDIT-2026-10-17.md)、[MASTER-PROMPT](MASTER-PROMPT.md)、[机器记录](verification.json)。[第一轮历史](../AUDIT-2026-10-16.md)和 `verification-2026-10-16.json` 保留当时证据，不覆盖旧日志冒充新结果。

输入 PLUS ZIP revision 为 `f6c3d4ea457349d760c31c203069c119fc5f6613`。交付是该快照上的累计未发布补丁；Git baseline `a5c5e30b1d9b5d47be76cc47046b40f57962ea6d` 仅为本地比较快照。

已合入源码：NSIS 架构枚举；MRU load/rename 重复与别名归属；debug 快照 OOM；COM tag/指针类型；zig GUI 与 selfshot imm32；两轮手势基线清理和拖动交接；1:1 边界/计步/重绘/方向；滚轮余量；笔/触摸区分。未改发布版本、vendored libwebp 或历史作者。

**D1/D2 尚未整体验收。** 36 DIP 地板和每消息 0.5–2.0 政策保留；低于地板冻结，跨过后重新建基线。真实“不响应/大跳变”因果及体验不能靠宿主测试结案。原编号和完成条件已保留在 MASTER，不能从待办中消失。

本轮回归执行真实 C 缩放/渲染链；独立 1024 组矩阵、121 条变异与交叉构建见机器记录。坐标转换、view_set、窗口消息仍为适配器。缓存构建只输出少量警告不等于告警清零。

不要沿用旧结论：NSIS 有用户覆盖路径，9 属于另一套合法架构枚举；COM 是原型作用域 tag 问题。Clang 16 改的是函数指针，普通指针默认升级记录在 Clang 22。本机第一轮 zig 0.17 严格完整 x64 已通过。author:null、压缩包大小、AI 提交身份和断言数量都不能单独证明质量因果。

待有条件时执行 Windows/NSIS/MSVC 安装发布、真实触屏/捕获/锚点、UI/DPI、ARM64 运行和 GPU 分配验证。没有“全项目无缺陷”结论。代码变化后重跑相关门槛；变异只在隔离副本，禁止整树 checkout 抹掉修复。
