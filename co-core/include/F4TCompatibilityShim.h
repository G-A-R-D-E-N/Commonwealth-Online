%:pragma once

%:include "F4TGnsSnapshotEnvelope.h"
%:include "F4TSnapshotDelta.h"

%:include <any>
%:include <array>
%:include <cstddef>
%:include <cstdint>
%:include <functional>
%:include <map>
%:include <optional>
%:include <span>
%:include <string>
%:include <string_view>
%:include <unordered_map>
%:include <variant>
%:include <vector>

namespace F4T::Compatibility
{
	enum class ConversionDirection : std::uint8_t
	{
		LegacyToComponent = 1,
		ComponentToLegacy = 2
	};

	using ConversionDirections = std::uint8_t;
	using CanonicalValue = std::any;

	inline constexpr ConversionDirections LegacyToComponentDirection = static_cast<ConversionDirections>(ConversionDirection::LegacyToComponent);
	inline constexpr ConversionDirections ComponentToLegacyDirection = static_cast<ConversionDirections>(ConversionDirection::ComponentToLegacy);
	inline constexpr ConversionDirections AllConversionDirections = LegacyToComponentDirection | ComponentToLegacyDirection;

	[[nodiscard]] inline constexpr bool SupportsDirection(ConversionDirections a_directions, ConversionDirection a_direction) noexcept
	{
		return (a_directions & static_cast<ConversionDirections>(a_direction)) != 0;
	}

	struct LegacyFamilyMetadata
	{
		std::string name;
	};

	struct ComponentFamilyMetadata
	{
		F4T::Replication::Family family{};
	};

	struct FamilyAdapterMetadata
	{
		LegacyFamilyMetadata legacy;
		ComponentFamilyMetadata component;
		ConversionDirections supportedDirections{};
		std::string requiredComponentCapability;
	};

	using LegacyToCanonicalFn = std::function<std::optional<CanonicalValue>(std::string_view)>;
	using CanonicalToLegacyFn = std::function<std::optional<std::string>(const CanonicalValue&)>;
	using CanonicalToComponentFn = std::function<std::optional<std::vector<std::uint8_t>>(const CanonicalValue&)>;
	using ComponentToCanonicalFn = std::function<std::optional<CanonicalValue>(std::span<const std::uint8_t>)>;

	struct FamilyAdapter
	{
		FamilyAdapterMetadata metadata;
		LegacyToCanonicalFn legacyToCanonical;
		CanonicalToLegacyFn canonicalToLegacy;
		CanonicalToComponentFn canonicalToComponent;
		ComponentToCanonicalFn componentToCanonical;

		[[nodiscard]] bool Supports(ConversionDirection a_direction) const noexcept;
	};

	class CompatibilityRegistry
	{
	public:
		[[nodiscard]] bool Register(FamilyAdapter a_adapter);
		[[nodiscard]] const FamilyAdapter* FindByLegacyFamily(std::string_view a_name) const noexcept;
		[[nodiscard]] const FamilyAdapter* FindByComponentFamily(F4T::Replication::Family a_family) const noexcept;
		[[nodiscard]] bool SupportsComponent(F4T::Replication::Family a_family) const noexcept;
		[[nodiscard]] std::string_view RequiredComponentCapability(F4T::Replication::Family a_family) const noexcept;

	private:
		std::array<std::optional<FamilyAdapter>, 256> m_byComponent;
		std::unordered_map<std::string, std::uint8_t> m_legacyFamilies;
	};

	struct SessionCapabilities
	{
		std::vector<std::string> values;

		[[nodiscard]] bool Has(std::string_view a_capability) const noexcept;
	};

	enum class Representation : std::uint8_t
	{
		Legacy = 0,
		Component = 1
	};

	struct RepresentationSelectionKey
	{
		std::uint32_t recipientId{};
		std::uint32_t entityId{};
		F4T::Replication::Family componentFamily{};
		std::uint64_t revision{};

		[[nodiscard]] friend bool operator<(const RepresentationSelectionKey& a_lhs, const RepresentationSelectionKey& a_rhs) noexcept
		{
			if (a_lhs.recipientId != a_rhs.recipientId) {
				return a_lhs.recipientId < a_rhs.recipientId;
			}
			if (a_lhs.entityId != a_rhs.entityId) {
				return a_lhs.entityId < a_rhs.entityId;
			}
			if (a_lhs.componentFamily != a_rhs.componentFamily) {
				return static_cast<std::uint8_t>(a_lhs.componentFamily) < static_cast<std::uint8_t>(a_rhs.componentFamily);
			}
			return a_lhs.revision < a_rhs.revision;
		}

