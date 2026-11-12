"""Check the Simplified Chinese help without building PHD2 or using a camera."""
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit
import argparse
import re
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "help"
HELP = ROOT / "locale" / "zh_CN" / "help"
CMAKE = "cmake"


class Page(HTMLParser):
    def __init__(self, path):
        super().__init__(convert_charrefs=True)
        self.anchors = set()
        self.links = []
        self.text = path.read_text(encoding="utf-8-sig")
        self.feed(self.text)

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if attrs.get("id"):
            self.anchors.add(attrs["id"])
        if tag == "a" and attrs.get("name"):
            self.anchors.add(attrs["name"])
        for key in ("href", "src"):
            if attrs.get(key):
                self.links.append(attrs[key])
        if tag == "param" and attrs.get("name", "").lower() == "local":
            self.links.append(attrs["value"])


def project_pages():
    text = (HELP / "PHD2GuideHelp.hhp").read_text(encoding="utf-8")
    return [line.strip() for line in text.split("[FILES]", 1)[1].split("[", 1)[0].splitlines() if line.strip()]


class ChineseHelpTests(unittest.TestCase):
    def test_all_pages_and_anchors_are_preserved(self):
        listed = set(project_pages())
        for original in SOURCE.glob("*.htm*"):
            with self.subTest(page=original.name):
                self.assertIn(original.name, listed)
                translated = Page(HELP / original.name)
                self.assertFalse(Page(original).anchors - translated.anchors, "missing contextual help anchors")
                self.assertTrue(re.search(r"charset\s*=\s*[\"']?utf-8", translated.text, re.I), "missing UTF-8 declaration")
                self.assertTrue(re.search(r"[\u4e00-\u9fff]", translated.text), "page has no Chinese text")
        self.assertIn("High_speed_capture.htm", listed)

    def test_local_links_and_images(self):
        pages = {name: Page(HELP / name) for name in project_pages()}
        pages.update({name: Page(HELP / name) for name in ("PHD2GuideHelp.hhc", "PHD2GuideHelp.hhk.in")})
        filenames = {path.name for path in HELP.iterdir() if path.is_file()}
        problems = []
        for name, page in pages.items():
            for link in page.links:
                url = urlsplit(link)
                if url.scheme or url.netloc:
                    continue
                target_name = unquote(url.path) or name
                target = HELP / target_name
                # ZIP paths are case-sensitive even when this test runs on Windows.
                if target_name not in filenames or not target.is_file():
                    problems.append(f"{name}: missing {link}")
                    continue
                if url.fragment and target_name in pages:
                    if unquote(url.fragment) not in pages[target_name].anchors:
                        problems.append(f"{name}: missing anchor {link}")
        self.assertEqual(problems, [], "\n".join(problems))

    def test_translated_navigation(self):
        contents = (HELP / "PHD2GuideHelp.hhc").read_text(encoding="utf-8")
        self.assertIn('value="说明"', contents)
        self.assertIn('value="故障排除及分析"', contents)
        for index in ("PHD2GuideHelp.hhc", "PHD2GuideHelp.hhk.in"):
            text = (HELP / index).read_text(encoding="utf-8")
            labels = re.findall(r'<param name="Name" value="([^"]+)"', text)
            self.assertTrue(labels)
            for label in labels:
                self.assertRegex(label, r"[\u4e00-\u9fff]", f"untranslated navigation: {label}")

    def test_native_help_archive(self):
        with tempfile.TemporaryDirectory(prefix="help-regression-", dir=Path.cwd()) as folder:
            command = [CMAKE, f"-Dproject_root_dir={ROOT.as_posix()}",
                       f"-Dinput_folder={HELP.as_posix()}", f"-Doutput_folder={Path(folder).as_posix()}",
                       "-Dlist_of_files=" + ";".join((HELP / name).as_posix() for name in project_pages()),
                       "-P", str(ROOT / "cmake_modules" / "PHD2GenerateDocScript.cmake")]
            result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            with zipfile.ZipFile(Path(folder) / "PHD2GuideHelp.zip") as archive:
                names = set(archive.namelist())
                self.assertTrue(set(project_pages()).issubset(names))
                self.assertEqual(archive.read("PHD2GuideHelp.hhk"), (HELP / "PHD2GuideHelp.hhk.in").read_bytes())
                for image in HELP.glob("*.png"):
                    self.assertIn(image.name, names)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", default="cmake")
    args, remaining = parser.parse_known_args()
    CMAKE = args.cmake
    unittest.main(argv=[__file__] + remaining)
