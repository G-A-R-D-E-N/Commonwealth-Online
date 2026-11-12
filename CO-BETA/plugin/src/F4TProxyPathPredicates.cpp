#include "pch.h"
// address source:commonlibf4-nomad

#include "F4TProxyPathPredicates.h"
#include "F4TFailClosedNotice.h"
#include "F4TRuntimeSelect.h"

namespace F4T::ProxyPathPredicates
{
	namespace
	{
// RE-Work/subsystems/commonwealth-online-re/ai-path-cancel-co143/README.md (ids verified per runtime)
		inline constexpr REL::ID kIsPathValid{ 1522194, 2230273, 2230273 };
		inline constexpr REL::ID kIsPathing{ 989661, 2230276, 2230276 };
		inline constexpr REL::ID kIsPathingComplete{ 817283, 2230274, 2230274 };


		bool IsVerifiedRuntime()
		{
			static const bool verified = [] {
				const auto version = REX::FModule::GetExecutingModule().GetFileVersion();
				if (version == F4T::RuntimeSelect::VerifiedVersion()) {
					return true;
				}
				REX::WARN("Commonwealth Online path predicates are disabled: runtime {} is not verified (expected {}).", version.string(), F4T::RuntimeSelect::VerifiedVersion().string());
				F4T::FailClosedNotice::Show("Commonwealth Online is not verified for this Fallout 4 version: multiplayer path checks are off.");
				return false;
			}();
			return verified;
		}

		bool CallPathPredicate(const REL::ID& a_id, RE::Actor& a_actor)
		{
			using func_t = bool (*)(RE::Actor*);
			REL::Relocation<func_t> func{ a_id };
			return func(&a_actor);
		}
	}

	PathState ReadPathState(RE::Actor& a_actor)
	{
		if (!IsVerifiedRuntime()) {
			return {};
		}

		if (!a_actor.actorMover || !a_actor.movementController) {
			return {};
		}

		return PathState{
			CallPathPredicate(kIsPathing, a_actor),
			CallPathPredicate(kIsPathValid, a_actor),
			CallPathPredicate(kIsPathingComplete, a_actor)
		};
	}
}