		[[nodiscard]] friend bool operator==(const RepresentationSelectionKey&, const RepresentationSelectionKey&) noexcept = default;
	};

	struct RepresentationSelection
	{
		RepresentationSelectionKey key;
		Representation representation{ Representation::Legacy };
		const FamilyAdapter* adapter{};
	};

	class RepresentationPolicy
	{
	public:
		explicit RepresentationPolicy(const CompatibilityRegistry& a_registry) noexcept : m_registry(a_registry) {}
		RepresentationPolicy(
			const CompatibilityRegistry& a_registry,
			const F4T::Replication::FrozenComponentDefinitionTable& a_definitions) noexcept :
			m_registry(a_registry),
			m_definitions(&a_definitions)
		{}

		[[nodiscard]] std::optional<RepresentationSelection> Select(
			const RepresentationSelectionKey& a_key,
			std::string_view a_legacyFamily,
			const SessionCapabilities& a_capabilities) const noexcept;

	private:
		const CompatibilityRegistry& m_registry;
		const F4T::Replication::FrozenComponentDefinitionTable* m_definitions{};
	};

	class RepresentationLedger
	{
	public:
		[[nodiscard]] bool Commit(const RepresentationSelection& a_selection);
		[[nodiscard]] bool Contains(const RepresentationSelectionKey& a_key) const;
		void Clear() noexcept;

	private:
		std::map<RepresentationSelectionKey, Representation> m_selections;
	};

	using WirePayload = std::variant<std::string, std::vector<std::uint8_t>>;

	struct ProjectedPayload
	{
		Representation representation{ Representation::Legacy };
		WirePayload payload;
	};

	[[nodiscard]] std::optional<CanonicalValue> DecodeLegacy(const FamilyAdapter& a_adapter, std::string_view a_payload);
	[[nodiscard]] std::optional<std::string> EncodeLegacy(const FamilyAdapter& a_adapter, const CanonicalValue& a_value);
	[[nodiscard]] std::optional<std::vector<std::uint8_t>> EncodeComponent(const FamilyAdapter& a_adapter, const CanonicalValue& a_value);
	[[nodiscard]] std::optional<CanonicalValue> DecodeComponent(const FamilyAdapter& a_adapter, std::span<const std::uint8_t> a_payload);
	[[nodiscard]] std::optional<ProjectedPayload> Project(const RepresentationSelection& a_selection, const CanonicalValue& a_value);

	enum class ReceiveClassification : std::uint8_t
	{
		Rejected = 0,
		Legacy = 1,
		Component = 2
	};

	enum class ReceiveRejectReason : std::uint8_t
	{
		None = 0,
		EmptyPayload,
		MalformedCog2,
		MalformedJson,
		ComponentNotNegotiated,
		InvalidComponent
	};

	struct ReceiveDemuxResult
	{
		ReceiveClassification classification{ ReceiveClassification::Rejected };
		ReceiveRejectReason rejection{ ReceiveRejectReason::None };
		std::size_t legacyPayloadOffset{};
		std::optional<F4T::GnsSnapshotEnvelope::Header> legacyHeader;
		std::optional<F4T::Replication::DecodedFrame> componentFrame;

		[[nodiscard]] bool Accepted() const noexcept { return classification != ReceiveClassification::Rejected; }
	};

	class ReceiveDemux
	{
	public:
		explicit ReceiveDemux(const CompatibilityRegistry& a_registry) noexcept : m_registry(a_registry) {}
		ReceiveDemux(
			const CompatibilityRegistry& a_registry,
			const F4T::Replication::FrozenComponentDefinitionTable& a_definitions) noexcept :
			m_registry(a_registry),
			m_definitions(&a_definitions)
		{}

		[[nodiscard]] ReceiveDemuxResult Classify(
			std::span<const std::uint8_t> a_payload,
			const SessionCapabilities& a_capabilities) const;

	private:
		const CompatibilityRegistry& m_registry;
		const F4T::Replication::FrozenComponentDefinitionTable* m_definitions{};
	};
}
