### PHD2 Guiding

PHD2 is telescope guiding software that keeps your telescope locked on target, letting you concentrate on other aspects of astro-imaging or spectroscopy.

### This fork

This is an experimental development fork of [PHD2](https://github.com/OpenPHDGuiding/phd2), maintained on branch `perf/zwo-video-stats`. It is based on upstream `master` (commit `264a64d2`, 2026-11-02) and is not affiliated with or supported by the PHD2 maintainers.

The fork focuses on guide-camera performance and transport, without changing the guiding algorithms (centroid, weighted average) or pulse scheduling:

- **Guide-camera transport and free-running/video modes**
  - ZWO ASI: optional video mode, USB bandwidth and high-speed readout controls.
  - ToupTek: free-running video mode, `RealTime` stale-frame policy, USB speed, and `Toupcam_PullImageV3` with frame sequence numbers.
  - QHY: optional live (free-running) mode with reduced per-frame SDK calls.
  - PlayerOne, SVBONY and older vendors (Moravian, SBIG, FLI, ...) use the same generic capture path; vendor-specific tuning is planned but not complete.
- **Display/paint-path performance** on high-DPI screens (buffered-paint removal, cached graph background, fused downscaling).
- **Configuration robustness** with wx 3.2 (type-agnostic profile reads).
- **Multi-star candidate pool**: `/guider/multistar/MaxCandidates` (default 20, range 9..24); up to 9 stars are still used per frame.
- **Chinese (zh_CN / zh_TW) localization** updates.

Detailed change log: [CHANGELOG.md](CHANGELOG.md) (Chinese). Build instructions and user documentation are those of upstream PHD2; see [README-PHD2.txt.in](README-PHD2.txt.in), the `build/` scripts and <https://openphdguiding.org>. Vendor SDK binaries under `cameras/` are included as in the upstream tree.

Status: experimental. Expect changes to be validated on real hardware; use at your own risk.

### Regression tests and Chinese help

Hardware-free capture and image-preview regression tests can be built separately
from PHD2 and the vendor SDKs; see [tests/README.md](tests/README.md).

Simplified Chinese in-app help is provided under [locale/zh_CN/help](locale/zh_CN/help),
including the full manual and a [high-speed capture guide](locale/zh_CN/help/High_speed_capture.htm).
The normal documentation build creates the `zh_CN` help archive automatically.
Screenshots retain the original UI labels, with Chinese explanations in the text.

### Web site

  https://openphdguiding.org

### User support forum

  https://groups.google.com/g/open-phd-guiding

### Contributing to PHD2

  https://github.com/OpenPHDGuiding

If you have an idea for a PR, please submit a Github issue for discussion with the developers. We are no longer accepting PRs from all Github users as the maintainers all work on PHD2 in our limited free time, and the increased number of LLM-generated PRs exceeds our capacity to handle them.
