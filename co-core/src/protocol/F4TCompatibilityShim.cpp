%:include "F4TCompatibilityShim.h"

%:include <algorithm>
%:include <cstdint>
%:include <string>
%:include <string_view>
%:include <utility>

%:include <nlohmann/json.hpp>

namespace
{
	using Family = F4T::Replication::Family;
	using Json = nlohmann::json;

	[[nodiscard]] std::optional<std::uint8_t> FamilyIndex(Family a_family) noexcept
	{
		const auto index = static_cast<std::uint16_t>(static_cast<std::uint8_t>(a_family));
		if (index == 0 || index >= 256U) {
			return std::nullopt;
		}
		return static_cast<std::uint8_t>(index);
	}

	[[nodiscard]] bool IsJsonWhitespace(std::uint8_t a_byte) noexcept
	{
		return a_byte == static_cast<std::uint8_t>(' ') ||
		       a_byte == static_cast<std::uint8_t>('\t') ||
		       a_byte == static_cast<std::uint8_t>('\r') ||
		       a_byte == static_cast<std::uint8_t>('\n');
	}

	[[nodiscard]] std::size_t FirstNonWhitespace(std::span<const std::uint8_t> a_payload) noexcept
	{
		std::size_t index = 0;
		while (index < a_payload.size() && IsJsonWhitespace(a_payload[index])) {
			++index;
		}
		return index;
	}

	[[nodiscard]] std::optional<std::string_view> ExpectedLegacyType(Family a_family) noexcept
	{
		switch (a_family) {
		case Family::Transform:
			return "transform";
		case Family::NpcState:
			return "npcState";
		default:
			return std::nullopt;
		}
	}

	[[nodiscard]] bool IsValidLegacyJson(std::span<const std::uint8_t> a_payload, std::optional<Family> a_expectedFamily)
	{
		try {
			const std::string text(reinterpret_cast<const char*>(a_payload.data()), a_payload.size());
			const auto packet = Json::parse(text);
			if (!packet.is_object()) {
				return false;
			}
			const auto type = packet.find("type");
			if (type == packet.end() || !type->is_string()) {
				return false;
			}
			if (a_expectedFamily.has_value()) {
				const auto expectedType = ExpectedLegacyType(*a_expectedFamily);
				if (!expectedType.has_value() || type->get<std::string>() != *expectedType) {
					return false;
				}
			}
			return true;
		} catch (...) {
			return false;
		}
	}
}

namespace F4T::Compatibility
{
	bool FamilyAdapter::Supports(ConversionDirection a_direction) const noexcept
	{
		if (!SupportsDirection(metadata.supportedDirections, a_direction)) {
			return false;
		}
		if (a_direction == ConversionDirection::LegacyToComponent) {
			return static_cast<bool>(legacyToCanonical) && static_cast<bool>(canonicalToComponent);
		}
		if (a_direction == ConversionDirection::ComponentToLegacy) {
			return static_cast<bool>(componentToCanonical) && static_cast<bool>(canonicalToLegacy);
		}
		return false;
	}

	bool CompatibilityRegistry::Register(FamilyAdapter a_adapter)
	{
		const auto familyIndex = FamilyIndex(a_adapter.metadata.component.family);
		const bool declaresLegacyToComponent = SupportsDirection(
			a_adapter.metadata.supportedDirections,
			ConversionDirection::LegacyToComponent);
		const bool declaresComponentToLegacy = SupportsDirection(
			a_adapter.metadata.supportedDirections,
			ConversionDirection::ComponentToLegacy);
		if (a_adapter.metadata.legacy.name.empty() ||
			!familyIndex.has_value() ||
			a_adapter.metadata.requiredComponentCapability.empty() ||
			(a_adapter.metadata.supportedDirections & static_cast<ConversionDirections>(~AllConversionDirections)) != 0 ||
			a_adapter.metadata.supportedDirections == 0 ||
			(declaresLegacyToComponent && !a_adapter.Supports(ConversionDirection::LegacyToComponent)) ||
			(declaresComponentToLegacy && !a_adapter.Supports(ConversionDirection::ComponentToLegacy))) {
			return false;
		}
		if (m_byComponent[*familyIndex].has_value()) {
			return false;
		}
		for (const auto& [name, index] : m_legacyFamilies) {
			if (name == a_adapter.metadata.legacy.name || index == *familyIndex) {
				return false;
			}
		}

		const auto name = a_adapter.metadata.legacy.name;
		m_legacyFamilies.emplace(name, *familyIndex);
		m_byComponent[*familyIndex] = std::move(a_adapter);
		return true;
	}

