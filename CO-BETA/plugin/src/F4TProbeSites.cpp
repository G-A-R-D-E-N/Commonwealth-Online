#include "pch.h"

#include "F4TProbeSites.h"

#include "F4TInlineHook.h"
#include "F4TProbeCapture.h"
#include "F4TProbeRecord.h"
#include "F4TProbeRegistry.h"
#include "F4TProbeVersionGate.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <span>
#include <string>
#include <string_view>

namespace
{
	using F4T::ProbeRegistry::HookPlan;
	using F4T::ProbeRegistry::InstallOutcome;
	using F4T::ProbeRegistry::MaxProbes;
	using F4T::ProbeRegistry::RuntimeSettings;
	using F4T::ProbeRegistry::Spec;
	using F4T::ProbeVersionGate::Runtime;
	using Context = safetyhook::Context;

	constexpr std::uint16_t UnloadInsertId = 2;
	constexpr std::uint16_t CommandExecuteDispatchId = 3;
	constexpr std::uint16_t ConsoleCompileId = 4;
	constexpr std::uint16_t VslotOwnerId = 5;
	constexpr std::uint16_t NgEntry361Id = 6;
	constexpr std::uint16_t TaskUnpackCase4aId = 7;
	constexpr std::uint16_t AeGlobalCallArgId = 8;
	constexpr std::uint16_t HandlerAOgId = 9;
	constexpr std::uint16_t HandlerANgId = 10;
	constexpr std::uint16_t SetVolumeId = 11;
	constexpr std::uint16_t PitchChannelId = 12;
	constexpr std::uint16_t VmBridgeReadId = 13;

	constexpr std::uint32_t StandInFormIdFloor = 0xFF000000;
	constexpr std::uint64_t LogFirstHits = 20;
	constexpr std::uint64_t LogEveryHits = 500;
	constexpr std::size_t MaxRecordedValues = F4T::ProbeRecord::MaxArgs - 1;

	std::array<std::uintptr_t, MaxProbes> g_siteAddress{};
	std::array<std::string_view, MaxProbes> g_names{};
	std::array<std::atomic<std::uint64_t>, MaxProbes> g_hits{};

	std::uintptr_t GameBase() noexcept
	{
		return reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
	}

	std::uint64_t Rva(std::uint64_t a_address) noexcept
	{
		const auto base = GameBase();
		return a_address >= base ? a_address - base : 0;
	}

