// Voice package discovery shared by the GUI and regression checks.
#pragma once
#include <algorithm>
#include <deque>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace nvtool {
namespace voice {
namespace fs = std::filesystem;

struct Package {
    fs::path path;
    std::wstring part; // empty for legacy monolithic PCK
};
struct Language {
    std::wstring name;
    std::vector<Package> packages;
};
struct Pair {
    fs::path chinese;
    fs::path reference;
};
inline std::wstring lower(std::wstring s) {
    for (auto& c : s) if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
    return s;
}
inline std::wstring part_of(const fs::path& path, std::wstring& language) {
    auto stem = lower(path.stem().wstring());
    auto pos = stem.find(L'_');
    language = stem.substr(0, pos);
    return pos == std::wstring::npos ? L"" : stem.substr(pos);
}
inline bool is_part(const std::wstring& part) {
    return part.empty() || part == L"_common" || part == L"_ingame" || part == L"_outgame";
}
inline std::wstring language_name(const std::wstring& prefix) {
    if (prefix == L"japanese" || prefix == L"japanesejp") return L"日语";
    if (prefix == L"english" || prefix == L"englishus") return L"英语";
    if (prefix == L"thai" || prefix == L"thaith") return L"泰语";
    return L"";
}
inline std::vector<Package> chinese_packages(const fs::path& dir) {
    std::vector<Package> split, mono;
    std::error_code ec;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code entry_ec;
        if (!it->is_regular_file(entry_ec) || lower(it->path().extension().wstring()) != L".pck") continue;
        std::wstring language;
        auto part = part_of(it->path(), language);
        if (language != L"chinesecn" || !is_part(part)) continue;
        (part.empty() ? mono : split).push_back({it->path(), part});
    }
    auto& result = split.empty() ? mono : split;
    std::sort(result.begin(), result.end(), [](const Package& a, const Package& b) { return a.part < b.part; });
    return result;
}
inline bool complete(const std::vector<Package>& packages) {
    if (packages.size() == 1 && packages[0].part.empty()) return true;
    return packages.size() == 3 && packages[0].part == L"_common" &&
           packages[1].part == L"_ingame" && packages[2].part == L"_outgame";
}
inline fs::path locate_directory(fs::path base) {
    std::error_code ec;
    if (fs::is_regular_file(base, ec) || lower(base.extension().wstring()) == L".exe") base = base.parent_path();
    base = base.lexically_normal();
    if (base.empty()) return {};
    std::vector<fs::path> roots{base};
    // Search the selected installation only; never recurse across an entire drive.
    auto parent = base.parent_path();
    if (!parent.empty() && parent != parent.root_path() && parent != base) roots.push_back(parent);
    for (const auto& root : roots) {
        for (const auto& prefix : {fs::path(), fs::path(L"NarakaBladepoint_Data"),
                                  fs::path(L"program") / L"NarakaBladepoint_Data"}) {
            fs::path candidate = root / prefix;
            if (complete(chinese_packages(candidate))) return candidate;
            candidate /= fs::path(L"StreamingAssets") / L"Audio" / L"GeneratedSoundBanks" / L"Windows";
            if (complete(chinese_packages(candidate))) return candidate;
        }
    }
    for (const auto& root : roots) {
        std::deque<std::pair<fs::path, int>> pending{{root, 0}};
        size_t visited = 0;
        while (!pending.empty() && visited++ < 4096) {
            auto current = pending.front(); pending.pop_front();
            if (complete(chinese_packages(current.first))) return current.first;
            if (current.second >= 9) continue;
            std::error_code iter_ec;
            for (fs::directory_iterator it(current.first, fs::directory_options::skip_permission_denied, iter_ec), end;
                 !iter_ec && it != end; it.increment(iter_ec)) {
                std::error_code item_ec;
                if (it->is_symlink(item_ec) || !it->is_directory(item_ec)) continue;
                auto name = lower(it->path().filename().wstring());
                if (name == L".git" || name == L"backup" || name == L"backups" || name == L"patch") continue;
                if (pending.size() < 4096) pending.push_back({it->path(), current.second + 1});
            }
        }
    }
    return {};
}
inline std::vector<Language> reference_languages(const fs::path& root) {
    // Groups are per directory and language prefix: never join unrelated downloads.
    std::map<std::wstring, Language> groups;
    auto inspect = [&](const fs::path& dir) {
        std::error_code ec;
        for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            std::error_code item_ec;
            if (!it->is_regular_file(item_ec) || lower(it->path().extension().wstring()) != L".pck") continue;
            std::wstring prefix;
            auto part = part_of(it->path(), prefix);
            auto name = language_name(prefix);
            if (name.empty() || !is_part(part)) continue;
            auto key = lower(dir.wstring()) + L"|" + prefix + (part.empty() ? L"|mono" : L"|split");
            auto& group = groups[key]; group.name = name;
            group.packages.push_back({it->path(), part});
        }
    };
    inspect(root);
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code item_ec;
        if (it->is_symlink(item_ec)) { it.disable_recursion_pending(); continue; }
        if (it.depth() >= 8) it.disable_recursion_pending();
        if (it->is_directory(item_ec)) inspect(it->path());
    }
    std::vector<Language> result;
    for (auto& item : groups) {
        auto& group = item.second;
        std::sort(group.packages.begin(), group.packages.end(), [](const Package& a, const Package& b) { return a.part < b.part; });
        result.push_back(group);
    }
    return result;
}
inline std::vector<Pair> pair_packages(const std::vector<Package>& chinese, const Language& language) {
    if (!complete(chinese) || !complete(language.packages)) return {};
    std::vector<Pair> pairs;
    for (const auto& package : chinese) {
        auto match = std::find_if(language.packages.begin(), language.packages.end(), [&](const Package& ref) {
            return ref.part == package.part;
        });
        if (match == language.packages.end()) return {};
        pairs.push_back({package.path, match->path});
    }
    return pairs;
}
} // namespace voice
} // namespace nvtool