	const FamilyAdapter* CompatibilityRegistry::FindByLegacyFamily(std::string_view a_name) const noexcept
	{
		for (const auto& [name, index] : m_legacyFamilies) {
			if (name == a_name) {
				return m_byComponent[index].has_value() ? &m_byComponent[index].value() : nullptr;
			}
		}
		return nullptr;
	}

	const FamilyAdapter* CompatibilityRegistry::FindByComponentFamily(F4T::Replication::Family a_family) const noexcept
	{
		const auto familyIndex = FamilyIndex(a_family);
		if (!familyIndex.has_value() || !m_byComponent[*familyIndex].has_value()) {
			return nullptr;
		}
		return &m_byComponent[*familyIndex].value();
	}

	bool CompatibilityRegistry::SupportsComponent(F4T::Replication::Family a_family) const noexcept
	{
		const auto* adapter = FindByComponentFamily(a_family);
		return adapter != nullptr && adapter->Supports(ConversionDirection::ComponentToLegacy);
	}

	std::string_view CompatibilityRegistry::RequiredComponentCapability(F4T::Replication::Family a_family) const noexcept
	{
		const auto* adapter = FindByComponentFamily(a_family);
		return adapter != nullptr ? std::string_view{ adapter->metadata.requiredComponentCapability } : std::string_view{};
	}

	bool SessionCapabilities::Has(std::string_view a_capability) const noexcept
	{
		return std::any_of(values.begin(), values.end(), [a_capability](const std::string& value) {
			return value == a_capability;
		});
	}

	std::optional<RepresentationSelection> RepresentationPolicy::Select(
		const RepresentationSelectionKey& a_key,
		std::string_view a_legacyFamily,
		const SessionCapabilities& a_capabilities) const noexcept
	{
		const auto* adapter = m_registry.FindByLegacyFamily(a_legacyFamily);
		if (adapter == nullptr || adapter->metadata.component.family != a_key.componentFamily) {
			return std::nullopt;
		}

		const auto componentCapability = m_registry.RequiredComponentCapability(a_key.componentFamily);
		const auto channel = m_definitions != nullptr ? m_definitions->Find(a_key.componentFamily) : std::nullopt;
		const bool channelAllowed = m_definitions == nullptr ||
			(channel.has_value() && (*channel != F4T::Replication::Channel::Reliable || a_capabilities.Has("component-snapshot-channels-v1")));
		if (adapter->Supports(ConversionDirection::LegacyToComponent) &&
			a_capabilities.Has("component-snapshot-v1") &&
			!componentCapability.empty() &&
			a_capabilities.Has(componentCapability) &&
			channelAllowed) {
			return RepresentationSelection{ a_key, Representation::Component, adapter };
		}
		if (!adapter->Supports(ConversionDirection::ComponentToLegacy)) {
			return std::nullopt;
		}
		return RepresentationSelection{ a_key, Representation::Legacy, adapter };
	}

	bool RepresentationLedger::Commit(const RepresentationSelection& a_selection)
	{
		if (a_selection.adapter == nullptr ||
			a_selection.adapter->metadata.component.family != a_selection.key.componentFamily ||
			m_selections.contains(a_selection.key)) {
			return false;
		}
		if (a_selection.representation == Representation::Legacy &&
			!a_selection.adapter->Supports(ConversionDirection::ComponentToLegacy)) {
			return false;
		}
		if (a_selection.representation == Representation::Component &&
			!a_selection.adapter->Supports(ConversionDirection::LegacyToComponent)) {
			return false;
		}
		m_selections.emplace(a_selection.key, a_selection.representation);
		return true;
	}

