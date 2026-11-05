# 本地改动记录

基于上游 `dev`（版本号仍为 2.6.14）的本地分支 `perf/zwo-video-stats`。

原则：**导星算法、脉冲调度、相机驱动行为不变**；只改显示路径、工具窗口和信息展示。
所有新增开关的默认值都等于上游行为，不改配置就是官方行为。

## 显示与帧率

- 采集路径不再强制同步重绘：`UpdateImageDisplay` 末尾的 `Refresh(); Update();` 去掉
  `Update()`，重绘回到事件循环里合并（`4541082`）
- 主画面去掉多余的一层客户区缓存位图：`wxAutoBufferedPaintDC` → `wxPaintDC`，
  `Guider::OnErase` 不再 `Skip()`。这一项随窗口面积线性增长，120 DPI 高分屏上尤其明显
  （`009b6e7`）
- 图像**缩小时**可选单遍渲染：直接按目标分辨率做 box 采样并在路上套 LUT，跳过整帧
  `CopyToImage` + `Rescale`。放大仍走 wx 的双线性，避免小图马赛克
  （`/Display/FusedRender`，默认 **0**；`0eabfae`）
- 移除显示帧率上限开关（`/Display/MaxFps` 已删除）。当初它有效是因为重绘本身要 40-75 ms，
  瓶颈消失后同一开关只剩 1 ms 差异（`3427312`）
- 曲线窗口：只重绘绘图区、去掉它自己那层缓冲、网格笔从 `wxPENSTYLE_DOT` 换成
  `wxPENSTYLE_SHORT_DASH`（GDI 逐像素画点线，单次重绘 38 → 17 ms）（`7c59138`、`3fdc68c`）
- `timeBeginPeriod(1)`：1 ms 系统定时器，去掉相机轮询被 15.6 ms 量化的问题
  （Windows 专用，`CMakeLists.txt` 链接 `winmm`）（`adfcace`）
- `usImage::CalcStats` 从采集 worker 移到显示侧，不再占用"图像进内存 → 脉冲发出"的
  关键路径；`StatsValid` 防止重复计算（`adfcace`）
- 每帧路径上的 profile 读取改为启动时缓存（`LoadProfileSettings`），否则每帧一次注册表
  查询并写一行日志（`c2433bc`）

## 状态窗口

- 新增"帧率"一行：0.8/0.2 的 EMA，间隔超过 1 s 视为暂停不计入（`dee9e83`）
- 停止采集时重置 EMA（`338f419`）；无数据时该单元格留空而不是显示占位串（`c9e5848`）

## 星点图（Tools > Star Profile）

- 放大视图改为从**原始帧**裁剪，坐标与 `CurrentPosition()` 同源。原来从缩放过的
  `DisplayedImage()` 裁剪、却用图像坐标摆十字，一个标量 `scaleFactor` 无法同时表示
  `dstW/srcW` 和 `dstH/srcH`，于是留下一个恒定的亚像素偏移（放大 9 倍后 4-9 屏幕像素）
  （`7dd19ce`）
- 十字加上 `0.5*(zoom-1)` 的半像素项：`wxImage::Rescale` 把目标像素 k 映射到源坐标
  `(k+0.5)/zoom-0.5`（`4e73a61`、`99e8d82`）
- 修掉同一函数里的 use-after-free：绘制时读取 `CurrentImage()`，而那指针由采集线程持有、
  下一帧到达时会被 delete，崩溃表现为 wx 回调里的 `0xc000041d`（`7dd19ce`）
- 该窗口的 `wxAutoBufferedPaintDC` 去掉——本来每次就是全量重绘，缓冲只多拷一遍
  （`7dd19ce`）
- 探针（`/Display/PaintTiming=1` 时输出）：十字位置、渲染后质心、图像坐标质心、帧号。
  注意探针自身有成本（每帧 `ConvertToImage` + 扫 326×326）

## 相机

- ZWO 可选视频模式与带宽覆盖：`/camera/ZWO/VideoMode`（默认 **0** = snap）、
  `/camera/ZWO/BandWidth`（默认 **40** = 上游行为）。视频模式在短曝光下快一个量级，
  但长曝光会把脉冲压进曝光窗口，所以不默认开启（`c0a5454`）
- `usImage::CalcStats` 用已有直方图取 `FiltMin/FiltMax`，去掉整帧 `Median3` 和第二趟扫描；
  `MinADU/MaxADU/MedianADU` 不变（8.3 MP 下 90 → 10 ms）（`c0a5454`）
- 模拟器传感器尺寸可配置：`/SimCam/width`、`/SimCam/height`（默认 752x580）（`4a1129e`）

## 实测

条件：模拟器，1920x1080，16-bit，10 ms 曝光，GUIDING 状态，取中位数。

| 条件 | 周期 | 组成 |
|---|---|---|
| `frame_download_ms=50`（模拟器默认） | ~100 ms（10 fps） | 假下载 50 + 应用侧 ~40 |
| `frame_download_ms=0` | **59 ms（17 fps）** | 模拟器成像 28、导星脉冲 24、重绘 12、星点+算法 9（含探针） |

结论：应用侧目前只剩图像重绘约 12 ms。真实相机的 1080p 视频模式读出 11 ms、没有模拟器
那 18 ms 成像开销，按现开销算 30 fps 可达。

其他机型（早前测）：4K snap 249 ms → 视频模式 128 ms；1080p snap 158 ms → 视频模式 80 ms。

## 已知未做

- 4K 30 fps 不可达：video 读出 34 ms（消费者慢于帧间隔时 `flush_buffered_image`
  每轮丢两帧），加上 8-10 ms 统计与 1 ms 寻星约 44 ms/22 fps。要突破需要把"排空到最新帧"
  改成"只取最新帧"，预计 34 → 21 ms
- 显示部分仍有 ~12 ms（图像）+ 关闭窗口时 0、打开曲线窗口时 ~17 ms
