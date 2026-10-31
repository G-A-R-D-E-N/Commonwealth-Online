#include "F4TFriendValidation.h"

#include <cassert>
#include <string>

namespace
{
	using nlohmann::json;
	using namespace F4T::FriendValidation;

	void TestRequest()
	{
		const auto base = NormalizeFriendRequest(json{ { "type", "friendRequest" }, { "targetPlayerId", 7 } });
		assert(base && base->at("targetPlayerId") == 7 && base->at("type") == "friendRequest");
		const auto unsignedId = NormalizeFriendRequest(json{ { "targetPlayerId", std::uint32_t{ 7 } } });
		assert(unsignedId && unsignedId->at("targetPlayerId") == 7);
		assert(!NormalizeFriendRequest(json{ { "targetPlayerId", 0 } }));
		assert(!NormalizeFriendRequest(json{ { "targetPlayerId", -1 } }));
		assert(!NormalizeFriendRequest(json{ { "targetPlayerId", 4294967296 } }));
		assert(!NormalizeFriendRequest(json{ { "targetPlayerId", 1.5 } }));
		assert(!NormalizeFriendRequest(json{ { "targetPlayerId", "7" } }));
		assert(!NormalizeFriendRequest(json{ { "targetPlayerId", true } }));
		assert(!NormalizeFriendRequest(json{ { "type", "friendRequest" } }));
		assert(!NormalizeFriendRequest(json::array()));
	}

	void TestResponse()
	{
		const auto accept = NormalizeFriendRequestResponse(json{ { "requestId", "fr-1" }, { "accept", true } });
		assert(accept && accept->at("requestId") == "fr-1" && accept->at("accept") == true);
		const auto decline = NormalizeFriendRequestResponse(json{ { "requestId", "fr-2" }, { "accept", false } });
		assert(decline && decline->at("accept") == false);
		assert(!NormalizeFriendRequestResponse(json{ { "requestId", "fr-1" } }));
		assert(!NormalizeFriendRequestResponse(json{ { "requestId", "fr-1" }, { "accept", "true" } }));
		assert(!NormalizeFriendRequestResponse(json{ { "requestId", "" }, { "accept", true } }));
		assert(!NormalizeFriendRequestResponse(json{ { "requestId", std::string(65, 'a') }, { "accept", true } }));
		assert(!NormalizeFriendRequestResponse(json{ { "accept", true } }));
		assert(!NormalizeFriendRequestResponse(json::array()));
	}

	void TestRemove()
	{
		const auto remove = NormalizeFriendRemove(json{ { "friendId", "f12" } });
		assert(remove && remove->at("friendId") == "f12" && remove->at("type") == "friendRemove");
		assert(!NormalizeFriendRemove(json{ { "friendId", "" } }));
		assert(!NormalizeFriendRemove(json{ { "friendId", 12 } }));
		assert(!NormalizeFriendRemove(json{ { "friendId", std::string(65, 'a') } }));
		assert(!NormalizeFriendRemove(json{ { "type", "friendRemove" } }));
		assert(!NormalizeFriendRemove(json::array()));
	}

	void TestListRequest()
	{
		const auto list = NormalizeFriendListRequest(json{ { "type", "friendListRequest" } });
		assert(list && list->at("type") == "friendListRequest");
		assert(NormalizeFriendListRequest(json::object()));
		assert(!NormalizeFriendListRequest(json::array()));
		assert(!NormalizeFriendListRequest(json{ "friendListRequest" }));
	}
}

int main()
{
	TestRequest();
	TestResponse();
	TestRemove();
	TestListRequest();
	return 0;
}
