// Exercise the actual GUI operation functions against generated, valid AKPK files.
#include "../src/gui.cpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace testfs = std::filesystem;
static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static void u32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes.at(at + i) = static_cast<uint8_t>(value >> (8 * i));
}
static void fixture(const testfs::path& path, bool japanese) {
    testfs::create_directories(path.parent_path());
    constexpr uint32_t banks = 3, header = 0x54 + banks * 20;
    constexpr uint32_t body_size = 56;
    std::vector<uint8_t> bytes(4096 * banks + body_size, 0);
    std::memcpy(bytes.data(), "AKPK", 4);
    u32(bytes, 4, header); u32(bytes, 8, 1); u32(bytes, 0x50, banks);
    for (uint32_t i = 0; i < banks; ++i) {
        auto body = size_t(4096) * (i + 1);
        uint32_t id = i == 0 ? nvtool::fnv1_str("dialogue_ingame_viper") :
                      i == 1 ? nvtool::fnv1_str("dialogue_ingame_feria") : i + 1000;
        auto entry = 0x54 + i * 20;
        u32(bytes, entry, id); u32(bytes, entry + 4, 4096);
        u32(bytes, entry + 8, body_size); u32(bytes, entry + 12, i + 1); u32(bytes, entry + 16, 1);
        std::memcpy(bytes.data() + body, "BKHD", 4); u32(bytes, body + 4, 16);
        u32(bytes, body + 8, 150); u32(bytes, body + 12, id);
        u32(bytes, body + 16, japanese ? 0x12345678 : 0xf1efa9da);
        std::memcpy(bytes.data() + body + 24, "DIDX", 4); u32(bytes, body + 28, 12);
        u32(bytes, body + 32, i + 1); u32(bytes, body + 36, 0); u32(bytes, body + 40, 4);
        std::memcpy(bytes.data() + body + 44, "DATA", 4); u32(bytes, body + 48, 4);
        u32(bytes, body + 52, japanese ? 0x22220000 + i : 0x11110000 + i);
    }
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    check(static_cast<bool>(out), "cannot create AKPK fixture");
}
static uint32_t payload(const testfs::path& path, uint32_t index) {
    nvtool::AkpkFile file;
    check(file.open(path.wstring()), "fixture/output cannot be parsed");
    std::vector<uint8_t> bytes;
    check(file.read(file.banks().at(index).body_start + 52, 4, bytes), "cannot read bank payload");
    uint32_t result = 0;
    std::memcpy(&result, bytes.data(), 4);
    return result;
}
int main() {
    using namespace nvtool;
    try {
        auto root = testfs::temp_directory_path() / (L"naraka-operations-test-" + std::to_wstring(GetCurrentProcessId()));
        auto game = root / L"Naraka" / L"program";
        auto audio = game / L"NarakaBladepoint_Data" / L"StreamingAssets" / L"Audio" / L"GeneratedSoundBanks" / L"Windows";
        auto reference = root / L"reference";
        std::vector<std::string> originals;
        for (const auto* part : {L"Common", L"Ingame", L"Outgame"}) {
            auto source = audio / (std::wstring(L"ChineseCN_") + part + L".pck");
            fixture(source, false);
            originals.push_back(sha256_file(source.wstring()));
            fixture(reference / (std::wstring(L"JapaneseJP_") + part + L".pck"), true);
        }
        auto music = audio / L"MusicInGame.pck";
        { std::ofstream out(music); out << "untouched game music"; }
        auto music_hash = sha256_file(music.wstring());
        g_sel_game = (game / L"NarakaBladepoint.exe").wstring();
        g_sel_jp.clear(); g_langref_file.clear();
        resolve_all();
        check(g_game_ok && g_chinese_packages.size() == 3 && !g_jp_ok, "Chinese-only installation must resolve without enabling replacement");
        check(!op_apply(true).ok, "missing target language must prevent replacement");
        g_sel_jp = reference.wstring();
        resolve_all();
        check(g_game_ok && g_jp_ok && active_voice_pairs().size() == 3, "target triple must pair with all Chinese packages");
        auto full = op_apply(true);
        if (!full.ok) std::wcerr << full.msg << L"\n" << full.detail << L"\n";
        check(full.ok, "full replacement failed");
        for (size_t i = 0; i < g_chinese_packages.size(); ++i) {
            const auto& path = g_chinese_packages[i].path;
            check(payload(path, 0) == 0x22220000, "target bank was not deployed");
            check(sha256_file(bak_path(path.wstring())) == originals[i], "original backup was changed");
            AkpkFile file; check(file.open(path.wstring()), "output parse failed");
            std::vector<uint8_t> language;
            check(file.read(file.banks()[0].body_start + 16, 4, language), "language field missing");
            uint32_t id = 0; std::memcpy(&id, language.data(), 4);
            check(id == 0xf1efa9da, "Chinese language ID was not preserved");
        }
        check(op_apply(true).ok, "re-applying must preserve the Chinese backups");
        for (size_t i = 0; i < g_chinese_packages.size(); ++i)
            check(sha256_file(bak_path(g_chinese_packages[i].path.wstring())) == originals[i], "re-apply overwrote Chinese backup");
        g_sel_jp.clear(); resolve_all();
        check(op_restore().ok, "restore must work without target package directory");
        for (size_t i = 0; i < g_chinese_packages.size(); ++i)
            check(sha256_file(g_chinese_packages[i].path.wstring()) == originals[i], "restore must be byte-exact");

        g_sel_jp = reference.wstring(); resolve_all();
        g_sel_roles = {"viper"}; g_sel_sys = false;
        check(op_apply(false).ok, "selected-role replacement failed");
        for (const auto& package : g_chinese_packages) {
            check(payload(package.path, 0) == 0x22220000, "selected Viper voice not replaced");
            check(payload(package.path, 1) == 0x11110001, "unselected Feria voice was replaced");
            check(payload(package.path, 2) == 0x11110002, "unselected system voice was replaced");
        }
        check(op_restore().ok, "selected-role restore failed");
        g_sel_roles.clear(); g_sel_sys = true;
        check(op_apply(false).ok, "system-only replacement failed");
        for (const auto& package : g_chinese_packages) {
            check(payload(package.path, 0) == 0x11110000, "system-only mode replaced Viper voice");
            check(payload(package.path, 1) == 0x11110001, "system-only mode replaced Feria voice");
            check(payload(package.path, 2) == 0x22220002, "system bank was not replaced");
        }
        check(op_restore().ok, "system-only restore failed");
        check(sha256_file(music.wstring()) == music_hash, "music file was modified");

        // Malformed target/backup files and missing markers must fail before touching originals.
        auto ref = reference / L"JapaneseJP_Common.pck";
        { std::ofstream out(ref, std::ios::binary | std::ios::trunc); out << "bad"; }
        check(!op_apply(true).ok, "malformed target should be rejected");
        check(sha256_file(g_chinese_packages[0].path.wstring()) == originals[0], "malformed target changed game file");
        fixture(ref, true);
        check(op_apply(true).ok, "replacement after valid target restoration failed");
        auto first = g_chinese_packages[0].path.wstring();
        auto backup_hash = sha256_file(bak_path(first));
        DeleteFileW(marker_path(first).c_str());
        check(!op_apply(true).ok, "missing marker must not allow backup overwrite");
        check(sha256_file(bak_path(first)) == backup_hash, "missing marker caused backup overwrite");
        check(marker_write(first, utf8_to_wide(sha256_file(first))), "restore marker failed");
        { std::ofstream out(testfs::path(bak_path(first)), std::ios::binary | std::ios::trunc); out << "bad"; }
        auto changed_hash = sha256_file(first);
        check(!op_restore().ok, "corrupt backup should prevent restore");
        check(sha256_file(first) == changed_hash, "corrupt backup overwrote game file");

        // Only remove this process-owned fixture directory.
        check(testfs::equivalent(root.parent_path(), testfs::temp_directory_path()), "unexpected fixture cleanup path");
        testfs::remove_all(root);
        std::cout << "PASS: actual GUI operations: Chinese-only detection, full and selected-role/system replacements, byte-exact restore, immutable backups, malformed-file rejection and unchanged music\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
