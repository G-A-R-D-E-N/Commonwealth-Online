#pragma once

#include <functional>

namespace CommonwealthOnline::Server
{
	class MutationAllowScope
	{
	public:
		MutationAllowScope(const std::function<bool()>*& a_slot, const std::function<bool()>& a_allow)
			: slot{ a_slot }, previous{ a_slot }
		{
			slot = &a_allow;
		}

		~MutationAllowScope()
		{
			slot = previous;
		}

		MutationAllowScope(const MutationAllowScope&) = delete;
		MutationAllowScope& operator=(const MutationAllowScope&) = delete;

	private:
		const std::function<bool()>*& slot;
		const std::function<bool()>* previous;
	};
}
