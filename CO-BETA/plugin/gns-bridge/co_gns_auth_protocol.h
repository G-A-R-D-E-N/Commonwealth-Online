#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#if !defined(_WIN32)
#include <cerrno>
#include <fcntl.h>
#include <openssl/evp.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
// Constraint: these donna symbols exist in GameNetworkingSockets::static only under USE_CRYPTO=BCrypt.
extern "C"
{
	void ed25519_publickey(const unsigned char* sk, unsigned char* pk);
	void ed25519_sign(const unsigned char* m, std::size_t mlen, const unsigned char* sk, const unsigned char* pk, unsigned char* rs);
	int ed25519_sign_open(const unsigned char* m, std::size_t mlen, const unsigned char* pk, const unsigned char* rs);
}
#endif

namespace co_gns::auth
{
	inline constexpr std::array<std::uint8_t, 4> kHelloMagic{ 'C', 'O', 'A', 'H' };
	inline constexpr std::array<std::uint8_t, 4> kReplyMagic{ 'C', 'O', 'A', 'R' };
	inline constexpr std::uint8_t kProtocolVersion = 1;
	inline constexpr std::size_t kSeedSize = 32;
	inline constexpr std::size_t kPublicKeySize = 32;
	inline constexpr std::size_t kNonceSize = 32;
	inline constexpr std::size_t kSignatureSize = 64;
	inline constexpr std::size_t kHelloSize = 4 + 1 + kNonceSize;
	inline constexpr std::size_t kReplySize = 4 + 1 + kPublicKeySize + kSignatureSize;
	inline constexpr std::string_view kSignatureDomain{ "co-gns-server-auth-v1" };
	inline constexpr std::size_t kSignatureMessageSize = kSignatureDomain.size() + 1 + kNonceSize;
	inline constexpr std::int32_t kAuthFailureReason = 4001;
	inline constexpr std::int32_t kAuthRequiredReason = 4002;
	inline constexpr std::chrono::milliseconds kAuthTimeout{ 10000 };

	using Seed = std::array<std::uint8_t, kSeedSize>;
	using PublicKey = std::array<std::uint8_t, kPublicKeySize>;
	using Nonce = std::array<std::uint8_t, kNonceSize>;
	using Signature = std::array<std::uint8_t, kSignatureSize>;
	using SignatureMessage = std::array<std::uint8_t, kSignatureMessageSize>;
	using HelloFrame = std::array<std::uint8_t, kHelloSize>;
	using ReplyFrame = std::array<std::uint8_t, kReplySize>;

	inline void Log(std::string_view a_text)
	{
		std::fprintf(stderr, "%s\n", std::string{ a_text }.c_str());
		std::fflush(stderr);
	}

	inline std::string EnvValue(const char* a_name)
	{
		if (a_name == nullptr) {
			return {};
		}
#if defined(_MSC_VER)
		char* buffer = nullptr;
		std::size_t size = 0;
		if (_dupenv_s(&buffer, &size, a_name) != 0 || buffer == nullptr) {
			return {};
		}
		std::string value{ buffer };
		std::free(buffer);
		return value;
#else
		const char* value = std::getenv(a_name);
		return value == nullptr ? std::string{} : std::string{ value };
#endif
	}

	inline std::string ToHex(const std::uint8_t* a_data, std::size_t a_size)
	{
		static constexpr char kDigits[] = "0123456789abcdef";
		std::string out;
		out.reserve(a_size * 2);
		for (std::size_t i = 0; i < a_size; ++i) {
			out.push_back(kDigits[(a_data[i] >> 4) & 0x0F]);
			out.push_back(kDigits[a_data[i] & 0x0F]);
		}
		return out;
	}

	inline std::string ToHex(const PublicKey& a_key)
	{
		return ToHex(a_key.data(), a_key.size());
	}

