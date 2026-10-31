// Copyright (C) 2026 FatherLamb. Portions derived from FathersFallout, GPL-3.0 with additional terms. Used with the author's permission.
#include "F4TLogRedaction.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>

namespace
{
	using F4T::LogRedaction::RedactLog;

	constexpr auto kRedactionTimeLimit = std::chrono::seconds(60);

	void ReportMismatch(std::string_view a_case, std::string_view a_input, std::string_view a_expected, std::string_view a_actual)
	{
		std::fprintf(
			stderr,
			"redaction mismatch [%.*s]\n  input:    %.*s\n  expected: %.*s\n  actual:   %.*s\n",
			static_cast<int>(a_case.size()), a_case.data(),
			static_cast<int>(a_input.size()), a_input.data(),
			static_cast<int>(a_expected.size()), a_expected.data(),
			static_cast<int>(a_actual.size()), a_actual.data());
	}

	void ExpectRedacted(std::string_view a_case, std::string_view a_input, std::string_view a_expected)
	{
		const auto actual = RedactLog(a_input);
		if (actual != a_expected) {
			ReportMismatch(a_case, a_input, a_expected, actual);
			assert(false && "RedactLog produced an unexpected result");
		}
	}

	void ExpectUnchanged(std::string_view a_case, std::string_view a_input)
	{
		ExpectRedacted(a_case, a_input, a_input);
	}

	void CheckIpv4()
	{
		ExpectRedacted("ipv4 plain", "host 192.168.1.10 online", "host <ip> online");
		ExpectRedacted("ipv4 port", "connect 10.0.0.1:7777 now", "connect <ip> now");
		ExpectRedacted("ipv4 url", "http://192.168.1.5:8080/path", "http://<ip>/path");
		ExpectRedacted("ipv4 max", "a 255.255.255.255 b", "a <ip> b");
		ExpectRedacted("ipv4 two", "1.2.3.4 and 5.6.7.8", "<ip> and <ip>");
		ExpectUnchanged("ipv4 octet overflow", "256.1.1.1");
		ExpectUnchanged("ipv4 version", "client 1.10.163 ready");
		ExpectUnchanged("ipv4 trailing digit", "x9.9.9.9");
	}

	void CheckIpv6()
	{
		ExpectRedacted("ipv6 full", "peer 2001:0db8:85a3:0000:0000:8a2e:0370:7334 up", "peer <ip> up");
		ExpectRedacted("ipv6 full letters", "dead:beef:dead:beef:dead:beef:dead:beef", "<ip>");
		ExpectRedacted("ipv6 compressed", "peer 2001:db8::1 up", "peer <ip> up");
		ExpectRedacted("ipv6 loopback", "peer ::1 up", "peer <ip> up");
		ExpectRedacted("ipv6 uppercase", "PEER FE80::ABCD UP", "PEER <ip> UP");
		ExpectRedacted("ipv6 zone", "fe80::1%eth0", "<ip>");
		ExpectRedacted("ipv6 bracket port", "[2001:db8::1]:7777", "<ip>");
		ExpectRedacted("ipv6 bracket", "[2001:db8::1] reachable", "<ip> reachable");
		ExpectUnchanged("clock time", "time 12:34:56 done");
		ExpectUnchanged("clock time millis", "time 12:34:56.789 done");
		ExpectUnchanged("cxx qualified name", "std::vector<int> and RE::FormType");
		ExpectUnchanged("key value pair", "region: eu-west: 2");
	}

	void CheckSteam()
	{
		ExpectRedacted("steamid plain", "steamid 76561198000000000 linked", "steamid <steamid> linked");
		ExpectRedacted("steam target", "target steam:76561198000000000", "target <steamid>");
		ExpectRedacted("steam url", "steam://run/440", "<redacted>");
		ExpectRedacted("steam ticket", "steam:AwAAAABticket", "<redacted>");
		ExpectUnchanged("steamid short", "7656119800000000");
		ExpectUnchanged("steamid inside digits", "176561198000000000");
	}

