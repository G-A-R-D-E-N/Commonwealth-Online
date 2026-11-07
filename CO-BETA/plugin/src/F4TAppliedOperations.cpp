#include "pch.h"

#include "F4TAppliedOperations.h"
#include "F4TServerProfileStore.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace F4T::AppliedOperations
{
	namespace
	{
		std::filesystem::path ScopePath(std::string_view a_scope)
		{
			return F4T::ServerProfileStore::GetStoreDirectory() / (std::string{ a_scope } + ".applied");
		}
	}

	bool Contains(std::string_view a_scope, std::uint64_t a_operationId)
	{
		std::ifstream input(ScopePath(a_scope));
		std::uint64_t id{};
		while (input >> id) {
			if (id == a_operationId) {
				return true;
			}
		}
		return false;
	}

	bool Remember(std::string_view a_scope, std::uint64_t a_operationId)
	{
		const auto path = ScopePath(a_scope);
		std::error_code error;
		std::filesystem::create_directories(path.parent_path(), error);
		std::ofstream output(path, std::ios::app);
		output << '\n' << a_operationId;
		output.close();
		return !output.fail();
	}

	std::string ReadScope(std::string_view a_scope)
	{
		std::ifstream input(ScopePath(a_scope), std::ios::binary);
		std::ostringstream text;
		text << input.rdbuf();
		return text.str();
	}

	bool WriteScope(std::string_view a_scope, std::string_view a_text)
	{
		const auto path = ScopePath(a_scope);
		std::error_code error;
		std::filesystem::create_directories(path.parent_path(), error);
		auto temporary = path;
		temporary += ".tmp";
		std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
		output << a_text;
		output.close();
		if (output.fail()) {
			return false;
		}
		std::filesystem::rename(temporary, path, error);
		return !error;
	}
}
