#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace F4T::InventorySnapshot
{
	inline constexpr std::string_view kRequestType = "inventorySnapshotRequest";
	inline constexpr std::string_view kResultType = "inventorySnapshotResult";
	inline constexpr std::size_t kMaximumPartItems = 512;
	inline constexpr std::uint32_t kMaximumParts = 4;
	inline constexpr std::uint32_t kMaximumItemCount = 2147483647U;
	inline constexpr std::string_view kGainLimitedReason = "gainLimited";
	inline constexpr std::string_view kCapsItemKey = "fallout4.esm:0000000F";

	enum class GainMode { Off, Warn, Enforce };

	struct BucketLimit
	{
		std::uint64_t burst{};
		std::uint64_t onboardingBurst{};
		std::uint64_t refillPerMinute{};
	};

	struct GainPolicy
	{
		GainMode mode{ GainMode::Off };
		std::uint32_t maxStack{ 10000000U };
		std::map<std::string, BucketLimit> buckets;
	};

	struct Bucket
	{
		double tokens{};
		double at{};
	};

	using GainBuckets = std::map<std::string, Bucket>;

	struct GainVerdict
	{
		bool limited{};
		std::string detail;
		GainBuckets next;
	};

	using GainCheck = std::function<bool(const nlohmann::json& a_before, const nlohmann::json& a_after, std::uint32_t a_revision)>;

	struct Outcome
	{
		nlohmann::json packet;
		bool changed{};
	};

	struct Part
	{
		std::string operationId;
		std::uint32_t baseRevision{};
		std::uint32_t part{};
		std::uint32_t parts{};
		nlohmann::json wireItems;
	};

	[[nodiscard]] std::optional<nlohmann::json> NormalizeItems(const nlohmann::json& a_wireItems);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeSnapshotItems(const nlohmann::json& a_wireItems);
	[[nodiscard]] std::optional<Part> NormalizePart(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeRequest(const std::string& a_operationId,
		std::uint32_t a_baseRevision, const nlohmann::json& a_wireItems);
	[[nodiscard]] bool IsRequest(const nlohmann::json& a_request);
	[[nodiscard]] GainPolicy DefaultGainPolicy(GainMode a_mode);
	[[nodiscard]] std::optional<GainMode> ParseGainMode(std::string_view a_text);
	[[nodiscard]] GainVerdict EvaluateGain(const nlohmann::json& a_before, const nlohmann::json& a_after, bool a_onboarding,
		const GainPolicy& a_policy, const GainBuckets& a_buckets, double a_now);
	[[nodiscard]] Outcome Reconcile(nlohmann::json& a_root, const std::string& a_profileId, const nlohmann::json& a_request,
		const GainCheck& a_gain = {});
	std::uint32_t ValidateLoggedOperation(const nlohmann::json& a_operation, std::set<std::string>& a_seen,
		std::uint32_t a_expectedRevision, std::uint32_t a_revision);
}
