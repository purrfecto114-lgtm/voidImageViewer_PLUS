# 方向 A「淬炼」动态待办

基线：用户提供的 `1.1.17-repair.2-src.zip`，本地导入提交为准；不假定等于任务书的 `8362669`。日期：2026-10-22。

## 状态约定

- `[ ]` 未完成；`[x]` 已完成；`[-]` 有明确环境限制／待 Windows 实机验收。
- 测试只记本轮实际结果，包内历史 verification 不算本轮证据。

## 工作清单

- [x] 阅读任务书、解压源码与样例、建立可回滚基线。
- [x] 加载 library 与 PDF skills，查阅 Microsoft / W3C 技术依据。
- [x] 运行未修改基线的构建和适用回归，记录环境：x64 构建通过（Zig 定位适配），9 个宿主回归套件通过。
- [x] 以符号为锚点审阅修改函数、状态、调用者、相关头文件与测试 / 构建约束；对大模块做定向交互复查，不宣称进行了全树逐行安全审计。
- [x] 主题：暗色 hover / down，亮色系统色校正，三灰层级，accent 对比度。生产 C 取色探针通过；暗 hover 1.649:1 / ΔL*15.692，亮 hover/FACE 1.464:1，描边/白 3.168:1，accent 文字最小 3.086:1。
- [x] 状态栏：序号/文件名分列、去路径、右侧缩放 chip、26 DIP 最小高度（扣 native 边框）、×、保留降级及快路径。18,752 个生产布局宽度/掩码案例及 9 候选预算测试通过，菜单/模拟/原生回归及 x64 构建通过。
- [x] 一致性：共享 radius=6 / pad=8 / gap=4 token；13 半粗菜单、12 工具栏、11.5 状态、16 百分比；数字固定宽度字体，状态/胶囊共享基线。6 档 DPI 的生产字体缓存/资源生命周期/基线测试通过；x64 构建、菜单、模拟、字节回归通过。
- [x] P0：全屏 capture 长按保持可见，释放后正常 idle。生产定时器的 6 秒保持、释放后 idle、非分层回退、窗口模式、时钟回绕测试通过；x64 构建和菜单回归通过。
- [x] P1a：保留临时 1:1 滚动的可见位置与 native 几何；普通 scroll 本来不丢位移，保持原行为。生产取消函数与手势函数联合回归通过。
- [x] P1b / c：保留 36 DIP 冻结/重建基线及每消息 0.5–2.0 比率策略；已由生产 C 回归核对，缺少触屏轨迹，不凭推测调整手感。
- [x] P1d：触控签名 + 150ms 过渡尾窗过滤 down / double-click / up；保留被过滤 down 的配对 up，鼠标和笔不受过滤。快速新触控点击落在尾窗内也会被过滤，需实机评估此取舍。
- [x] P2：保留 150ms 轮询；ordinal 缺失时沿用 NULL 跳过和 owner-draw 回退；修复主窗口取消拖拽时无条件释放同线程新 owner 的 capture。原生联合测试与旧函数反证均有效。
- [x] 已复查 menubar / toolbar / settings / msgbox / dialogs / playlist 相关路径。新增 P2 候选（序号缓存、丢 capture 的延迟重排、msgbox HDC 复用）已报告，未扩大修改。
- [x] 每个生产逻辑块已构建并通过相关回归后提交。P2 同时补充四个旧实现反证：hold / decimal / gesture handoff / capture owner 均被新回归拒绝。
- [x] 10 个当前环境可执行套件全通过；4 个旧实现反证有效；104 单元 x64 与 ARM64 编译、禁改文件 / 函数、导入表与 51 样例一致性核验完成。
- [-] Windows PowerShell smoke / render golden、真实触控与 Win7/10/11 目检未执行；不能以 Linux 宿主适配器或交叉编译代替。Win7 UCRT 部署与原生 26 DIP 量测单列待验。
- [x] 已准备完整源码、分块提交、本轮证据、六页中文报告与 Windows 验收步骤；提交历史 / 补丁在归档时从当前 HEAD 导出。
- [x] 交付内容及范围已核验；归档、文件保存和下载链接由本会话最终回执确认。

## 当前事实与限制

- 原始附件没有二进制；当前主机没有 VS / Wine / PowerShell / Xvfb。
- 已安装到工作目录的 Zig 0.16.0；本轮 x64 与 ARM64 交叉构建已通过，Windows 实机运行仍未验证。
- 指定 GitHub fork 网页本次抓取被拒，不能以线上 HEAD 替代附件源码。
- 不触碰 GDI / hwgl / hwd3d / `_viv_hw_render_frame` / `viv_export` 的生产契约。

## 测试与决策日志

后续随实际执行更新。

- 主题块：x64 构建通过，菜单套件通过；4 条旧色值断言按新设计契约更新，新增生产 C 调色板探针。

- P1 块：生产手势/取消联合测试、菜单结构、zoom math 与 x64 构建通过；真实双指交接和尾窗大小仍待硬件验收。

- 新增 P0（已修复）：状态栏 MB/GB 小数尾部在覆盖终止符后再次求长度，可能扫描未初始化尾部并越界。非零尾部生产代码测试修复前失败、缓存长度修复后通过；x64 构建及菜单回归通过。

