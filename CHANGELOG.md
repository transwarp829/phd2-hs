# 本地改动记录

基于上游 `dev`（版本号仍为 2.6.14）的本地分支 `perf/zwo-video-stats`。

**导星算法、脉冲调度没有改动。** 变化集中在三处：显示路径、工具窗口、相机驱动的传输参数。
相机部分 ZWO 的默认值与上游一致；ToupTek 默认走最快路径，三项都可在相机属性对话框里单独回退。

## 显示与帧率

- 采集路径不再强制同步重绘：`UpdateImageDisplay` 末尾的 `Refresh(); Update();` 去掉 `Update()`，
  重绘回到事件循环里合并（`4541082`）
- 主画面去掉多余的一层客户区缓存位图：`wxAutoBufferedPaintDC` → `wxPaintDC`，
  `Guider::OnErase` 不再 `Skip()`。这一项随窗口面积线性增长，120 DPI 高分屏上尤其明显（`009b6e7`）
- 图像**缩小时**可选单遍渲染：直接按目标分辨率做 box 采样并在路上套 LUT，跳过整帧
  `CopyToImage` + `Rescale`；放大仍走 wx 的双线性，避免小图马赛克（`/Display/FusedRender`，默认 0；`0eabfae`）
- 曲线窗口：只重绘绘图区、去掉它自己那层缓冲、网格笔从 `wxPENSTYLE_DOT` 换成 `wxPENSTYLE_SHORT_DASH`
  （GDI 逐像素画点线，单次重绘 38 → 17 ms）（`7c59138`、`3fdc68c`）
- `timeBeginPeriod(1)`：1 ms 系统定时器，去掉相机轮询被 15.6 ms 量化的问题
  （Windows 专用，`CMakeLists.txt` 链接 `winmm`）（`adfcace`）
- `usImage::CalcStats` 从采集 worker 移到显示侧，不再占用「图像进内存 → 脉冲发出」的关键路径；
  `StatsValid` 防止重复计算（`adfcace`）
- 每帧路径上的 profile 读取改为启动时缓存（`LoadProfileSettings`），否则每帧一次注册表查询并写一行日志（`c2433bc`）

## 状态窗口

- 新增「帧率」一行：0.8/0.2 的 EMA，间隔超过 1 s 视为暂停不计入（`dee9e83`）
- 停止采集时重置 EMA（`338f419`）；无数据时该单元格留空而不是显示占位串（`c9e5848`）

## 星点图（Tools > Star Profile）

- 放大视图改为从**原始帧**裁剪，与 `CurrentPosition()` 共用图像坐标。原来从缩放过的
  `DisplayedImage()` 裁剪却用图像坐标摆十字，一个标量 `scaleFactor` 无法同时表示 `dstW/srcW` 和
  `dstH/srcH`，于是留下恒定的亚像素偏移（放大 9 倍后 4-9 屏幕像素）（`7dd19ce`）
- 十字加上 `0.5*(zoom-1)` 的半像素项：`wxImage::Rescale` 把目标像素 k 映射到源坐标 `(k+0.5)/zoom-0.5`
  （`4e73a61`、`99e8d82`）。加与不加在 9 倍放大下差 4 个屏幕像素
- 修掉同一函数里的 use-after-free：绘制时读取 `CurrentImage()`，而那指针由采集线程持有、下一帧到达时
  会被 delete，崩溃表现为 wx 回调里的 `0xc000041d`（`7dd19ce`）
- 该窗口的 `wxAutoBufferedPaintDC` 去掉——本来每次就是全量重绘，缓冲只多拷一遍（`7dd19ce`）
- 探针（`/Display/PaintTiming=1` 时输出）：十字位置、渲染后质心、两者差值。探针自身有成本
  （每帧 `ConvertToImage` + 扫 326×326），测量时它也是负载的一部分

## 相机

### ZWO

- 相机属性对话框新增 **High speed mode** 勾选框（默认开，`ASI_HIGH_SPEED_MODE`）。上游从未设置过这个
  控件，所以一直是 SDK 默认的关；它是用更低的 ADC 位深换读出速度，而 PHD2 输出本来就是 8-bit（`5c72ff1`）
- 相机属性对话框新增 **USB bandwidth**（40..100，默认 40 = 上游行为）和 **Video mode** 勾选框
  （默认关 = 上游的 snap 模式）（`c0a5454`、`4c002b7`）。`ASI_BANDWIDTHOVERLOAD` 上游钉在最小值且没有控件，
  而 QHY 的同类旋钮（`USB Traffic`）一直是可见的

### QHY

相机属性对话框新增 **Live mode (free running)** 勾选框（默认**关** = 上游的单帧模式，需重连生效）：

- 连接时 `SetQHYCCDStreamMode(handle, 1)`（必须在 `InitQHYCCD` 之前），初始化完成后 `BeginQHYCCDLive()`；
  取帧改为 `GetQHYCCDLiveFrame()`，跳过 `ExpQHYCCDSingleFrame` 和它那套按返回码决定的等待；
  断开或 ROI 变化时 `StopQHYCCDLive()`（ROI 改完再重新 Begin）（`b5611b0`）
- 流模式在 init 时锁定，所以 `BeginQHYCCDLive` 失败无法就地回落：会关闭设备并让连接失败，
  提示里写明要清的键名（`/camera/QHY/LiveMode`）
