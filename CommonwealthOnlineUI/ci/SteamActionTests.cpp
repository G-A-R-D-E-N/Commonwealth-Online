#include "F4TSteamActions.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
	using F4T::Steam::HostStartRequest;
	using F4T::Steam::JoinRequest;
	using F4T::Steam::ParseHostStart;
	using F4T::Steam::ParseJoin;
	using F4T::Steam::RouteSteamAction;

	struct Recorder
	{
		int           hostStarts{ 0 };
		int           hostStops{ 0 };
		int           hostEnters{ 0 };
		int           joins{ 0 };
		int           invites{ 0 };
		std::string   name;
		std::string   password;
		std::uint32_t maxPlayers{ 0 };
		std::string   friendCode;
		std::string   joinPassword;
		std::vector<nlohmann::json> events;
	};

	Recorder* g_recorder = nullptr;

	void OnHostStart(const char* a_name, const char* a_password, std::uint32_t a_maxPlayers)
	{
		++g_recorder->hostStarts;
		g_recorder->name = a_name ? a_name : "";
		g_recorder->password = a_password ? a_password : "";
		g_recorder->maxPlayers = a_maxPlayers;
	}

	void OnHostStop()
	{
		++g_recorder->hostStops;
	}

	void OnHostEnter()
	{
		++g_recorder->hostEnters;
	}

	void OnJoin(const char* a_friendCode, const char* a_password)
	{
		++g_recorder->joins;
		g_recorder->friendCode = a_friendCode ? a_friendCode : "";
		g_recorder->joinPassword = a_password ? a_password : "";
	}

	void OnInvite()
	{
		++g_recorder->invites;
	}

	void RecordEvent(void*, const nlohmann::json& a_event)
	{
		g_recorder->events.push_back(a_event);
	}

	COMMONWEALTH_ONLINE_UI_API::SteamCallbacks LiveCallbacks()
	{
		COMMONWEALTH_ONLINE_UI_API::SteamCallbacks callbacks;
		callbacks.HostStart = OnHostStart;
		callbacks.HostStop = OnHostStop;
		callbacks.HostEnter = OnHostEnter;
		callbacks.Join = OnJoin;
		callbacks.Invite = OnInvite;
		return callbacks;
	}

	struct Fixture
	{
		Recorder recorder;

		Fixture() { g_recorder = &recorder; }
		~Fixture() { g_recorder = nullptr; }

		bool Route(const nlohmann::json& a_action)
		{
			return RouteSteamAction(a_action, LiveCallbacks(), &RecordEvent, nullptr);
		}

		bool RouteWithoutCallbacks(const nlohmann::json& a_action)
		{
			return RouteSteamAction(a_action, COMMONWEALTH_ONLINE_UI_API::SteamCallbacks{}, &RecordEvent, nullptr);
		}
	};

	bool HasSingleErrorEvent(const Fixture& a_fixture, const char* a_type)
	{
		const auto& events = a_fixture.recorder.events;
		return events.size() == 1 &&
		       events.front().value("type", "") == a_type &&
		       !events.front().value("reason", "").empty();
	}

	bool RanNoCallback(const Fixture& a_fixture)
	{
		return a_fixture.recorder.hostStarts == 0 &&
		       a_fixture.recorder.hostStops == 0 &&
		       a_fixture.recorder.hostEnters == 0 &&
		       a_fixture.recorder.joins == 0 &&
		       a_fixture.recorder.invites == 0;
	}

	nlohmann::json HostStart(const std::string& a_name, const std::string& a_password, const nlohmann::json& a_maxPlayers)
	{
		return nlohmann::json{
			{ "action", "steamHostStart" },
			{ "name", a_name },
			{ "password", a_password },
			{ "maxPlayers", a_maxPlayers }
		};
	}

	nlohmann::json HostStartWithoutMaxPlayers(const std::string& a_name)
	{
		return nlohmann::json{
			{ "action", "steamHostStart" },
			{ "name", a_name }
		};
	}

	nlohmann::json Join(const std::string& a_address, const std::string& a_password)
	{
		return nlohmann::json{
			{ "action", "steamJoin" },
			{ "address", a_address },
			{ "password", a_password }
		};
	}

	void HostStartAcceptsValidInput()
	{
		Fixture fixture;
		assert(fixture.Route(HostStart("Nomad's Game", "hunter2", 8)));
		assert(fixture.recorder.hostStarts == 1);
		assert(fixture.recorder.name == "Nomad's Game");
		assert(fixture.recorder.password == "hunter2");
		assert(fixture.recorder.maxPlayers == 8);
		assert(fixture.recorder.events.empty());
	}

	void HostStartClampsMaxPlayers()
	{
		{
			Fixture fixture;
			assert(fixture.Route(HostStart("Game", "", 0)));
			assert(fixture.recorder.maxPlayers == 2);
			assert(fixture.recorder.events.empty());
		}
		{
			Fixture fixture;
			assert(fixture.Route(HostStart("Game", "", 1)));
			assert(fixture.recorder.maxPlayers == 2);
		}
		{
			Fixture fixture;
			assert(fixture.Route(HostStart("Game", "", 2)));
			assert(fixture.recorder.maxPlayers == 2);
		}
		{
			Fixture fixture;
			assert(fixture.Route(HostStart("Game", "", 15)));
			assert(fixture.recorder.maxPlayers == 15);
		}
		{
			Fixture fixture;
			assert(fixture.Route(HostStart("Game", "", 16)));
			assert(fixture.recorder.maxPlayers == 16);
		}
		{
			Fixture fixture;
			assert(fixture.Route(HostStart("Game", "", 999)));
			assert(fixture.recorder.maxPlayers == 16);
		}
		{
			Fixture fixture;
			assert(fixture.Route(HostStart("Game", "", -5)));
			assert(fixture.recorder.maxPlayers == 2);
		}
	}

	void HostStartRejectsBadName()
	{
		{
			Fixture fixture;
			assert(fixture.Route(HostStart("", "", 4)));
			assert(RanNoCallback(fixture));
			assert(HasSingleErrorEvent(fixture, "steamHostFailed"));
		}
		{
			Fixture fixture;
			assert(fixture.Route(HostStart(std::string(64, 'n'), "", 4)));
			assert(fixture.recorder.hostStarts == 1);
		}
		{
			Fixture fixture;
			assert(fixture.Route(HostStart(std::string(65, 'n'), "", 4)));
			assert(RanNoCallback(fixture));
			assert(HasSingleErrorEvent(fixture, "steamHostFailed"));
		}
		{
			Fixture fixture;
			assert(fixture.Route(HostStartWithoutMaxPlayers("Game")));
			assert(RanNoCallback(fixture));
			assert(HasSingleErrorEvent(fixture, "steamHostFailed"));
		}
		{
			Fixture fixture;
			assert(fixture.Route(nlohmann::json{ { "action", "steamHostStart" }, { "name", "" }, { "maxPlayers", 4 } }));
			assert(RanNoCallback(fixture));
			assert(HasSingleErrorEvent(fixture, "steamHostFailed"));
		}
	}

	void HostStartRejectsBadPassword()
	{
		{
			Fixture fixture;
			assert(fixture.Route(HostStart("Game", std::string(128, 'p'), 4)));
			assert(fixture.recorder.hostStarts == 1);
			assert(fixture.recorder.password.size() == 128);
		}
		{
			Fixture fixture;
			assert(fixture.Route(HostStart("Game", std::string(129, 'p'), 4)));
			assert(RanNoCallback(fixture));
			assert(HasSingleErrorEvent(fixture, "steamHostFailed"));
		}
	}

	void HostStartRejectsBadMaxPlayers()
	{
		{
			Fixture fixture;
			assert(fixture.Route(HostStart("Game", "", "8")));
			assert(RanNoCallback(fixture));
			assert(HasSingleErrorEvent(fixture, "steamHostFailed"));
		}
		{
			Fixture fixture;
			assert(fixture.Route(HostStart("Game", "", 4.5)));
			assert(RanNoCallback(fixture));
			assert(HasSingleErrorEvent(fixture, "steamHostFailed"));
		}
		{
			Fixture fixture;
			assert(fixture.Route(HostStart("Game", "", nullptr)));
			assert(RanNoCallback(fixture));
			assert(HasSingleErrorEvent(fixture, "steamHostFailed"));
		}
	}

	void HostStopCallsThrough()
	{
		Fixture fixture;
		assert(fixture.Route(nlohmann::json{ { "action", "steamHostStop" } }));
		assert(fixture.recorder.hostStops == 1);
		assert(fixture.recorder.events.empty());
	}

	void HostEnterRoutesToCallback()
	{
		{
			Fixture fixture;
			assert(fixture.Route(nlohmann::json{ { "action", "steamHostEnter" } }));
			assert(fixture.recorder.hostEnters == 1);
			assert(fixture.recorder.joins == 0);
			assert(fixture.recorder.events.empty());
		}
		{
			Fixture fixture;
			COMMONWEALTH_ONLINE_UI_API::SteamCallbacks callbacks = LiveCallbacks();
			callbacks.HostEnter = nullptr;
			assert(RouteSteamAction(nlohmann::json{ { "action", "steamHostEnter" } }, callbacks, &RecordEvent, nullptr));
			assert(RanNoCallback(fixture));
			assert(HasSingleErrorEvent(fixture, "steamHostFailed"));
			assert(fixture.recorder.events.front().value("reason", "") == F4T::Steam::kUnavailableReason);
		}
		assert(F4T::Steam::IsSteamAction("steamHostEnter"));
	}

	void JoinAcceptsValidInput()
	{
		Fixture fixture;
		assert(fixture.Route(Join("steam:76561197960265728", "")));
		assert(fixture.recorder.joins == 1);
		assert(fixture.recorder.friendCode == "76561197960265728");
		assert(fixture.recorder.joinPassword.empty());
		assert(fixture.recorder.events.empty());
	}

	void JoinAcceptsBareFriendCode()
	{
		Fixture fixture;
		assert(fixture.Route(Join("76561197960265728", "")));
		assert(fixture.recorder.joins == 1);
		assert(fixture.recorder.friendCode == "76561197960265728");
	}

	void JoinRejectsBadFriendCode()
	{
		{
			Fixture fixture;
			assert(fixture.Route(nlohmann::json{ { "action", "steamJoin" }, { "password", "" } }));
			assert(RanNoCallback(fixture));
			assert(HasSingleErrorEvent(fixture, "steamJoinFailed"));
		}
		{
			Fixture fixture;
			assert(fixture.Route(Join("", "")));
			assert(RanNoCallback(fixture));
			assert(HasSingleErrorEvent(fixture, "steamJoinFailed"));
		}
		{
			Fixture fixture;
			assert(fixture.Route(Join("steam:", "")));
			assert(RanNoCallback(fixture));
			assert(HasSingleErrorEvent(fixture, "steamJoinFailed"));
		}
		{
			Fixture fixture;
			assert(fixture.Route(Join(std::string(64, '1'), "")));
			assert(fixture.recorder.joins == 1);
		}
		{
			Fixture fixture;
			assert(fixture.Route(Join(std::string(65, '1'), "")));
			assert(RanNoCallback(fixture));
			assert(HasSingleErrorEvent(fixture, "steamJoinFailed"));
		}
		{
			Fixture fixture;
			assert(fixture.Route(Join("steam:1", std::string(129, 'p'))));
			assert(RanNoCallback(fixture));
			assert(HasSingleErrorEvent(fixture, "steamJoinFailed"));
		}
	}

	void MissingCallbacksEmitUnavailable()
	{
		{
			Fixture fixture;
			assert(fixture.RouteWithoutCallbacks(HostStart("Game", "", 4)));
			assert(RanNoCallback(fixture));
			assert(HasSingleErrorEvent(fixture, "steamHostFailed"));
			assert(fixture.recorder.events.front().value("reason", "") == F4T::Steam::kUnavailableReason);
		}
		{
			Fixture fixture;
			assert(fixture.RouteWithoutCallbacks(nlohmann::json{ { "action", "steamHostStop" } }));
			assert(RanNoCallback(fixture));
			assert(HasSingleErrorEvent(fixture, "steamHostFailed"));
			assert(fixture.recorder.events.front().value("reason", "") == F4T::Steam::kUnavailableReason);
		}
		{
			Fixture fixture;
			assert(fixture.RouteWithoutCallbacks(Join("steam:1", "")));
			assert(RanNoCallback(fixture));
			assert(HasSingleErrorEvent(fixture, "steamJoinFailed"));
			assert(fixture.recorder.events.front().value("reason", "") == F4T::Steam::kUnavailableReason);
		}
		{
			Fixture fixture;
			assert(fixture.RouteWithoutCallbacks(nlohmann::json{ { "action", "steamInvite" } }));
			assert(RanNoCallback(fixture));
			assert(HasSingleErrorEvent(fixture, "steamInviteFailed"));
			assert(fixture.recorder.events.front().value("reason", "") == F4T::Steam::kUnavailableReason);
		}
	}

	void InviteCallsThrough()
	{
		Fixture fixture;
		assert(fixture.Route(nlohmann::json{ { "action", "steamInvite" } }));
		assert(fixture.recorder.invites == 1);
		assert(fixture.recorder.events.empty());
		assert(F4T::Steam::IsSteamAction("steamInvite"));
	}

	void UnknownActionsAreNotHandled()
	{
		Fixture fixture;
		assert(!fixture.Route(nlohmann::json{ { "action", "refreshServerList" } }));
		assert(!fixture.Route(nlohmann::json{ { "action", "joinServer" }, { "serverId", "steam:1" } }));
		assert(!fixture.Route(nlohmann::json{ { "action", "" } }));
		assert(!fixture.Route(nlohmann::json::array()));
		assert(!fixture.Route(nlohmann::json("steamHostStart")));
		assert(RanNoCallback(fixture));
		assert(fixture.recorder.events.empty());
	}

	void ParsersWorkStandalone()
	{
		HostStartRequest host;
		std::string error;
		assert(ParseHostStart(HostStart("Game", "pw", 4), host, error));
		assert(host.name == "Game");
		assert(host.password == "pw");
		assert(host.maxPlayers == 4);

		JoinRequest join;
		assert(ParseJoin(Join("steam:42", "pw"), join, error));
		assert(join.friendCode == "42");
		assert(join.password == "pw");
		assert(ParseJoin(Join("steam:auto", ""), join, error));
		assert(join.friendCode == "auto");
		assert(ParseJoin(Join("Nomad", ""), join, error));
		assert(join.friendCode == "Nomad");
	}
}

