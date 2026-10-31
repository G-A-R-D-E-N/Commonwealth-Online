#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::Downed
{
	struct RespawnFriend
	{
		std::uint32_t playerId{};
		std::string name;
	};

	class IDownedUiHost
	{
	public:
		virtual ~IDownedUiHost() = default;

		virtual void ShowDownedOverlay(std::int32_t a_remainingMs, bool a_rescuerNearby) = 0;
		virtual void HideDownedOverlay() = 0;
		virtual void ShowRespawnMenu(
			const std::vector<std::string>& a_points,
			const std::vector<RespawnFriend>& a_friends) = 0;
		virtual void HideRespawnMenu() = 0;
		virtual void ShowReviveChannel(std::string_view a_targetName, double a_progress) = 0;
		virtual void HideReviveChannel() = 0;
	};

	void SetDownedUiHost(IDownedUiHost* a_host);
	void AttachDownedUi();
	[[nodiscard]] bool HasDownedUiHost();
	void OnRespawnChosen(std::string_view a_point, std::optional<std::uint32_t> a_friendId);
}
