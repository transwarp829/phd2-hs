#include "log_upload_files.h"

#include <iostream>
#include <stdexcept>

static void Require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}

int main()
{
    try
    {
        const std::string time = "2026-11-13_090000";
        const std::string main = "PHD2_DebugLog_" + time + ".txt";
        const std::string part1 = "PHD2_DebugLog_" + time + "_part001.txt";
        const std::string part2 = "PHD2_DebugLog_" + time + "_part002.txt";
        const std::string part10 = "PHD2_DebugLog_" + time + "_part010.txt";
        const std::string second = "PHD2_DebugLog_2_" + time + ".txt";
        const std::string secondPart = "PHD2_DebugLog_2_" + time + "_part001.txt";
        const std::string guide = "PHD2_GuideLog_" + time + ".txt";
        const std::string secondGuide = "PHD2_GuideLog_2_" + time + ".txt";
        const auto grouped = log_upload::GroupFiles({ part10, secondPart, part2, main, part1, part1, second, guide, secondGuide,
                                                      "unrelated.txt", "PHD2_GuideLog_" + time + "_part001.txt" });
        Require(grouped.size() == 2, "volumes or instances were grouped incorrectly");
        Require(grouped.at(time).guide == guide, "original guide-log name changed");
        Require(grouped.at(time).debug == std::vector<std::string>({ main, part1, part2, part10 }),
                "debug volumes are missing, duplicated, or out of order");
        const auto& other = grouped.at("2_" + time);
        Require(other.session.instance == 2 && other.guide == secondGuide,
                "qualified guide log did not stay with its own instance");
        Require(other.debug == std::vector<std::string>({ second, secondPart }),
                "instance qualifier was stripped or sessions were mixed");

        // An old main volume may already have been removed by retention. The
        // remaining numbered volumes still form an uploadable session.
        const auto orphaned = log_upload::GroupFiles({ part2 });
        Require(orphaned.size() == 1 && orphaned.at(time).debug.front() == part2,
                "numbered volumes require a main log to be discovered");

        // A new volume can appear after selection. Preserve the selected main
        // name even if it has disappeared, so the real ZIP read reports failure.
        const auto upload = log_upload::DebugFilesForUpload(time, { main, part1 }, { part1, part2, second, secondPart });
        Require(upload == std::vector<std::string>({ main, part1, part2 }),
                "upload snapshot lost selected files or missed fresh volumes");
        Require(log_upload::DebugFilesForUpload("3_" + time, {}, { main, second }).empty(),
                "upload selected a different instance");

        // Do not turn arbitrary paths or suffixes into ZIP entries.
        Require(log_upload::GroupFiles({ "../" + main, main + ".bak", "PHD2_DebugLog_" + time + "_partbad.txt" }).empty(),
                "invalid filenames were accepted");
        std::cout << "log upload grouping tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
