#pragma once

#include <cstdint>
#include <memory>

namespace RE { class Actor; class TESObjectREFR; }

namespace F4T::SleepMarker
{
	void Install();
	using Token = std::shared_ptr<const void>;
	Token Register(RE::Actor& a_actor, RE::TESObjectREFR& a_bed, std::uint32_t a_marker);
	void Remove(const Token& a_token);
}
