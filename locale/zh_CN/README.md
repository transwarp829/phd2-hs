# 简体中文帮助

`help/` 包含当前英文手册全部 13 页的简体中文译文，以及本分支的
`High_speed_capture.htm`。原文件名和页面锚点保持不变；截图沿用原图，
正文保留必要的英文控件名便于对照。部分原手册失效的本地链接已修正。

正常 CMake 构建会发现此目录并生成 `zh_CN_html`，将
`PHD2GuideHelp.zip` 放入 `tmp_build_html/zh_CN/`。部署到应用的
`locale/zh_CN/` 后，简体中文界面会优先加载该帮助包。

目录由 `PHD2GuideHelp.hhc` 提供；中文索引模板
`PHD2GuideHelp.hhk.in` 会原样复制为包内的 `PHD2GuideHelp.hhk`。
其他未提供模板的语言继续使用原有自动索引生成方式。

正文 HTML 使用 UTF-8 并声明相应的 META charset。目录和索引也以 UTF-8
保存，但编码只在 `.hhp` 中用 `Charset=utf-8` 指定，不要给 `.hhc` 或
`.hhk.in` 再加入 META charset：wxHtmlHelpData 的兼容转换会重复解码，
导致中文目录标签消失。`tests/help_load_test.cpp` 会实际加载帮助包并验证入口。

修改后按 `tests/README.md` 运行回归测试，同时检查链接、锚点、截图、
中文入口和 ZIP 打包。界面翻译中的“说明”和“故障排除及分析”必须与目录标签一致。
