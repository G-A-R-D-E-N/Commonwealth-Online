#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace CommonwealthOnline::Server
{
	struct AdminCommand
	{
		std::uint64_t ticket{};
		nlohmann::json request;
	};

	class AdminControlServer
	{
	public:
		using TimeoutInstaller = std::function<bool(std::uintptr_t, bool)>;
		using DirectHandler = std::function<nlohmann::json(const nlohmann::json&)>;

		AdminControlServer(
			std::uint16_t a_port,
			std::string a_adminToken,
			TimeoutInstaller a_timeoutInstaller = {},
			DirectHandler a_directHandler = {});
		~AdminControlServer();

		AdminControlServer(const AdminControlServer&) = delete;
		AdminControlServer& operator=(const AdminControlServer&) = delete;

		[[nodiscard]] bool Start(std::string& a_error);
		void Stop();
		[[nodiscard]] std::uint16_t LocalPort() const noexcept;
		[[nodiscard]] bool TryPop(AdminCommand& a_command);
		[[nodiscard]] bool Complete(std::uint64_t a_ticket, nlohmann::json a_response);

		[[nodiscard]] static bool TokenEquals(std::string_view a_supplied, std::string_view a_expected) noexcept;
		[[nodiscard]] static nlohmann::json Failure(std::string a_error, const nlohmann::json* a_request = nullptr, nlohmann::json a_data = nullptr);
		[[nodiscard]] static nlohmann::json Success(
			nlohmann::json a_data = nullptr,
			std::string a_message = {},
			const nlohmann::json* a_request = nullptr);

	private:
		struct Waiter;

		enum class CommandState : std::uint8_t
		{
			Queued,
			Executing
		};

		enum class CancelOutcome : std::uint8_t
		{
			Cancelled,
			Executing,
			Settled
		};

		void AcceptLoop(std::uintptr_t a_listenSocket);
		void HandleClient(std::uintptr_t a_clientSocket);
		[[nodiscard]] nlohmann::json ProcessLine(std::string_view a_line);
		[[nodiscard]] nlohmann::json QueueAndWait(nlohmann::json a_request);
		void FailOutstanding(std::string_view a_reason);
		[[nodiscard]] CancelOutcome CancelIfQueued(std::uint64_t a_ticket);

		std::uint16_t _requestedPort{};
		std::string _adminToken;
		TimeoutInstaller _timeoutInstaller;
		DirectHandler _directHandler;
		std::atomic_bool _running{ false };
		std::atomic_uint16_t _localPort{ 0 };
		std::uintptr_t _listenSocket{ static_cast<std::uintptr_t>(-1) };
		std::thread _thread;
		mutable std::mutex _queueGate;
		std::deque<AdminCommand> _commands;
		std::unordered_map<std::uint64_t, std::shared_ptr<Waiter>> _waiters;
		std::unordered_map<std::uint64_t, CommandState> _states;
		std::uint64_t _nextTicket{ 1 };
		std::mutex _clientGate;
		std::condition_variable _clientsIdle;
		std::size_t _activeClients{};
		std::vector<std::uintptr_t> _clientSockets;
		bool _socketRuntimeHeld{};
	};
}
