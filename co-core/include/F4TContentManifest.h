#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace F4T::ContentManifest
{
	struct Entry
	{
		std::string relativePath;
		std::string sha256Hex;
	};

	[[nodiscard]] std::string Sha256Hex(std::string_view a_bytes);

	class Sha256Stream
	{
	public:
		Sha256Stream();
		~Sha256Stream();
		Sha256Stream(Sha256Stream&&) noexcept;
		Sha256Stream& operator=(Sha256Stream&&) noexcept;
		Sha256Stream(const Sha256Stream&) = delete;
		Sha256Stream& operator=(const Sha256Stream&) = delete;

		void Update(std::string_view a_bytes);
		[[nodiscard]] std::string FinishHex();

	private:
		struct State;
		std::unique_ptr<State> state;
	};

	[[nodiscard]] std::optional<std::string> Sha256HexFile(const std::filesystem::path& a_path);

	[[nodiscard]] bool IsValidFingerprint(std::string_view a_sha256Hex) noexcept;

	[[nodiscard]] bool IsValidEntry(const Entry& a_entry);

	[[nodiscard]] std::optional<std::string> ComputeFingerprint(std::span<const Entry> a_entries);
}