	bool RepresentationLedger::Contains(const RepresentationSelectionKey& a_key) const
	{
		return m_selections.contains(a_key);
	}

	void RepresentationLedger::Clear() noexcept
	{
		m_selections.clear();
	}

	std::optional<CanonicalValue> DecodeLegacy(const FamilyAdapter& a_adapter, std::string_view a_payload)
	{
		if (!a_adapter.Supports(ConversionDirection::LegacyToComponent)) {
			return std::nullopt;
		}
		try {
			return a_adapter.legacyToCanonical(a_payload);
		} catch (...) {
			return std::nullopt;
		}
	}

	std::optional<std::string> EncodeLegacy(const FamilyAdapter& a_adapter, const CanonicalValue& a_value)
	{
		if (!a_adapter.Supports(ConversionDirection::ComponentToLegacy)) {
			return std::nullopt;
		}
		try {
			return a_adapter.canonicalToLegacy(a_value);
		} catch (...) {
			return std::nullopt;
		}
	}

	std::optional<std::vector<std::uint8_t>> EncodeComponent(const FamilyAdapter& a_adapter, const CanonicalValue& a_value)
	{
		if (!a_adapter.Supports(ConversionDirection::LegacyToComponent)) {
			return std::nullopt;
		}
		try {
			return a_adapter.canonicalToComponent(a_value);
		} catch (...) {
			return std::nullopt;
		}
	}

	std::optional<CanonicalValue> DecodeComponent(const FamilyAdapter& a_adapter, std::span<const std::uint8_t> a_payload)
	{
		if (!a_adapter.Supports(ConversionDirection::ComponentToLegacy)) {
			return std::nullopt;
		}
		try {
			return a_adapter.componentToCanonical(a_payload);
		} catch (...) {
			return std::nullopt;
		}
	}

	std::optional<ProjectedPayload> Project(const RepresentationSelection& a_selection, const CanonicalValue& a_value)
	{
		if (a_selection.adapter == nullptr ||
			a_selection.adapter->metadata.component.family != a_selection.key.componentFamily) {
			return std::nullopt;
		}
		if (a_selection.representation == Representation::Legacy) {
			const auto payload = EncodeLegacy(*a_selection.adapter, a_value);
			return payload.has_value() ? std::optional<ProjectedPayload>{ ProjectedPayload{ Representation::Legacy, std::move(*payload) } } : std::nullopt;
		}
		if (a_selection.representation == Representation::Component) {
			const auto payload = EncodeComponent(*a_selection.adapter, a_value);
			return payload.has_value() ? std::optional<ProjectedPayload>{ ProjectedPayload{ Representation::Component, std::move(*payload) } } : std::nullopt;
		}
		return std::nullopt;
	}

