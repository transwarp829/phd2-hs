#include <wx/app.h>
#include <wx/filesys.h>
#include <wx/fontmap.h>
#include <wx/fs_zip.h>
#include <wx/html/helpdata.h>
#include <wx/log.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

// A console app initializes the HTML/ZIP modules without requiring a display.
class HelpTestApp : public wxAppConsole
{
};
wxIMPLEMENT_APP_NO_MAIN(HelpTestApp);

static void Require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

int main(int argc, char **argv)
{
    wxLog::SetActiveTarget(new wxLogStderr);
    wxSetAssertHandler(
        [](const wxString& file, int line, const wxString& function, const wxString& condition, const wxString& message)
        {
            std::cerr << file.ToStdString() << ':' << line << ' ' << function.ToStdString() << ' ' << condition.ToStdString()
                      << ' ' << message.ToStdString() << '\n';
            std::_Exit(2);
        });
    if (argc != 2 || !wxEntryStart(argc, argv))
        return 1;
    int result = 0;
    try
    {
        std::cout << "wxWidgets initialized\n" << std::flush;
        // The HTML reader needs the full mapper even though no windows are shown.
        delete wxFontMapper::Set(new wxFontMapper);
        wxFileSystem::AddHandler(new wxZipFSHandler);
        wxHtmlHelpData help;
        Require(help.AddBook(wxString::FromUTF8(argv[1])), "wxWidgets could not open the Chinese help archive");
        std::cout << "Help index loaded\n" << std::flush;
        Require(help.FindPageByName(wxString::FromUTF8(u8"说明")).EndsWith("Introduction.htm"),
                "Chinese Help menu target could not be resolved");
        Require(help.FindPageByName(wxString::FromUTF8(u8"故障排除及分析")).EndsWith("Trouble_shooting.htm"),
                "Chinese troubleshooting target could not be resolved");
        Require(help.FindPageByName(wxString::FromUTF8(u8"高速采集与低延迟")).EndsWith("High_speed_capture.htm"),
                "high-speed guide could not be resolved");
        Require(help.GetIndexArray().size() >= 160, "translated index was not loaded");
        std::cout << "Chinese help archive loaded by wxWidgets\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        result = 1;
    }
    wxEntryCleanup();
    return result;
}
