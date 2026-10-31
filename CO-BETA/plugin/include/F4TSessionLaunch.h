#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace F4SE
{
	class MessagingInterface;
}

namespace F4T::SessionLaunch
{
	struct JoinRequest
	{
		std::string   id;
		std::string   name;
		std::string   host;
		std::uint16_t port{ 7777 };
		std::optional<std::string> serverPassword;
	};

	enum class LaunchOutcome
	{
		Failed,
		Complete,
		Pending
	};

	using JoinCompletion = std::function<void(bool, std::string_view)>;

	LaunchOutcome LaunchValidatedSession(
		const JoinRequest& a_request,
		std::string& a_error,
		JoinCompletion a_onComplete = {});
	bool CancelPendingJoin(std::string_view a_reason);
	bool CanPublishGameplayTransform();
	bool CanApplyServerCorrection();
	void OnF4SEMessage(F4SE::MessagingInterface::Message* a_msg);
	void Update();
	void TryCompletePendingProfileBind(const std::optional<std::string>& a_savedName);
}