	inline bool FromHex(std::string_view a_text, std::uint8_t* a_out, std::size_t a_size)
	{
		if (a_text.size() != a_size * 2) {
			return false;
		}
		const auto digit = [](char a_char) -> int {
			if (a_char >= '0' && a_char <= '9') {
				return a_char - '0';
			}
			if (a_char >= 'a' && a_char <= 'f') {
				return a_char - 'a' + 10;
			}
			if (a_char >= 'A' && a_char <= 'F') {
				return a_char - 'A' + 10;
			}
			return -1;
		};
		for (std::size_t i = 0; i < a_size; ++i) {
			const int high = digit(a_text[i * 2]);
			const int low = digit(a_text[i * 2 + 1]);
			if (high < 0 || low < 0) {
				return false;
			}
			a_out[i] = static_cast<std::uint8_t>((high << 4) | low);
		}
		return true;
	}

	inline Nonce RandomNonce()
	{
		Nonce nonce{};
		std::random_device device;
		for (std::size_t i = 0; i < nonce.size(); i += 4) {
			const auto value = static_cast<std::uint32_t>(device());
			nonce[i + 0] = static_cast<std::uint8_t>(value & 0xFFU);
			nonce[i + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFU);
			nonce[i + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFU);
			nonce[i + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFFU);
		}
		return nonce;
	}

	inline Seed RandomSeed()
	{
		const auto nonce = RandomNonce();
		Seed seed{};
		std::memcpy(seed.data(), nonce.data(), seed.size());
		return seed;
	}

	inline bool DerivePublicKey(const Seed& a_seed, PublicKey& a_out)
	{
#if defined(_WIN32)
		ed25519_publickey(a_seed.data(), a_out.data());
		return true;
#else
		const std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(
			EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, a_seed.data(), a_seed.size()),
			EVP_PKEY_free);
		std::size_t length = a_out.size();
		return key && EVP_PKEY_get_raw_public_key(key.get(), a_out.data(), &length) == 1 && length == a_out.size();
#endif
	}

	inline SignatureMessage BuildSignatureMessage(const Nonce& a_nonce)
	{
		SignatureMessage message{};
		std::memcpy(message.data(), kSignatureDomain.data(), kSignatureDomain.size());
		message[kSignatureDomain.size()] = 0;
		std::memcpy(message.data() + kSignatureDomain.size() + 1, a_nonce.data(), a_nonce.size());
		return message;
	}

	inline bool Ed25519Sign(const Seed& a_seed, const std::uint8_t* a_message, std::size_t a_size, Signature& a_out)
	{
#if defined(_WIN32)
		PublicKey publicKey{};
		if (!DerivePublicKey(a_seed, publicKey)) {
			return false;
		}
		ed25519_sign(a_message, a_size, a_seed.data(), publicKey.data(), a_out.data());
		return true;
#else
		const std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(
			EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, a_seed.data(), a_seed.size()),
			EVP_PKEY_free);
		const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
		std::size_t signatureSize = a_out.size();
		return key && context && EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, key.get()) == 1 &&
			EVP_DigestSign(context.get(), a_out.data(), &signatureSize, a_message, a_size) == 1 &&
			signatureSize == a_out.size();
#endif
	}

	inline bool Ed25519Verify(const PublicKey& a_publicKey, const std::uint8_t* a_message, std::size_t a_size, const Signature& a_signature)
	{
#if defined(_WIN32)
		return ed25519_sign_open(a_message, a_size, a_publicKey.data(), a_signature.data()) == 0;
#else
		const std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(
			EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, a_publicKey.data(), a_publicKey.size()),
			EVP_PKEY_free);
		const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
		return key && context && EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, key.get()) == 1 &&
			EVP_DigestVerify(context.get(), a_signature.data(), a_signature.size(), a_message, a_size) == 1;