	void CheckUserPaths()
	{
		ExpectRedacted("user backslash", R"(saved C:\Users\Alice\Documents\x.txt ok)", R"(saved C:\Users\<user>\Documents\x.txt ok)");
		ExpectRedacted("user forward slash", "saved C:/Users/Bob/data", "saved C:/Users/<user>/data");
		ExpectRedacted("user lowercase drive", R"(saved c:\users\carol\d)", R"(saved c:\users\<user>\d)");
		ExpectRedacted("user json escaped", R"("path":"C:\\Users\\Alice\\save")", R"("path":"C:\\Users\\<user>\\save")");
		ExpectRedacted("user unc prefix", R"(\\?\D:\Users\Dave\x)", R"(\\?\D:\Users\<user>\x)");
		ExpectRedacted(
			"user name stops at line end",
			"C:\\Users\\bob failed\nnext line 1.2.3.4",
			"C:\\Users\\<user>\nnext line <ip>");
		ExpectRedacted("user name stops at tab", "C:\\Users\\bob\t1.2.3.4", "C:\\Users\\<user>\t<ip>");
		ExpectRedacted(
			"user name stops at quote",
			R"("path":"C:\Users\bob","ip":"1.2.3.4")",
			R"("path":"C:\Users\<user>","ip":"<ip>")");
		ExpectRedacted("user name stops at pipe", R"(C:\Users\bob|1.2.3.4)", R"(C:\Users\<user>|<ip>)");
		ExpectUnchanged("windows system path", R"(C:\Windows\System32\winhttp.dll)");
		ExpectUnchanged("relative path", R"(CommonwealthOnline\recent-servers.json)");
		ExpectUnchanged("users without name", R"(C:\Users\)");
	}

	void CheckSensitiveKeys()
	{
		ExpectRedacted("key equals", "resumeToken=abc123", "resumeToken=<redacted>");
		ExpectRedacted("key colon", "resumeToken: abc123 next", "resumeToken: <redacted> next");
		ExpectRedacted("key json", R"("resumeToken":"abc123")", R"("resumeToken":"<redacted>")");
		ExpectRedacted("key json spaced", R"("serverPassword": "hunter2")", R"("serverPassword": "<redacted>")");
		ExpectRedacted("password comma", "serverPassword=hunter2,", "serverPassword=<redacted>,");
		ExpectRedacted("session id", "sessionId=deadbeef;", "sessionId=<redacted>;");
		ExpectRedacted("session nonce", "sessionNonce: 42 rest", "sessionNonce: <redacted> rest");
		ExpectRedacted("token", "token=abcdef", "token=<redacted>");
		ExpectRedacted("password", "password=hunter2", "password=<redacted>");
		ExpectRedacted("protected token", R"("resumeTokenProtected":"blob==")", R"("resumeTokenProtected":"<redacted>")");
		ExpectRedacted("token ack longest first", "resumeTokenAck=rotated", "resumeTokenAck=<redacted>");
		ExpectRedacted("snake case", "server_password=hunter2 resume_token=abc", "server_password=<redacted> resume_token=<redacted>");
		ExpectRedacted(
			"mixed json line",
			R"(hello {"resumeToken":"t1","serverPassword":"p1"} ip=10.1.2.3 user C:\Users\Dave)",
			R"(hello {"resumeToken":"<redacted>","serverPassword":"<redacted>"} ip=<ip> user C:\Users\<user>)");
		ExpectRedacted("password spaced", "password hunter2", "password <redacted>");
		ExpectRedacted("server password spaced", "serverPassword correct-horse-battery", "serverPassword <redacted>");
		ExpectRedacted("resume token spaced", "resumeToken abc123def456", "resumeToken <redacted>");
		ExpectRedacted(
			"bearer spaced",
			"Authorization: Bearer eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxIn0.sig",
			"Authorization: Bearer <redacted>");
		ExpectRedacted("bearer lowercase", "bearer ABCDEFGHIJK", "bearer <redacted>");
		ExpectRedacted("password spaced quoted", R"(password "hunter two")", R"(password "<redacted>")");
		ExpectUnchanged("hasPassword flag", "hasPassword=true");
		ExpectUnchanged("hasPassword spaced", "hasPassword true");
		ExpectUnchanged("passworded word", "passworded: false");
		ExpectUnchanged("token word", "the token bucket refilled");
		ExpectUnchanged("password protected", "password protected");
		ExpectUnchanged("password required", "the password required is 8 characters");
		ExpectUnchanged("server password is set", "serverPassword is set");
		ExpectUnchanged("resume token missing", "resumeToken missing");
		ExpectUnchanged("bearer prose", "the bearer token is stale");
		ExpectUnchanged("key word alone", "password");
		ExpectUnchanged("session id spaced", "sessionId missing");
	}

