#pragma once

#include "AdminCommandDispatcher.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CommonwealthOnline::Server
{
	enum class AdminChange
	{
		Changed,
		Unchanged,
		Fixed,
		Invalid
	};

	class AdminRoster
	{
	public:
		static constexpr std::size_t kMaxGranted = 64;

		AdminRoster(std::vector<std::string> a_configured, std::filesystem::path a_path);

		[[nodiscard]] bool Contains(std::string_view a_profileId) const;
		[[nodiscard]] bool ContainsStored(std::string_view a_profileId) const;
		[[nodiscard]] AdminChange Grant(std::string_view a_profileId);
		[[nodiscard]] AdminChange Revoke(std::string_view a_profileId);
		void SetHost(std::string_view a_profileId);
		void NoteHost(bool a_isHost, std::string_view a_profileId);
		[[nodiscard]] nlohmann::json List() const;
		[[nodiscard]] std::optional<std::string> TakeLoadWarning();

	private:
		void Save() const;

		mutable std::mutex _mutex;
		std::vector<std::string> _configured;
		std::vector<std::string> _granted;
		std::string _host;
		std::filesystem::path _path;
		std::optional<std::string> _loadWarning;
	};

	using ProfileOfPlayer = std::function<std::optional<std::string>(std::uint32_t)>;
	using AdminAnnounce = std::function<void(std::string_view)>;

	void InstallAdminRosterHooks(AdminCommandHooks& a_hooks, AdminRoster& a_roster, ProfileOfPlayer a_profileOfPlayer, AdminAnnounce a_announce);
}
