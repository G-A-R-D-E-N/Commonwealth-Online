#include "F4TValidatorFuzzHarness.h"

#include "F4TNetworkValidation.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using namespace ValidatorFuzz;
	namespace NV = F4T::NetworkValidation;
	using Verdict = NV::MovementVerdict;
	using Budget = NV::MovementTransitionBudget;

	constexpr std::string_view kCell = "0000003C";
	constexpr std::string_view kWorldspace = "0000003D";
	constexpr std::string_view kOtherCell = "0000003E";
	constexpr std::string_view kNormalTypes[] = { "normal", "bogus", "Teleport", "" };
	constexpr std::string_view kTransitionTypes[] = { "teleport", "cell_change", "worldspace_change", "fast_travel" };
	constexpr std::string_view kSessionStartTypes[] = { "load", "spawn" };
	constexpr double kRelativeTolerance = 1e-9;
	constexpr double kTransitionGaps[] = { -0.5, 0.0, 0.5, 1.0, 1.5, 5.0, 30.0, 61.0, 200.0 };

	bool IsDigit(char a_ch)
	{
		return a_ch >= '0' && a_ch <= '9';
	}

	double NonFinite(Rng& a_rng)
	{
		constexpr double kValues[] = {
			std::numeric_limits<double>::quiet_NaN(),
			std::numeric_limits<double>::infinity(),
			-std::numeric_limits<double>::infinity()
		};
		return kValues[a_rng.Below(std::size(kValues))];
	}

	double Signed(Rng& a_rng, double a_scale)
	{
		return (static_cast<double>(a_rng.Below(2001)) / 1000.0 - 1.0) * a_scale;
	}

	bool NearlyEqual(double a_left, double a_right)
	{
		return std::fabs(a_left - a_right) <= kRelativeTolerance * std::max(1.0, std::fabs(a_right));
	}

	bool SameBits(double a_left, double a_right)
	{
		return std::memcmp(&a_left, &a_right, sizeof(double)) == 0;
	}

	bool SameResult(const NV::MovementValidationResult& a_left, const NV::MovementValidationResult& a_right)
	{
		return a_left.verdict == a_right.verdict && SameBits(a_left.distance, a_right.distance) &&
		       SameBits(a_left.allowedDistance, a_right.allowedDistance) &&
		       SameBits(a_left.elapsedSeconds, a_right.elapsedSeconds);
	}

	std::string Describe(const NV::MovementValidationResult& a_result)
	{
		return "verdict=" + std::to_string(static_cast<int>(a_result.verdict)) +
		       " distance=" + std::to_string(a_result.distance) +
		       " allowed=" + std::to_string(a_result.allowedDistance) +
		       " elapsed=" + std::to_string(a_result.elapsedSeconds);
	}

	std::optional<double> ReferenceClock(std::string_view a_text)
	{
		if (a_text.size() != 4 || !std::all_of(a_text.begin(), a_text.end(), IsDigit)) {
			return std::nullopt;
		}
		const int hours = (a_text[0] - '0') * 10 + (a_text[1] - '0');
		const int minutes = (a_text[2] - '0') * 10 + (a_text[3] - '0');
		if (hours > 23 || minutes > 59) {
			return std::nullopt;
		}
		return hours + minutes / 60.0;
	}

	std::string ClockCandidate(Rng& a_rng)
	{
		char buffer[8]{};
		const auto hours = a_rng.Below(100);
		const auto minutes = a_rng.Below(100);
		switch (a_rng.Below(4)) {
		case 0:
			std::snprintf(buffer, sizeof(buffer), "%02zu%02zu", hours, minutes);
			return std::string(buffer, 4);
		case 1:
			std::snprintf(buffer, sizeof(buffer), "%02zu:%02zu", hours, minutes);
			return std::string(buffer, 5);
		case 2:
			return ClockText(a_rng);
		default:
			std::snprintf(buffer, sizeof(buffer), "%02zu%02zu", hours, minutes);
			buffer[a_rng.Below(4)] = static_cast<char>(RandomByte(a_rng));
			return std::string(buffer, 4);
		}
	}

	void FuzzServerTime(std::uint64_t a_seed)
	{
		const Trial corpusTrial{ "NetworkValidation::ParseServerTimeHHmm", a_seed, 0 };
		for (const std::string_view text : { "0000", "1200", "2359" }) {
			if (!Invoke(corpusTrial, [&] { return NV::ParseServerTimeHHmm(text); })) {
				Fail(corpusTrial, "seed corpus time was rejected: " + std::string{ text });
			}
		}

		Rng rng{ std::mt19937_64{ a_seed } };
		std::size_t accepted = 0;
		std::size_t rejected = 0;
		for (std::size_t iteration = 0; iteration < kIterations; ++iteration) {
			const Trial trial{ "NetworkValidation::ParseServerTimeHHmm", a_seed, iteration };
			const auto text = ClockCandidate(rng);
			const auto parsed = Invoke(trial, [&] { return NV::ParseServerTimeHHmm(text); });
			const auto expected = ReferenceClock(text);
			if (parsed.has_value() != expected.has_value()) {
				Fail(trial, "accept/reject disagrees with reference parser for \"" + text + "\"");
			}
			if (!parsed) {
				++rejected;
				continue;
			}
			++accepted;
			if (!(*parsed >= 0.0F && *parsed < 24.0F) || std::fabs(static_cast<double>(*parsed) - *expected) > 1e-5) {
				Fail(trial, "accepted time is outside [0,24) or differs from reference for \"" + text + "\"");
			}
		}
		if (accepted == 0 || rejected == 0) {
			Fail(corpusTrial, "server time oracle did not produce both accepted and rejected inputs");
		}
	}

	void Expect(const Trial& a_trial, const NV::MovementValidationResult& a_result, Verdict a_expected, std::string_view a_what)
	{
		if (a_result.verdict != a_expected) {
			Fail(a_trial, std::string{ a_what } + ": " + Describe(a_result));
		}
	}

	void FuzzMovementBounds(std::uint64_t a_seed)
	{
		const Trial corpusTrial{ "NetworkValidation::ValidateMovement", a_seed, 0 };
		const NV::MovementState origin{ 0.0, 0.0, 0.0, kCell, kWorldspace, "normal" };
		const NV::MovementState nearby{ 1000.0, 0.0, 0.0, kCell, kWorldspace, "normal" };
		Expect(corpusTrial, Invoke(corpusTrial, [&] { return NV::ValidateMovement(origin, 10.0, nearby, 10.2, Budget{}); }),
			Verdict::AcceptedNormalMovement, "seed corpus move was not accepted");

		Rng rng{ std::mt19937_64{ a_seed } };
		std::size_t accepted = 0;
		std::size_t rejected = 0;
		for (std::size_t iteration = 0; iteration < kIterations; ++iteration) {
			const Trial trial{ "NetworkValidation::ValidateMovement", a_seed, iteration };
			const NV::MovementState previous{ Signed(rng, 1000.0), Signed(rng, 1000.0), Signed(rng, 1000.0), kCell, kWorldspace, "normal" };
			const auto previousTime = static_cast<double>(rng.Below(100000)) / 1000.0;
			const auto now = rng.Below(8) == 0 ? NonFinite(rng) : previousTime + static_cast<double>(rng.Below(25001)) / 1000.0 - 5.0;

			const auto delta = now - previousTime;
			const auto elapsed = std::isfinite(delta) && delta >= 0.0 ?
			                     std::min(delta, NV::MaximumMovementValidationElapsedSeconds) :
			                     0.0;
			const auto allowed = NV::MovementGraceDistance + NV::MaximumNormalMovementSpeed * elapsed;
			constexpr double kFactors[] = { 0.0, 0.5, 0.999, 1.001, 1.5, 4.0 };
			const auto ux = Signed(rng, 1.0);
			const auto uy = Signed(rng, 1.0);
			const auto uz = Signed(rng, 1.0);
			const auto norm = std::sqrt(ux * ux + uy * uy + uz * uz);
			const auto scale = norm > 0.0 ? allowed * kFactors[rng.Below(std::size(kFactors))] / norm : 0.0;
			NV::MovementState current{
				previous.x + ux * scale,
				previous.y + uy * scale,
				previous.z + uz * scale,
				kCell,
				kWorldspace,
				kNormalTypes[rng.Below(std::size(kNormalTypes))]
			};

			const auto scopeChanged = rng.Below(6) == 0;
			if (scopeChanged) {
				(rng.Below(2) == 0 ? current.cellId : current.worldspaceId) = kOtherCell;
			}
			const auto poisoned = !scopeChanged && rng.Below(8) == 0;
			if (poisoned) {
				current.x = NonFinite(rng);
			}

			std::optional<NV::MovementState> prior{ previous };
			std::optional<double> priorTime{ previousTime };
			const auto priorKind = rng.Below(10);
			if (priorKind == 0) {
				prior.reset();
			} else if (priorKind == 1) {
				priorTime.reset();
			}

			const auto result = Invoke(trial, [&] { return NV::ValidateMovement(prior, priorTime, current, now, Budget{}); });
			if (!SameResult(result, Invoke(trial, [&] { return NV::ValidateMovement(prior, priorTime, current, now, Budget{}); }))) {
				Fail(trial, "ValidateMovement is not deterministic for identical inputs");
			}
			if (result.Accepted()) {
				++accepted;
			} else {
				++rejected;
			}

			const auto dx = current.x - previous.x;
			const auto dy = current.y - previous.y;
			const auto dz = current.z - previous.z;
			const auto distance = std::sqrt(dx * dx + dy * dy + dz * dz);
			if (!std::isfinite(now)) {
				Expect(trial, result, Verdict::RejectedTime, "non-finite time must be rejected");
			} else if (!prior || !priorTime) {
				Expect(trial, result, Verdict::AcceptedFirstTransform, "missing prior state must start a session");
			} else if (now < previousTime) {
				Expect(trial, result, Verdict::RejectedTime, "time moving backwards must be rejected");
			} else if (scopeChanged) {
				Expect(trial, result, Verdict::RejectedScopeChange, "cell or worldspace change must be rejected");
			} else if (poisoned) {
				Expect(trial, result, Verdict::RejectedDistance, "non-finite coordinate must be rejected");
			} else {
				if (!NearlyEqual(result.elapsedSeconds, elapsed) || !NearlyEqual(result.allowedDistance, allowed) ||
				    !NearlyEqual(result.distance, distance)) {
					Fail(trial, "reported distance, allowance or elapsed time disagrees with the model: " + Describe(result));
				}
				if (distance <= allowed * (1.0 - kRelativeTolerance)) {
					Expect(trial, result, Verdict::AcceptedNormalMovement, "move within bound was not accepted");
				} else if (distance > allowed * (1.0 + kRelativeTolerance)) {
					Expect(trial, result, Verdict::RejectedDistance, "move beyond bound was not rejected");
				}
			}
		}
		if (accepted == 0 || rejected == 0) {
			Fail(corpusTrial, "movement bounds oracle did not produce both accepted and rejected inputs");
		}
	}

	struct RateModel
	{
		std::vector<double> accepted;
	};

	bool RateAllows(const RateModel& a_model, double a_now)
	{
		const auto& times = a_model.accepted;
		if (!times.empty() && !(a_now - times.back() >= NV::MinimumMovementTransitionIntervalSeconds)) {
			return false;
		}
		if (times.size() < NV::MaximumMovementTransitionsPerWindow) {
			return true;
		}
		return a_now - times[times.size() - NV::MaximumMovementTransitionsPerWindow] > NV::MovementTransitionWindowSeconds;
	}

	void RateRecord(RateModel& a_model, double a_now)
	{
		a_model.accepted.push_back(a_now);
	}

	void FuzzTransitions(std::uint64_t a_seed)
	{
		const Trial corpusTrial{ "NetworkValidation::ValidateMovement", a_seed, 0 };
		for (const auto type : kSessionStartTypes) {
			if (!NV::IsSessionStartMovementType(type) || !NV::IsMovementTransitionType(type)) {
				Fail(corpusTrial, "session start type is not a transition type: " + std::string{ type });
			}
		}
		for (const auto type : kNormalTypes) {
			if (NV::IsMovementTransitionType(type)) {
				Fail(corpusTrial, "normal type is classified as a transition: " + std::string{ type });
			}
		}

		Rng rng{ std::mt19937_64{ a_seed } };
		std::size_t accepted = 0;
		std::size_t rejected = 0;
		for (std::size_t iteration = 0; iteration < kIterations; ++iteration) {
			const Trial trial{ "NetworkValidation::ValidateMovement", a_seed, iteration };
			const NV::MovementState origin{ 0.0, 0.0, 0.0, kCell, kWorldspace, "normal" };
			Budget budget{};
			RateModel model{};
			std::optional<double> lastAccepted;
			double now = 0.0;
			for (std::size_t step = 0, steps = 1 + rng.Below(40); step < steps; ++step) {
				now += rng.Below(2) == 0 ?
				       kTransitionGaps[rng.Below(std::size(kTransitionGaps))] :
				       static_cast<double>(rng.Below(3000)) / 1000.0 - 0.5;
				const auto sessionStart = rng.Below(10) == 0;
				const auto type = sessionStart ?
				                  kSessionStartTypes[rng.Below(std::size(kSessionStartTypes))] :
				                  kTransitionTypes[rng.Below(std::size(kTransitionTypes))];
				const NV::MovementState move{ 1'000'000.0, 0.0, 0.0, kCell, kWorldspace, type };

				const auto result = Invoke(trial, [&] { return NV::ValidateMovement(origin, 0.0, move, now, budget); });
				Verdict expected = Verdict::RejectedTransitionContext;
				if (now < 0.0) {
					expected = Verdict::RejectedTime;
				} else if (!sessionStart) {
					expected = RateAllows(model, now) ? Verdict::AcceptedExplicitTransition : Verdict::RejectedTransitionRate;
				}
				if (result.verdict != expected) {
					Fail(trial, "transition verdict disagrees with rate model at t=" + std::to_string(now) + ": " + Describe(result));
				}
				if (!result.Accepted()) {
					++rejected;
					continue;
				}
				++accepted;
				if (lastAccepted && !(now - *lastAccepted >= NV::MinimumMovementTransitionIntervalSeconds)) {
					Fail(trial, "accepted transitions are closer than the minimum interval; t=" + std::to_string(now));
				}
				lastAccepted = now;
				budget.Record(now);
				RateRecord(model, now);
			}
		}
		if (accepted == 0 || rejected == 0) {
			Fail(corpusTrial, "transition oracle did not produce both accepted and rejected inputs");
		}
	}
}

int main()
{
	FuzzServerTime(kBaseSeed + 1);
	FuzzMovementBounds(kBaseSeed + 2);
	FuzzTransitions(kBaseSeed + 3);
	std::cout << "validator oracle fuzz passed: targets=3 iterations_per_target=" << kIterations << " base_seed=" << kBaseSeed << '\n';
	return 0;
}
