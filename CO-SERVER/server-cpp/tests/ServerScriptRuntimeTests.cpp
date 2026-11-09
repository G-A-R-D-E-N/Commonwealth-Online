#include "ServerScriptResources.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace Scripting = CommonwealthOnline::Server::Scripting;

namespace
{
	using Lines = std::vector<std::string>;

	bool Contains(const Lines& a_lines, std::string_view a_text)
	{
		return std::find(a_lines.begin(), a_lines.end(), a_text) != a_lines.end();
	}

	bool Mentions(const Lines& a_lines, std::string_view a_text)
	{
		return std::any_of(a_lines.begin(), a_lines.end(), [a_text](const std::string& a_line) { return a_line.find(a_text) != std::string::npos; });
	}

	std::size_t CountOf(const Lines& a_lines, std::string_view a_text)
	{
		return static_cast<std::size_t>(std::count_if(a_lines.begin(), a_lines.end(), [a_text](const std::string& a_line) { return a_line == a_text; }));
	}

	void WriteFile(const std::filesystem::path& a_path, std::string_view a_text)
	{
		std::filesystem::create_directories(a_path.parent_path());
		std::ofstream out(a_path, std::ios::binary | std::ios::trunc);
		out << a_text;
	}

	std::string StateOf(const Scripting::ResourceManager& a_manager, std::string_view a_name)
	{
		for (const auto& item : a_manager.List()) {
			if (item.at("name") == std::string{ a_name }) {
				return item.at("state").get<std::string>();
			}
		}
		return {};
	}

	std::string DetailOf(const Scripting::ResourceManager& a_manager, std::string_view a_name)
	{
		for (const auto& item : a_manager.List()) {
			if (item.at("name") == std::string{ a_name }) {
				return item.at("detail").get<std::string>();
			}
		}
		return {};
	}

	Scripting::ScriptRuntime* g_nestedRuntime{};

	JSValue EnterNested(JSContext* a_context, JSValueConst, int, JSValueConst*)
	{
		if (g_nestedRuntime == nullptr) {
			return JS_UNDEFINED;
		}
		JSValue global = JS_GetGlobalObject(a_context);
		JSValue target = JS_GetPropertyStr(a_context, global, "inner");
		if (JS_IsUndefined(target)) {
			JS_FreeValue(a_context, target);
			target = JS_GetPropertyStr(a_context, global, "target");
		}
		JS_FreeValue(a_context, global);
		JSValue result = JS_UNDEFINED;
		const bool called = g_nestedRuntime->Call(target, 0, nullptr, &result);
		JS_FreeValue(a_context, target);
		if (!called) {
			JS_FreeValue(a_context, result);
			return JS_NewInt32(a_context, -1);
		}
		return result;
	}

	void InstallNested(Scripting::ScriptRuntime& a_runtime)
	{
		g_nestedRuntime = &a_runtime;
		JSContext* context = a_runtime.Context();
		JSValue global = JS_GetGlobalObject(context);
		JS_SetPropertyStr(context, global, "__enterNested", JS_NewCFunction(context, &EnterNested, "__enterNested", 0));
		JS_FreeValue(context, global);
	}
	struct FakeCpu
	{
		double clock{};
		double cost{};

		double Sample()
		{
			clock += cost;
			return clock;
		}
	};
}

