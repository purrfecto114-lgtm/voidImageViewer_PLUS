# 本次验证环境说明

运行环境是受限 Linux 文件系统，未提供 Windows、Visual Studio、NSIS 或触屏/GPU 运行环境。宿主 C 回归使用 GCC；交叉编译工具来自 PyPI 的 ziglang 0.16.0 / 0.17.0 wheel。

原版 Zig 可输出版本，但编译会因无法读取 `/proc/self/exe` 报 `unable to find zig self exe path: FileNotFound`；默认临时目录也不可用。这属于验证环境的工具定位问题，不是项目 C 编译错误。

为继续验证，在工具链副本中仅把一次出现的 14 字节定位字符串 `/proc/self/exe` 换为同长度的 `./zig-selfpath`，让它读取当前工作目录里的本地工具链符号链接；未改变编译器前端、诊断策略、代码生成逻辑或系统权限。Zig cache 和 TMPDIR 指向任务目录。输入 wheel、原始/适配编译器的 SHA-256 记录在 verification.json。所有本次 Zig 构建结论均来自该环境适配副本，尚待标准 CI 环境复验。

定位链接、编译器和下载包不进入交付源码；正常环境直接使用 CONTRIBUTING 中的固定版本命令，无需这项适配。测试构建缺 imm32 的失败日志与修复后链接日志分别保留，未将失败日志改写成成功。

ASan 的默认 LeakSanitizer 在此环境无法遍历 `/proc/*/task`。对 MRU 别名回归的单独 ASan 实验在临时生成体中设置 `__asan_default_options` 关闭 leak 检查与符号化，内存访问检查仍启用，fixture 另检查分配归属。正确源码通过，退回旧式别名比较时出现 heap-use-after-free。常规原生 gate 使用 UBSan，不依赖这项环境适配。

第二轮（2026-10-17）继续使用同一 0.16.0 环境适配副本与既有 Zig cache，重跑 x64、ARM64、selfshot 全脚本。每次脚本清空项目对象目录，但未清全局编译缓存；本轮日志各见 1 条 warning，只代表该次可见诊断，不能与第一轮全量 779/780 直接相减或声称告警清零。当前产物哈希与所编译 viv_view.c 哈希分别记录在 verification.json。0.17.0 的完整构建结果属于第一轮历史，第二轮未重跑该工具链。
