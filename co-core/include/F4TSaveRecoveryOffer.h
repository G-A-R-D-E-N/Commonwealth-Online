#pragma once

#include "F4TCharacterSlots.h"
#include "F4TContentManifest.h"
#include "F4TSaveProfileLogic.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>

namespace F4T::SaveRecovery
{
    struct Offer
    {
        std::string profile, character, save, sha256;
        std::uint64_t generation{};
    };

    inline std::optional<Offer> ParseOffer(const nlohmann::json& a_packet)
    {
        try {
            if (!a_packet.is_object() || a_packet.size() != 2 || a_packet.at("type") != "saveRecoveryOffer") return std::nullopt;
            const auto& value = a_packet.at("approval");
            if (!value.is_object() || value.size() != 5 || !value.at("generation").is_number_unsigned()) return std::nullopt;
            Offer offer{ value.at("profileId").get<std::string>(), value.at("characterId").get<std::string>(),
                value.at("saveFile").get<std::string>(), value.at("sha256").get<std::string>(), value.at("generation").get<std::uint64_t>() };
            const auto slot = F4T::CharacterSlots::ParseSlotKey(offer.profile);
            if ((!F4T::CharacterSlots::AccountKey(offer.profile) && (!slot || slot->second >= F4T::CharacterSlots::MaximumCharacterSlots)) ||
                offer.generation == 0 || offer.save.size() > 256 || offer.save.find_first_of("/\\:") != std::string::npos ||
                std::any_of(offer.save.begin(), offer.save.end(), [](unsigned char c) { return c < 32 || c == 127; }) ||
                !F4T::SaveProfileLogic::CanRebindProfileSave(offer.character, offer.save) ||
                !F4T::ContentManifest::IsValidFingerprint(offer.sha256)) return std::nullopt;
            return offer;
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
}
