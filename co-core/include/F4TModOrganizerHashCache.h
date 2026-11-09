#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace F4T::ModOrganizerHashCache
{
	struct Key
	{
		std::string path;
		std::uint64_t size{};
		std::int64_t modifiedTicks{};
	};

	class Cache
	{
	public:
		[[nodiscard]] static Cache Load(const std::filesystem::path& a_file);

		[[nodiscard]] std::optional<std::string> Find(const std::string& a_path, std::uint64_t a_size, std::int64_t a_ticks) const;
		void Put(const std::string& a_path, std::uint64_t a_size, std::int64_t a_ticks, std::string a_sha256Hex);
		bool Save(const std::filesystem::path& a_file) const;

	private:
		struct Entry
		{
			Key key;
			std::string sha256;
			mutable bool used{};
		};

		std::map<std::string, Entry> entries_;
	};
}
