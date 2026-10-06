# Rillshot 1.2.2：资源与体验审查

日期：2026-10-06。基线为仓库提交 `d2bd86a` 的 1.2.1 源码。以下结果来自同一 Windows 11 主机、x64/MSVC 19.51 Release 构建。1.2.2 仍为开发候选，未发布 Stable。

## 截图区域外调暗

「设置 → 偏好 → 截图时调暗区域外」默认关闭，保存到 INI 的 capture/dimOutside。旧配置缺少该键时继续关闭；保存主题或快捷键时会同时保留该偏好。

开启后创建一个无激活、鼠标穿透的静态分层窗口，通过窗口区域精确减去物理像素坐标的截图矩形。没有全屏截图位图、采样线程或动画定时器。结束、取消、启动失败、析构时销毁窗口；显示布局改变时立即隐藏，避免旧孔洞覆盖新布局。

分层窗口鼠标穿透依据 [Microsoft 的窗口说明](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#layered-windows)。受控桌面实测确认开启前后 DXGI/GDI 截图区域的 RGB 像素相同，孔洞之外亮度下降。边界回归检查首末行/列、焦点保持、命中透传与窗口清理。

## 捕获与拼接审查

- 原 DXGI 每次采样均创建 factory/device/duplication/staging。新版将 device、duplication、ROI staging 保存在单次任务的后端实例中；采样不再重复建立 GPU 会话。
- 已有 ROI 像素且没有新桌面帧时，直接读自己的 staging 副本。首次等待最多 100 ms，之后以 0 ms 查询；不是从已释放的 acquired desktop texture 读取。桌面更新照常复制新 ROI。API 的超时和访问丢失语义见 [AcquireNextFrame 文档](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutputduplication-acquirenextframe)。
- 选区或输出布局改变时重建资源；访问丢失/设备/映射错误时释放旧状态。Auto 模式将最近成功的后端放到前面，避免每个 GDI 采样前重复尝试不支持的 DXGI；健康后端失效时仍会尝试其他后端。
- NCC 粗筛、精排预算、64 MiB 双帧亮度缓存上限、精确行 KMP、低信息/低置信度/歧义门禁均保持。没有通过降低匹配质量或缩短稳定观察来换取速度。
- 120 ms 最小等待、50 ms 采样间隔、两次连续稳定比较、512 MiB 组装图像预算和恢复点节奏保持。512 MiB 是组装图像预算，进程还会使用捕获帧、GPU 资源、亮度缓存与编码器工作内存。
- 最终固定边框复制完成后先释放末帧和捕获资源，再编码结果，降低保存阶段的无用常驻资源。

## 长图输出

原恢复点与最终输出先 materialize 整张图，会同时持有图像块和等大的连续副本。新版以输出顺序访问图像块，分批向 WIC 写出，每批最多 64 行；向上截图反向访问图像块，但每块内部保持从上到下。编码器依据 [WritePixels 的重复调用契约](https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwicbitmapframeencode-writepixels) 写入后续行。

保留临时文件、原子替换和未经确认不覆盖的策略。PNG/BMP 解码后与参考文档逐像素相同；测试包含不整除 64 的块高、跨接缝行序、向上/向下和拒绝覆盖。现有日志的 materializeElapsedMs 字段保留为 0，编码耗时继续记录。

## 测量

每项使用独立进程运行 3 次，基线/新版交替执行，表中为中位数。重复捕获包含首个设备初始化；没有保存桌面画面。长图为 1024×32768 BGRA、32 个图像块的合成图，解压像素总计 128 MiB。

| 局部测量 | 1.2.1 | 1.2.2 |
|---|---:|---:|
| 1280×720 DXGI 连续 30 次采样的平均耗时 | 107.073 ms | 6.286 ms |
| 捕获进程峰值工作集 | 56.980 MiB | 47.445 MiB |
| 合成长图保存耗时 | 372.463 ms | 353.232 ms |
| 保存进程峰值工作集 | 270.512 MiB | 142.605 MiB |
| 保存进程峰值提交内存 | 262.621 MiB | 134.418 MiB |

捕获循环耗时减少约 94.1%，合成保存峰值工作集减少约 47.3%。**这不是整次长截图的速度主张**：真实会话还包含滚动、稳定观察、拼接、页面动画和编码，压缩内容与设备也会影响结果。这里的保存耗时改善较小，主要收益是消除一个完整长图副本。

捕获平均耗时三次原始值：1.2.1 为 107.073 / 110.017 / 106.858 ms；1.2.2 为 6.356 / 5.892 / 6.286 ms。新版每 30 次只创建 1 个设备和 1 张 staging 纹理，24 / 26 / 27 次使用未更新的 ROI。

保存峰值工作集三次原始值：1.2.1 为 270.508 / 270.512 / 270.516 MiB；1.2.2 为 142.602 / 142.609 / 142.605 MiB。相同内容的接缝合成基准：普通接缝约 8.843 ms，相同画面约 0.843 ms；接缝实现本轮未改变。

## 验证与复验

- Windows 严格警告原生构建与 12/12 CTest 通过。
- 模拟后端覆盖失败返回和抛出异常时的回退，以及随后沿用健康后端，原有异常边界保持。
- 受控桌面验证：ROI RGB 相同、区域外调暗、30 次采样资源复用、重绘后新帧刷新、改换 ROI 后重新初始化，通过。
- WinUI Release 构建通过，0 警告、0 错误；Portable Preview 打包与 CLI 1.2.2 版本检查通过。
- 发布脚本恢复 10 个临时环境变量的同进程检查通过；MSBuild 节点复用已关闭，避免卸载临时盘后遗留失效路径。

```powershell
cmake -S . -B out/build/windows-122 -G "Visual Studio 18 2026" -A x64 `
  -DRILLSHOT_BUILD_TESTS=ON -DRILLSHOT_BUILD_WINDOWS_APPS=ON `
  -DRILLSHOT_WARNINGS_AS_ERRORS=ON -DRILLSHOT_BUILD_BENCHMARKS=ON
cmake --build out/build/windows-122 --config Release --parallel 2 -- /nr:false
ctest --test-dir out/build/windows-122 -C Release --output-on-failure
./out/build/windows-122/Release/windows_resource_benchmark.exe --verify-capture
./out/build/windows-122/Release/windows_resource_benchmark.exe --capture
./out/build/windows-122/Release/windows_resource_benchmark.exe --save
```

`--verify-capture` 会短暂显示自己的无激活色块测试窗口，结束后清理。`--capture` 只读取实时桌面选区并输出数值；`--save` 只编码合成图，并删除生成的临时文件。它们均为可选基准，不是 CI 门禁。

本次没有完成真实云文档、HDR/旋转屏、显示器热插拔、锁屏恢复与图形界面操作的完整人工矩阵；这些不能由受控色块或模拟后端替代。正式 Stable 发布前仍按 RELEASING.md 进行实际页面验收。
