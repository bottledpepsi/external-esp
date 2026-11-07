// Bookmarks.h - places in a recording that the user marked, kept in a small file next to it (<recording>.bookmarks.json).
//
// A bookmark is a record index in the .esprec and a short label. The file is a JSON array of strings, each "<index> <label>" ("1200 the fight"):
//   ["340 spawn", "1200 the fight"]
// Bad entries (no index, an index past the recording, a label with control characters) are dropped one by one and reported; the rest are kept.
// At most kMax bookmarks, sorted by index, one per index (adding at an index that has one replaces its label).
//
// Portable (no Windows headers).
#pragma once
#include "ConfigStore.h"
#include "Json.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

class Bookmarks
{
public:
    static constexpr size_t kMax = 200;
    static constexpr size_t kMaxLabel = 40;
    static constexpr size_t kMaxFileBytes = 32 * 1024;

    struct Mark
    {
        size_t index = 0;
        std::string label;
    };

    static std::filesystem::path PathFor(const std::filesystem::path& recording)
    {
        std::filesystem::path p = recording;
        p += ".bookmarks.json";
        return p;
    }

    // Printable ASCII only, trimmed, at most kMaxLabel characters ("" stays "").
    static std::string CleanLabel(const std::string& in)
    {
        std::string s;
        for (unsigned char c : in)
        {
            if (s.size() >= kMaxLabel) break;
            if (c >= 32 && c < 127) s += static_cast<char>(c);
        }
        const size_t a = s.find_first_not_of(' ');
        if (a == std::string::npos) return "";
        s.erase(0, a);
        s.erase(s.find_last_not_of(' ') + 1);
        return s;
    }

    size_t Size() const { return marks_.size(); }
    const Mark& At(size_t i) const { return marks_[i]; }
    void Clear() { marks_.clear(); }

    // false when the list is full and `index` is new.
    bool Add(size_t index, const std::string& label)
    {
        const std::string l = CleanLabel(label);
        auto it = std::lower_bound(marks_.begin(), marks_.end(), index, [](const Mark& m, size_t i) { return m.index < i; });
        if (it != marks_.end() && it->index == index)
        {
            it->label = l;
            return true;
        }
        if (marks_.size() >= kMax) return false;
        marks_.insert(it, Mark{index, l});
        return true;
    }
    bool Remove(size_t index)
    {
        auto it = std::lower_bound(marks_.begin(), marks_.end(), index, [](const Mark& m, size_t i) { return m.index < i; });
        if (it == marks_.end() || it->index != index) return false;
        marks_.erase(it);
        return true;
    }
    // The first bookmark after / the last bookmark before a position; null when there is none.
    const Mark* Next(size_t after) const
    {
        auto it = std::upper_bound(marks_.begin(), marks_.end(), after, [](size_t i, const Mark& m) { return i < m.index; });
        return it == marks_.end() ? nullptr : &*it;
    }
    const Mark* Prev(size_t before) const
    {
        auto it = std::lower_bound(marks_.begin(), marks_.end(), before, [](const Mark& m, size_t i) { return m.index < i; });
        return it == marks_.begin() ? nullptr : &*(it - 1);
    }

    std::string ToJson() const
    {
        std::string j = "[";
        for (size_t i = 0; i < marks_.size(); ++i)
            j += (i ? ",\n " : "\n ") + json::Escape(std::to_string(marks_[i].index) + (marks_[i].label.empty() ? "" : " " + marks_[i].label));
        return j + (marks_.empty() ? "]\n" : "\n]\n");
    }

    // Replaces the list with what `text` holds; `recordCount` is the recording's size (an index at or past it is dropped). Returns false only when
    // the text is not a JSON array at all (the list is then left empty); individual bad entries go to `warnings`.
    bool FromJson(const std::string& text, size_t recordCount, std::vector<std::string>* warnings = nullptr)
    {
        marks_.clear();
        if (text.size() > kMaxFileBytes) return false;
        std::string err;
        const auto arr = json::ParseStringArray(text, &err);
        if (!arr || !json::IsValid(text))
        {
            if (warnings) warnings->push_back("not a JSON array of strings");
            return false;
        }
        for (const std::string& e : *arr)
        {
            size_t used = 0;
            unsigned long long idx = 0;
            while (used < e.size() && used < 12 && e[used] >= '0' && e[used] <= '9')
            {
                idx = idx * 10 + static_cast<unsigned>(e[used] - '0');
                ++used;
            }
            if (used == 0 || (used < e.size() && e[used] != ' '))
            {
                if (warnings) warnings->push_back("\"" + CleanLabel(e).substr(0, 20) + "\": no index - dropped");
                continue;
            }
            if (idx >= recordCount)
            {
                if (warnings) warnings->push_back("index " + std::to_string(idx) + " is past the end of the recording - dropped");
                continue;
            }
            if (!Add(static_cast<size_t>(idx), e.substr(used))) break; // full
        }
        return true;
    }

    bool Save(const std::filesystem::path& file, std::string* error = nullptr) const { return configstore::WriteAtomic(file, ToJson(), error); }

    // A missing file is fine: an empty list, true. Unreadable / not an array: false.
    bool Load(const std::filesystem::path& file, size_t recordCount, std::vector<std::string>* warnings = nullptr)
    {
        marks_.clear();
        std::error_code ec;
        if (!std::filesystem::exists(file, ec)) return true;
        const auto size = std::filesystem::file_size(file, ec);
        if (ec || size > kMaxFileBytes) return false;
        std::ifstream f(file, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        return FromJson(ss.str(), recordCount, warnings);
    }

private:
    std::vector<Mark> marks_;
};
