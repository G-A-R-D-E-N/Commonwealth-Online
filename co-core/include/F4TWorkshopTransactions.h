#pragma once

#include "F4TFormIdentity.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace F4T::Workshop
{
	using PlayerId = std::uint32_t;
	using BuildZoneId = std::uint64_t;
	using OperationId = std::uint64_t;
	using ObjectId = std::uint64_t;
	using Revision = std::uint64_t;

	inline constexpr std::size_t MaximumRememberedOperations = 4096;
	inline constexpr std::size_t MaximumPluginNameBytes = 255;
	inline constexpr std::size_t MaximumWorldTombstones = 4096;
	inline constexpr std::size_t MaximumTombstonesPerProfile = 256;
	inline constexpr std::uint32_t MaximumLocalFormId = 0x00FFFFFF;

	enum class OperationKind : std::uint8_t
	{
		Place,
		Move,
		Scrap,
		ScrapWorld
	};

	enum class ApplyStatus : std::uint8_t
	{
		Committed,
		StaleRevision,
		InvalidRequest,
		UnknownBuildZone,
		ObjectNotFound,
		OutsideBuildZone,
		BudgetExceeded,
		ObjectIdExhausted,
		RevisionExhausted,
		NotAuthorized,
		AlreadyRemoved
	};

	struct WorkshopAuthority
	{
		using CanBuildAt = std::function<bool(const std::string& a_profileId, BuildZoneId a_buildZoneId)>;

		[[nodiscard]] static bool AllowAll(const std::string&, BuildZoneId) noexcept
		{
			return true;
		}
	};

	struct FormKey
	{
		std::string plugin;
		std::uint32_t localFormId{};

		[[nodiscard]] bool operator==(const FormKey&) const noexcept = default;
	};

	struct Transform
	{
		float x{};
		float y{};
		float z{};
		float pitch{};
		float yaw{};
		float roll{};
		float scale{ 1.0F };

		[[nodiscard]] bool operator==(const Transform&) const noexcept = default;
	};

	struct BuildZonePolicy
	{
		BuildZoneId buildZoneId{};
		FormKey space;
		float centerX{};
		float centerY{};
		float centerZ{};
		float radius{};
		float maxScale{ 1.0F };
		std::uint32_t maxObjects{};
		bool hasWorkbench{};
		float workbenchX{};
		float workbenchY{};
		float workbenchZ{};
		float captureRadius{};
	};

	struct Request
	{
		PlayerId playerId{};
		std::string profileId;
		BuildZoneId buildZoneId{};
		OperationId operationId{};
		Revision expectedRevision{};
		OperationKind kind{ OperationKind::Place };
		ObjectId objectId{};
		FormKey form;
		FormKey space;
		Transform transform;

		[[nodiscard]] bool operator==(const Request&) const noexcept = default;
	};

	struct ObjectState
	{
		ObjectId objectId{};
		BuildZoneId buildZoneId{};
		FormKey form;
		FormKey space;
		Transform transform;
	};

	struct StoredTombstone
	{
		ObjectState state;
		std::string profileId;
	};

	struct Result
	{
		ApplyStatus status{ ApplyStatus::InvalidRequest };
		Revision revision{};
		ObjectId objectId{};
		bool replayed{};
		std::optional<ObjectState> object;
	};

	class SettlementState
	{
	public:
		explicit SettlementState(BuildZonePolicy a_policy, ObjectId a_firstObjectId = 1) noexcept :
			m_policy(std::move(a_policy)),
			m_policyValid(ValidPolicy(m_policy)),
			m_nextObjectId(a_firstObjectId == 0 ? 1 : a_firstObjectId)
		{}

		[[nodiscard]] bool PolicyValid() const noexcept { return m_policyValid; }
		[[nodiscard]] BuildZoneId Id() const noexcept { return m_policy.buildZoneId; }
		[[nodiscard]] const BuildZonePolicy& Policy() const noexcept { return m_policy; }
		[[nodiscard]] Revision CurrentRevision() const noexcept { return m_revision; }
		[[nodiscard]] std::size_t ObjectCount() const noexcept { return m_objects.size(); }
		[[nodiscard]] bool WithinRadius(double a_x, double a_y, double a_z) const noexcept
		{
			const double dx = a_x - m_policy.centerX;
			const double dy = a_y - m_policy.centerY;
			const double dz = a_z - m_policy.centerZ;
			const double radius = m_policy.radius;
			return dx * dx + dy * dy + dz * dz <= radius * radius;
		}

		[[nodiscard]] const ObjectState* Find(ObjectId a_objectId) const noexcept
		{
			const auto it = m_objects.find(a_objectId);
			return it == m_objects.end() ? nullptr : &it->second;
		}

		[[nodiscard]] std::vector<ObjectState> Objects() const
		{
			return Collect(m_objects);
		}

		[[nodiscard]] std::vector<ObjectState> Tombstones() const
		{
			return Collect(m_tombstones);
		}

		[[nodiscard]] ObjectId NextObjectId() const noexcept { return m_nextObjectId; }

		[[nodiscard]] std::vector<StoredTombstone> StoredTombstones() const
		{
			std::vector<StoredTombstone> result;
			result.reserve(m_tombstones.size());
			for (const auto& entry : m_tombstones) {
				const auto owner = m_tombstoneProfiles.find(entry.first);
				result.push_back({ entry.second, owner == m_tombstoneProfiles.end() ? std::string{} : owner->second });
			}
			return result;
		}

		[[nodiscard]] bool Restore(
			Revision a_revision,
			ObjectId a_nextObjectId,
			const std::vector<ObjectState>& a_objects,
			const std::vector<StoredTombstone>& a_tombstones)
		{
			if (!m_policyValid || a_nextObjectId == 0 ||
				a_objects.size() > m_policy.maxObjects ||
				a_tombstones.size() > MaximumWorldTombstones) {
				return false;
			}
			std::map<ObjectId, ObjectState> objects;
			std::map<ObjectId, ObjectState> tombstones;
			std::map<ObjectId, std::string> owners;
			std::map<std::string, std::size_t> perProfile;
			const auto usable = [&](const ObjectState& a_state) {
				return a_state.objectId != 0 && a_state.objectId < a_nextObjectId &&
				       a_state.buildZoneId == m_policy.buildZoneId &&
				       !objects.contains(a_state.objectId) && !tombstones.contains(a_state.objectId) &&
				       Valid(a_state.form) && Valid(a_state.space) && Valid(a_state.transform);
			};
			for (const auto& object : a_objects) {
				if (!usable(object)) return false;
				objects.emplace(object.objectId, object);
			}
			for (const auto& tombstone : a_tombstones) {
				if (!usable(tombstone.state) || tombstone.profileId.empty() ||
					LocalFormIdOutOfRange(tombstone.state.form.localFormId) ||
					++perProfile[tombstone.profileId] > MaximumTombstonesPerProfile) {
					return false;
				}
				tombstones.emplace(tombstone.state.objectId, tombstone.state);
				owners.emplace(tombstone.state.objectId, tombstone.profileId);
			}
			m_revision = a_revision;
			m_nextObjectId = a_nextObjectId;
			m_objects = std::move(objects);
			m_tombstones = std::move(tombstones);
			m_tombstoneProfiles = std::move(owners);
			m_results.clear();
			m_resultOrder.clear();
			return true;
		}

		[[nodiscard]] Result Apply(
			const Request& a_request,
			const WorkshopAuthority::CanBuildAt& a_canBuildAt = WorkshopAuthority::AllowAll)
		{
			if (!m_policyValid ||
				a_request.playerId == 0 ||
				a_request.profileId.empty() ||
				a_request.buildZoneId != m_policy.buildZoneId ||
				a_request.operationId == 0) {
				return { ApplyStatus::InvalidRequest, m_revision, 0, false, std::nullopt };
			}

			const OperationKey key{ a_request.profileId, a_request.operationId };
			if (const auto it = m_results.find(key); it != m_results.end()) {
				Request stored = it->second.request;
				stored.playerId = a_request.playerId;
				if (!(stored == a_request)) {
					return { ApplyStatus::InvalidRequest, m_revision, a_request.objectId, false, std::nullopt };
				}
				auto replay = it->second.result;
				replay.replayed = true;
				return replay;
			}

			Result result{ ApplyStatus::InvalidRequest, m_revision, a_request.objectId, false, std::nullopt };
			if (a_request.expectedRevision != m_revision) {
				result.status = ApplyStatus::StaleRevision;
				return Remember(key, a_request, std::move(result));
			}
			if (m_revision == std::numeric_limits<Revision>::max()) {
				result.status = ApplyStatus::RevisionExhausted;
				return Remember(key, a_request, std::move(result));
			}
			if (!a_canBuildAt(a_request.profileId, m_policy.buildZoneId)) {
				result.status = ApplyStatus::NotAuthorized;
				return Remember(key, a_request, std::move(result));
			}

			switch (a_request.kind) {
			case OperationKind::Place:
				result = Place(a_request);
				break;
			case OperationKind::Move:
				result = Move(a_request);
				break;
			case OperationKind::Scrap:
				result = Scrap(a_request);
				break;
			case OperationKind::ScrapWorld:
				result = ScrapWorld(a_request);
				break;
			default:
				break;
			}
			return Remember(key, a_request, std::move(result));
		}

	private:
		using OperationKey = std::pair<std::string, OperationId>;

		struct RememberedOperation
		{
			Request request;
			Result result;
		};

		[[nodiscard]] static bool Valid(const FormKey& a_form) noexcept
		{
			return !a_form.plugin.empty() &&
			       a_form.plugin.size() <= MaximumPluginNameBytes &&
			       a_form.localFormId != 0;
		}

		[[nodiscard]] static bool LocalFormIdOutOfRange(std::uint32_t a_localFormId) noexcept
		{
			return a_localFormId > MaximumLocalFormId;
		}

		[[nodiscard]] static bool Valid(const Transform& a_transform) noexcept
		{
			return std::isfinite(a_transform.x) &&
			       std::isfinite(a_transform.y) &&
			       std::isfinite(a_transform.z) &&
			       std::isfinite(a_transform.pitch) &&
			       std::isfinite(a_transform.yaw) &&
			       std::isfinite(a_transform.roll) &&
			       std::isfinite(a_transform.scale) &&
			       a_transform.scale > 0.0F;
		}

		[[nodiscard]] static bool ValidPolicy(const BuildZonePolicy& a_policy) noexcept
		{
			return a_policy.buildZoneId != 0 &&
			       Valid(a_policy.space) &&
			       std::isfinite(a_policy.centerX) &&
			       std::isfinite(a_policy.centerY) &&
			       std::isfinite(a_policy.centerZ) &&
			       std::isfinite(a_policy.radius) &&
			       std::isfinite(a_policy.maxScale) &&
			       std::isfinite(a_policy.workbenchX) &&
			       std::isfinite(a_policy.workbenchY) &&
			       std::isfinite(a_policy.workbenchZ) &&
			       std::isfinite(a_policy.captureRadius) &&
			       a_policy.captureRadius >= 0.0F &&
			       a_policy.radius > 0.0F &&
			       a_policy.maxScale > 0.0F &&
			       a_policy.maxObjects > 0;
		}

		[[nodiscard]] static std::vector<ObjectState> Collect(const std::map<ObjectId, ObjectState>& a_states)
		{
			std::vector<ObjectState> result;
			result.reserve(a_states.size());
			for (const auto& entry : a_states) {
				result.push_back(entry.second);
			}
			return result;
		}

		[[nodiscard]] bool InBuildZone(const Request& a_request) const noexcept
		{
			return a_request.transform.scale <= m_policy.maxScale &&
			       a_request.space == m_policy.space &&
			       WithinRadius(a_request.transform.x, a_request.transform.y, a_request.transform.z);
		}

		[[nodiscard]] Result Place(const Request& a_request)
		{
			if (!Valid(a_request.form) || !Valid(a_request.space) || !Valid(a_request.transform)) {
				return { ApplyStatus::InvalidRequest, m_revision, 0, false, std::nullopt };
			}
			if (!InBuildZone(a_request)) {
				return { ApplyStatus::OutsideBuildZone, m_revision, 0, false, std::nullopt };
			}
			if (m_objects.size() >= m_policy.maxObjects) {
				return { ApplyStatus::BudgetExceeded, m_revision, 0, false, std::nullopt };
			}
			if (m_nextObjectId == 0) {
				return { ApplyStatus::ObjectIdExhausted, m_revision, 0, false, std::nullopt };
			}

			const auto objectId = m_nextObjectId++;
			ObjectState object{
				objectId,
				m_policy.buildZoneId,
				a_request.form,
				a_request.space,
				a_request.transform
			};
			m_objects.emplace(objectId, object);
			return Commit(objectId, object);
		}

		[[nodiscard]] Result Move(const Request& a_request)
		{
			if (a_request.objectId == 0 || !Valid(a_request.space) || !Valid(a_request.transform)) {
				return { ApplyStatus::InvalidRequest, m_revision, a_request.objectId, false, std::nullopt };
			}
			const auto it = m_objects.find(a_request.objectId);
			if (it == m_objects.end()) {
				return { ApplyStatus::ObjectNotFound, m_revision, a_request.objectId, false, std::nullopt };
			}
			if (!InBuildZone(a_request)) {
				return { ApplyStatus::OutsideBuildZone, m_revision, a_request.objectId, false, std::nullopt };
			}

			it->second.space = a_request.space;
			it->second.transform = a_request.transform;
			return Commit(a_request.objectId, it->second);
		}

		[[nodiscard]] Result Scrap(const Request& a_request)
		{
			if (a_request.objectId == 0) {
				return { ApplyStatus::InvalidRequest, m_revision, 0, false, std::nullopt };
			}
			const auto it = m_objects.find(a_request.objectId);
			if (it == m_objects.end()) {
				return { ApplyStatus::ObjectNotFound, m_revision, a_request.objectId, false, std::nullopt };
			}
			m_objects.erase(it);
			++m_revision;
			return { ApplyStatus::Committed, m_revision, a_request.objectId, false, std::nullopt };
		}

		[[nodiscard]] Result ScrapWorld(const Request& a_request)
		{
			if (!Valid(a_request.form) || LocalFormIdOutOfRange(a_request.form.localFormId) ||
				!Valid(a_request.space) || !Valid(a_request.transform)) {
				return { ApplyStatus::InvalidRequest, m_revision, 0, false, std::nullopt };
			}
			if (!(a_request.space == m_policy.space) ||
				!WithinRadius(a_request.transform.x, a_request.transform.y, a_request.transform.z)) {
				return { ApplyStatus::OutsideBuildZone, m_revision, 0, false, std::nullopt };
			}
			std::size_t ownedByProfile = 0;
			for (const auto& entry : m_tombstones) {
				if (entry.second.form == a_request.form) {
					return { ApplyStatus::AlreadyRemoved, m_revision, 0, false, std::nullopt };
				}
				const auto owner = m_tombstoneProfiles.find(entry.first);
				if (owner != m_tombstoneProfiles.end() && owner->second == a_request.profileId) {
					++ownedByProfile;
				}
			}
			if (m_tombstones.size() >= MaximumWorldTombstones || ownedByProfile >= MaximumTombstonesPerProfile) {
				return { ApplyStatus::BudgetExceeded, m_revision, 0, false, std::nullopt };
			}
			if (m_nextObjectId == 0) {
				return { ApplyStatus::ObjectIdExhausted, m_revision, 0, false, std::nullopt };
			}

			const auto objectId = m_nextObjectId++;
			ObjectState tombstone{
				objectId,
				m_policy.buildZoneId,
				a_request.form,
				a_request.space,
				a_request.transform
			};
			m_tombstones.emplace(objectId, tombstone);
			m_tombstoneProfiles.emplace(objectId, a_request.profileId);
			return Commit(objectId, tombstone);
		}

		[[nodiscard]] Result Commit(ObjectId a_objectId, const ObjectState& a_object)
		{
			++m_revision;
			return { ApplyStatus::Committed, m_revision, a_objectId, false, a_object };
		}

		[[nodiscard]] Result Remember(OperationKey a_key, const Request& a_request, Result a_result)
		{
			if (a_result.status != ApplyStatus::Committed) {
				return a_result;
			}
			if (m_resultOrder.size() == MaximumRememberedOperations) {
				m_results.erase(m_resultOrder.front());
				m_resultOrder.pop_front();
			}
			m_resultOrder.push_back(a_key);
			m_results.emplace(a_key, RememberedOperation{ a_request, a_result });
			return a_result;
		}

		BuildZonePolicy m_policy;
		bool m_policyValid{};
		Revision m_revision{};
		ObjectId m_nextObjectId{ 1 };
		std::map<ObjectId, ObjectState> m_objects;
		std::map<ObjectId, ObjectState> m_tombstones;
		std::map<ObjectId, std::string> m_tombstoneProfiles;
		std::map<OperationKey, RememberedOperation> m_results;
		std::deque<OperationKey> m_resultOrder;
	};
}
