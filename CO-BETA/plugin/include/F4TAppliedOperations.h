#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace F4T::AppliedOperations
{
	bool Contains(std::string_view a_scope, std::uint64_t a_operationId);
	bool Remember(std::string_view a_scope, std::uint64_t a_operationId);
	std::string ReadScope(std::string_view a_scope);
	bool WriteScope(std::string_view a_scope, std::string_view a_text);
	bool Exists(std::string_view a_scope);
	void PruneBelow(std::string_view a_scope, std::uint64_t a_retainFromOperationId);
}
