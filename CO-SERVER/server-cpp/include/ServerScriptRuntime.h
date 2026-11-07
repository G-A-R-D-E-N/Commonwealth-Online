#pragma once

#include "quickjs.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace CommonwealthOnline::Server::Scripting
{
	enum class Fault : std::uint8_t
	{
		None,
		Load,
		CpuBudget,
		Memory
	};

	struct Limits
	{
		std::size_t memoryBytes{ 16U * 1024U * 1024U };
		std::chrono::milliseconds callbackBudget{ 50 };
		std::uint32_t restartLimit{ 3U };
		double restartWindowSeconds{ 300.0 };
		double cpuShareWindowSeconds{ 5.0 };
		double cpuShareThreshold{ 0.5 };
	};

	using LogSink = std::function<void(std::string_view)>;

	class ScriptRuntime
	{
	public:
		ScriptRuntime(std::string a_name, const Limits& a_limits, LogSink a_log);
		~ScriptRuntime();
		ScriptRuntime(const ScriptRuntime&) = delete;
		ScriptRuntime& operator=(const ScriptRuntime&) = delete;

		[[nodiscard]] const std::string& GetName() const noexcept;
		[[nodiscard]] bool Load(std::string_view a_source, std::string_view a_fileName, double a_now);
		void Tick(double a_now);
		void Stop() noexcept;
		[[nodiscard]] Fault GetFault() const noexcept;
		[[nodiscard]] const std::string& GetError() const noexcept;
		[[nodiscard]] std::size_t TimerCount() const noexcept;
		[[nodiscard]] JSContext* Context() const noexcept;
		void OnStop(std::function<void()> a_callback);
		bool Call(JSValueConst a_function, int a_argc, JSValueConst* a_argv, JSValue* a_result = nullptr);

	private:
		struct Impl;
		std::unique_ptr<Impl> _impl;
	};
}
