# 后续复查任务提示：保留 D1/D2 追踪

接手 `voidImageViewer_PLUS` build 114 两轮累计修复。先读根目录 `AUDIT-2026-10-17.md`、`audit/handoff.md` 与当前 `audit/verification.json`，再核对真实代码。第一轮历史在 `AUDIT-2026-10-16.md` 与 `audit/verification-2026-10-16.json`。未发布、未拉取远端 HEAD、未重写上游历史。

报告、提示词、作者陈述和测试数量只是线索；按源码与可重复实验裁定，以官方 Web 核对 API。保留原任务编号，不因相关状态已修就删除整个待办，也不把未证实根因当事实。

| 原项目 | 当前已修 | 仍未完成 |
| --- | --- | --- |
| D1：贴近双指开始时不响应 | 丢弃基线同时清残余倍率；无效 GF_BEGIN 不继承旧段基线 | 36 DIP 地板保留。低于地板冻结，越过后首样本重建基线，不能称“必然整次失效”。调整地板需要真实设备序列与明确行为要求 |
| D2：展开时大跳变 | 1:1 重复首步、漏搜负梯级、短轴误判、模式漏更新、无候选反向 clamp；小量滚轮累计 | 0.5–2.0 每消息政策保留；真实触屏未验收。距离比大不等于数据坏，不凭空预热 N 样本或收紧速度阈值 |
| D3：单指转双指拖动交接 | GID_BEGIN 调用 doing_cancel 再复位；BEGIN/END 返回 DefWindowProc | 适配器证明调用，捕获重入、提升鼠标交错与锚点仍需 Windows 验证 |

缩放路径为 `_viv_on_gesture` → `_viv_zoom_steps`；滚轮单独累计带符号余量后调用同一函数。不要恢复梯级→模拟滚轮量→梯级。1:1 方向上无可达梯级时保留原模式，避免 clamp 反向。极端填满配置下，原生大小与允许区间仍可能不连续，不能写“所有过渡完全连续”。

先运行：

```sh
python3 tests/native_regression_test.py --sanitize
python3 audit/evidence/replay_zoom_matrix.py
python3 tests/zoom_math_test.py
python3 tests/menu_structure_test.py
```

原生回归执行真实 C renderer、梯级与动作；坐标转换、view_set、窗口消息仍有适配器。模型、清单、PE 检查不能代替 GUI/触控/安装。当前变异表 121 行，只在隔离副本运行；要求基线绿、锚点有效、退出原因正确、源码还原。禁止 `git checkout -- .` 清掉工作树合法修复。

下一步优先取得 Windows 证据：

1. D1/D2 的时间、dwID、dwFlags、完整 ullArguments、中心、DPI、捕获、doing、1:1/梯级/实际尺寸；跨地板、收拢再展开、快慢变化与逆转。
2. 真实捕获交接；带工具栏/状态栏、多显示器、不同 DPI 的缩放锚点。
3. NSIS/VS 安装与发布配置，架构分支、smoke/render/QOI 和发布安全门。
4. 同图、同配置、同导航及实际后端的内存分配；用复现条件和截图核对 UI。

保持小范围：先失败回归，再修根因并复查。保留 C/头文件 CRLF、其他文本 LF 和版权；真正发布时再改版本。不猜历史作者，不用全局强制转换掩盖错误，不把缓存可见警告数当全量清洁证明。
