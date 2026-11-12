#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::SurvivalItems
{
    class Catalog
    {
    public:
        [[nodiscard]] bool Add(std::string a_itemKey, std::string a_kind)
        {
            if (a_itemKey.empty() || (a_kind != "food" && a_kind != "drink")) return false;
            m_kinds.insert_or_assign(std::move(a_itemKey), std::move(a_kind));
            return true;
        }

        [[nodiscard]] std::optional<std::string> KindOf(const std::string& a_itemKey) const
        {
            const auto found = m_kinds.find(a_itemKey);
            if (found == m_kinds.end()) return std::nullopt;
            return found->second;
        }

        [[nodiscard]] bool Accepts(const std::string& a_itemKey, std::string_view a_kind) const
        {
            const auto kind = KindOf(a_itemKey);
            return kind && *kind == a_kind;
        }

        [[nodiscard]] std::size_t Size() const noexcept { return m_kinds.size(); }

    private:
        std::map<std::string, std::string> m_kinds;
    };
}
