/* No se compila. El mod se construye con los archivos .c y build_native.bat. */
#pragma once

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

struct Settings {
    bool enabled = true;
    bool avoid_repeat = true;
    std::vector<std::wstring> arrange_source;
    std::vector<std::wstring> arrange_pool;
    std::vector<std::wstring> arrange_boundary;
    std::vector<std::wstring> arrange_loop;
    std::vector<std::wstring> original_source;
    std::vector<std::wstring> original_pool;
    std::vector<std::wstring> original_boundary;
    std::vector<std::wstring> original_loop;
};

enum class RouteAction {
    Ignore,
    Passthrough,
    Boundary,
    Lock,
    Hold
};

struct RouteResult {
    RouteAction action = RouteAction::Ignore;
    std::wstring path;
    std::wstring bank;
    std::wstring requested_id;
    std::wstring chosen_id;
    bool requested_loop = false;
};

class PathRouter {
public:
    PathRouter(Settings settings, std::function<int(int)> rng)
        : settings_(std::move(settings)), rng_(std::move(rng)) {}

    void set_last(const std::wstring& bank, const std::wstring& id) {
        last_bank_ = bank;
        last_id_ = id;
    }

    const std::wstring& locked_id() const { return locked_id_; }
    const std::wstring& locked_bank() const { return locked_bank_; }
    const std::wstring& last_id() const { return last_id_; }

    RouteResult Route(const std::wstring& path) {
        RouteResult result;
        result.path = path;
        if (!settings_.enabled) {
            result.action = RouteAction::Ignore;
            return result;
        }

        std::wstring bank;
        std::wstring id;
        bool loop = false;
        if (!Parse(path, bank, id, loop)) {
            result.action = RouteAction::Ignore;
            return result;
        }

        result.bank = bank;
        result.requested_id = id;
        result.requested_loop = loop;

        const auto& boundary = bank == L"arrange" ? settings_.arrange_boundary : settings_.original_boundary;
        const auto& source = bank == L"arrange" ? settings_.arrange_source : settings_.original_source;
        const auto& pool = bank == L"arrange" ? settings_.arrange_pool : settings_.original_pool;
        const auto& loops = bank == L"arrange" ? settings_.arrange_loop : settings_.original_loop;

        if (Contains(boundary, id)) {
            locked_id_.clear();
            locked_bank_.clear();
            result.action = RouteAction::Boundary;
            return result;
        }

        if (!Contains(source, id)) {
            result.action = RouteAction::Passthrough;
            return result;
        }

        if (pool.empty()) {
            result.action = RouteAction::Passthrough;
            return result;
        }

        if (locked_id_.empty() || locked_bank_ != bank) {
            locked_id_ = Pick(pool);
            locked_bank_ = bank;
            last_id_ = locked_id_;
            last_bank_ = bank;
            result.action = RouteAction::Lock;
        } else {
            result.action = RouteAction::Hold;
        }

        result.chosen_id = locked_id_;
        const bool target_has_loop = Contains(loops, locked_id_);
        result.path = ReplaceFileName(path, FileNameFor(locked_id_, loop && target_has_loop));
        return result;
    }

    static bool Parse(const std::wstring& path, std::wstring& bank, std::wstring& id, bool& loop) {
        std::wstring lower = ToLower(path);
        size_t folder = lower.rfind(L"um48000hz");
        size_t folder_len = 10;
        if (folder != std::wstring::npos) {
            bank = L"arrange";
        } else {
            folder = lower.rfind(L"48000hz");
            folder_len = 7;
            if (folder == std::wstring::npos) return false;
            bank = L"original";
        }

        size_t name_pos = folder + folder_len;
        if (name_pos < path.size() && (path[name_pos] == L'\\' || path[name_pos] == L'/'))
            ++name_pos;
        if (name_pos >= path.size()) return false;
        std::wstring file = path.substr(name_pos);
        size_t slash = file.find_last_of(L"\\/");
        if (slash != std::wstring::npos) file = file.substr(slash + 1);

        std::wstring stem;
        if (!ConsumeExtension(file, stem)) return false;
        return ParseStem(bank, stem, id, loop);
    }

private:
    static std::wstring ToLower(std::wstring value) {
        for (auto& ch : value) {
            if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
        }
        return value;
    }

    static bool EqualsId(const std::wstring& a, const std::wstring& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            wchar_t ca = a[i];
            wchar_t cb = b[i];
            if (ca >= L'a' && ca <= L'z') ca = static_cast<wchar_t>(ca - L'a' + L'A');
            if (cb >= L'a' && cb <= L'z') cb = static_cast<wchar_t>(cb - L'a' + L'A');
            if (ca != cb) return false;
        }
        return true;
    }

    static bool Contains(const std::vector<std::wstring>& list, const std::wstring& id) {
        return std::any_of(list.begin(), list.end(), [&](const std::wstring& item) {
            return EqualsId(item, id);
        });
    }

    static bool ConsumeExtension(const std::wstring& file, std::wstring& stem) {
        if (file.size() < 5) return false;
        const wchar_t* ext = file.c_str() + file.size() - 4;
        if ((ext[0] == L'.') &&
            (ext[1] == L'o' || ext[1] == L'O') &&
            (ext[2] == L'g' || ext[2] == L'G') &&
            (ext[3] == L'g' || ext[3] == L'G')) {
            stem = file.substr(0, file.size() - 4);
            return true;
        }
        return false;
    }

    static bool ParseStem(const std::wstring& bank, const std::wstring& stem, std::wstring& id, bool& loop) {
        std::wstring upper = stem;
        for (auto& ch : upper) {
            if (ch >= L'a' && ch <= L'z') ch = static_cast<wchar_t>(ch - L'a' + L'A');
        }
        loop = false;
        if (upper.size() > 4 && upper.compare(upper.size() - 4, 4, L"LOOP") == 0) {
            loop = true;
            upper.resize(upper.size() - 4);
        }
        if (bank == L"arrange") {
            if (upper.size() != 5 || upper.compare(0, 3, L"BGM") != 0) return false;
            if (upper[3] < L'0' || upper[3] > L'9' || upper[4] < L'0' || upper[4] > L'9') return false;
            id = upper;
            return true;
        }
        if (upper == L"DUMMY") {
            id = upper;
            return true;
        }
        if (upper.size() != 2) return false;
        for (wchar_t ch : upper) {
            bool hex = (ch >= L'0' && ch <= L'9') || (ch >= L'A' && ch <= L'F');
            if (!hex) return false;
        }
        id = upper;
        return true;
    }

    static std::wstring FileNameFor(const std::wstring& id, bool loop) {
        std::wstring name = id;
        if (loop) name += L"LOOP";
        name += L".ogg";
        return name;
    }

    static std::wstring ReplaceFileName(const std::wstring& path, const std::wstring& file_name) {
        size_t slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos) return file_name;
        return path.substr(0, slash + 1) + file_name;
    }

    std::wstring Pick(const std::vector<std::wstring>& pool) {
        int n = static_cast<int>(pool.size());
        int index = 0;
        if (n > 1) {
            index = rng_(n);
            if (index < 0) index = 0;
            if (index >= n) index = n - 1;
        }
        if (settings_.avoid_repeat && n > 1 && EqualsId(pool[static_cast<size_t>(index)], last_id_)) {
            index = (index + 1) % n;
        }
        return pool[static_cast<size_t>(index)];
    }

    Settings settings_;
    std::function<int(int)> rng_;
    std::wstring locked_id_;
    std::wstring locked_bank_;
    std::wstring last_id_;
    std::wstring last_bank_;
};