	__declspec(noinline) bool CopyGuarded(std::uintptr_t a_address, void* a_out, std::size_t a_size) noexcept
	{
		__try {
			std::memcpy(a_out, reinterpret_cast<const void*>(a_address), a_size);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	std::uint64_t Read(std::uintptr_t a_address, std::size_t a_size) noexcept
	{
		std::uint64_t value = 0;
		return CopyGuarded(a_address, &value, a_size) ? value : 0;
	}

	std::uint64_t ReturnRva(const Context& a_ctx) noexcept
	{
		return Rva(Read(a_ctx.rsp, 8));
	}

	bool StandIn(std::uint64_t a_value) noexcept
	{
		return static_cast<std::uint32_t>(a_value) >= StandInFormIdFloor;
	}

	std::string ClassName(std::uintptr_t a_vtable) noexcept
	{
		const auto complete = Read(a_vtable - 8, 8);
		if (complete == 0 || Read(complete, 4) != 1) {
			return {};
		}
		const auto descriptor = GameBase() + Read(complete + 0x0C, 4);
		std::string name;
		for (std::uintptr_t offset = 0x10; offset < 0x10 + 128; ++offset) {
			const auto character = static_cast<char>(Read(descriptor + offset, 1));
			if (character == '\0') {
				break;
			}
			name.push_back(character);
		}
		return name;
	}

	void Observe(
		std::uint16_t a_probeId,
		std::uintptr_t a_vtable,
		std::span<const std::uint64_t> a_values,
		bool a_chain) noexcept
	{
		if (!F4T::ProbeRegistry::ShouldRecord(a_probeId)) {
			return;
		}
		const auto site = g_siteAddress[a_probeId];
		std::array<std::uint64_t, F4T::ProbeRecord::MaxArgs> args{};
		args[0] = Rva(site);
		const auto count = std::min(a_values.size(), MaxRecordedValues);
		std::copy_n(a_values.begin(), count, args.begin() + 1);
		F4T::ProbeCapture::Push(
			a_probeId,
			F4T::ProbeRecord::Kind::Snapshot,
			site,
			std::span<const std::uint64_t>(args.data(), count + 1),
			0);

		const auto hit = g_hits[a_probeId].fetch_add(1, std::memory_order_relaxed) + 1;
		if (hit > LogFirstHits && hit % LogEveryHits != 0) {
			return;
		}
		// TODO-verify(#407)
		try {
			auto line = std::format("Commonwealth Online probe: {} hit {} site RVA 0x{:X}", g_names[a_probeId], hit, args[0]);
			for (std::size_t index = 1; index <= count; ++index) {
				line += std::format(" v{}=0x{:X}", index, args[index]);
			}
			if (a_vtable != 0) {
				line += std::format(" class={}", ClassName(a_vtable));
			}
			if (a_chain) {
				std::array<std::uint64_t, F4T::ProbeCapture::MaxStackChain> chain{};
				const auto frames = F4T::ProbeCapture::CaptureStackChain(GameBase(), chain);
				line += " chain";
				for (std::size_t index = 0; index < frames; ++index) {
					line += std::format(" 0x{:X}", chain[index]);
				}
			}
			REX::INFO("{}", line);
		} catch (...) {
		}
	}

	void UnloadInsertRecord(Context& a_ctx, std::uint64_t a_key, std::uint64_t a_value)
	{
		if (!StandIn(a_key) && !StandIn(a_value)) {
			return;
		}
		const std::array<std::uint64_t, 4> values{ a_ctx.rcx, a_key, a_value, ReturnRva(a_ctx) };
		Observe(UnloadInsertId, 0, values, false);
	}

	void UnloadInsertByValue(Context& a_ctx)
	{
		UnloadInsertRecord(a_ctx, static_cast<std::uint32_t>(a_ctx.rdx), a_ctx.r8);
	}

	void UnloadInsertByPointer(Context& a_ctx)
	{
		UnloadInsertRecord(a_ctx, Read(a_ctx.rdx, 4), Read(a_ctx.r8, 4));
	}

	// TODO-verify(#407)
	void CommandExecuteDispatch(Context& a_ctx)
	{
		const std::array<std::uint64_t, 3> values{ a_ctx.rdi, a_ctx.rax, Rva(Read(a_ctx.rax + 0x20, 8)) };
		Observe(CommandExecuteDispatchId, a_ctx.rax, values, false);
	}

	// TODO-verify(#407)
	void VmBridgeRead(Context& a_ctx)
	{
		const std::array<std::uint64_t, 1> values{ a_ctx.rdi };
		Observe(VmBridgeReadId, Read(a_ctx.rdi, 8), values, false);
	}

	void ConsoleCompile(Context& a_ctx)
	{
		const std::array<std::uint64_t, 4> values{ a_ctx.rcx, a_ctx.rdx, a_ctx.r8, ReturnRva(a_ctx) };
		Observe(ConsoleCompileId, 0, values, true);
	}

	void VslotOwner(Context& a_ctx)
	{
		// Site RVA source:docs/code-notes/re-probe.md
		const auto vtable = Read(a_ctx.rcx, 8);
		const std::array<std::uint64_t, 3> values{ a_ctx.rcx, vtable, Rva(vtable) };
		Observe(VslotOwnerId, vtable, values, false);
	}

	void NgEntry361(Context& a_ctx)
	{
		const std::array<std::uint64_t, 2> values{ a_ctx.rcx, ReturnRva(a_ctx) };
		Observe(NgEntry361Id, 0, values, false);
	}

	void TaskUnpackCase4a(Context& a_ctx)
	{
		const std::array<std::uint64_t, 3> values{
			a_ctx.rcx,
			Read(a_ctx.rcx, 4),
			Read(a_ctx.rcx + 8, 8),
		};
		Observe(TaskUnpackCase4aId, 0, values, false);
	}

	void AeGlobalCallArg(Context& a_ctx)
	{
		const auto vtable = Read(a_ctx.rcx, 8);
		const std::array<std::uint64_t, 2> values{ a_ctx.rcx, Rva(vtable) };
		Observe(AeGlobalCallArgId, vtable, values, false);
	}

	void HandlerAOg(Context& a_ctx)
	{
		const std::array<std::uint64_t, 2> values{ a_ctx.rdi, Read(a_ctx.rdi + 0x266, 1) };
		Observe(HandlerAOgId, 0, values, false);
	}

	void HandlerANg(Context& a_ctx)
	{
		const std::array<std::uint64_t, 2> values{ a_ctx.r14, Read(a_ctx.r14 + 0x2AE, 1) };
		Observe(HandlerANgId, 0, values, false);
	}

	void SetVolumeEntry(Context& a_ctx)
	{
		const std::array<std::uint64_t, 3> values{ a_ctx.rcx, a_ctx.xmm1.u32[0], ReturnRva(a_ctx) };
		Observe(SetVolumeId, 0, values, false);
	}

	void PitchChannelCopy(Context& a_ctx)
	{
		const std::array<std::uint64_t, 2> values{ a_ctx.r9, Read(a_ctx.r9 + 0x18, 4) };
		Observe(PitchChannelId, 0, values, false);
	}

	struct Site
	{
		std::uint16_t probeId;
		Runtime runtime;
		std::uintptr_t rva;
		std::array<std::uint8_t, 16> bytes;
		safetyhook::MidHookFn handler;
	};

	// TODO-verify(#407)
	constexpr auto kSites = std::to_array<Site>({
		{ UnloadInsertId, Runtime::Ae, 0xBE91A0, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56 }, &UnloadInsertByPointer },
		{ UnloadInsertId, Runtime::Ng, 0xB67B00, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56 }, &UnloadInsertByPointer },
		{ UnloadInsertId, Runtime::Og, 0xCD8720, { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x89, 0x54, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57 }, &UnloadInsertByValue },
		{ CommandExecuteDispatchId, Runtime::Og, 0x125F3C3, { 0xFF, 0x50, 0x20, 0xEB, 0x51, 0x48, 0x8D, 0x15, 0x15, 0x6A, 0xB2, 0x01, 0x49, 0x8B, 0xCF, 0xFF }, &CommandExecuteDispatch },
		{ ConsoleCompileId, Runtime::Og, 0x4E29B0, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57 }, &ConsoleCompile },
		{ VslotOwnerId, Runtime::Og, 0x160815C, { 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x08, 0x33, 0xD2, 0x48, 0x8B, 0xCB, 0xE8, 0x64, 0xC3, 0xA1, 0xFE }, &VslotOwner },
		{ VslotOwnerId, Runtime::Ng, 0x11F40EF, { 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x08, 0xFF, 0x15, 0x45, 0xA4, 0x05, 0x01, 0x8B, 0xF0, 0x39, 0x87 }, &VslotOwner },
		{ VslotOwnerId, Runtime::Ae, 0x130E75F, { 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x08, 0xFF, 0x15, 0x3D, 0x1B, 0x13, 0x01, 0x8B, 0xF0, 0x39, 0x87 }, &VslotOwner },
		{ NgEntry361Id, Runtime::Ng, 0xAB6130, { 0x40, 0x55, 0x57, 0x41, 0x56, 0x48, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00, 0x49, 0x8B, 0xF9, 0x49 }, &NgEntry361 },
		{ TaskUnpackCase4aId, Runtime::Ng, 0xBC81AE, { 0x48, 0x8B, 0x59, 0x08, 0x48, 0x85, 0xDB, 0x0F, 0x84, 0x69, 0xF7, 0xFF, 0xFF, 0x48, 0x8B, 0xCB }, &TaskUnpackCase4a },
		{ TaskUnpackCase4aId, Runtime::Ae, 0xC4E1AE, { 0x48, 0x8B, 0x59, 0x08, 0x48, 0x85, 0xDB, 0x0F, 0x84, 0x69, 0xF7, 0xFF, 0xFF, 0x48, 0x8B, 0xCB }, &TaskUnpackCase4a },
		{ AeGlobalCallArgId, Runtime::Ae, 0xC3B000, { 0xE8, 0x2B, 0xB3, 0x11, 0x00, 0x88, 0x03, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3, 0xCC, 0xCC, 0xCC }, &AeGlobalCallArg },
		{ HandlerAOgId, Runtime::Og, 0xB7F1EC, { 0x80, 0xBF, 0x66, 0x02, 0x00, 0x00, 0x00, 0x0F, 0x85, 0xDB, 0x00, 0x00, 0x00, 0x80, 0x3D, 0x28 }, &HandlerAOg },
		{ HandlerANgId, Runtime::Ng, 0xA60B12, { 0x41, 0x80, 0xBE, 0xAE, 0x02, 0x00, 0x00, 0x00, 0x0F, 0x85, 0x16, 0x01, 0x00, 0x00, 0x80, 0x3D }, &HandlerANg },
		{ SetVolumeId, Runtime::Og, 0x1AEE320, { 0x48, 0x83, 0xEC, 0x48, 0x48, 0x8B, 0x09, 0x48, 0x85, 0xC9, 0x74, 0x34, 0x48, 0x8B, 0x01, 0xFF }, &SetVolumeEntry },
		{ SetVolumeId, Runtime::Ng, 0x15298C0, { 0x48, 0x83, 0xEC, 0x48, 0x48, 0x8B, 0x09, 0x48, 0x85, 0xC9, 0x74, 0x34, 0x48, 0x8B, 0x01, 0xFF }, &SetVolumeEntry },
		{ SetVolumeId, Runtime::Ae, 0x1643F00, { 0x48, 0x83, 0xEC, 0x48, 0x48, 0x8B, 0x09, 0x48, 0x85, 0xC9, 0x74, 0x34, 0x48, 0x8B, 0x01, 0xFF }, &SetVolumeEntry },
		{ PitchChannelId, Runtime::Og, 0x162169F, { 0x41, 0x8B, 0x41, 0x18, 0x48, 0x8B, 0x51, 0x10, 0x42, 0x89, 0x04, 0x82, 0xFF, 0xC6, 0x41, 0x3B }, &PitchChannelCopy },
		{ VmBridgeReadId, Runtime::Og, 0x125F396, { 0x48, 0x85, 0xFF, 0x74, 0x2D, 0x48, 0x8B, 0x07, 0x48, 0x8B, 0xCF, 0xFF, 0x50, 0x10, 0x84, 0xC0 }, &VmBridgeRead },
		{ VmBridgeReadId, Runtime::Ng, 0xFB4AC3, { 0x48, 0x85, 0xFF, 0x0F, 0x84, 0x42, 0x05, 0x00, 0x00, 0x48, 0x8B, 0x07, 0x48, 0x8B, 0xCF, 0xFF }, &VmBridgeRead },
		{ VmBridgeReadId, Runtime::Ae, 0x103AAC3, { 0x48, 0x85, 0xFF, 0x0F, 0x84, 0x42, 0x05, 0x00, 0x00, 0x48, 0x8B, 0x07, 0x48, 0x8B, 0xCF, 0xFF }, &VmBridgeRead },
	});

	std::string_view RuntimeName(Runtime a_runtime) noexcept
	{
		switch (a_runtime) {
		case Runtime::Og:
			return "1.10.163 (OG)";
		case Runtime::Ng:
			return "1.10.984 (NG)";
		case Runtime::Ae:
			return "1.11.240 (AE)";
		}
		return "unknown runtime";
	}

	InstallOutcome InstallProbe(const RuntimeSettings& a_settings, HookPlan& a_plan)
	{
		const auto runtime = F4T::ProbeVersionGate::RuntimeOf(a_settings.version);
		if (!runtime) {
			REX::INFO("Commonwealth Online probe: '{}' has no supported runtime, nothing installed", a_settings.name);
			return InstallOutcome::Disabled;
		}
		for (const auto& site : kSites) {
			if (site.probeId != a_settings.probeId || site.runtime != *runtime) {
				continue;
			}
			const auto target = GameBase() + site.rva;
			g_siteAddress[a_settings.probeId] = target;
			g_names[a_settings.probeId] = a_settings.name;
			a_plan.target = target;
			a_plan.expectedPrologue = site.bytes;
			a_plan.midDestination = site.handler;
			return InstallOutcome::Enabled;
		}
		REX::INFO("Commonwealth Online probe: '{}' is not available on {}", a_settings.name, RuntimeName(*runtime));
		return InstallOutcome::Disabled;
	}

	constexpr std::array<Spec, 12> kSpecs{{
		{ UnloadInsertId, "res408-unload-insert", &InstallProbe },
		{ CommandExecuteDispatchId, "res408-command-execute-dispatch", &InstallProbe },
		{ ConsoleCompileId, "res408-console-compile", &InstallProbe },
		{ VslotOwnerId, "res364-vslot1-owner", &InstallProbe },
		{ NgEntry361Id, "res361-ng-entry", &InstallProbe },
		{ TaskUnpackCase4aId, "res349-taskunpack-case4a", &InstallProbe },
		{ AeGlobalCallArgId, "res410-ae-global-call-arg", &InstallProbe },
		{ HandlerAOgId, "res410-handler-a-og", &InstallProbe },
		{ HandlerANgId, "res410-handler-a-ng", &InstallProbe },
		{ SetVolumeId, "res385-setvolume-entry", &InstallProbe },
		{ PitchChannelId, "res407-pitch-channel-copy", &InstallProbe },
		{ VmBridgeReadId, "res553-vmbridge-read", &InstallProbe },
	}};
}

namespace F4T::ProbeSites
{
	void RegisterAll()
	{
		for (const auto& spec : kSpecs) {
			if (!F4T::ProbeRegistry::Register(spec)) {
				REX::WARN("Commonwealth Online probe: '{}' could not be registered", spec.name);
			}
		}
	}
}