- P2 构建通过；ARM64 交叉编译补充通过。新建的四项反证均先成功编译，再在旧生产代码上失败，修复树对应测试通过。

- 最终边界：源码和 PE 相对基线均无新增 DLL / API 导入；原 Zig 基线与本件都带 UCRT，不能承诺裸 Win7 启动。6 页 PDF 已渲染逐页目检，并核查文本边界。

---

# refine-B：地毯式诊断清偿轮（2026-10-25）

基线：refine-A 交付包（8ecd49b）。输入：外部地毯式诊断报告（P0=0 / P1×3 / P2×15）。

## 属地复核结论（先验后修，不盲改）

- 属实并修复：P2-1 msgbox HDC 释放后复用（ReleaseDC 前移测量块）；P2-3 more_wbuf 64 容量配 1024 预算（string_printf 参数化 + _n 变体，契约随参数走）；P2-4 序号缓存无失效通道（删缓存直查：事件驱动更新一次 O(n) 名字比较，代价比失效通道便宜）；P2-5 胶囊丢 capture 不清偿（WM_CAPTURECHANGED 内清偿重排）；P2-8 string_cat 无终止符契约（第一循环加 size 地板）；P2-9 ini_precheck 短读（numread == size 方可扫描）；P2-10 utf8_to_int / string_to_int 有符号溢出 UB 与 INT_MIN/-1（饱和钳制，i 永不为 INT_MIN，出口 i*sign 无 #DE）+ auto_fit 乘除 64 位中间量 + 负值地板钳制（viv.c / viv_view.c 共 4 处）；P2-11 config.c *p++ 弃引用；P2-7 vs2026 ARM64 输出名漂移（统一 voidImageViewer.exe）；P2-13 VS2005 绝对库路径（两工程 Debug 配置删除）；P2-15 NOTICES 未披露 libwebp 5 处本地整数加宽（补 Local modifications 条目）。
- 属实并部分清偿：P1-1 交付验证件无 CFG——zig 验证线补 -mguard=cf 编译插桩 + os.c 弱符号 dispatch thunk（.00cfg 语义；MSVC 编译为空块，四构建清单不动）；zig cc 0.16 过滤链接侧 /guard:cf，GUARD_CF 位在 zig 产物上不可达（已在 build.sh 注释如实声明），PE 硬化以 mingw（已带 --guard-cf）与 MSVC（/guard:cf）线为准。
- 属实并补测试：P1-2 qoi.c 手工解码器零行为测试——新增 tests/qoi_decode_test.py + tests/native/qoi.c 宿主套件：每次运行从工作树提取真实 qoi_load / _qoi_read_u32_be / safe_size_mul 与 Win32 桩（hglobal 流、阶段标记、分配宏、双门像素预算）链接执行；23 用例覆盖全部 chunk 类型、行推进、尾随块宽限、RUN 越界守卫、三通道 alpha 钉 255、敌意电池（坏 magic / channels / colorspace / 零尺寸 / 短流 / 坏终止标记 / 截断 RGB 与 LUMA / RGB 贴终止标记 / 输入耗尽 / 双预算门 / 中途取消）；UBSan 下全绿。mutation_teeth 注册 qoi 套件 + 4 颗新牙齿（RGB 尾界 / RUN 画布界 / 终止标记检查 / channels 门）+ 2 颗 round-151 旧牙升级行为级判定，全部实测咬合。
- 复核后不属实：P2-2「满长保存路径 1-wchar 越界写」——viv_view.c:952 / 1134 与 viv_load.c:1044 三处 tobuf 实际均为 wchar_t tobuf[STRING_SIZE+1]（容量 1025，nMaxFile=1024 时 GetSaveFileName 至多回填 1023 字符 + NUL，tobuf[len+1]=tobuf[1024] 在界内），报告引用的 `tobuf[STRING_SIZE]` 在交付树中不存在，未做任何修改。
- 复核后不动：localization.c 的 <0 死检查（枚举参数防御残留，删除零收益且留下负索引敞口）；P1-3 viv_install.c 注册表行为测试（需 Windows 实机，宿主无法真实化，维持 [-] 状态）；P2-6 binary.json 警告计数矛盾与 P2-14 路径泄漏（证据类文件不入本轮净源码包，矛盾面随包消失）。

## 本轮验证（Linux 宿主，实际执行）

- [x] 104 单元 x64 zig 构建（含 -mguard=cf 插桩 + 弱 thunk 链接闭合）；104 单元 ARM64 构建通过。
- [x] 10 个宿主套件全绿：native(--sanitize) / refine / menu(2495 钉，含 nav-index 钉随缓存移除更新) / zoom / byte / simulation / pixel_golden / repair×2 / qoi(新, --sanitize)。
- [x] qoi 牙齿 6/6 咬合实测（突变注入 → 套件红 → 恢复）。
- [x] clang 单文件复扫：config.c / string.c 无 unused-value / 无新增非良性警告。
- [-] Windows 实机项不变：smoke / render golden / 触控 / Win7 UCRT 仍待实机；gl_runtime 套件本机无 libEGL 不可复验（与 refine-A 环境边界一致）。
