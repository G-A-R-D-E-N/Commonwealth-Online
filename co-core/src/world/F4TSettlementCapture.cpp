#include "F4TSettlementCapture.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace F4T::SettlementCapture
{
	namespace
	{
		constexpr double kMaximumCaptureRadius{ 6000.0 };
		constexpr double kMaximumCoordinate{ 10000000.0 };
	}

	Workbench WorkbenchOf(const Workshop::BuildZonePolicy& a_policy, const Policy& a_captureRules) noexcept
	{
		const double radius = a_policy.captureRadius > 0.0F ? static_cast<double>(a_policy.captureRadius) : a_captureRules.defaultRadius;
		return Workbench{
			a_policy.buildZoneId,
			a_policy.hasWorkbench ? a_policy.workbenchX : a_policy.centerX,
			a_policy.hasWorkbench ? a_policy.workbenchY : a_policy.centerY,
			a_policy.hasWorkbench ? a_policy.workbenchZ : a_policy.centerZ,
			(std::min)(radius, a_captureRules.maximumRadius)
		};
	}

	Standing Classify(const Workbench& a_workbench, const Presence& a_presence, const Policy& a_policy) noexcept
	{
		const auto age = a_presence.positionAgeSeconds;
		if (!a_presence.positionKnown || !std::isfinite(age) || age < 0.0 || age > a_policy.maximumPositionAgeSeconds ||
			!std::isfinite(a_presence.x) || !std::isfinite(a_presence.y) || !std::isfinite(a_presence.z)) {
			return Standing::Stale;
		}
		if (!a_presence.vitalsKnown || !a_presence.alive) {
			return Standing::NotAlive;
		}
		if (a_presence.downed) {
			return Standing::Downed;
		}
		const double dx = a_presence.x - a_workbench.x;
		const double dy = a_presence.y - a_workbench.y;
		const double dz = a_presence.z - a_workbench.z;
		if (!std::isfinite(a_workbench.x) || !std::isfinite(a_workbench.y) || !std::isfinite(a_workbench.z) || !std::isfinite(a_workbench.radius) ||
			dx * dx + dy * dy + dz * dz > a_workbench.radius * a_workbench.radius) {
			return Standing::OutOfRange;
		}
		return Standing::Eligible;
	}

	bool ValidPolicy(const Policy& a_policy) noexcept
	{
		return std::isfinite(a_policy.holdSeconds) && a_policy.holdSeconds > 0.0 &&
		       std::isfinite(a_policy.heartbeatTimeoutSeconds) && a_policy.heartbeatTimeoutSeconds > 0.0 &&
		       std::isfinite(a_policy.maximumPositionAgeSeconds) && a_policy.maximumPositionAgeSeconds > 0.0 &&
		       std::isfinite(a_policy.defaultRadius) && a_policy.defaultRadius > 0.0 &&
		       std::isfinite(a_policy.maximumRadius) && a_policy.maximumRadius > 0.0 &&
		       a_policy.maximumAttempts > 0;
	}

	Status StatusOf(Standing a_standing) noexcept
	{
		switch (a_standing) {
		case Standing::Eligible:
			return Status::Holding;
		case Standing::Stale:
			return Status::StalePosition;
		case Standing::OutOfRange:
			return Status::NotInRange;
		case Standing::NotAlive:
			return Status::NotAlive;
		case Standing::Downed:
			return Status::Downed;
		}
		return Status::InvalidRequest;
	}

	std::string_view StatusName(Status a_status) noexcept
	{
		switch (a_status) {
		case Status::Started:
			return "started";
		case Status::Holding:
			return "holding";
		case Status::Contested:
			return "contested";
		case Status::Captured:
			return "captured";
		case Status::Cancelled:
			return "cancelled";
		case Status::InvalidRequest:
			return "invalidRequest";
		case Status::UnknownBuildZone:
			return "unknownBuildZone";
		case Status::AlreadyOwner:
			return "alreadyOwner";
		case Status::NoAttempt:
			return "noAttempt";
		case Status::Busy:
			return "busy";
		case Status::NotInRange:
			return "notInRange";
		case Status::StalePosition:
			return "stalePosition";
		case Status::NotAlive:
			return "notAlive";
		case Status::Downed:
			return "downed";
		case Status::TimedOut:
			return "timedOut";
		case Status::RateLimited:
			return "rateLimited";
		case Status::SettlementLimit:
			return "settlementLimit";
		case Status::PersistenceUnavailable:
			return "persistenceUnavailable";
		}
		return "invalidRequest";
	}

	std::optional<Request> NormalizeRequest(const nlohmann::json& a_packet)
	{
		const auto zone = a_packet.find("buildZoneId");
		const auto op = a_packet.find("op");
		if (zone == a_packet.end() || op == a_packet.end() || !zone->is_number_unsigned() || !op->is_string()) {
			return std::nullopt;
		}
		const auto& name = op->get_ref<const std::string&>();
		const auto id = zone->get<Workshop::BuildZoneId>();
		if (name == "start") {
			return Request{ id, Op::Start };
		}
		if (name == "continue") {
			return Request{ id, Op::Continue };
		}
		if (name == "cancel") {
			return Request{ id, Op::Cancel };
		}
		return std::nullopt;
	}

	nlohmann::json MakeState(
		const Workbench& a_workbench,
		std::string_view a_capturerProfileId,
		std::string_view a_ownerProfileId,
		Status a_status,
		double a_elapsedSeconds,
		double a_holdSeconds)
	{
		return nlohmann::json{
			{ "type", "settlementCaptureState" },
			{ "buildZoneId", a_workbench.buildZoneId },
			{ "status", StatusName(a_status) },
			{ "capturerProfileId", std::string{ a_capturerProfileId } },
			{ "ownerProfileId", std::string{ a_ownerProfileId } },
			{ "raid", !a_ownerProfileId.empty() && a_ownerProfileId != a_capturerProfileId },
			{ "elapsedSeconds", a_elapsedSeconds },
			{ "holdSeconds", a_holdSeconds },
			{ "workbenchX", a_workbench.x },
			{ "workbenchY", a_workbench.y },
			{ "workbenchZ", a_workbench.z },
			{ "captureRadius", a_workbench.radius }
		};
	}

	std::optional<DecodedState> DecodeState(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		const auto type = a_packet.find("type");
		const auto zone = a_packet.find("buildZoneId");
		const auto status = a_packet.find("status");
		const auto capturer = a_packet.find("capturerProfileId");
		const auto owner = a_packet.find("ownerProfileId");
		const auto raid = a_packet.find("raid");
		const auto elapsed = a_packet.find("elapsedSeconds");
		const auto hold = a_packet.find("holdSeconds");
		if (type == a_packet.end() || !type->is_string() || type->get<std::string>() != "settlementCaptureState" ||
			zone == a_packet.end() || !zone->is_number_unsigned() || *zone == 0 ||
			status == a_packet.end() || !status->is_string() ||
			capturer == a_packet.end() || !capturer->is_string() ||
			owner == a_packet.end() || !owner->is_string() ||
			raid == a_packet.end() || !raid->is_boolean() ||
			elapsed == a_packet.end() || !elapsed->is_number() || !std::isfinite(elapsed->get<double>()) ||
			hold == a_packet.end() || !hold->is_number() || !std::isfinite(hold->get<double>())) {
			return std::nullopt;
		}
		DecodedState state;
		state.buildZoneId = zone->get<Workshop::BuildZoneId>();
		state.status = status->get<std::string>();
		state.capturerProfileId = capturer->get<std::string>();
		state.ownerProfileId = owner->get<std::string>();
		state.raid = raid->get<bool>();
		state.elapsedSeconds = elapsed->get<double>();
		state.holdSeconds = hold->get<double>();
		const auto x = a_packet.find("workbenchX");
		const auto y = a_packet.find("workbenchY");
		const auto z = a_packet.find("workbenchZ");
		const auto radius = a_packet.find("captureRadius");
		if (x != a_packet.end() && y != a_packet.end() && z != a_packet.end() && radius != a_packet.end() &&
			x->is_number() && y->is_number() && z->is_number() && radius->is_number()) {
			const auto px = x->get<double>();
			const auto py = y->get<double>();
			const auto pz = z->get<double>();
			const auto pr = radius->get<double>();
			if (std::isfinite(px) && std::isfinite(py) && std::isfinite(pz) && std::isfinite(pr) && pr > 0.0 && std::fabs(px) <= kMaximumCoordinate && std::fabs(py) <= kMaximumCoordinate && std::fabs(pz) <= kMaximumCoordinate) {
				state.hasCapturePoint = true;
				state.workbenchX = px;
				state.workbenchY = py;
				state.workbenchZ = pz;
				state.captureRadius = (std::min)(pr, kMaximumCaptureRadius);
			}
		}
		return state;
	}

	nlohmann::json MakeReject(Workshop::BuildZoneId a_buildZoneId, Status a_status)
	{
		return nlohmann::json{
			{ "type", "settlementCaptureReject" },
			{ "buildZoneId", a_buildZoneId },
			{ "status", StatusName(a_status) }
		};
	}

	nlohmann::json MakeZones(const std::vector<Workbench>& a_workbenches)
	{
		auto zones = nlohmann::json::array();
		for (const auto& bench : a_workbenches) {
			zones.push_back(nlohmann::json{
				{ "buildZoneId", bench.buildZoneId },
				{ "x", bench.x },
				{ "y", bench.y },
				{ "z", bench.z },
				{ "radius", bench.radius } });
		}
		return nlohmann::json{ { "type", "settlementCaptureZones" }, { "zones", std::move(zones) } };
	}

	std::optional<std::vector<Workbench>> DecodeZones(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		const auto type = a_packet.find("type");
		const auto zones = a_packet.find("zones");
		if (type == a_packet.end() || !type->is_string() || type->get<std::string>() != "settlementCaptureZones" ||
			zones == a_packet.end() || !zones->is_array()) {
			return std::nullopt;
		}
		std::vector<Workbench> decoded;
		decoded.reserve(zones->size());
		for (const auto& entry : *zones) {
			if (!entry.is_object()) {
				return std::nullopt;
			}
			const auto zone = entry.find("buildZoneId");
			const auto x = entry.find("x");
			const auto y = entry.find("y");
			const auto z = entry.find("z");
			const auto radius = entry.find("radius");
			if (zone == entry.end() || !zone->is_number_unsigned() || *zone == 0 ||
				x == entry.end() || !x->is_number() || y == entry.end() || !y->is_number() ||
				z == entry.end() || !z->is_number() || radius == entry.end() || !radius->is_number()) {
				return std::nullopt;
			}
			const Workbench bench{ zone->get<Workshop::BuildZoneId>(), x->get<double>(), y->get<double>(), z->get<double>(), radius->get<double>() };
			if (!std::isfinite(bench.x) || !std::isfinite(bench.y) || !std::isfinite(bench.z) || !std::isfinite(bench.radius) || bench.radius <= 0.0 || std::fabs(bench.x) > kMaximumCoordinate || std::fabs(bench.y) > kMaximumCoordinate || std::fabs(bench.z) > kMaximumCoordinate) {
				return std::nullopt;
			}
			decoded.push_back(Workbench{ bench.buildZoneId, bench.x, bench.y, bench.z, (std::min)(bench.radius, kMaximumCaptureRadius) });
		}
		return decoded;
	}

	std::set<Workshop::BuildZoneId> Tracker::ActiveZones() const
	{
		std::set<Workshop::BuildZoneId> zones;
		for (const auto& entry : m_attempts) {
			zones.insert(entry.second.buildZoneId);
		}
		return zones;
	}

	std::vector<std::string> Tracker::CapturersForZone(Workshop::BuildZoneId a_buildZoneId) const
	{
		std::vector<std::string> capturers;
		for (const auto& [profileId, attempt] : m_attempts) {
			if (attempt.buildZoneId == a_buildZoneId) {
				capturers.push_back(profileId);
			}
		}
		return capturers;
	}

	Standing Tracker::StandingOf(const Workbench& a_workbench, std::string_view a_profileId, const std::vector<Presence>& a_presences) const noexcept
	{
		for (const auto& presence : a_presences) {
			if (presence.profileId == a_profileId) {
				return Classify(a_workbench, presence, m_policy);
			}
		}
		return Standing::Stale;
	}

	bool Tracker::Contested(const Workbench& a_workbench, std::string_view a_profileId, const std::vector<Presence>& a_presences) const noexcept
	{
		for (const auto& presence : a_presences) {
			if (presence.profileId != a_profileId && Classify(a_workbench, presence, m_policy) == Standing::Eligible) {
				return true;
			}
		}
		return false;
	}

	bool Tracker::HeartbeatExpired(const Attempt& a_attempt, double a_nowSeconds) const noexcept
	{
		return a_nowSeconds - a_attempt.lastHeartbeat > m_policy.heartbeatTimeoutSeconds;
	}

	bool Tracker::RefreshContest(Attempt& a_attempt, bool a_contested, double a_nowSeconds) noexcept
	{
		const bool changed = a_contested != a_attempt.contested;
		if (a_contested || changed) {
			a_attempt.holdStart = a_nowSeconds;
		}
		a_attempt.contested = a_contested;
		return changed;
	}

	Reply Tracker::Handle(
		const Request& a_request,
		const Workbench& a_workbench,
		std::string_view a_ownerProfileId,
		std::string_view a_requesterProfileId,
		const std::vector<Presence>& a_presences,
		double a_nowSeconds)
	{
		if (!std::isfinite(a_nowSeconds) || a_request.buildZoneId == 0 || a_request.buildZoneId != a_workbench.buildZoneId ||
			!SettlementAccess::ValidProfileId(a_requesterProfileId)) {
			return { Status::InvalidRequest, 0.0 };
		}
		const std::string requester{ a_requesterProfileId };
		auto found = m_attempts.find(requester);
		const bool mine = found != m_attempts.end() && found->second.buildZoneId == a_request.buildZoneId;
		if (a_request.op == Op::Cancel) {
			if (!mine) {
				return { Status::NoAttempt, 0.0 };
			}
			m_attempts.erase(found);
			return { Status::Cancelled, 0.0 };
		}
		if (a_ownerProfileId == a_requesterProfileId) {
			return { Status::AlreadyOwner, 0.0 };
		}
		if (a_request.op == Op::Continue && !mine) {
			return { Status::NoAttempt, 0.0 };
		}
		if (a_request.op == Op::Start && found != m_attempts.end() && !mine) {
			return { Status::Busy, 0.0 };
		}
		const auto standing = StandingOf(a_workbench, requester, a_presences);
		if (standing != Standing::Eligible) {
			if (mine) {
				m_attempts.erase(found);
			}
			return { StatusOf(standing), 0.0, mine };
		}
		if (mine && HeartbeatExpired(found->second, a_nowSeconds)) {
			m_attempts.erase(found);
			return { Status::TimedOut, 0.0, true };
		}
		const bool started = !mine;
		if (started) {
			if (m_attempts.size() >= m_policy.maximumAttempts) {
				return { Status::Busy, 0.0 };
			}
			found = m_attempts.emplace(requester, Attempt{ a_request.buildZoneId, a_nowSeconds, a_nowSeconds, false }).first;
		}
		auto& attempt = found->second;
		attempt.lastHeartbeat = a_nowSeconds;
		const bool contested = Contested(a_workbench, requester, a_presences);
		(void)RefreshContest(attempt, contested, a_nowSeconds);
		if (contested) {
			return { Status::Contested, 0.0 };
		}
		return { started ? Status::Started : Status::Holding, a_nowSeconds - attempt.holdStart };
	}

	std::vector<Event> Tracker::Update(
		const Workbench& a_workbench,
		std::string_view a_ownerProfileId,
		const std::vector<Presence>& a_presences,
		double a_nowSeconds)
	{
		std::vector<Event> events;
		if (!std::isfinite(a_nowSeconds)) {
			return events;
		}
		for (auto it = m_attempts.begin(); it != m_attempts.end();) {
			auto& attempt = it->second;
			if (attempt.buildZoneId != a_workbench.buildZoneId) {
				++it;
				continue;
			}
			const auto standing = StandingOf(a_workbench, it->first, a_presences);
			std::optional<Status> failure;
			if (it->first == a_ownerProfileId) {
				failure = Status::AlreadyOwner;
			} else if (standing != Standing::Eligible) {
				failure = StatusOf(standing);
			} else if (HeartbeatExpired(attempt, a_nowSeconds)) {
				failure = Status::TimedOut;
			}
			if (failure) {
				events.push_back({ a_workbench.buildZoneId, it->first, *failure, 0.0 });
				it = m_attempts.erase(it);
				continue;
			}
			const bool contested = Contested(a_workbench, it->first, a_presences);
			const bool changed = RefreshContest(attempt, contested, a_nowSeconds);
			if (changed) {
				events.push_back({ a_workbench.buildZoneId, it->first, contested ? Status::Contested : Status::Holding, 0.0 });
			} else if (!contested && a_nowSeconds - attempt.holdStart >= m_policy.holdSeconds) {
				events.push_back({ a_workbench.buildZoneId, it->first, Status::Captured, m_policy.holdSeconds });
				it = m_attempts.erase(it);
				continue;
			}
			++it;
		}
		return events;
	}

	void Tracker::DropZone(Workshop::BuildZoneId a_buildZoneId)
	{
		for (auto it = m_attempts.begin(); it != m_attempts.end();) {
			it = it->second.buildZoneId == a_buildZoneId ? m_attempts.erase(it) : std::next(it);
		}
	}
}
