#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <vector>

#include "F4TSnapshotDelta.h"

namespace F4T::Replication
{
	using PeerId = std::uint32_t;

	class PeerEntitySet
	{
	public:
		bool AddPeer(PeerId a_peer)
		{
			return m_peers.emplace(a_peer, std::set<EntityId>{}).second;
		}

		bool RemovePeer(PeerId a_peer)
		{
			return m_peers.erase(a_peer) != 0;
		}

		[[nodiscard]] bool HasPeer(PeerId a_peer) const
		{
			return m_peers.find(a_peer) != m_peers.end();
		}

		bool RegisterEntity(EntityId a_entity)
		{
			return m_entities.insert(a_entity).second;
		}

		bool DespawnEntity(EntityId a_entity)
		{
			if (m_entities.erase(a_entity) == 0) {
				return false;
			}
			for (auto& [peer, visible] : m_peers) {
				visible.erase(a_entity);
			}
			return true;
		}

		[[nodiscard]] bool IsRegistered(EntityId a_entity) const
		{
			return m_entities.find(a_entity) != m_entities.end();
		}

		bool Show(PeerId a_peer, EntityId a_entity)
		{
			const auto peer = m_peers.find(a_peer);
			if (peer == m_peers.end() || !IsRegistered(a_entity)) {
				return false;
			}
			return peer->second.insert(a_entity).second;
		}

		bool Hide(PeerId a_peer, EntityId a_entity)
		{
			const auto peer = m_peers.find(a_peer);
			if (peer == m_peers.end()) {
				return false;
			}
			return peer->second.erase(a_entity) != 0;
		}

		// Atomic: a duplicate or unregistered id in a_desired rejects the whole call, prior set unchanged.
		bool SetVisibleSet(PeerId a_peer, std::span<const EntityId> a_desired)
		{
			const auto peer = m_peers.find(a_peer);
			if (peer == m_peers.end()) {
				return false;
			}
			std::set<EntityId> next;
			for (const EntityId entity : a_desired) {
				if (!IsRegistered(entity)) {
					return false;
				}
				if (!next.insert(entity).second) {
					return false;
				}
			}
			peer->second = std::move(next);
			return true;
		}

		[[nodiscard]] bool IsVisible(PeerId a_peer, EntityId a_entity) const
		{
			const auto peer = m_peers.find(a_peer);
			return peer != m_peers.end() && peer->second.find(a_entity) != peer->second.end();
		}

		[[nodiscard]] std::vector<EntityId> VisibleEntities(PeerId a_peer) const
		{
			const auto peer = m_peers.find(a_peer);
			if (peer == m_peers.end()) {
				return {};
			}
			return std::vector<EntityId>(peer->second.begin(), peer->second.end());
		}

		template <class Fn>
		void ForEachVisibleEntity(PeerId a_peer, Fn a_fn) const
		{
			const auto peer = m_peers.find(a_peer);
			if (peer == m_peers.end()) {
				return;
			}
			for (const EntityId entity : peer->second) {
				a_fn(entity);
			}
		}

		[[nodiscard]] std::vector<PeerId> Peers() const
		{
			std::vector<PeerId> peers;
			peers.reserve(m_peers.size());
			for (const auto& [peer, visible] : m_peers) {
				peers.push_back(peer);
			}
			return peers;
		}

		[[nodiscard]] std::vector<EntityId> Entities() const
		{
			return std::vector<EntityId>(m_entities.begin(), m_entities.end());
		}

		[[nodiscard]] std::size_t VisibleCount(PeerId a_peer) const
		{
			const auto peer = m_peers.find(a_peer);
			return peer == m_peers.end() ? std::size_t{ 0 } : peer->second.size();
		}

		[[nodiscard]] std::size_t PeerCount() const noexcept { return m_peers.size(); }
		[[nodiscard]] std::size_t EntityCount() const noexcept { return m_entities.size(); }

	private:
		std::set<EntityId> m_entities;
		std::map<PeerId, std::set<EntityId>> m_peers;
	};

	[[nodiscard]] inline SnapshotState SelectPeerSnapshot(
		const PeerEntitySet& a_sets,
		PeerId a_peer,
		const SnapshotState& a_state)
	{
		SnapshotState selected;
		for (const auto& [key, bytes] : a_state) {
			if (a_sets.IsVisible(a_peer, key.entityId)) {
				selected.insert({ key, bytes });
			}
		}
		return selected;
	}
}