	void CheckIdentifiersAndHashes()
	{
		ExpectUnchanged("engine hex id", "ref 0x0001F66B resolved");
		ExpectUnchanged("engine bare hex id", "ref 0001F66B resolved");
		ExpectUnchanged("sha256", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
		ExpectUnchanged("timestamp", "2026-08-14 00:07:07");
		ExpectUnchanged("timestamp bracketed", "[2026-08-14 00:07:07]");
		ExpectRedacted("function name", "LoadLibraryExW and GetSaveFolderName", "LoadLibraryExW and GetSaveFolderName");
	}

	void CheckIdempotence()
	{
		const std::string corpus =
			R"(host 192.168.1.10:7777 peer [2001:db8::1]:5000 other 2001:db8::1 user C:\Users\Alice\x steam:76561198000000000)" "\n"
			R"({"resumeToken":"t1","serverPassword": "p1"} password=abc token: def hasPassword=true 0x0001F66B 12:34:56 1.10.163)";

		const auto once = RedactLog(corpus);
		const auto twice = RedactLog(once);
		assert(once == twice && "RedactLog must be idempotent");
		assert(once.find("192.168.") == std::string::npos);
		assert(once.find("Alice") == std::string::npos);
		assert(once.find("76561198000000000") == std::string::npos);
		assert(once.find("hasPassword=true") != std::string::npos);
		assert(once.find("0x0001F66B") != std::string::npos);
	}

	void CheckSharedLogComposition()
	{
		using F4T::LogRedaction::ComposeSharedLog;
		assert(ComposeSharedLog("", "host 192.168.1.10 online\n") == "host <ip> online\n");
		assert(ComposeSharedLog("old 10.0.0.1\n", "new\n") == "===== previous session =====\nold <ip>\n===== current session =====\nnew\n");
		assert(ComposeSharedLog("old 10.0.0.1", "new\n") == "===== previous session =====\nold <ip>\n===== current session =====\nnew\n");
		assert(ComposeSharedLog("prev\n", "") == "===== previous session =====\nprev\n===== current session =====\n");
	}

	void CheckLargeInputTiming()
	{
		constexpr std::string_view kLineTemplate =
			"[2026-08-14 00:07:07] [net] peer 192.168.1.10:7777 said hello resumeToken=abc123def456 steam:76561198000000000 save C:\\Users\\Alice\\Documents\\Save1.fos\n";

		std::string input;
		input.reserve(12000 * kLineTemplate.size());
		for (int index = 0; index < 12000; ++index) {
			input += kLineTemplate;
		}
		input += std::string(200000, ':');
		input += "\n";
		assert(input.size() >= 1000000 && "large input fixture must exceed 1 MiB");

		const auto start = std::chrono::steady_clock::now();
		const auto redacted = RedactLog(input);
		const auto elapsed = std::chrono::steady_clock::now() - start;

		assert(!redacted.empty());
		assert(redacted.size() >= input.size() / 2);
		assert(redacted.find("192.168.") == std::string::npos);
		assert(redacted.find("abc123def456") == std::string::npos);
		assert(redacted.find("Alice") == std::string::npos);
		assert(elapsed < kRedactionTimeLimit);

		std::printf(
			"F4TLogRedactionTests: redacted %zu bytes of %zu in %lld ms\n",
			redacted.size(),
			input.size(),
			static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()));
	}
}

int main()
{
	CheckIpv4();
	CheckIpv6();
	CheckSteam();
	CheckUserPaths();
	CheckSensitiveKeys();
	CheckIdentifiersAndHashes();
	CheckIdempotence();
	CheckSharedLogComposition();
	CheckLargeInputTiming();

	std::printf("F4TLogRedactionTests: all checks passed\n");
	return 0;
}