#endif
	}

	inline bool SignNonce(const Seed& a_seed, const Nonce& a_nonce, Signature& a_out)
	{
		const auto message = BuildSignatureMessage(a_nonce);
		return Ed25519Sign(a_seed, message.data(), message.size(), a_out);
	}

	inline bool VerifyNonce(const PublicKey& a_publicKey, const Nonce& a_nonce, const Signature& a_signature)
	{
		const auto message = BuildSignatureMessage(a_nonce);
		return Ed25519Verify(a_publicKey, message.data(), message.size(), a_signature);
	}

	inline HelloFrame BuildHello(const Nonce& a_nonce)
	{
		HelloFrame frame{};
		std::memcpy(frame.data(), kHelloMagic.data(), kHelloMagic.size());
		frame[kHelloMagic.size()] = kProtocolVersion;
		std::memcpy(frame.data() + kHelloMagic.size() + 1, a_nonce.data(), a_nonce.size());
		return frame;
	}

	inline bool ParseHello(const std::uint8_t* a_data, std::size_t a_size, Nonce& a_outNonce)
	{
		if (a_data == nullptr || a_size != kHelloSize || std::memcmp(a_data, kHelloMagic.data(), kHelloMagic.size()) != 0 ||
			a_data[kHelloMagic.size()] != kProtocolVersion) {
			return false;
		}
		std::memcpy(a_outNonce.data(), a_data + kHelloMagic.size() + 1, a_outNonce.size());
		return true;
	}

	inline ReplyFrame BuildReply(const PublicKey& a_publicKey, const Signature& a_signature)
	{
		ReplyFrame frame{};
		std::memcpy(frame.data(), kReplyMagic.data(), kReplyMagic.size());
		frame[kReplyMagic.size()] = kProtocolVersion;
		std::memcpy(frame.data() + kReplyMagic.size() + 1, a_publicKey.data(), a_publicKey.size());
		std::memcpy(frame.data() + kReplyMagic.size() + 1 + a_publicKey.size(), a_signature.data(), a_signature.size());
		return frame;
	}

	inline bool ParseReply(const std::uint8_t* a_data, std::size_t a_size, PublicKey& a_outPublicKey, Signature& a_outSignature)
	{
		if (a_data == nullptr || a_size != kReplySize || std::memcmp(a_data, kReplyMagic.data(), kReplyMagic.size()) != 0 ||
			a_data[kReplyMagic.size()] != kProtocolVersion) {
			return false;
		}
		const auto* body = a_data + kReplyMagic.size() + 1;
		std::memcpy(a_outPublicKey.data(), body, a_outPublicKey.size());
		std::memcpy(a_outSignature.data(), body + a_outPublicKey.size(), a_outSignature.size());
		return true;
	}

	inline std::string_view Trim(std::string_view a_text)
	{
		const auto isSpace = [](char a_char) {
			return a_char == ' ' || a_char == '\t' || a_char == '\r' || a_char == '\n' || a_char == '\v' || a_char == '\f';
		};
		std::size_t begin = 0;
		std::size_t end = a_text.size();
		while (begin < end && isSpace(a_text[begin])) {
			++begin;
		}
		while (end > begin && isSpace(a_text[end - 1])) {
			end -= 1;
		}
		return a_text.substr(begin, end - begin);
	}

	inline bool ReadTextFile(const std::filesystem::path& a_path, std::string& a_out)
	{
		std::ifstream file{ a_path, std::ios::in | std::ios::binary };
		if (!file) {
			return false;
		}
		a_out.assign(std::istreambuf_iterator<char>{ file }, std::istreambuf_iterator<char>{});
		return true;
	}

	inline bool WriteTextFile(const std::filesystem::path& a_path, const std::string& a_contents)
	{
		std::ofstream file{ a_path, std::ios::out | std::ios::binary | std::ios::trunc };
		if (!file) {
			return false;
		}
		file.write(a_contents.data(), static_cast<std::streamsize>(a_contents.size()));
		file.flush();
		return static_cast<bool>(file);
	}

	inline std::filesystem::path PinFilePath()
	{
		const auto overridePath = EnvValue("CO_GNS_CLIENT_PIN_FILE");
		if (!overridePath.empty()) {
			return std::filesystem::path{ overridePath };
		}
		const auto localAppData = EnvValue("LOCALAPPDATA");
		if (!localAppData.empty()) {
			return std::filesystem::path{ localAppData } / "CommonwealthOnline" / "gns-server-pins.txt";
		}
		return std::filesystem::path{ "gns-server-pins.txt" };
	}

	struct PinEntry
	{
		std::string key;
		PublicKey value{};
	};

	inline bool ReadPinFile(const std::filesystem::path& a_path, std::vector<PinEntry>& a_out)
	{
		std::string contents;
		if (!ReadTextFile(a_path, contents)) {
			return false;
		}

		std::size_t offset = 0;
		while (offset <= contents.size()) {
			const auto lineEnd = contents.find('\n', offset);
			const auto raw = std::string_view{ contents }.substr(
				offset,
				lineEnd == std::string::npos ? contents.size() - offset : lineEnd - offset);
			const auto line = Trim(raw);
			offset = lineEnd == std::string::npos ? contents.size() + 1 : lineEnd + 1;
			if (line.empty() || line.front() == '#') {
				continue;
			}
			const auto separator = line.find('=');
			if (separator == std::string_view::npos) {
				continue;
			}
			PinEntry entry{};
			entry.key = std::string{ Trim(line.substr(0, separator)) };
			if (!FromHex(Trim(line.substr(separator + 1)), entry.value.data(), entry.value.size())) {
				continue;
			}
			a_out.push_back(std::move(entry));
		}
		return true;
	}

	inline bool WritePinFile(const std::filesystem::path& a_path, const std::vector<PinEntry>& a_entries)
	{
		std::error_code errorCode;
		const auto parent = a_path.parent_path();
		if (!parent.empty()) {
			std::filesystem::create_directories(parent, errorCode);
		}
		std::string contents{ "# co-gns server identity pins\n" };
		for (const auto& entry : a_entries) {
			contents += entry.key;
			contents += '=';
			contents += ToHex(entry.value);
			contents += '\n';
		}
		return WriteTextFile(a_path, contents);
	}

	inline bool LookupEnvPin(std::string_view a_key, PublicKey& a_out)
	{
		const auto overrideValue = EnvValue("CO_GNS_SERVER_PIN");
		if (overrideValue.empty()) {
			return false;
		}
		std::string_view text{ overrideValue };
		while (!text.empty()) {
			const auto separator = text.find(';');
			const auto entry = Trim(text.substr(0, separator));
			text = separator == std::string_view::npos ? std::string_view{} : text.substr(separator + 1);
			const auto equals = entry.find('=');
			if (equals == std::string_view::npos) {
				continue;
			}
			if (Trim(entry.substr(0, equals)) != a_key) {
				continue;
			}
			return FromHex(Trim(entry.substr(equals + 1)), a_out.data(), a_out.size());
		}
		return false;
	}

	enum class PinResult
	{
		Match,
		FirstUse,
		Mismatch
	};

	inline PinResult CheckPin(std::string_view a_key, const PublicKey& a_publicKey, std::string& a_error)
	{
		PublicKey pinned{};
		if (LookupEnvPin(a_key, pinned)) {
			if (pinned == a_publicKey) {
				return PinResult::Match;
			}
			a_error = "server identity does not match CO_GNS_SERVER_PIN for " + std::string{ a_key };
			return PinResult::Mismatch;
		}

		const auto path = PinFilePath();
		std::vector<PinEntry> entries;
		ReadPinFile(path, entries);
		for (const auto& entry : entries) {
			if (entry.key != a_key) {
				continue;
			}
			if (entry.value == a_publicKey) {
				return PinResult::Match;
			}
			a_error = "server identity changed for " + std::string{ a_key } + " (pinned " + ToHex(entry.value) + ", presented " +
				ToHex(a_publicKey) + ")";
			return PinResult::Mismatch;
		}

		PinEntry entry{};
		entry.key = std::string{ a_key };
		entry.value = a_publicKey;
		entries.push_back(std::move(entry));
		if (!WritePinFile(path, entries)) {
			a_error = "could not persist the first-use identity pin for " + std::string{ a_key } + " at " + path.string();
			return PinResult::Mismatch;
		}
		return PinResult::FirstUse;
	}

	inline std::filesystem::path ServerIdentityPath()
	{
		const auto overridePath = EnvValue("CO_GNS_SERVER_IDENTITY_FILE");
		if (!overridePath.empty()) {
			return std::filesystem::path{ overridePath };
		}
		return std::filesystem::path{ "co_gns_server_identity.key" };
	}

	class ServerIdentity
	{
	public:
		bool LoadOrCreate(std::string& a_error)
		{
			const auto path = ServerIdentityPath();
			std::string seedText;
			if (!ReadTextFile(path, seedText)) {
				if (std::filesystem::exists(path)) {
					a_error = "server identity file " + path.string() + " exists but is unreadable or malformed";
					return false;
				}
				const auto seed = RandomSeed();
				if (!WriteSeedFile(path, seed)) {
					a_error = "could not create the server identity file " + path.string() +
						"; set CO_GNS_SERVER_IDENTITY_FILE to a writable path";
					return false;
				}
				seed_ = seed;
			} else {
#if !defined(_WIN32)
				(void)::chmod(path.c_str(), 0600);
#endif
				if (!FromHex(Trim(seedText), seed_.data(), seed_.size())) {
					a_error = "server identity file " + path.string() + " does not contain a valid 32-byte hex seed";
					return false;
				}
			}
			if (!DerivePublicKey(seed_, publicKey_)) {
				a_error = "could not derive the server public key from the identity seed";
				return false;
			}

			const auto publicPath = path.string() + ".pub";
			const auto publicHex = ToHex(publicKey_);
			if (!WriteTextFile(publicPath, publicHex + "\n")) {
				a_error = "could not publish the server public key to " + publicPath;
				return false;
			}
			Log("[co-gns] server identity public key " + publicHex + " (" + publicPath + ")");
			loaded_ = true;
			return true;
		}

		bool Loaded() const
		{
			return loaded_;
		}

		const PublicKey& PublicKeyValue() const
		{
			return publicKey_;
		}

		void Sign(const Nonce& a_nonce, Signature& a_out) const
		{
			if (!SignNonce(seed_, a_nonce, a_out)) {
				a_out.fill(0);
			}
		}

	private:
#if !defined(_WIN32)
		static bool WritePrivateSeedFile(const std::filesystem::path& a_temporary, const std::filesystem::path& a_path, const std::string& a_contents)
		{
			::unlink(a_temporary.c_str());
			const int descriptor = ::open(a_temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
			if (descriptor < 0) {
				return false;
			}
			std::size_t written = 0;
			while (written < a_contents.size()) {
				const auto count = ::write(descriptor, a_contents.data() + written, a_contents.size() - written);
				if (count < 0 && errno == EINTR) {
					continue;
				}
				if (count <= 0) {
					break;
				}
				written += static_cast<std::size_t>(count);
			}
			const bool synced = written == a_contents.size() && ::fsync(descriptor) == 0;
			const bool closed = ::close(descriptor) == 0;
			std::error_code renameError;
			if (synced && closed) {
				std::filesystem::rename(a_temporary, a_path, renameError);
			}
			if (!synced || !closed || renameError) {
				std::error_code removeError;
				std::filesystem::remove(a_temporary, removeError);
				return false;
			}
			auto directoryPath = a_path.parent_path();
			if (directoryPath.empty()) {
				directoryPath = ".";
			}
			const int directory = ::open(directoryPath.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
			if (directory >= 0) {
				(void)::fsync(directory);
				(void)::close(directory);
			}
			return true;
		}
#endif

		static bool WriteSeedFile(const std::filesystem::path& a_path, const Seed& a_seed)
		{
			std::error_code errorCode;
			const auto parent = a_path.parent_path();
			if (!parent.empty()) {
				std::filesystem::create_directories(parent, errorCode);
			}
			const auto temporaryPath = std::filesystem::path{ a_path.string() + ".tmp" };
			const auto contents = ToHex(a_seed.data(), a_seed.size()) + "\n";
#if defined(_WIN32)
			if (!WriteTextFile(temporaryPath, contents)) {
				return false;
			}
			std::error_code renameError;
			std::filesystem::rename(temporaryPath, a_path, renameError);
			return !renameError;
#else
			return WritePrivateSeedFile(temporaryPath, a_path, contents);
#endif
		}

		Seed seed_{};
		PublicKey publicKey_{};
		bool loaded_{ false };
	};
}
