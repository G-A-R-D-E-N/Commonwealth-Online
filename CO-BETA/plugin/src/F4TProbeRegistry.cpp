#include "pch.h"

#include "F4TProbeRegistry.h"

#include <array>
#include <atomic>
#include <format>
#include <optional>
#include <utility>

namespace
{
	using F4T::ProbeRegistry::HookPlan;
	using F4T::ProbeRegistry::InstallOutcome;
	using F4T::ProbeRegistry::LogFn;
	using F4T::ProbeRegistry::MaxProbes;
	using F4T::ProbeRegistry::MaxSpecs;
	using F4T::ProbeRegistry::RuntimeSettings;
	using F4T::ProbeRegistry::Spec;

	struct Gate
	{
		std::atomic<std::uint64_t> calls{ 0 };
		std::atomic<std::uint64_t> emitted{ 0 };
		std::atomic<std::uint32_t> sampling{ 0 };
		std::atomic<std::uint32_t> perProbeCap{ 0 };
	};

	std::array<Spec, MaxSpecs> g_specs{};
	std::size_t g_specCount = 0;
	std::array<Gate, MaxProbes> g_gates{};
	std::array<std::optional<F4T::InlineHook::Result>, MaxProbes> g_hooks{};
	std::array<std::optional<F4T::InlineHook::MidResult>, MaxProbes> g_mids{};

	void LogLine(LogFn a_log, std::string a_message) noexcept
	{
		if (a_log) {
			a_log(a_message);
		}
	}
}

namespace F4T::ProbeRegistry
{
	bool Register(const Spec& a_spec) noexcept
	{
		if (a_spec.probeId >= MaxProbes || !a_spec.install || a_spec.name.empty() || g_specCount >= MaxSpecs) {
			return false;
		}
		for (std::size_t index = 0; index < g_specCount; ++index) {
			if (g_specs[index].probeId == a_spec.probeId) {
				return false;
			}
		}
		g_specs[g_specCount++] = a_spec;
		return true;
	}

	void Install(
		const ProbeManifest::Document& a_manifest,
		ProbeVersionGate::RuntimeVersion a_version,
		LogFn a_log)
	{
		const auto module = REX::FModule::GetExecutingModule();
		const auto text = module.GetSection(".text");

		for (std::size_t index = 0; index < g_specCount; ++index) {
			const auto& spec = g_specs[index];
			const ProbeManifest::Entry* entry = nullptr;
			for (const auto& candidate : a_manifest.probes) {
				if (candidate.name == spec.name) {
					entry = &candidate;
					break;
				}
			}
			if (!entry) {
				LogLine(a_log, std::format("probe '{}' has no manifest entry; not installed", spec.name));
				continue;
			}
			if (!entry->enabled) {
				LogLine(a_log, std::format("probe '{}' disabled by the manifest", spec.name));
				continue;
			}

			HookPlan plan{};
			const RuntimeSettings settings{ spec.probeId, spec.name, entry->sampling, entry->perProbeCap, a_version };
			if (spec.install(settings, plan) != InstallOutcome::Enabled) {
				LogLine(a_log, std::format("probe '{}' installer declined", spec.name));
				continue;
			}

			if (plan.midDestination) {
				InlineHook::Request request{};
				request.target = plan.target;
				request.codeSection = { text.GetAddress(), text.GetAddress() + text.GetSize() };
				request.expectedPrologue = plan.expectedPrologue;
				auto& slot = g_mids[spec.probeId].emplace();
				InlineHook::InstallMid(request, plan.midDestination, a_log, slot);
				if (slot.verdict != InlineHookGuard::InstallVerdict::kOk) {
					g_mids[spec.probeId].reset();
					LogLine(a_log, std::format("probe '{}' hook was not installed", spec.name));
					continue;
				}
			} else if (plan.target != 0) {
				InlineHook::Request request{};
				request.target = plan.target;
				request.codeSection = { text.GetAddress(), text.GetAddress() + text.GetSize() };
				request.expectedPrologue = plan.expectedPrologue;
				request.destination = plan.destination;
				auto& slot = g_hooks[spec.probeId].emplace();
				InlineHook::Install(request, a_log, slot);
				if (slot.verdict != InlineHookGuard::InstallVerdict::kOk) {
					g_hooks[spec.probeId].reset();
					LogLine(a_log, std::format("probe '{}' hook was not installed", spec.name));
					continue;
				}
			}

			auto& gate = g_gates[spec.probeId];
			gate.sampling.store(entry->sampling, std::memory_order_relaxed);
			gate.perProbeCap.store(entry->perProbeCap, std::memory_order_relaxed);
			LogLine(a_log, std::format(
				"probe '{}' enabled (sampling {}, perProbeCap {})",
				spec.name,
				entry->sampling,
				entry->perProbeCap));
		}
	}

	bool ShouldRecord(std::uint16_t a_probeId) noexcept
	{
		if (a_probeId >= MaxProbes) {
			return false;
		}
		auto& gate = g_gates[a_probeId];
		const auto sampling = gate.sampling.load(std::memory_order_relaxed);
		if (sampling == 0) {
			return false;
		}
		const auto call = gate.calls.fetch_add(1, std::memory_order_relaxed) + 1;
		if (sampling > 1 && (call % sampling) != 0) {
			return false;
		}
		const auto cap = gate.perProbeCap.load(std::memory_order_relaxed);
		if (cap != 0) {
			const auto slot = gate.emitted.fetch_add(1, std::memory_order_relaxed);
			if (slot >= cap) {
				return false;
			}
		}
		return true;
	}
}
