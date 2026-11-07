#include "ServerScriptRuntime.h"

#include "quickjs.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace CommonwealthOnline::Server::Scripting
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		constexpr std::size_t kMaxTimers{ 1024 };
		constexpr std::uint32_t kMaxConsoleLines{ 100 };
		constexpr std::size_t kMaxConsoleBytes{ 1024 };
		constexpr std::size_t kFailHeadroomBytes{ 256U * 1024U };
		constexpr int kMaxCallDepth{ 8 };
		constexpr std::size_t kMaxDrainJobs{ 100000 };
		constexpr std::size_t kBlockHeaderBytes{ 16U };

		std::string Clip(std::string a_text)
		{
			std::replace(a_text.begin(), a_text.end(), '\r', ' ');
			std::replace(a_text.begin(), a_text.end(), '\n', ' ');
			if (a_text.size() > kMaxConsoleBytes) {
				std::size_t keep = kMaxConsoleBytes;
				for (; keep > 0 && (static_cast<unsigned char>(a_text[keep]) & 0xC0U) == 0x80U; --keep) {
				}
				a_text.resize(keep);
			}
			return a_text;
		}
	}

	struct ScriptRuntime::Impl
	{
		struct Timer
		{
			std::uint32_t id{};
			double due{};
			double interval{};
			bool repeat{};
			JSValue callback{};
		};

		std::string name;
		Limits limits;
		LogSink log;
		JSRuntime* rt{};
		JSContext* ctx{};
		Clock::time_point deadline{};
		bool interrupted{};
		bool stopped{};
		int depth{};
		std::size_t memoryLimit{};
		std::size_t liveBytes{};
		bool allocFailed{};
		Fault fault{ Fault::None };
		std::string error;
		double now{};
		std::uint32_t nextTimerId{ 1 };
		std::uint32_t printed{};
		std::vector<Timer> timers;
		std::vector<std::function<void()>> stopHooks;

		~Impl()
		{
			Clear();
			if (ctx) {
				JS_FreeContext(ctx);
			}
			if (rt) {
				JS_FreeRuntime(rt);
			}
		}

		bool Fits(std::size_t a_size, std::size_t a_replaced) const noexcept
		{
			const std::size_t others = liveBytes - a_replaced;
			return a_size <= memoryLimit && others <= memoryLimit - a_size;
		}

		static void* Allocate(void* a_opaque, std::size_t a_size)
		{
			auto& impl = *static_cast<Impl*>(a_opaque);
			if (!impl.Fits(a_size, 0)) {
				impl.allocFailed = true;
				return nullptr;
			}
			void* block = std::malloc(a_size + kBlockHeaderBytes);
			if (!block) {
				impl.allocFailed = true;
				return nullptr;
			}
			*static_cast<std::size_t*>(block) = a_size;
			impl.liveBytes += a_size;
			return static_cast<char*>(block) + kBlockHeaderBytes;
		}

		static void* AllocateZeroed(void* a_opaque, std::size_t a_count, std::size_t a_size)
		{
			if (a_size != 0 && a_count > SIZE_MAX / a_size) {
				static_cast<Impl*>(a_opaque)->allocFailed = true;
				return nullptr;
			}
			const std::size_t total = a_count * a_size;
			void* block = Allocate(a_opaque, total);
			if (block) {
				std::memset(block, 0, total);
			}
			return block;
		}

		static void* Reallocate(void* a_opaque, void* a_ptr, std::size_t a_size)
		{
			if (!a_ptr) {
				return Allocate(a_opaque, a_size);
			}
			auto& impl = *static_cast<Impl*>(a_opaque);
			char* block = static_cast<char*>(a_ptr) - kBlockHeaderBytes;
			const std::size_t old = *reinterpret_cast<std::size_t*>(block);
			if (a_size > old && !impl.Fits(a_size, old)) {
				impl.allocFailed = true;
				return nullptr;
			}
			void* moved = std::realloc(block, a_size + kBlockHeaderBytes);
			if (!moved) {
				impl.allocFailed = true;
				return nullptr;
			}
			*static_cast<std::size_t*>(moved) = a_size;
			impl.liveBytes = impl.liveBytes - old + a_size;
			return static_cast<char*>(moved) + kBlockHeaderBytes;
		}

		static void Release(void* a_opaque, void* a_ptr)
		{
			if (!a_ptr) {
				return;
			}
			auto& impl = *static_cast<Impl*>(a_opaque);
			char* block = static_cast<char*>(a_ptr) - kBlockHeaderBytes;
			impl.liveBytes -= *reinterpret_cast<std::size_t*>(block);
			std::free(block);
		}

		static std::size_t UsableSize(const void* a_ptr)
		{
			return a_ptr ? *reinterpret_cast<const std::size_t*>(static_cast<const char*>(a_ptr) - kBlockHeaderBytes) : 0U;
		}

		void Log(const std::string& a_text)
		{
			if (log && printed++ < kMaxConsoleLines) {
				log("[script:" + name + "] " + a_text);
			}
		}

		void Clear()
		{
			for (const auto& timer : timers) {
				JS_FreeValue(ctx, timer.callback);
			}
			timers.clear();
			stopped = true;
			auto hooks = std::exchange(stopHooks, {});
			for (const auto& hook : hooks) {
				hook();
			}
		}

		bool Enter()
		{
			if (depth >= kMaxCallDepth) {
				Log("script re-entry limit reached");
				return false;
			}
			if (depth == 0) {
				interrupted = false;
				printed = 0;
				deadline = Clock::now() + limits.callbackBudget;
			}
			++depth;
			return true;
		}

		void Leave()
		{
			if (depth > 0) {
				depth -= 1;
			}
		}

		void DiscardPending()
		{
			if (JS_HasException(ctx)) {
				JSValue pending = JS_GetException(ctx);
				JS_FreeValue(ctx, pending);
			}
		}

		int OwnString(JSValueConst a_object, const char* a_property, std::string& a_text)
		{
			const JSAtom atom = JS_NewAtom(ctx, a_property);
			if (atom == JS_ATOM_NULL) {
				DiscardPending();
				return -1;
			}
			JSPropertyDescriptor descriptor{};
			const int found = JS_GetOwnProperty(ctx, &descriptor, a_object, atom);
			JS_FreeAtom(ctx, atom);
			if (found < 0) {
				DiscardPending();
				return -1;
			}
			if (!found) {
				return 0;
			}
			int result = 0;
			if (JS_IsString(descriptor.value)) {
				if (const char* value = JS_ToCString(ctx, descriptor.value)) {
					a_text = value;
					JS_FreeCString(ctx, value);
					result = 1;
				} else {
					DiscardPending();
					result = -1;
				}
			}
			JS_FreeValue(ctx, descriptor.value);
			JS_FreeValue(ctx, descriptor.getter);
			JS_FreeValue(ctx, descriptor.setter);
			return result;
		}

		std::string ExceptionText(JSValueConst a_exception, bool& a_outOfMemory)
		{
			a_outOfMemory = JS_IsNull(a_exception);
			if (JS_IsString(a_exception)) {
				memoryLimit = limits.memoryBytes + kFailHeadroomBytes;
				const char* text = JS_ToCString(ctx, a_exception);
				memoryLimit = limits.memoryBytes;
				if (!text) {
					DiscardPending();
					a_outOfMemory = true;
					return "non-error exception";
				}
				std::string message{ text };
				JS_FreeCString(ctx, text);
				return message;
			}
			if (!JS_IsError(a_exception)) {
				return "non-error exception";
			}
			memoryLimit = limits.memoryBytes + kFailHeadroomBytes;
			std::string detail;
			int result = OwnString(a_exception, "message", detail);
			if (result == 0) {
				result = OwnString(a_exception, "stack", detail);
			}
			memoryLimit = limits.memoryBytes;
			if (result < 0) {
				a_outOfMemory = true;
			}
			return result > 0 && !detail.empty() ? "Error: " + detail : std::string{ "Error" };
		}

		void Fail(Fault a_fault, const char* a_message = nullptr)
		{
			JSValue exception = JS_GetException(ctx);
			bool outOfMemory = false;
			std::string message = ExceptionText(exception, outOfMemory);
			JS_FreeValue(ctx, exception);
			DiscardPending();
			const bool memoryText = allocFailed || message.find("out of memory") != std::string::npos;
			allocFailed = false;
			message = Clip(std::move(message));
			if (interrupted) {
				a_fault = Fault::CpuBudget;
				message = "CPU budget exceeded";
			} else if (outOfMemory || memoryText) {
				a_fault = Fault::Memory;
				message = "memory limit exceeded";
			} else if (a_message) {
				message = a_message;
			}
			Log(message);
			if (a_fault != Fault::None) {
				fault = a_fault;
				error = message;
				Clear();
			}
		}

		bool Settle(JSValue a_result, Fault a_fault, JSValue* a_keep = nullptr)
		{
			const bool ok = !JS_IsException(a_result);
			if (ok && a_keep != nullptr) {
				*a_keep = a_result;
			}
			else {
				JS_FreeValue(ctx, a_result);
			}
			if (!ok) {
				Fail(a_fault);
			} else if (allocFailed) {
				Fail(Fault::Memory);
			}
			return ok;
		}

		void Drain()
		{
			JSContext* job = nullptr;
			std::size_t executed = 0;
			while (!stopped && JS_IsJobPending(rt)) {
				if (executed >= kMaxDrainJobs) {
					Fail(Fault::CpuBudget, "too many pending jobs");
					return;
				}
				if (Clock::now() >= deadline) {
					interrupted = true;
					Fail(Fault::CpuBudget);
					return;
				}
				const int result = JS_ExecutePendingJob(rt, &job);
				++executed;
				if (result < 0) {
					Fail(Fault::Load);
					return;
				}
				if (allocFailed) {
					Fail(Fault::Memory);
					return;
				}
				if (result == 0) {
					return;
				}
			}
		}

		void Fire(double a_now)
		{
			const std::uint32_t first = nextTimerId;
			const auto tickEnd = Clock::now() + limits.callbackBudget;
			while (!stopped && Clock::now() < tickEnd) {
				auto next = timers.end();
				for (auto it = timers.begin(); it != timers.end(); ++it) {
					if (it->id < first && it->due <= a_now && (next == timers.end() || it->due < next->due || (it->due == next->due && it->id < next->id))) {
						next = it;
					}
				}
				if (next == timers.end()) {
					return;
				}
				JSValue callback = JS_DupValue(ctx, next->callback);
				if (next->repeat) {
					next->due = a_now + next->interval;
				} else {
					JS_FreeValue(ctx, next->callback);
					timers.erase(next);
				}
				if (!Enter()) {
					JS_FreeValue(ctx, callback);
					return;
				}
				Settle(JS_Call(ctx, callback, JS_UNDEFINED, 0, nullptr), Fault::None);
				JS_FreeValue(ctx, callback);
				Drain();
				Leave();
			}
		}

		static Impl& Of(JSContext* a_ctx)
		{
			return *static_cast<Impl*>(JS_GetRuntimeOpaque(JS_GetRuntime(a_ctx)));
		}

		static int Interrupt(JSRuntime*, void* a_opaque)
		{
			auto& impl = *static_cast<Impl*>(a_opaque);
			if (Clock::now() < impl.deadline) {
				return 0;
			}
			impl.interrupted = true;
			return 1;
		}

		static void Rejection(JSContext* a_ctx, JSValueConst, JSValueConst a_reason, bool a_handled, void*)
		{
			if (a_handled) {
				return;
			}
			Impl& impl = Of(a_ctx);
			bool outOfMemory = false;
			std::string message = impl.ExceptionText(a_reason, outOfMemory);
			if (message == "non-error exception") {
				message = "non-error rejection";
			}
			impl.Log("unhandled rejection: " + Clip(std::move(message)));
		}

		static JSValue Print(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			Impl& impl = Of(a_ctx);
			if (impl.printed >= kMaxConsoleLines) {
				return JS_UNDEFINED;
			}
			std::string line;
			for (int index = 0; index < a_argc && line.size() < kMaxConsoleBytes; ++index) {
				const char* text = JS_ToCString(a_ctx, a_argv[index]);
				if (!text) {
					return JS_EXCEPTION;
				}
				if (index > 0) {
					line += ' ';
				}
				line += text;
				JS_FreeCString(a_ctx, text);
			}
			impl.Log(Clip(std::move(line)));
			return JS_UNDEFINED;
		}

		static JSValue AddTimer(JSContext* a_ctx, int a_argc, JSValueConst* a_argv, bool a_repeat)
		{
			if (a_argc < 1 || !JS_IsFunction(a_ctx, a_argv[0])) {
				return JS_ThrowTypeError(a_ctx, "timer callback must be a function");
			}
			double ms = 0.0;
			if (a_argc > 1 && JS_ToFloat64(a_ctx, &ms, a_argv[1]) < 0) {
				return JS_EXCEPTION;
			}
			if (!(ms >= 0.0)) {
				ms = 0.0;
			}
			Impl& impl = Of(a_ctx);
			if (impl.timers.size() >= kMaxTimers) {
				return JS_ThrowRangeError(a_ctx, "too many timers");
			}
			const double seconds = std::max(ms, a_repeat ? 1.0 : 0.0) / 1000.0;
			const std::uint32_t id = impl.nextTimerId++;
			impl.timers.push_back(Timer{ id, impl.now + seconds, seconds, a_repeat, JS_DupValue(a_ctx, a_argv[0]) });
			return JS_NewFloat64(a_ctx, static_cast<double>(id));
		}

		static JSValue SetTimeout(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			return AddTimer(a_ctx, a_argc, a_argv, false);
		}

		static JSValue SetInterval(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			return AddTimer(a_ctx, a_argc, a_argv, true);
		}

		static JSValue ClearTimer(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			double id = 0.0;
			if (a_argc > 0 && JS_ToFloat64(a_ctx, &id, a_argv[0]) < 0) {
				return JS_EXCEPTION;
			}
			Impl& impl = Of(a_ctx);
			const auto match = std::find_if(impl.timers.begin(), impl.timers.end(), [id](const Timer& a_timer) { return static_cast<double>(a_timer.id) == id; });
			if (match != impl.timers.end()) {
				JS_FreeValue(a_ctx, match->callback);
				impl.timers.erase(match);
			}
			return JS_UNDEFINED;
		}

		void InstallGlobals()
		{
			JSValue global = JS_GetGlobalObject(ctx);
			JSValue console = JS_NewObject(ctx);
			for (const char* method : { "log", "info", "warn", "error", "debug" }) {
				JS_SetPropertyStr(ctx, console, method, JS_NewCFunction(ctx, &Print, method, 1));
			}
			JS_SetPropertyStr(ctx, global, "console", console);
			JS_SetPropertyStr(ctx, global, "setTimeout", JS_NewCFunction(ctx, &SetTimeout, "setTimeout", 2));
			JS_SetPropertyStr(ctx, global, "setInterval", JS_NewCFunction(ctx, &SetInterval, "setInterval", 2));
			JS_SetPropertyStr(ctx, global, "clearTimeout", JS_NewCFunction(ctx, &ClearTimer, "clearTimeout", 1));
			JS_SetPropertyStr(ctx, global, "clearInterval", JS_NewCFunction(ctx, &ClearTimer, "clearInterval", 1));
			JS_FreeValue(ctx, global);
		}
	};

	ScriptRuntime::ScriptRuntime(std::string a_name, const Limits& a_limits, LogSink a_log) :
		_impl{ std::make_unique<Impl>() }
	{
		_impl->name = std::move(a_name);
		_impl->limits = a_limits;
		_impl->log = std::move(a_log);
		_impl->memoryLimit = _impl->limits.memoryBytes;
		const JSMallocFunctions allocator{ &Impl::AllocateZeroed, &Impl::Allocate, &Impl::Release, &Impl::Reallocate, &Impl::UsableSize };
		_impl->rt = JS_NewRuntime2(&allocator, _impl.get());
		if (!_impl->rt) {
			throw std::runtime_error("QuickJS runtime allocation failed");
		}
		JS_SetMaxStackSize(_impl->rt, 256U * 1024U);
		JS_SetCanBlock(_impl->rt, false);
		JS_SetRuntimeOpaque(_impl->rt, _impl.get());
		JS_SetInterruptHandler(_impl->rt, &Impl::Interrupt, _impl.get());
		JS_SetHostPromiseRejectionTracker(_impl->rt, &Impl::Rejection, _impl.get());
		_impl->ctx = JS_NewContext(_impl->rt);
		if (!_impl->ctx) {
			throw std::runtime_error("QuickJS context allocation failed");
		}
		_impl->InstallGlobals();
		if (_impl->allocFailed) {
			throw std::runtime_error("QuickJS context allocation failed");
		}
	}

	ScriptRuntime::~ScriptRuntime() = default;

	bool ScriptRuntime::Load(std::string_view a_source, std::string_view a_fileName, double a_now)
	{
		JS_UpdateStackTop(_impl->rt);
		_impl->now = a_now;
		if (_impl->stopped) {
			return false;
		}
		if (!_impl->Enter()) {
			return false;
		}
		const std::string fileName{ a_fileName };
		const bool settled = _impl->Settle(JS_Eval(_impl->ctx, a_source.data(), a_source.size(), fileName.c_str(), JS_EVAL_TYPE_GLOBAL), Fault::Load);
		if (settled && _impl->depth == 1) {
			_impl->Drain();
		}
		_impl->Leave();
		return _impl->fault == Fault::None;
	}

	void ScriptRuntime::Tick(double a_now)
	{
		JS_UpdateStackTop(_impl->rt);
		_impl->now = a_now;
		_impl->Fire(a_now);
	}

	void ScriptRuntime::Stop() noexcept
	{
		_impl->Clear();
	}

	Fault ScriptRuntime::GetFault() const noexcept
	{
		return _impl->fault;
	}

	const std::string& ScriptRuntime::GetError() const noexcept
	{
		return _impl->error;
	}

	std::size_t ScriptRuntime::TimerCount() const noexcept
	{
		return _impl->timers.size();
	}

	JSContext* ScriptRuntime::Context() const noexcept
	{
		return _impl->ctx;
	}

	void ScriptRuntime::OnStop(std::function<void()> a_callback)
	{
		_impl->stopHooks.push_back(std::move(a_callback));
	}

	const std::string& ScriptRuntime::GetName() const noexcept
	{
		return _impl->name;
	}

	bool ScriptRuntime::Call(JSValueConst a_function, int a_argc, JSValueConst* a_argv, JSValue* a_result)
	{
		JS_UpdateStackTop(_impl->rt);
		if (_impl->stopped) {
			if (a_result != nullptr) *a_result = JS_UNDEFINED;
			return false;
		}
		if (!_impl->Enter()) {
			if (a_result != nullptr) *a_result = JS_UNDEFINED;
			return false;
		}
		JSValue kept = JS_UNDEFINED;
		const bool ok = _impl->Settle(JS_Call(_impl->ctx, a_function, JS_UNDEFINED, a_argc, a_argv), Fault::None, a_result != nullptr ? &kept : nullptr);
		if (_impl->depth == 1) {
			_impl->Drain();
		}
		_impl->Leave();
		if (a_result != nullptr) *a_result = kept;
		return ok;
	}
}
