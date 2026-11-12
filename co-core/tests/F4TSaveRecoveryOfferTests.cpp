#include "F4TSaveRecoveryOffer.h"

#include <cassert>
#include <string>

int main()
{
    using nlohmann::json;
    const json packet{ { "type", "saveRecoveryOffer" }, { "approval", {
        { "profileId", "co-12345678#1" }, { "characterId", "AAAA1111" },
        { "saveFile", "Save9_AAAA1111_Nomad_Commonwealth_20260814000101_1_2" },
        { "sha256", std::string(64, 'a') }, { "generation", std::uint64_t{1} } } } };
    const auto offer = F4T::SaveRecovery::ParseOffer(packet);
    assert(offer && offer->profile == "co-12345678#1" && offer->generation == 1);
    auto invalid = packet;
    invalid["approval"]["characterId"] = "BBBB2222";
    assert(!F4T::SaveRecovery::ParseOffer(invalid));
    invalid = packet;
    invalid["approval"]["profileId"] = "co-12345678#8";
    assert(!F4T::SaveRecovery::ParseOffer(invalid));
    invalid = packet;
    invalid["approval"]["saveFile"] = "../Save9_AAAA1111_Nomad_Commonwealth_20260814000101_1_2";
    assert(!F4T::SaveRecovery::ParseOffer(invalid));
    invalid = packet;
    invalid["approval"]["sha256"] = std::string(64, 'z');
    assert(!F4T::SaveRecovery::ParseOffer(invalid));
    invalid = packet;
    invalid["approval"]["generation"] = 0;
    assert(!F4T::SaveRecovery::ParseOffer(invalid));
    invalid = packet;
    invalid["approval"]["extra"] = true;
    assert(!F4T::SaveRecovery::ParseOffer(invalid));
    invalid = packet;
    invalid["approval"].erase("saveFile");
    assert(!F4T::SaveRecovery::ParseOffer(invalid));
}
