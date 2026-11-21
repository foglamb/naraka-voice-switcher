#include "voice_files.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static void touch(const fs::path& path) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << "fixture";
    if (!out) throw std::runtime_error("could not create fixture file");
}

int main() {
    auto root = fs::temp_directory_path() / fs::path("naraka-voice-files-test");
    std::error_code ec;
    fs::remove_all(root, ec);
    auto windows_dir = root / "Naraka" / "program" / "NarakaBladepoint_Data" /
        "StreamingAssets" / "Audio" / "GeneratedSoundBanks" / "Windows";
    fs::create_directories(windows_dir);
    for (const auto* suffix : {"Common", "Ingame", "Outgame"})
        touch(windows_dir / (std::string("ChineseCN_") + suffix + ".pck"));
    for (const auto* extra : {"Init.pck", "MusicInGame.pck", "SoundActor1.pck"}) touch(windows_dir / extra);

    auto exe = root / "Naraka" / "program" / "NarakaBladepoint.exe";
    touch(exe);
    auto located = nvtool::voice::locate_directory(exe);
    require(located == windows_dir, "selected game exe did not resolve to the voice directory");
    auto chinese = nvtool::voice::chinese_packages(located);
    require(chinese.size() == 3, "must identify only the three Chinese voice shards");
    require(nvtool::voice::complete(chinese), "the three Chinese shards must be a complete set");
    require(chinese[0].part == L"_common" && chinese[1].part == L"_ingame" && chinese[2].part == L"_outgame",
            "Chinese voice shards must be sorted and classified correctly");

    auto refs = root / "reference";
    for (const auto* lang : {"JapaneseJP", "EnglishUS"})
        for (const auto* suffix : {"Common", "Ingame", "Outgame"})
            touch(refs / (std::string(lang) + "_" + suffix + ".pck"));
    // A Japanese subdirectory must be discovered recursively as a complete language set.
    auto japanese_dir = refs / "jp";
    for (const auto* suffix : {"Common", "Ingame", "Outgame"})
        touch(japanese_dir / (std::string("JapaneseJP_") + suffix + ".pck"));
    auto languages = nvtool::voice::reference_languages(refs);
    size_t matching_languages = 0;
    for (const auto& language : languages) {
        auto pairs = nvtool::voice::pair_packages(chinese, language);
        if (language.name == L"日语" || language.name == L"英语") {
            require(pairs.size() == 3, "each complete target language must match the three Chinese shards");
            ++matching_languages;
        }
    }
    require(matching_languages == 3, "both root languages and the nested Japanese language must be found");

    touch(refs / "JapaneseJP_Common.pck");
    touch(refs / "JapaneseJP_Ingame.pck");
    // The partial language set must never be accepted as a valid replacement.
    fs::remove(refs / "EnglishUS_Outgame.pck", ec);
    auto incomplete_languages = nvtool::voice::reference_languages(refs);
    for (const auto& language : incomplete_languages) {
        if (language.name == L"英语" && language.packages.front().path.parent_path() == refs)
            require(nvtool::voice::pair_packages(chinese, language).empty(), "partial target shard set must be rejected");
    }

    // Preserve backwards compatibility for older single-file game and reference packages.
    auto legacy_game = root / "legacy-game";
    auto legacy_ref = root / "legacy-ref";
    touch(legacy_game / "ChineseCN.pck");
    touch(legacy_ref / "Japanese.pck");
    auto legacy_cn = nvtool::voice::chinese_packages(legacy_game);
    auto legacy_languages = nvtool::voice::reference_languages(legacy_ref);
    require(legacy_cn.size() == 1 && nvtool::voice::complete(legacy_cn), "legacy ChineseCN.pck must remain supported");
    require(legacy_languages.size() == 1 && nvtool::voice::pair_packages(legacy_cn, legacy_languages[0]).size() == 1,
            "legacy single-file Japanese.pck must remain supported");

    fs::remove(windows_dir / "ChineseCN_Outgame.pck", ec);
    require(nvtool::voice::locate_directory(exe).empty(), "an incomplete game shard set must not be advertised as ready");
    fs::remove_all(root, ec);
    std::cout << "PASS: Windows executable-path discovery, three-shard grouping, language matching, partial-set rejection and legacy compatibility\n";
    return 0;
}
