#include "pch.h"

#include "F4TProxyPathPredicates.h"

namespace F4T::ProxyPathPredicates
{
	namespace
	{
// RE-Work/subsystems/commonwealth-online-re/ai-path-cancel-co143/README.md (ids verified per runtime)
#if defined(F4T_TARGET_OG)
		inline constexpr REL::ID kIsPathValid{ 1522194 };
		inline constexpr REL::ID kIsPathing{ 989661 };
		inline constexpr REL::ID kIsPathingComplete{ 817283 };
#elif defined(F4T_TARGET_NG) || defined(F4T_TARGET_AE)
		inline constexpr REL::ID kIsPathValid{ 2230273 };
		inline constexpr REL::ID kIsPathing{ 2230276 };
		inline constexpr REL::ID kIsPathingComplete{ 2230274 };
#else
#error "A Commonwealth Online runtime target is required"
#endif

// source:RE-Work/subsystems/commonwealth-online-re/ai-path-cancel-co143/README.md (verified runtime versions)
#if defined(F4T_TARGET_OG)
		inline constexpr REL::Version kVerifiedVersion{ 1, 10, 163 };
#elif defined(F4T_TARGET_NG)
		inline constexpr REL::Version kVerifiedVersion{ 1, 10, 984 };
#elif defined(F4T_TARGET_AE)
		inline constexpr REL::Version kVerifiedVersion{ 1, 11, 240 };
#endif

		bool IsVerifiedRuntime()
		{
			static const bool verified = [] {
				const auto version = REX::FModule::GetExecutingModule().GetFileVersion();
				if (version == kVerifiedVersion) {
					return true;
				}
				REX::WARN("Commonwealth Online path predicates are disabled: runtime {} is not verified (expected {}).", version.string(), kVerifiedVersion.string());
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