	ReceiveDemuxResult ReceiveDemux::Classify(
		std::span<const std::uint8_t> a_payload,
		const SessionCapabilities& a_capabilities) const
	{
		ReceiveDemuxResult result;
		if (a_payload.empty()) {
			result.rejection = ReceiveRejectReason::EmptyPayload;
			return result;
		}

		const bool hasCog2Magic = a_payload.size() >= F4T::GnsSnapshotEnvelope::Magic.size() &&
			std::equal(F4T::GnsSnapshotEnvelope::Magic.begin(), F4T::GnsSnapshotEnvelope::Magic.end(), a_payload.begin());
		if (hasCog2Magic) {
			F4T::GnsSnapshotEnvelope::Header header{};
			if (F4T::GnsSnapshotEnvelope::Decode(a_payload, header) != F4T::GnsSnapshotEnvelope::DecodeResult::Valid ||
				!IsValidLegacyJson(a_payload.subspan(F4T::GnsSnapshotEnvelope::HeaderSize), header.family)) {
				result.rejection = ReceiveRejectReason::MalformedCog2;
				return result;
			}
			result.classification = ReceiveClassification::Legacy;
			result.legacyPayloadOffset = F4T::GnsSnapshotEnvelope::HeaderSize;
			result.legacyHeader = header;
			return result;
		}

		const auto jsonOffset = FirstNonWhitespace(a_payload);
		if (jsonOffset == a_payload.size()) {
			result.rejection = ReceiveRejectReason::EmptyPayload;
			return result;
		}
		if (a_payload[jsonOffset] == static_cast<std::uint8_t>('{') || a_payload[jsonOffset] == static_cast<std::uint8_t>('[')) {
			if (!IsValidLegacyJson(a_payload.subspan(jsonOffset), std::nullopt)) {
				result.rejection = ReceiveRejectReason::MalformedJson;
				return result;
			}
			result.classification = ReceiveClassification::Legacy;
			result.legacyPayloadOffset = jsonOffset;
			return result;
		}

		if (!a_capabilities.Has("component-snapshot-v1")) {
			result.rejection = ReceiveRejectReason::ComponentNotNegotiated;
			return result;
		}
		if (m_definitions == nullptr) {
			result.rejection = ReceiveRejectReason::InvalidComponent;
			return result;
		}

		F4T::Replication::DecodedFrame frame;
		const auto status = F4T::Replication::DecodeFrame(a_payload, frame, [this, &a_capabilities, &frame](Family a_family) {
			const auto* adapter = m_registry.FindByComponentFamily(a_family);
			const auto requiredCapability = m_registry.RequiredComponentCapability(a_family);
			const auto channel = m_definitions->Find(a_family);
			const bool channelAllowed = frame.channel != F4T::Replication::Channel::Reliable || a_capabilities.Has("component-snapshot-channels-v1");
			const bool familyChannelAllowed = channel.has_value() && *channel == frame.channel;
			return adapter != nullptr &&
			       m_registry.SupportsComponent(a_family) &&
			       !requiredCapability.empty() &&
			       a_capabilities.Has(requiredCapability) &&
			       channelAllowed &&
			       familyChannelAllowed;
		});
		if (status != F4T::Replication::DecodeStatus::Valid) {
			result.rejection = ReceiveRejectReason::InvalidComponent;
			return result;
		}
		if (frame.channel == F4T::Replication::Channel::Reliable && !a_capabilities.Has("component-snapshot-channels-v1")) {
			result.rejection = ReceiveRejectReason::InvalidComponent;
			return result;
		}
		for (const auto& [key, bytes] : frame.baseline) {
			const auto* adapter = m_registry.FindByComponentFamily(key.family);
			if (adapter == nullptr || !DecodeComponent(*adapter, bytes).has_value()) {
				result.rejection = ReceiveRejectReason::InvalidComponent;
				return result;
			}
		}
		for (const auto& entity : frame.delta.changedEntities) {
			for (const auto& component : entity.changed) {
				const auto* adapter = m_registry.FindByComponentFamily(component.family);
				if (adapter == nullptr || !DecodeComponent(*adapter, component.data).has_value()) {
					result.rejection = ReceiveRejectReason::InvalidComponent;
					return result;
				}
			}
		}
		if (m_definitions != nullptr) {
			for (const auto& entity : frame.delta.changedEntities) {
				for (const auto family : entity.removedComponents) {
					const auto channel = m_definitions->Find(family);
					const auto* adapter = m_registry.FindByComponentFamily(family);
					const auto requiredCapability = m_registry.RequiredComponentCapability(family);
					if (adapter == nullptr || !m_registry.SupportsComponent(family) || requiredCapability.empty() ||
						!a_capabilities.Has(requiredCapability) || !channel.has_value() || *channel != frame.channel ||
						(frame.channel == F4T::Replication::Channel::Reliable && !a_capabilities.Has("component-snapshot-channels-v1"))) {
						result.rejection = ReceiveRejectReason::InvalidComponent;
						return result;
					}
				}
			}
		}
		result.classification = ReceiveClassification::Component;
		result.componentFrame = std::move(frame);
		return result;
	}
}
