# Hardware-free regression tests

These tests exercise the same capture-state helpers used by the QHY, ToupTek
and ZWO drivers. Fake SDK adapters model an exposure already in progress,
a full frame queue, callback/wait races and SDK failures. No camera, vendor
SDK library, network service or telescope is required.

The star-profile test uses real `wxImage` pixels. It paints an interpolated
preview, switches to raw view without a new frame, resizes repeatedly and
checks that both the source pixels and subsequent previews are unchanged.
The help tests check the Chinese manual's pages, original anchors, local
links, screenshots, translated navigation and the native CMake ZIP package.
They also open the archive with `wxHtmlHelpData` and resolve the actual Chinese
Help-menu targets, catching encoding errors that a ZIP-only check cannot detect.

## Run all tests

Requirements: C++14 compiler, CMake 3.16+, wxWidgets 3.0+ development files
(html, core and base), and Python 3. wxWidgets 3.2 is recommended for this branch.
On Debian/Ubuntu the image dependency is `libwxgtk3.2-dev`.

```sh
cmake -S tests -B build-capture-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build-capture-tests --config Release --parallel
ctest --test-dir build-capture-tests -C Release --output-on-failure
```

On Windows, point CMake's `wxWidgets_ROOT_DIR` and `wxWidgets_LIB_DIR` at a
wxWidgets installation built with the same compiler. Alternatively, provide
an unpacked **complete wxWidgets release source archive** using
`-DPHD2_TEST_WX_SOURCE_DIR=/path/to/wxWidgets`; CMake then builds its core/base
and HTML dependencies locally. Tests do not download dependencies themselves.

If wxWidgets is unavailable, `-DPHD2_TEST_WXIMAGE=OFF` runs the camera tests
and, when Python is installed, the structural help checks; it explicitly skips
the pixel and native help-loader tests. The normal full PHD2 build registers
this suite with CTest only when configured with `-DPHD2_BUILD_TESTS=ON` (off by
default so release builds do not carry the test binaries).

Run just the help checks with:

```sh
python tests/help_translation_test.py --cmake cmake
```

## Coverage and limits

- QHY: saved versus active mode, reconnects, long/short exposure changes,
  first-frame configuration, geometry invalidation, stable settings with no
  extra SDK writes, and stop/set/start failures.
- ToupTek: consume an already-full queue without waiting for another event,
  pending frames, callback races, spurious wakeups, SDK errors, cancellation
  and timeout. SDK calls must occur outside the callback mutex.
- ZWO: exposure/gain transitions including 10 s to 10 ms, stable settings,
  single-frame mode, unsupported gain control, and SDK read/write/stop/start
  failures without incorrectly marking the stream as running.
- Star profile: switching raw/interpolated mode and resizing cannot mutate
  the shared source crop.
- Logging: byte/time flush thresholds, idle tails, explicit flush, record-boundary
  rotation, existing files, oversized records, failure fallback and real stdio I/O.
- Upload: every numbered volume stays with its session/instance, including parts
  created after selection; missing selected files are retained for error reporting.
- Statistics: visibility controls timer lifetime, refreshes are limited to 4 Hz,
  unchanged text is not painted, and stopped capture clears the display.

These checks validate software ordering and state, not undocumented SDK or
camera firmware behavior. Hardware validation is still needed for actual
frame freshness, timing, USB reliability and guiding accuracy.
