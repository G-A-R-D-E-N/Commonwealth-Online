#include "pch.h"

#include "F4TDownedUi.h"

#include "CommonwealthOnlineUI_API.h"

#include <charconv>
#include <cstring>
#include <nlohmann/json.hpp>
#include <string>

namespace F4T::Downed
{
	namespace
	{
		COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI4* g_uiDowned{ nullptr };

		void OnUiRespawnChosen(const char* a_point, const char* a_friendId)
		{
			if (!a_point) {
				return;
			}
			std::optional<std::uint32_t> friendId;
			if (a_friendId && *a_friendId) {
				std::uint32_t parsed{};
				const auto end = a_friendId + std::strlen(a_friendId);
				if (const auto [ptr, ec] = std::from_chars(a_friendId, end, parsed); ec == std::errc{} && ptr == end) {
					friendId = parsed;
				}
			}
			OnRespawnChosen(a_point, friendId);
		}

		const char* PointLabel(const std::string& a_point)
		{
			if (a_point == "checkpoint") {
				return "Last save";
			}
			if (a_point == "lastbed") {
				return "Last bed";
			}
			if (a_point == "friend") {
				return "Friend";
			}
			return "Sanctuary";
		}

		class PrismaDownedUiHost final : public IDownedUiHost
		{
		public:
			void ShowDownedOverlay(std::int32_t a_remainingMs, bool a_rescuerNearby) override
			{
				nlohmann::json state{ { "phase", "downed" }, { "remainingMs", a_remainingMs } };
				if (a_rescuerNearby) {
					state["rescuer"] = { { "name", "Ally" }, { "progress", 0.0 } };
				}
				Push(state, Shown::kOverlay);
			}

			void HideDownedOverlay() override { Hide(Shown::kOverlay); }

			void ShowRespawnMenu(const std::vector<std::string>& a_points, const std::vector<RespawnFriend>& a_friends) override
			{
				nlohmann::json state{ { "phase", "dead" }, { "points", nlohmann::json::array() }, { "friends", nlohmann::json::array() } };
				for (const auto& point : a_points) {
					state["points"].push_back({ { "id", point }, { "label", PointLabel(point) }, { "available", true } });
				}
				for (const auto& entry : a_friends) {
					state["friends"].push_back({ { "id", std::to_string(entry.playerId) }, { "name", entry.name } });
				}
				Push(state, Shown::kRespawn);
			}

			void HideRespawnMenu() override { Hide(Shown::kRespawn); }

			void ShowReviveChannel(std::string_view a_targetName, double a_progress) override
			{
				nlohmann::json state{
					{ "phase", "hidden" },
					{ "reviving", { { "name", a_targetName }, { "progress", a_progress } } }
				};
				Push(state, Shown::kChannel);
			}

			void HideReviveChannel() override { Hide(Shown::kChannel); }

		private:
			enum class Shown
			{
				kNone,
				kOverlay,
				kRespawn,
				kChannel
			};

			void Push(const nlohmann::json& a_state, Shown a_shown)
			{
				_shown = a_shown;
				g_uiDowned->PushDownedState(a_state.dump().c_str());
			}

			void Hide(Shown a_shown)
			{
				if (_shown == a_shown) {
					Push(nlohmann::json{ { "phase", "hidden" } }, Shown::kNone);
				}
			}

			Shown _shown{ Shown::kNone };
		};

		PrismaDownedUiHost g_host;
	}

	void AttachDownedUi()
	{
		g_uiDowned = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI4>();
		if (!g_uiDowned || !g_uiDowned->PushDownedState || !g_uiDowned->SetRespawnChosenCallback) {
			REX::WARN("Downed: CommonwealthOnlineUI downed API not found; respawn falls back to a direct request.");
			return;
		}
		g_uiDowned->SetRespawnChosenCallback(OnUiRespawnChosen);
		SetDownedUiHost(&g_host);
	}
}