namespace
{
	using COMMONWEALTH_ONLINE_UI_API::InterfaceVersion;
	using COMMONWEALTH_ONLINE_UI_API::InterfaceVersionMap;
	using COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI7;
	using COMMONWEALTH_ONLINE_UI_API::SteamCallbacks;

	static_assert(static_cast<std::uint8_t>(InterfaceVersion::V7) == 7);
	static_assert(InterfaceVersionMap<IVCommonwealthOnlineUI7>::version == InterfaceVersion::V7);

	void ContractSetSteamCallbacks(const SteamCallbacks*) {}
	void ContractPushSteamEvent(const char*) {}

	[[maybe_unused]] const IVCommonwealthOnlineUI7 kContractV7{ ContractSetSteamCallbacks, ContractPushSteamEvent };

	void InterfaceContract()
	{
		assert(kContractV7.SetSteamCallbacks == &ContractSetSteamCallbacks);
		assert(kContractV7.PushSteamEvent == &ContractPushSteamEvent);
	}
}

int main()
{
	HostStartAcceptsValidInput();
	HostStartClampsMaxPlayers();
	HostStartRejectsBadName();
	HostStartRejectsBadPassword();
	HostStartRejectsBadMaxPlayers();
	HostStopCallsThrough();
	HostEnterRoutesToCallback();
	JoinAcceptsValidInput();
	JoinAcceptsBareFriendCode();
	JoinRejectsBadFriendCode();
	MissingCallbacksEmitUnavailable();
	InviteCallsThrough();
	UnknownActionsAreNotHandled();
	ParsersWorkStandalone();
	InterfaceContract();
	return 0;
}
