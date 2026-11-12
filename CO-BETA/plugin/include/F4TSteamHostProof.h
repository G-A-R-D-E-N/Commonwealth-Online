#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace F4T::Steam
{
	class SteamHostProof
	{
	public:
		void Set(std::uint16_t a_port, std::optional<std::string> a_proof)
		{
			std::scoped_lock lock(_mutex);
			_port = a_port;
			_proof = std::move(a_proof);
		}

		[[nodiscard]] std::optional<std::string> ProofFor(std::uint16_t a_port) const
		{
			std::scoped_lock lock(_mutex);
			return a_port == _port ? _proof : std::nullopt;
		}

		void Clear()
		{
			std::scoped_lock lock(_mutex);
			_port = 0;
			_proof.reset();
		}

	private:
		mutable std::mutex _mutex;
		std::uint16_t _port{};
		std::optional<std::string> _proof;
	};

	[[nodiscard]] inline SteamHostProof& HostProof()
	{
		static SteamHostProof proof;
		return proof;
	}
}