int main()
{
	const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	const auto root = std::filesystem::temp_directory_path() / ("co-script-tests-" + std::to_string(stamp));
	std::filesystem::remove_all(root);

	{
		Lines lines;
		const Scripting::Limits limits{ 16U * 1024U * 1024U, std::chrono::milliseconds{ 20 } };
		Scripting::ScriptRuntime looping{ "loop", limits, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool loopLoaded = looping.Load("while (true) {}", "loop.js", 0.0);
		assert(!loopLoaded);
		assert(looping.GetFault() == Scripting::Fault::CpuBudget);

		Lines alive;
		const Scripting::Limits survivorLimits{ 16U * 1024U * 1024U, std::chrono::milliseconds{ 500 } };
		Scripting::ScriptRuntime survivor{ "alive", survivorLimits, [&alive](std::string_view a_line) { alive.emplace_back(a_line); } };
		const bool survivorLoaded = survivor.Load("console.log('still running')", "alive.js", 0.0);
		assert(survivorLoaded);
		assert(Contains(alive, "[script:alive] still running"));
	}

	{
		Lines lines;
		Scripting::ScriptRuntime bomb{ "bomb", Scripting::Limits{ 4U * 1024U * 1024U, std::chrono::milliseconds{ 500 } }, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool bombLoaded = bomb.Load("const kept = []; for (;;) kept.push(new Array(4096).fill(7));", "bomb.js", 0.0);
		assert(!bombLoaded);
		assert(bomb.GetFault() == Scripting::Fault::Memory);
	}

	{
		Lines lines;
		Scripting::ScriptRuntime timers{ "timers", Scripting::Limits{}, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool timersLoaded = timers.Load("setTimeout(() => { throw new Error('boom'); }, 0); setTimeout(() => console.log('second'), 0);", "timers.js", 0.0);
		assert(timersLoaded);
		timers.Tick(1.0);
		assert(Mentions(lines, "boom"));
		assert(Contains(lines, "[script:timers] second"));
		assert(timers.GetFault() == Scripting::Fault::None);
	}

	{
		Lines lines;
		Scripting::ScriptRuntime toString{ "tostring", Scripting::Limits{}, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool toStringLoaded = toString.Load("const e = new Error('safe message'); e.toString = () => { throw new Error('toString boom'); }; throw e;", "tostring.js", 0.0);
		assert(!toStringLoaded);
		assert(toString.GetFault() == Scripting::Fault::Load);
		assert(toString.GetError() == "Error: safe message");
		assert(!Mentions(lines, "toString boom"));
	}

	{
		Lines lines;
		Scripting::ScriptRuntime getter{ "getter", Scripting::Limits{}, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool getterLoaded = getter.Load("const e = new Error('x'); Object.defineProperty(e, 'message', { get() { throw new Error('message boom'); } }); Object.defineProperty(e, 'stack', { get() { throw new Error('stack boom'); } }); throw e;", "getter.js", 0.0);
		assert(!getterLoaded);
		assert(getter.GetFault() == Scripting::Fault::Load);
		assert(getter.GetError() == "Error");
		assert(!Mentions(lines, "boom"));
	}

	{
		Lines lines;
		Scripting::ScriptRuntime rejecting{ "reject", Scripting::Limits{}, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool rejectingLoaded = rejecting.Load("Promise.reject(new Error('async boom'));", "reject.js", 0.0);
		assert(rejectingLoaded);
		assert(rejecting.GetFault() == Scripting::Fault::None);
		assert(Mentions(lines, "unhandled rejection: Error: async boom"));
	}

	{
		Lines lines;
		Scripting::ScriptRuntime stopping{ "stop", Scripting::Limits{}, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool stopLoaded = stopping.Load("setTimeout(() => console.log('once'), 0); setInterval(() => console.log('every'), 10);", "stop.js", 0.0);
		assert(stopLoaded);
		assert(stopping.TimerCount() == 2);
		stopping.Tick(0.005);
		assert(CountOf(lines, "[script:stop] once") == 1);
		assert(CountOf(lines, "[script:stop] every") == 0);
		stopping.Tick(0.02);
		assert(CountOf(lines, "[script:stop] every") == 1);
		stopping.Stop();
		assert(stopping.TimerCount() == 0);
		const auto before = lines.size();
		stopping.Tick(10.0);
		assert(lines.size() == before);
	}

	{
		Lines lines;
		Scripting::ScriptRuntime globals{ "g", Scripting::Limits{}, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool globalsLoaded = globals.Load("console.log([typeof require, typeof std, typeof os, typeof fetch, typeof XMLHttpRequest].join(' '))", "g.js", 0.0);
		assert(globalsLoaded);
		assert(Contains(lines, "[script:g] undefined undefined undefined undefined undefined"));
	}

	{
		Lines lines;
		Scripting::ScriptRuntime capped{ "cap", Scripting::Limits{}, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool cappedLoaded = capped.Load("let n = 0; try { for (;;) { setTimeout(() => {}, 1000); ++n; } } catch (e) { console.log('timers', n); } for (let i = 0; i < 500; ++i) console.log('spam'); ", "cap.js", 0.0);
		assert(cappedLoaded);
		assert(Contains(lines, "[script:cap] timers 1024"));
		assert(capped.TimerCount() == 1024);
		assert(CountOf(lines, "[script:cap] spam") == 99);
	}

	{
		Scripting::ScriptRuntime metered{ "metered", Scripting::Limits{ 16U * 1024U * 1024U, std::chrono::seconds{ 10 } }, [](std::string_view) {} };
		assert(metered.BusySeconds() == 0.0);
		assert(metered.Load("let n = 0; for (let i = 0; i < 300000; ++i) n += i;", "metered.js", 0.0));
		assert(metered.BusySeconds() > 0.0);
	}

	{
		Lines lines;
		Scripting::ScriptRuntime wide{ "wide", Scripting::Limits{}, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool wideLoaded = wide.Load("console.log('y'.repeat(5000))", "wide.js", 0.0);
		assert(wideLoaded);
		assert(lines.size() == 1 && lines.front().size() == std::string_view{ "[script:wide] " }.size() + 1024);
	}

	{
		Lines lines;
		const Scripting::Limits limits{ 16U * 1024U * 1024U, std::chrono::milliseconds{ 250 } };
		Scripting::ScriptRuntime busy{ "busy", limits, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool busyLoaded = busy.Load("for (let i = 0; i < 3; ++i) setTimeout(() => { const end = Date.now() + 150; while (Date.now() < end) {} console.log('fired'); }, 0);", "busy.js", 0.0);
		assert(busyLoaded);
		busy.Tick(1.0);
		assert(CountOf(lines, "[script:busy] fired") < 3);
		busy.Tick(1.0);
		busy.Tick(1.0);
		assert(CountOf(lines, "[script:busy] fired") == 3);
		assert(busy.GetFault() == Scripting::Fault::None);
	}

	{
		Lines lines;
		Scripting::ScriptRuntime deep{ "deep", Scripting::Limits{}, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool deepLoaded = deep.Load("function f(){ return f() + 1 } f()", "deep.js", 0.0);
		assert(!deepLoaded);
		assert(deep.GetFault() == Scripting::Fault::Load);
		const bool deepAgain = deep.Load("console.log('after')", "deep2.js", 0.0);
		assert(!deepAgain);
		deep.Tick(1.0);
		assert(!Mentions(lines, "after"));
	}

	{
		Lines lines;
		Scripting::ScriptRuntime chain{ "chain", Scripting::Limits{}, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool chainLoaded = chain.Load("const f = () => Promise.resolve().then(f); f();", "chain.js", 0.0);
		assert(!chainLoaded);
		assert(chain.GetFault() == Scripting::Fault::CpuBudget || chain.GetFault() == Scripting::Fault::Memory);
		assert(chain.GetError() == "CPU budget exceeded" || chain.GetError() == "memory limit exceeded" || chain.GetError() == "too many pending jobs");
	}

	{
		Lines lines;
		Scripting::ScriptRuntime bounded{ "bounded", Scripting::Limits{ 16U * 1024U * 1024U, std::chrono::milliseconds{ 2000 } }, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool boundedLoaded = bounded.Load("let n = 0; const f = () => { if (++n < 5000) Promise.resolve().then(f); }; f();", "bounded.js", 0.0);
		assert(boundedLoaded);
		assert(bounded.GetFault() == Scripting::Fault::None);
	}

	{
		Lines lines;
		Scripting::ScriptRuntime drainCap{ "drain", Scripting::Limits{ 16U * 1024U * 1024U, std::chrono::milliseconds{ 300000 } }, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool drainCapLoaded = drainCap.Load("let n = 0; const f = () => { if (++n < 100005) Promise.resolve().then(f); }; f();", "drain.js", 0.0);
		assert(!drainCapLoaded);
		assert(drainCap.GetFault() == Scripting::Fault::CpuBudget);
		assert(drainCap.GetError() == "too many pending jobs");
	}

	{
		Lines lines;
		Scripting::ScriptRuntime oomJob{ "oomjob", Scripting::Limits{ 16U * 1024U * 1024U, std::chrono::milliseconds{ 1000 } }, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool oomJobLoaded = oomJob.Load("Promise.resolve().then(() => { const a = []; for (;;) a.push(new Array(1e6).fill(0)); });", "oomjob.js", 0.0);
		assert(!oomJobLoaded);
		assert(oomJob.GetFault() == Scripting::Fault::Memory);
		assert(oomJob.GetError() == "memory limit exceeded");
	}

	{
		Lines lines;
		Scripting::ScriptRuntime oomAsync{ "oomasync", Scripting::Limits{ 16U * 1024U * 1024U, std::chrono::milliseconds{ 1000 } }, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool oomAsyncLoaded = oomAsync.Load("async function f() { await 0; const a = []; for (;;) a.push(new Array(1e6).fill(0)); } f();", "oomasync.js", 0.0);
		assert(!oomAsyncLoaded);
		assert(oomAsync.GetFault() == Scripting::Fault::Memory);
	}

	{
		Lines lines;
		Scripting::ScriptRuntime oomCaught{ "oomcaught", Scripting::Limits{ 16U * 1024U * 1024U, std::chrono::milliseconds{ 1000 } }, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool oomCaughtLoaded = oomCaught.Load("try { const a = []; for (;;) a.push(new Array(1e6).fill(0)); } catch (e) {}", "oomcaught.js", 0.0);
		assert(!oomCaughtLoaded);
		assert(oomCaught.GetFault() == Scripting::Fault::Memory);
	}

	{
		Lines lines;
		Scripting::ScriptRuntime oomTimer{ "oomtimer", Scripting::Limits{ 16U * 1024U * 1024U, std::chrono::milliseconds{ 1000 } }, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool oomTimerLoaded = oomTimer.Load("setTimeout(() => Promise.resolve().then(() => { const a = []; for (;;) a.push(new Array(1e6).fill(0)); }), 0);", "oomtimer.js", 0.0);
		assert(oomTimerLoaded);
		assert(oomTimer.GetFault() == Scripting::Fault::None);
		oomTimer.Tick(1.0);
		assert(oomTimer.GetFault() == Scripting::Fault::Memory);
		assert(oomTimer.TimerCount() == 0);
	}

	{
		Lines lines;
		Scripting::ScriptRuntime bounded{ "bounded", Scripting::Limits{}, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool boundedLoaded = bounded.Load("let n = 0; const f = () => { if (++n < 100) { const end = Date.now() + 5; while (Date.now() < end) {} Promise.resolve().then(f); } }; f();", "bounded.js", 0.0);
		assert(!boundedLoaded);
		assert(bounded.GetFault() == Scripting::Fault::CpuBudget);
		assert(bounded.GetError() == "CPU budget exceeded");
	}

	{
		Lines lines;
		Scripting::ScriptRuntime waiting{ "wait", Scripting::Limits{}, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool waitLoaded = waiting.Load("Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0)", "wait.js", 0.0);
		assert(waitLoaded || waiting.GetFault() == Scripting::Fault::Load);
	}

	{
		Lines lines;
		Scripting::ScriptRuntime plain{ "plain", Scripting::Limits{}, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool plainLoaded = plain.Load("throw { toString() { return new Uint8Array(2**31).length + '' } }", "plain.js", 0.0);
		assert(!plainLoaded);
		assert(plain.GetFault() == Scripting::Fault::Load);
		assert(plain.GetError() == "non-error exception");
		assert(Contains(lines, "[script:plain] non-error exception"));
	}

	{
		Lines lines;
		Scripting::ScriptRuntime longError{ "long", Scripting::Limits{}, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool longLoaded = longError.Load("throw new Error('x'.repeat(100000))", "long.js", 0.0);
		assert(!longLoaded);
		assert(longError.GetError().size() <= 1024);
		assert(Mentions(lines, "[script:long] Error: xxx"));
	}

	{
		Lines lines;
		Scripting::ScriptRuntime heap{ "heap", Scripting::Limits{ 4U * 1024U * 1024U, std::chrono::milliseconds{ 2000 } }, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool heapLoaded = heap.Load("const kept = []; for (;;) kept.push({});", "heap.js", 0.0);
		assert(!heapLoaded);
		assert(heap.GetFault() == Scripting::Fault::Memory);
	}

	{
		Lines lines;
		Scripting::ScriptRuntime forged{ "forge", Scripting::Limits{}, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool forgedLoaded = forged.Load(R"(console.log('a\nb\rc'))", "forge.js", 0.0);
		assert(forgedLoaded);
		assert(Contains(lines, "[script:forge] a b c"));
	}

	{
		Lines lines;
		Scripting::ScriptRuntime utf{ "utf", Scripting::Limits{}, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool utfLoaded = utf.Load(R"(console.log('a'.repeat(1023) + '\u00e9'.repeat(4)))", "utf.js", 0.0);
		assert(utfLoaded);
		assert(lines.size() == 1 && lines.front().size() == std::string_view{ "[script:utf] " }.size() + 1023);
		assert(lines.front().back() == 'a');
	}

	{
		Lines lines;
		Scripting::ScriptRuntime cruel{ "cruel", Scripting::Limits{}, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool cruelLoaded = cruel.Load("throw new Error('one\\ntwo')", "cruel.js", 0.0);
		assert(!cruelLoaded);
		assert(cruel.GetError() == "Error: one two");
	}

	{
		Lines lines;
		Scripting::ScriptRuntime reentrant{ "reentry", Scripting::Limits{ 16U * 1024U * 1024U, std::chrono::milliseconds{ 500 } }, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool reentryLoaded = reentrant.Load("var depth = 0; function target() { ++depth; const nested = __enterNested(); return nested === -1 ? -depth : nested; }", "reentry.js", 0.0);
		assert(reentryLoaded);
		InstallNested(reentrant);
		JSContext* context = reentrant.Context();
		JSValue global = JS_GetGlobalObject(context);
		JSValue target = JS_GetPropertyStr(context, global, "target");
		JSValue result = JS_UNDEFINED;
		assert(reentrant.Call(target, 0, nullptr, &result));
		assert(reentrant.GetFault() == Scripting::Fault::None);
		JSValue depth = JS_GetPropertyStr(context, global, "depth");
		int32_t reached = 0;
		assert(JS_ToInt32(context, &reached, depth) == 0);
		assert(reached == 8);
		JS_FreeValue(context, depth);
		JS_FreeValue(context, result);
		JS_FreeValue(context, target);
		JS_FreeValue(context, global);
	}

	{
		Lines lines;
		Scripting::ScriptRuntime budget{ "reentry-budget", Scripting::Limits{ 16U * 1024U * 1024U, std::chrono::milliseconds{ 120 } }, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool budgetLoaded = budget.Load("function burn(ms) { const end = Date.now() + ms; while (Date.now() < end) {} } function inner() { return 7; } function target() { burn(80); __enterNested(); burn(80); return 1; }", "reentry-budget.js", 0.0);
		assert(budgetLoaded);
		InstallNested(budget);
		JSContext* context = budget.Context();
		JSValue global = JS_GetGlobalObject(context);
		JSValue target = JS_GetPropertyStr(context, global, "target");
		JSValue result = JS_UNDEFINED;
		assert(!budget.Call(target, 0, nullptr, &result));
		assert(budget.GetFault() == Scripting::Fault::CpuBudget);
		JS_FreeValue(context, result);
		JS_FreeValue(context, target);
		JS_FreeValue(context, global);
	}

	{
		Lines lines;
		Scripting::ScriptRuntime outer{ "drain", Scripting::Limits{ 16U * 1024U * 1024U, std::chrono::milliseconds{ 500 } }, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
		const bool outerLoaded = outer.Load("function inner() { console.log('inner'); } function target() { Promise.resolve().then(() => console.log('late')); __enterNested(); console.log('outer-end'); }", "drain.js", 0.0);
		assert(outerLoaded);
		InstallNested(outer);
		JSContext* context = outer.Context();
		JSValue global = JS_GetGlobalObject(context);
		JSValue target = JS_GetPropertyStr(context, global, "target");
		JSValue result = JS_UNDEFINED;
		assert(outer.Call(target, 0, nullptr, &result));
		assert(outer.GetFault() == Scripting::Fault::None);
		const auto indexOf = [&lines](const std::string& a_text) {
			for (std::size_t i = 0; i < lines.size(); ++i) {
				if (lines[i] == a_text) {
					return i;
				}
			}
			return lines.size();
		};
		const std::size_t inner = indexOf("[script:drain] inner");
		const std::size_t outerEnd = indexOf("[script:drain] outer-end");
		const std::size_t late = indexOf("[script:drain] late");
		assert(inner < outerEnd);
		assert(outerEnd < late);
		assert(late < lines.size());
		JS_FreeValue(context, result);
		JS_FreeValue(context, target);
		JS_FreeValue(context, global);
	}

	WriteFile(root / "outside" / "index.js", "console.log('outside');");
	WriteFile(root / "linked" / "co-resource.json", R"({"name":"linked","version":"1.0.0","server":["dist/index.js"]})");
	std::error_code linkError;
	std::filesystem::create_directory_symlink(root / "outside", root / "linked" / "dist", linkError);
	WriteFile(root / "outside" / "manifest.json", R"({"name":"manifestlink","version":"1.0.0","server":[]})");
	std::filesystem::create_directories(root / "manifestlink");
	std::error_code manifestLinkError;
	std::filesystem::create_symlink(root / "outside" / "manifest.json", root / "manifestlink" / "co-resource.json", manifestLinkError);
	WriteFile(root / "bounded.txt", "0123456789abcdef");
	assert(Scripting::ReadBoundedText(root / "bounded.txt", 16U, "bounded.txt").size() == 16U);
	bool boundedRefused{};
	try {
		static_cast<void>(Scripting::ReadBoundedText(root / "bounded.txt", 15U, "bounded.txt"));
	} catch (const std::invalid_argument& a_error) {
		boundedRefused = std::string_view{ a_error.what() }.find("larger than 15 bytes") != std::string_view::npos;
	}
	assert(boundedRefused);
	WriteFile(root / "empty" / "co-resource.json", R"({"name":"empty","version":"1.0.0","server":[]})");
	WriteFile(root / "board" / "co-resource.json", R"({"name":"board","version":"1.0.0","server":[],"views":["board.html","web/board.js"]})");
	WriteFile(root / "badview" / "co-resource.json", R"({"name":"badview","version":"1.0.0","server":[],"views":["../board.html"]})");
	WriteFile(root / "escape" / "co-resource.json", R"({"name":"escape","version":"1.0.0","server":["../outside.js"]})");
	WriteFile(root / "gated" / "co-resource.json", R"({"name":"gated","version":"1.0.0","server":["dist/index.js"],"permissions":["chat.send"]})");
	WriteFile(root / "gated" / "dist" / "index.js", "console.log('gated-started');");
	WriteFile(root / "hot" / "co-resource.json", R"({"name":"hot","version":"1.0.0","server":["dist/index.js"]})");
	WriteFile(root / "hot" / "dist" / "index.js", "console.log('v1');");
	WriteFile(root / "base" / "co-resource.json", R"({"name":"base","version":"1.0.0","server":[]})");
	WriteFile(root / "user" / "co-resource.json", R"({"name":"user","version":"1.0.0","server":[],"dependencies":["base"]})");
	WriteFile(root / "dot" / "co-resource.json", R"({"name":"dot","version":"1.0.0","server":["."]})");
	WriteFile(root / "big" / "co-resource.json", R"({"name":"big","version":"1.0.0","server":["dist/index.js"]})");
	WriteFile(root / "big" / "dist" / "index.js", std::string(1024U * 1024U + 1U, ' '));
	WriteFile(root / "bigsum" / "co-resource.json", R"({"name":"bigsum","version":"1.0.0","server":["a.js","a.js","a.js","a.js","a.js","a.js","a.js","a.js","a.js"]})");
	WriteFile(root / "bigsum" / "a.js", std::string(1024U * 1024U, ' '));
	WriteFile(root / "bigmf" / "co-resource.json", R"({"name":"bigmf","version":"1.0.0","server":[],"pad":")" + std::string(64U * 1024U, 'a') + R"("})");
	WriteFile(root / "a.b" / "co-resource.json", R"({"name":"a.b","version":"1.0.0","server":[]})");

	Lines lines;
	Scripting::ResourceManager manager{ root, Scripting::Limits{}, [&lines](std::string_view a_line) { lines.emplace_back(a_line); } };
	manager.Tick(0.0);
	assert(StateOf(manager, "empty") == "pending");
	assert(StateOf(manager, "board") == "pending");
	assert(StateOf(manager, "user") == "pending");
	assert(manager.Approve("user") == "waiting for dependencies");
	assert(StateOf(manager, "user") == "pending");
	assert(DetailOf(manager, "user") == "waiting for dependencies");
	assert(manager.Approve("base").empty());
	assert(StateOf(manager, "user") == "running");
	for (const char* name : { "empty", "board", "hot" }) {
		assert(manager.Approve(name).empty());
	}
	assert(StateOf(manager, "empty") == "running");
	assert(StateOf(manager, "board") == "running");
	assert(StateOf(manager, "badview") == "invalid");
	assert(DetailOf(manager, "badview").find("not a resource-relative view path") != std::string::npos);
	assert(linkError || StateOf(manager, "linked") == "invalid");
	assert(manifestLinkError || StateOf(manager, "manifestlink") == "invalid");
	assert(manifestLinkError || DetailOf(manager, "manifestlink").find("escapes the resource") != std::string::npos);
	assert(!Mentions(lines, "outside"));
	assert(StateOf(manager, "escape") == "invalid");
	const auto escapeEnsure = manager.Ensure("escape");
	assert(!escapeEnsure.empty());
	assert(StateOf(manager, "dot") == "invalid");
	assert(DetailOf(manager, "dot").find("not a regular file") != std::string::npos);
	assert(StateOf(manager, "big") == "invalid");
	assert(DetailOf(manager, "big").find("larger than") != std::string::npos);
	assert(StateOf(manager, "bigsum") == "invalid");
	assert(DetailOf(manager, "bigsum").find("in total") != std::string::npos);
	assert(StateOf(manager, "bigmf") == "invalid");
	assert(DetailOf(manager, "bigmf").find("larger than") != std::string::npos);
	assert(StateOf(manager, "a.b") == "invalid");
	assert(DetailOf(manager, "a.b").find("not a valid resource name") != std::string::npos);
	assert(StateOf(manager, "gated") == "pending");
	assert(!Mentions(lines, "gated-started"));
	const auto approved = manager.Approve("gated");
	assert(approved.empty());
	assert(StateOf(manager, "gated") == "running");
	assert(Contains(lines, "[script:gated] gated-started"));
	assert(std::filesystem::exists(root / "approvals.json"));
	assert(Contains(lines, "[script:hot] v1"));
	WriteFile(root / "hot" / "dist" / "index.js", "console.log('v2');");
	manager.Refresh();
	assert(StateOf(manager, "hot") == "pending");
	assert(DetailOf(manager, "hot") == "code changed since approval");
	assert(!Mentions(lines, "[script:hot] v2"));
	assert(manager.Approve("hot").empty());
	assert(Contains(lines, "[script:hot] v2"));
	assert(StateOf(manager, "hot") == "running");
	assert(StateOf(manager, "user") == "running");
	const auto baseStop = manager.Stop("base");
	assert(baseStop.empty());
	assert(StateOf(manager, "user") == "pending");
	assert(StateOf(manager, "base") == "stopped");
	const auto baseEnsure = manager.Ensure("base");
	assert(baseEnsure.empty());
	assert(StateOf(manager, "user") == "running");
	const auto missingStop = manager.Stop("missing");
	assert(missingStop == "unknown resource");
	assert(CountOf(lines, "[script:gated] gated-started") == 1);

	CommonwealthOnline::Server::AdminCommandHooks hooks;
	Scripting::BindAdminHooks(manager, hooks);
	const auto resourceList = hooks.resources();
	assert(resourceList.is_array());
	const auto missingEnsure = hooks.ensureResource("missing");
	assert(!missingEnsure.ok);
	const auto gatedApprove = hooks.approveResource("gated");
	assert(gatedApprove.ok);
	const auto refreshed = hooks.refreshResources();
	assert(refreshed.ok);
	const auto gatedHookStop = hooks.stopResource("gated");
	assert(gatedHookStop.ok);

	{
		const auto permRoot = root / "perm-root";
		WriteFile(permRoot / "perm" / "co-resource.json", R"({"name":"perm","version":"1.0.0","server":["dist/index.js"],"permissions":["chat.send"]})");
		WriteFile(permRoot / "perm" / "dist" / "index.js", "console.log('perm-started');");
		Lines permLines;
		Scripting::ResourceManager perms{ permRoot, Scripting::Limits{}, [&permLines](std::string_view a_line) { permLines.emplace_back(a_line); } };
		perms.Tick(0.0);
		assert(StateOf(perms, "perm") == "pending");
		const auto permApprove = perms.Approve("perm");
		assert(permApprove.empty());
		assert(StateOf(perms, "perm") == "running");
		assert(std::filesystem::exists(permRoot / "approvals.json"));
		assert(!std::filesystem::exists(permRoot / "approvals.json.tmp"));
		WriteFile(permRoot / "approvals.json", "{}");
		perms.Refresh();
		assert(StateOf(perms, "perm") == "pending");
		const auto permReapprove = perms.Approve("perm");
		assert(permReapprove.empty());
		assert(StateOf(perms, "perm") == "running");
		std::filesystem::remove(permRoot / "approvals.json");
		perms.Refresh();
		assert(StateOf(perms, "perm") == "pending");
		assert(CountOf(permLines, "[script:perm] perm-started") == 2);
	}

	{
		const auto codeRoot = root / "code-root";
		WriteFile(codeRoot / "approvals.json", R"({"legacy":["chat.send"]})");
		WriteFile(codeRoot / "codechg" / "co-resource.json", R"({"name":"codechg","version":"1.0.0","server":["dist/index.js"],"permissions":["chat.send"]})");
		WriteFile(codeRoot / "codechg" / "dist" / "index.js", "console.log('codechg-v1');");
		WriteFile(codeRoot / "split" / "co-resource.json", R"({"name":"split","version":"1.0.0","server":["a.js","b.js"],"permissions":["chat.send"]})");
		WriteFile(codeRoot / "split" / "a.js", "console.log('one');");
		WriteFile(codeRoot / "split" / "b.js", "console.log('two');");
		WriteFile(codeRoot / "legacy" / "co-resource.json", R"({"name":"legacy","version":"1.0.0","server":["dist/index.js"],"permissions":["chat.send"]})");
		WriteFile(codeRoot / "legacy" / "dist" / "index.js", "console.log('legacy-started');");
		WriteFile(codeRoot / "free" / "co-resource.json", R"({"name":"free","version":"1.0.0","server":["dist/index.js"]})");
		WriteFile(codeRoot / "free" / "dist" / "index.js", "console.log('free-started');");
		Lines codeLines;
		Scripting::ResourceManager guard{ codeRoot, Scripting::Limits{}, [&codeLines](std::string_view a_line) { codeLines.emplace_back(a_line); } };
		const auto approvedOf = [&guard](std::string_view a_name) {
			for (const auto& item : guard.List()) {
				if (item.at("name") == std::string{ a_name }) {
					return item.at("approved").get<bool>();
				}
			}
			return false;
		};
		guard.Tick(0.0);
		assert(StateOf(guard, "free") == "pending");
		assert(DetailOf(guard, "free") == "awaiting approval");
		assert(!approvedOf("free"));
		assert(!Mentions(codeLines, "free-started"));
		guard.Refresh();
		assert(CountOf(codeLines, "[scripts] free awaiting approval") == 1);
		assert(guard.Approve("free").empty());
		assert(StateOf(guard, "free") == "running");
		assert(approvedOf("free"));
		assert(Contains(codeLines, "[script:free] free-started"));
		assert(StateOf(guard, "codechg") == "pending");
		assert(DetailOf(guard, "codechg") == "awaiting approval");
		assert(Mentions(codeLines, "approval ignored for legacy"));
		assert(StateOf(guard, "legacy") == "pending");
		assert(!Mentions(codeLines, "legacy-started"));
		assert(guard.Approve("codechg").empty());
		assert(guard.Approve("split").empty());
		assert(guard.Approve("legacy").empty());
		assert(StateOf(guard, "codechg") == "running");
		assert(StateOf(guard, "split") == "running");
		assert(StateOf(guard, "legacy") == "running");
		assert(Contains(codeLines, "[script:codechg] codechg-v1"));
		const auto stored = [&codeRoot] {
			std::ifstream storedFile(codeRoot / "approvals.json");
			return nlohmann::json::parse(storedFile);
		}();
		assert(stored.at("codechg").at("permissions") == nlohmann::json::array({ "chat.send" }));
		assert(stored.at("codechg").at("digest").get<std::string>().size() == 64U);
		assert(stored.at("free").at("permissions") == nlohmann::json::array());
		assert(stored.at("free").at("digest").get<std::string>().size() == 64U);

		WriteFile(codeRoot / "codechg" / "dist" / "index.js", "console.log('codechg-v2');");
		guard.Refresh();
		assert(StateOf(guard, "codechg") == "pending");
		assert(DetailOf(guard, "codechg") == "code changed since approval");
		assert(!approvedOf("codechg"));
		assert(!Mentions(codeLines, "codechg-v2"));
		assert(guard.Approve("codechg").empty());
		assert(StateOf(guard, "codechg") == "running");
		assert(Contains(codeLines, "[script:codechg] codechg-v2"));

		WriteFile(codeRoot / "split" / "a.js", "console.log('one');console.log('two');");
		WriteFile(codeRoot / "split" / "b.js", "");
		guard.Refresh();
		assert(StateOf(guard, "split") == "pending");
		assert(DetailOf(guard, "split") == "code changed since approval");
		assert(guard.Approve("split").empty());
		assert(CountOf(codeLines, "[script:split] one") == 2);

		WriteFile(codeRoot / "codechg" / "co-resource.json", R"({"name":"codechg","version":"1.0.1","server":["dist/index.js"],"permissions":["chat.send"]})");
		guard.Refresh();
		assert(StateOf(guard, "codechg") == "pending");
		assert(DetailOf(guard, "codechg") == "code changed since approval");
		assert(StateOf(guard, "free") == "running");
		WriteFile(codeRoot / "free" / "dist" / "index.js", "console.log('free-v2');");
		guard.Refresh();
		assert(StateOf(guard, "free") == "pending");
		assert(DetailOf(guard, "free") == "code changed since approval");
		assert(!Mentions(codeLines, "free-v2"));
		assert(CountOf(codeLines, "[scripts] free code changed since approval") == 1);
		guard.Refresh();
		assert(CountOf(codeLines, "[scripts] free code changed since approval") == 1);
		assert(guard.Approve("free").empty());
		assert(StateOf(guard, "free") == "running");
		assert(Contains(codeLines, "[script:free] free-v2"));
		Lines restartedLines;
		Scripting::ResourceManager restarted{ codeRoot, Scripting::Limits{}, [&restartedLines](std::string_view a_line) { restartedLines.emplace_back(a_line); } };
		restarted.Tick(0.0);
		assert(StateOf(restarted, "free") == "running");
		assert(Contains(restartedLines, "[script:free] free-v2"));
	}

	{
		const auto malformedRoot = root / "malformed-root";
		WriteFile(malformedRoot / "good" / "co-resource.json", R"({"name":"good","version":"1.0.0","server":["dist/index.js"],"permissions":["chat.send"]})");
		WriteFile(malformedRoot / "good" / "dist" / "index.js", "console.log('good-started');");
		WriteFile(malformedRoot / "bad" / "co-resource.json", R"({"name":"bad","version":"1.0.0","server":["dist/index.js"],"permissions":["chat.send"]})");
		WriteFile(malformedRoot / "bad" / "dist" / "index.js", "console.log('bad-started');");
		{
			Lines firstLines;
			Scripting::ResourceManager first{ malformedRoot, Scripting::Limits{}, [&firstLines](std::string_view a_line) { firstLines.emplace_back(a_line); } };
			first.Tick(0.0);
			assert(first.Approve("good").empty());
			assert(first.Approve("bad").empty());
		}
		auto document = [&malformedRoot] {
			std::ifstream storedFile(malformedRoot / "approvals.json");
			return nlohmann::json::parse(storedFile);
		}();
		document["bad"]["permissions"] = nlohmann::json::array({ 1 });
		WriteFile(malformedRoot / "approvals.json", document.dump());
		Lines malformedLines;
		Scripting::ResourceManager guard{ malformedRoot, Scripting::Limits{}, [&malformedLines](std::string_view a_line) { malformedLines.emplace_back(a_line); } };
		guard.Tick(0.0);
		assert(Mentions(malformedLines, "approval ignored for bad: malformed entry"));
		assert(StateOf(guard, "good") == "running");
		assert(Contains(malformedLines, "[script:good] good-started"));
		assert(StateOf(guard, "bad") == "pending");
		assert(!Mentions(malformedLines, "bad-started"));
	}

	{
		const Scripting::Limits limits{ 16U * 1024U * 1024U, std::chrono::milliseconds{ 250 } };
		Lines turnLines;
		WriteFile(root / "turns" / "spin-a" / "co-resource.json", R"({"name":"spin-a","version":"1.0.0","server":["dist/index.js"]})");
		WriteFile(root / "turns" / "spin-a" / "dist" / "index.js", "setInterval(() => { const end = Date.now() + 150; while (Date.now() < end) {} console.log('A'); }, 0);");
		WriteFile(root / "turns" / "spin-b" / "co-resource.json", R"({"name":"spin-b","version":"1.0.0","server":["dist/index.js"]})");
		WriteFile(root / "turns" / "spin-b" / "dist" / "index.js", "setInterval(() => { const end = Date.now() + 150; while (Date.now() < end) {} console.log('B'); }, 0);");
		WriteFile(root / "turns" / "spin-c" / "co-resource.json", R"({"name":"spin-c","version":"1.0.0","server":["dist/index.js"]})");
		WriteFile(root / "turns" / "spin-c" / "dist" / "index.js", "setInterval(() => { const end = Date.now() + 150; while (Date.now() < end) {} console.log('C'); }, 0);");
		Scripting::ResourceManager turns{ root / "turns", limits, [&turnLines](std::string_view a_line) { turnLines.emplace_back(a_line); } };
		turns.Refresh();
		assert(turns.Approve("spin-a").empty());
		assert(turns.Approve("spin-b").empty());
		assert(turns.Approve("spin-c").empty());
		turns.Tick(0.0);
		for (double now = 1.0; now <= 6.0; now += 1.0) {
			turns.Tick(now);
		}
		assert(CountOf(turnLines, "[script:spin-a] A") >= 1);
		assert(CountOf(turnLines, "[script:spin-b] B") >= 1);
		assert(CountOf(turnLines, "[script:spin-c] C") >= 1);
	}

	{
		Lines crashLines;
		WriteFile(root / "crash" / "crashy" / "co-resource.json", R"({"name":"crashy","version":"1.0.0","server":["dist/index.js"],"errorBehavior":"restart"})");
		WriteFile(root / "crash" / "crashy" / "dist" / "index.js", "setInterval(() => { while (true) {} }, 0);");
		WriteFile(root / "stop" / "stopper" / "co-resource.json", R"({"name":"stopper","version":"1.0.0","server":["dist/index.js"]})");
		WriteFile(root / "stop" / "stopper" / "dist" / "index.js", "setInterval(() => { while (true) {} }, 0);");
		Scripting::ResourceManager stopper{ root / "stop", Scripting::Limits{}, [&crashLines](std::string_view a_line) { crashLines.emplace_back(a_line); } };
		stopper.Refresh();
		assert(stopper.Approve("stopper").empty());
		stopper.Tick(0.0);
		assert(StateOf(stopper, "stopper") == "running");
		stopper.Tick(1.0);
		assert(StateOf(stopper, "stopper") == "failed");
		Scripting::ResourceManager crash{ root / "crash", Scripting::Limits{}, [&crashLines](std::string_view a_line) { crashLines.emplace_back(a_line); } };
		crash.Refresh();
		assert(crash.Approve("crashy").empty());
		crash.Tick(0.0);
		assert(StateOf(crash, "crashy") == "running");
		crash.Tick(1.0);
		assert(StateOf(crash, "crashy") == "running");
		crash.Tick(2.0);
		assert(StateOf(crash, "crashy") == "running");
		crash.Tick(3.0);
		assert(StateOf(crash, "crashy") == "running");
		crash.Tick(4.0);
		assert(StateOf(crash, "crashy") == "failed");
		assert(Mentions(crashLines, "CPU budget exceeded"));
	}

	{
		Lines windowLines;
		Scripting::Limits windowed{};
		windowed.restartWindowSeconds = 10.0;
		WriteFile(root / "window-a" / "windowed" / "co-resource.json", R"({"name":"windowed","version":"1.0.0","server":["dist/index.js"],"errorBehavior":"restart"})");
		WriteFile(root / "window-a" / "windowed" / "dist" / "index.js", "setInterval(() => { while (true) {} }, 0);");
		Scripting::ResourceManager burst{ root / "window-a", windowed, [&windowLines](std::string_view a_line) { windowLines.emplace_back(a_line); } };
		burst.Refresh();
		assert(burst.Approve("windowed").empty());
		burst.Tick(0.0);
		for (int second = 1; second <= 3; ++second) {
			burst.Tick(second);
		}
		assert(StateOf(burst, "windowed") == "running");
		burst.Tick(4.0);
		assert(StateOf(burst, "windowed") == "failed");
		const auto ensured = burst.Ensure("windowed");
		assert(ensured.empty());
		assert(StateOf(burst, "windowed") == "running");
		for (int second = 5; second <= 7; ++second) {
			burst.Tick(second);
			assert(StateOf(burst, "windowed") == "running");
		}
		burst.Tick(8.0);
		assert(StateOf(burst, "windowed") == "failed");

		WriteFile(root / "window-b" / "spread" / "co-resource.json", R"({"name":"spread","version":"1.0.0","server":["dist/index.js"],"errorBehavior":"restart"})");
		WriteFile(root / "window-b" / "spread" / "dist" / "index.js", "setInterval(() => { while (true) {} }, 0);");
		Scripting::ResourceManager spread{ root / "window-b", windowed, [&windowLines](std::string_view a_line) { windowLines.emplace_back(a_line); } };
		spread.Refresh();
		assert(spread.Approve("spread").empty());
		spread.Tick(0.0);
		for (int second = 1; second <= 3; ++second) {
			spread.Tick(second);
		}
		assert(StateOf(spread, "spread") == "running");
		spread.Tick(20.0);
		assert(StateOf(spread, "spread") == "running");
		spread.Tick(21.0);
		spread.Tick(22.0);
		assert(StateOf(spread, "spread") == "running");
		spread.Tick(23.0);
		assert(StateOf(spread, "spread") == "failed");
	}

	{
		Lines shareLines;
		FakeCpu heavyCpu{ 0.0, 0.8 };
		WriteFile(root / "share-heavy" / "heavy" / "co-resource.json", R"({"name":"heavy","version":"1.0.0","server":["dist/index.js"]})");
		WriteFile(root / "share-heavy" / "heavy" / "dist" / "index.js", "console.log('heavy-started');");
		Scripting::ResourceManager heavy{ root / "share-heavy", Scripting::Limits{}, [&shareLines](std::string_view a_line) { shareLines.emplace_back(a_line); }, {}, [&heavyCpu](const Scripting::ScriptRuntime&) { return heavyCpu.Sample(); } };
		heavy.Refresh();
		assert(heavy.Approve("heavy").empty());
		heavy.Tick(0.0);
		for (int second = 1; second <= 4; ++second) {
			heavy.Tick(second);
		}
		assert(StateOf(heavy, "heavy") == "running");
		heavy.Tick(5.0);
		assert(StateOf(heavy, "heavy") == "failed");
		assert(DetailOf(heavy, "heavy").find("sustained CPU use") == 0);
	}

	{
		Lines lightLines;
		FakeCpu lightCpu{ 0.0, 0.1 };
		WriteFile(root / "share-light" / "light" / "co-resource.json", R"({"name":"light","version":"1.0.0","server":["dist/index.js"]})");
		WriteFile(root / "share-light" / "light" / "dist" / "index.js", "console.log('light-started');");
		Scripting::ResourceManager light{ root / "share-light", Scripting::Limits{}, [&lightLines](std::string_view a_line) { lightLines.emplace_back(a_line); }, {}, [&lightCpu](const Scripting::ScriptRuntime&) { return lightCpu.Sample(); } };
		light.Refresh();
		assert(light.Approve("light").empty());
		for (int second = 0; second <= 30; ++second) {
			light.Tick(second);
		}
		assert(StateOf(light, "light") == "running");
	}

	{
		Lines hogLines;
		FakeCpu hogCpu{ 0.0, 0.8 };
		WriteFile(root / "share-hog" / "hog" / "co-resource.json", R"({"name":"hog","version":"1.0.0","server":["dist/index.js"],"errorBehavior":"restart"})");
		WriteFile(root / "share-hog" / "hog" / "dist" / "index.js", "console.log('hog-started');");
		Scripting::ResourceManager hog{ root / "share-hog", Scripting::Limits{}, [&hogLines](std::string_view a_line) { hogLines.emplace_back(a_line); }, {}, [&hogCpu](const Scripting::ScriptRuntime&) { return hogCpu.Sample(); } };
		hog.Refresh();
		assert(hog.Approve("hog").empty());
		hog.Tick(0.0);
		for (int second = 1; second <= 19; ++second) {
			hog.Tick(second);
		}
		assert(StateOf(hog, "hog") == "running");
		hog.Tick(20.0);
		assert(StateOf(hog, "hog") == "failed");
		assert(DetailOf(hog, "hog").find("sustained CPU use") == 0);
	}

	std::filesystem::remove_all(root);
	return 0;
}
