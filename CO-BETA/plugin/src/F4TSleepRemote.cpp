#include "pch.h"

#include "F4TSleepRemote.h"
#include "F4TNetworking.h"
#include "F4TQuestFormKey.h"
#include "F4TWorkshopReplicationClient.h"

#include "RE/T/TESFurniture.h"

#include <mutex>

namespace F4T::SleepRemote
{
	namespace
	{
		std::mutex g_mutex;
		std::uint64_t g_generation{};
		SleepBedIdentity::RemoteStates g_states;

		bool SyncGeneration()
		{
			const auto generation = Networking::GetConnectionGeneration();
			const bool connected = Networking::IsConnectedToServer();
			if (g_generation != generation || !connected) {
				g_states.Clear();
				g_generation = generation;
			}
			return connected;
		}
	}

	void HandlePacket(const nlohmann::json& a_packet)
	{
		const auto state = SleepBedIdentity::ParseRelayedSleep(a_packet);
		if (!state) return;
		std::scoped_lock lock(g_mutex);
		if (SyncGeneration()) g_states.Apply(*state);
	}

	void Forget(std::uint32_t a_playerId)
	{
		std::scoped_lock lock(g_mutex);
		g_states.Erase(a_playerId);
	}

	void Clear()
	{
		std::scoped_lock lock(g_mutex);
		g_states.Clear();
	}

	SleepLookup Find(std::uint32_t a_playerId)
	{
		std::optional<SleepBedIdentity::RelayedSleep> state;
		std::optional<std::uint64_t> revision;
		{
			std::scoped_lock lock(g_mutex);
			state = SyncGeneration() ? g_states.Find(a_playerId) : std::nullopt;
			revision = g_states.Revision(a_playerId);
		}
		if (!state || !state->bed) return {};
		RE::ObjectRefHandle handle;
		if (const auto* form = std::get_if<QuestProtocol::FormKey>(&*state->bed)) {
			const auto formId = QuestFormKey::ToFormId(QuestProtocol::FormatFormKey(*form), QuestFormKey::GetLoadOrder());
			if (formId) {
				if (auto* ref = RE::TESForm::GetFormByID<RE::TESObjectREFR>(*formId)) handle = ref->GetHandle();
			}
		} else {
			const auto& object = std::get<SleepBedIdentity::WorkshopObject>(*state->bed);
			handle = WorkshopReplicationClient::FindPlacedObjectHandle(object.zone, object.object);
		}
		const auto ref = handle.get();
		auto* base = ref && !ref->IsDeleted() && !ref->IsDisabled() && ref->Get3D() ? ref->GetObjectReference() : nullptr;
		auto* furniture = base && base->GetFormType() == RE::ENUM_FORM_ID::kFURN ? base->As<RE::TESFurniture>() : nullptr;
		const bool ready = furniture && state->marker < furniture->markersArray.size() &&
			SleepBedIdentity::IsEnabledMarker(furniture->furnFlags, state->marker);
		{
			std::scoped_lock lock(g_mutex);
			if (!SyncGeneration()) return {};
			if (revision != g_states.Revision(a_playerId)) {
				return g_states.Find(a_playerId) ? SleepLookup{ LookupStatus::Pending } : SleepLookup{};
			}
		}
		return ready ? SleepLookup{ LookupStatus::Ready, handle, state->marker } : SleepLookup{ LookupStatus::Pending };
	}
}
