#pragma once

#include "ServerViolation.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CommonwealthOnline::Server
{
	class BanStore;

	enum class Action : std::uint8_t
	{
		Warn,
		Kick,
		TempBan,
		PermaBan
	};

	struct Consequence
	{
		Action action{};
		std::uint32_t seconds{};
	};

	struct ViolationSubject
	{
		std::string address;
		std::string steamId;
		bool hostSelf{};
		bool admin{};
		bool adminOnAddress{};
		bool connected{};
	};

	[[nodiscard]] Consequence ConsequenceFor(Violation a_violation, std::uint32_t a_strikes) noexcept;
	[[nodiscard]] std::string WithAppealLink(std::string_view a_text, std::string_view a_appealLink, std::size_t a_maximumBytes = std::string::npos);

	class StrikeLedger
	{
	public:
		[[nodiscard]] std::uint32_t Record(std::string_view a_subject, Violation a_violation, double a_nowSeconds);

	private:
		struct Strike
		{
			std::uint32_t count{};
			double lastSeenSeconds{};
		};

		std::map<std::pair<std::string, Violation>, Strike> _strikes;
	};

	class ViolationWindow
	{
	public:
		[[nodiscard]] bool Observe(double a_nowSeconds) noexcept;

	private:
		double _startedSeconds{};
		std::uint32_t _count{};
	};

	class ServerEnforcement
	{
	public:
		using SubjectOf = std::function<ViolationSubject(std::uint32_t)>;
		using EndSession = std::function<void(std::uint32_t, std::string_view, std::string_view)>;
		using Notify = std::function<void(std::uint32_t, std::string_view)>;

		ServerEnforcement(BanStore* a_bans, SubjectOf a_subjectOf, EndSession a_endSession, std::string a_appealLink, Notify a_notify = {});

		void ReportViolation(std::uint32_t a_connectionId, Violation a_violation, std::string_view a_detail);
		void ReportViolations(const std::vector<PendingViolation>& a_violations);

	private:
		BanStore* _bans;
		SubjectOf _subjectOf;
		EndSession _endSession;
		std::string _appealLink;
		Notify _notify;
		StrikeLedger _strikes;
	};
}
