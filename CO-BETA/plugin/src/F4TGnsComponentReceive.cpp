#include "pch.h"

#include "F4TGnsComponentReceive.h"

#include <any>
#include <optional>
#include <string>
#include <utility>

namespace F4T::ComponentReceive
{
	namespace
	{
		using CanonicalValue = F4T::Compatibility::CanonicalValue;

		std::optional<CanonicalValue> DecodeDropObject(std::span<const std::uint8_t> a_payload)
		{
			const auto object = F4T::Drops::DecodeStableDroppedObject(a_payload);
			if (!object) {
				return std::nullopt;
			}
			return CanonicalValue{ *object };
		}

		std::optional<std::string> EncodeDropObjectLegacy(const CanonicalValue& a_value)
		{
			const auto* object = std::any_cast<F4T::Drops::StableDroppedObject>(&a_value);
			if (object == nullptr) {
				return std::nullopt;
			}
			return nlohmann::json{
				{ "type", std::string{ kDropLegacyFamily } },
				{ "item", object->item },
				{ "position", nlohmann::json{
					{ "cellId", object->position.cellId },
					{ "worldspaceId", object->position.worldspaceId },
					{ "x", object->position.x },
					{ "y", object->position.y },
					{ "z", object->position.z } } }
			}.dump();
		}

		bool RegisterDropFamily(F4T::Compatibility::CompatibilityRegistry& a_registry,
			F4T::Replication::FrozenComponentDefinitionTable& a_definitions)
		{
			F4T::Replication::ComponentRegistry definitions;
			if (!definitions.RegisterDefinition(kDropFamily, F4T::Replication::AuthorityClass::Server, kDropChannel)) {
				return false;
			}
			const auto frozen = definitions.Freeze();
			if (!frozen) {
				return false;
			}

			F4T::Compatibility::FamilyAdapter adapter;
			adapter.metadata.legacy.name = std::string{ kDropLegacyFamily };
			adapter.metadata.component.family = kDropFamily;
			adapter.metadata.supportedDirections = F4T::Compatibility::ComponentToLegacyDirection;
			adapter.metadata.requiredComponentCapability = std::string{ F4T::InventorySync::kSnapshotCapability };
			adapter.componentToCanonical = DecodeDropObject;
			adapter.canonicalToLegacy = EncodeDropObjectLegacy;
			if (!a_registry.Register(std::move(adapter))) {
				return false;
			}

			a_definitions = *frozen;
			return true;
		}
	}

	void InstallComponentReceive(F4T::GnsWinsockShimState::SocketState& a_state)
	{
		if (a_state.componentReceiveInstalled) {
			return;
		}
		if (!RegisterDropFamily(a_state.compatibilityRegistry, a_state.componentDefinitions)) {
			return;
		}

		a_state.componentCapabilities.values.clear();
		for (const auto capability : F4T::InventorySync::kSessionCapabilities) {
			a_state.componentCapabilities.values.emplace_back(capability);
		}
		a_state.componentReceiveInstalled = true;
	}

	F4T::Compatibility::ReceiveDemuxResult ClassifyReceive(
		F4T::GnsWinsockShimState::SocketState& a_state,
		std::span<const std::uint8_t> a_payload)
	{
		InstallComponentReceive(a_state);
		const F4T::Compatibility::ReceiveDemux demux{ a_state.compatibilityRegistry, a_state.componentDefinitions };
		return demux.Classify(a_payload, a_state.componentCapabilities);
	}

	std::string HandleComponentFrame(const F4T::Compatibility::ReceiveDemuxResult& a_classified)
	{
		if (!a_classified.componentFrame) {
			return std::string{ kComponentDecodeFailure };
		}
		if (!F4T::InventorySync::ApplyDropFrame(*a_classified.componentFrame)) {
			return std::string{ kComponentDecodeFailure };
		}
		return {};
	}
}
