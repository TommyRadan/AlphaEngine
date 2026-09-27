// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <core/string_id.hpp>

#include <mutex>
#include <unordered_map>

#include <core/log.hpp>

namespace
{
    // Every string interned so far, keyed by its hash. Mapped values of an
    // unordered_map keep their address across rehashes and the strings are
    // never modified once stored, so the c_str() pointers handed out stay
    // valid for as long as the table does.
    struct intern_table
    {
        std::mutex mutex;
        std::unordered_map<uint64_t, std::string> strings;
    };

    intern_table& table()
    {
        // Deliberately never destroyed: an id held by a static that outlives
        // this translation unit's statics (a game module's name constant,
        // say) must still be able to read its text during exit.
        static intern_table* instance = new intern_table{};
        return *instance;
    }
} // namespace

core::string_id::string_id(const char* text) : string_id{text != nullptr ? std::string_view{text} : std::string_view{}}
{
}

core::string_id::string_id(const std::string& text) : string_id{std::string_view{text}} {}

core::string_id::string_id(std::string_view text)
{
    if (text.empty())
    {
        return;
    }

    const uint64_t hash = fnv1a_64(text);
    intern_table& interned = table();
    std::lock_guard<std::mutex> lock{interned.mutex};
    auto [it, inserted] = interned.strings.try_emplace(hash, text);
    if (!inserted && it->second != text)
    {
        LOG_ERR("core::string_id: '%.*s' and '%s' share the hash 0x%016llx and will compare equal",
                static_cast<int>(text.size()),
                text.data(),
                it->second.c_str(),
                static_cast<unsigned long long>(hash));
    }
    m_hash = hash;
    m_text = it->second.c_str();
    m_size = it->second.size();
}