- **没有 QHY 相机可测**，代码是照 SDK 头文件写的。默认关闭，所以不影响任何人；启用后从日志确认
  `QHY: stream mode = 1 (live)` 和 `QHY: live mode started`，再和单帧模式比较同曝光同 ROI 下的帧周期

- 顺带修掉上游一个类型不匹配：`/camera/QHY/usbtraffic` 用 `SetInt` 写入（wx 存成 REG_DWORD），
  对话框却用 `GetDouble` 读（基类按字符串读），于是第二次打开时 wx 报
  `registry value ... is not text (but of type DWORD)`，值被退回默认的 30。改成 `GetInt`（`9c0c175`）

### ToupTek

三个传输参数各自独立暴露在相机属性对话框里（都需重连生效），默认值都取最快：

- **Video mode**（默认开）：`TOUPCAM_OPTION_TRIGGER` 0/1。开着是自由运行、拿最新帧；关掉即上游的
  每帧一次软件触发（`fcba816`、`2d61ec9`）
- **USB speed**（下拉，按上次连接时相机上报的最大值填充，AE676M 是 0..2）：`Toupcam_put_Speed`。
  上游无条件钉在 0，读到非 0 还会再压回 0（`b4c82ed`、`2d61ec9`）
- **RealTime**（下拉 0/1/2，默认 **2**）：陈旧帧策略，不是限速。见下面的实测（`11afda0`、`4d16e84`）

连接时日志会打印 `Toupcam_get_ExpTimeRange`，用来确认该机型在视频模式下有没有曝光上限——
AE676M 实测两种模式都是 100 us .. 1000 s，2 s 曝光在视频模式下真的跑满 2001 ms（`5c72ff1`）。

取帧改用 `Toupcam_PullImageV3` 以便拿到 `ToupcamFrameInfoV3`，诊断开关
`/camera/ToupTek/FrameTiming` 会逐帧打印 `seq` 步进与时间戳增量（`4d16e84`）。

### 共用

- `usImage::CalcStats` 用已有直方图取 `FiltMin/FiltMax`，去掉整帧 `Median3` 和第二趟扫描；
  `MinADU/MaxADU/MedianADU` 不变（8.3 MP 下 90 → 10 ms）（`c0a5454`）
- 模拟器传感器尺寸可配置：`/SimCam/width`、`/SimCam/height`（默认 752x580）（`4a1129e`）

## 实测

### ToupTek AE676M：3536x3536，8-bit，10 ms 曝光

| 配置 | 周期 | fps | 曝光+读出 |
|---|---|---|---|
| 上游原状（触发 + speed 0 + RealTime 1） | 296 ms | 3.4 | 279 ms |
| 视频模式 | 98 ms | 10.2 | 82 ms |
| 视频 + speed 2 | 65 ms | 15.4 | 48 ms |
| 视频 + speed 2 + RealTime 2 | **34 ms** | **29.4** | 19 ms |

`RealTime` 三档在同样条件下的 `seq` 步进（逐帧记录）：0 和 2 都是 +1、34 ms，
**1 是 +2、65 ms，等于每两帧丢一帧**——PHD2 先等图像事件再取帧，而它的「丢弃排队帧」把正要取的
那一帧也丢了。2 保留了「落后时丢积压」的保证又不动 DDR 缓冲，所以默认用它。

固定开销 24 ms（读出约 9 ms + 应用侧约 15 ms），所以帧率随曝光上升会迅速让位于曝光本身：

| 曝光 | 周期 | 帧率 | 读出占比 |
|---|---|---|---|
| 10 ms | 34 ms | 29 fps | 26% |
| 100 ms | 124 ms | 8.1 fps | 7% |
| 1000 ms | 1024 ms | 1 fps | 0.9% |

### 模拟器：1920x1080，16-bit，10 ms，GUIDING 状态（中位数）

| 条件 | 周期 | 组成 |
|---|---|---|
| `frame_download_ms=50`（模拟器默认） | ~100 ms（10 fps） | 假下载 50 + 应用侧 ~40 |
| `frame_download_ms=0` | **59 ms（17 fps）** | 模拟器成像 28、导星脉冲 24、重绘 12、星点+算法 9（含探针） |

模拟器是快照渲染（`FillImage` 不含积分），所以它既不能复现拖线，也不能复现「脉冲落进曝光窗口」的偏差；
这两件事只能在真实星点上验证。

## 已知限制

- ZWO 4K 30 fps 不可达：video 读出 34 ms（消费者慢于帧间隔时 `flush_buffered_image` 每轮丢两帧），
  加 8-10 ms 统计与 1 ms 寻星约 44 ms/22 fps。要突破需要把「排空到最新帧」改成「只取最新帧」
- ToupTek 的硬件 ROI 只在 `UseSubframes` 打开时下发（搜索窗口，16 像素对齐），`LimitFrame` 被驱动忽略；
  改 ROI 会 `StopCapture()` 重启流，所以不能每帧动
- `ToupcamFrameInfoV3.timestamp` 不能当帧龄用：每帧只前进 18.1 ms 而墙钟过 34 ms，且随 `seq` 步进成倍数，
  它统计的是曝光时间而非时钟
- 显示部分仍有约 12 ms（图像重绘），打开曲线窗口时另有约 17 ms
