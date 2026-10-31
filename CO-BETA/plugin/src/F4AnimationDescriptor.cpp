#include "pch.h"

#include "F4AnimationDescriptor.h"
#include "F4TDebugLogging.h"

#include "RE/B/BGSAnimationSystemUtils.h"
#include "RE/B/BSFixedString.h"
#include "RE/I/IAnimationGraphManagerHolder.h"

#include <format>
#include <cmath>

namespace F4T::AnimationDescriptor
{
	namespace
	{
		void LogInfoMessage(const std::string& a_message)
		{
			F4T::DebugLogging::Write("AnimationDescriptor", a_message);
		}

		void LogWarningMessage(const std::string& a_message)
		{
			REX::WARN(std::string_view{ a_message });
		}

		bool AreFloatsDifferent(float a_lhs, float a_rhs)
		{
			if (!std::isfinite(a_lhs) || !std::isfinite(a_rhs)) {
				return a_lhs != a_rhs;
			}
			constexpr float kSpeedEpsilon = 0.5F;
			return std::fabs(a_lhs - a_rhs) > kSpeedEpsilon;
		}
	}

	static F4AnimationDescriptor g_humanoidDescriptor;
	static bool g_initialized = false;

	void F4AnimationDescriptor::InitializeHumanoidVariables()
	{
		m_floatVariables = {
			"Speed",
			"SpeedSmoothed",
		};

		for (std::size_t i = 0; i < m_floatVariables.size(); ++i) {
			m_floatVariableIndexMap[m_floatVariables[i]] = i;
		}

		m_boolVariables = {
			"IsSprinting",
			"bInJumpState",
		};

		for (std::size_t i = 0; i < m_boolVariables.size(); ++i) {
			m_boolVariableIndexMap[m_boolVariables[i]] = i;
		}

		m_intVariables = {
			"iIsInSneak",
			"iSyncWalkRun",
			"iSyncLocomotionSpeed",
			"iSyncJumpState",
		};

		for (std::size_t i = 0; i < m_intVariables.size(); ++i) {
			m_intVariableIndexMap[m_intVariables[i]] = i;
		}

		LogInfoMessage(std::format(
			"F4AnimationDescriptor: Initialized humanoid graph with {} floats, {} bools, {} ints",
			m_floatVariables.size(),
			m_boolVariables.size(),
			m_intVariables.size()));
	}

	const F4AnimationDescriptor& F4AnimationDescriptor::GetHumanoidDescriptor()
	{
		if (!g_initialized) {
			g_humanoidDescriptor.InitializeHumanoidVariables();
			g_initialized = true;
		}
		return g_humanoidDescriptor;
	}

	const std::string& F4AnimationDescriptor::GetFloatVariableName(std::size_t a_index) const
	{
		static const std::string kInvalid = "<invalid>";
		if (a_index >= m_floatVariables.size()) {
			return kInvalid;
		}
		return m_floatVariables[a_index];
	}

	const std::string& F4AnimationDescriptor::GetBoolVariableName(std::size_t a_index) const
	{
		static const std::string kInvalid = "<invalid>";
		if (a_index >= m_boolVariables.size()) {
			return kInvalid;
		}
		return m_boolVariables[a_index];
	}

	const std::string& F4AnimationDescriptor::GetIntVariableName(std::size_t a_index) const
	{
		static const std::string kInvalid = "<invalid>";
		if (a_index >= m_intVariables.size()) {
			return kInvalid;
		}
		return m_intVariables[a_index];
	}

	std::size_t F4AnimationDescriptor::GetFloatVariableIndex(const std::string& a_name) const
	{
		const auto it = m_floatVariableIndexMap.find(a_name);
		if (it != m_floatVariableIndexMap.end()) {
			return it->second;
		}
		return static_cast<std::size_t>(-1);
	}

	std::size_t F4AnimationDescriptor::GetBoolVariableIndex(const std::string& a_name) const
	{
		const auto it = m_boolVariableIndexMap.find(a_name);
		if (it != m_boolVariableIndexMap.end()) {
			return it->second;
		}
		return static_cast<std::size_t>(-1);
	}

	std::size_t F4AnimationDescriptor::GetIntVariableIndex(const std::string& a_name) const
	{
		const auto it = m_intVariableIndexMap.find(a_name);
		if (it != m_intVariableIndexMap.end()) {
			return it->second;
		}
		return static_cast<std::size_t>(-1);
	}

	bool F4AnimationDescriptor::HasFloatVariable(const std::string& a_name) const
	{
		return m_floatVariableIndexMap.find(a_name) != m_floatVariableIndexMap.end();
	}

	bool F4AnimationDescriptor::HasBoolVariable(const std::string& a_name) const
	{
		return m_boolVariableIndexMap.find(a_name) != m_boolVariableIndexMap.end();
	}

	bool F4AnimationDescriptor::HasIntVariable(const std::string& a_name) const
	{
		return m_intVariableIndexMap.find(a_name) != m_intVariableIndexMap.end();
	}

	namespace
	{
		bool HasAnimationGraphManager(const RE::IAnimationGraphManagerHolder& a_holder)
		{
			const auto* refr = dynamic_cast<const RE::TESObjectREFR*>(&a_holder);
			if (refr) {
				const auto* actor = refr->As<RE::Actor>();
				if (actor) {
					return actor->currentProcess &&
					       actor->currentProcess->middleHigh &&
					       actor->currentProcess->middleHigh->animationGraphManager.get() != nullptr;
				}
			}
			return false;
		}
	}

	bool F4AnimationDescriptor::SaveAnimationVariablesFromCache(
		RE::IAnimationGraphManagerHolder& a_holder,
		AnimationVariableSnapshot& a_snapshot) const
	{
		if (!HasAnimationGraphManager(a_holder)) {
			LogWarningMessage("SaveAnimationVariablesFromCache: Graph manager unavailable");
			return false;
		}

		a_snapshot.floats.clear();
		a_snapshot.bools.clear();
		a_snapshot.ints.clear();

		for (std::size_t i = 0; i < m_floatVariables.size(); ++i) {
			float value = 0.0F;
			if (TryReadFloatVariable(a_holder, i, value)) {
				a_snapshot.floats.push_back(value);
			} else {
				a_snapshot.floats.push_back(0.0F);
			}
		}

		for (std::size_t i = 0; i < m_boolVariables.size(); ++i) {
			bool value = false;
			if (TryReadBoolVariable(a_holder, i, value)) {
				a_snapshot.bools.push_back(value);
			} else {
				a_snapshot.bools.push_back(false);
			}
		}

		for (std::size_t i = 0; i < m_intVariables.size(); ++i) {
			std::uint32_t value = 0;
			if (TryReadIntVariable(a_holder, i, value)) {
				a_snapshot.ints.push_back(value);
			} else {
				a_snapshot.ints.push_back(0);
			}
		}

		return true;
	}

	bool F4AnimationDescriptor::LoadAnimationVariablesToCache(
		RE::IAnimationGraphManagerHolder& a_holder,
		const AnimationVariableSnapshot& a_snapshot) const
	{
		if (!HasAnimationGraphManager(a_holder)) {
			LogWarningMessage("LoadAnimationVariablesToCache: Graph manager unavailable");
			return false;
		}

		bool success = true;

		for (std::size_t i = 0; i < std::min(a_snapshot.floats.size(), m_floatVariables.size()); ++i) {
			if (!TryWriteFloatVariable(a_holder, i, a_snapshot.floats[i])) {
				success = false;
			}
		}

		for (std::size_t i = 0; i < std::min(a_snapshot.bools.size(), m_boolVariables.size()); ++i) {
			if (!TryWriteBoolVariable(a_holder, i, a_snapshot.bools[i])) {
				success = false;
			}
		}

		for (std::size_t i = 0; i < std::min(a_snapshot.ints.size(), m_intVariables.size()); ++i) {
			if (!TryWriteIntVariable(a_holder, i, a_snapshot.ints[i])) {
				success = false;
			}
		}

		return success;
	}

	bool F4AnimationDescriptor::TryReadFloatVariable(
		RE::IAnimationGraphManagerHolder& a_holder,
		std::size_t a_index,
		float& a_outValue) const
	{
		if (a_index >= m_floatVariables.size()) {
			return false;
		}

		const auto& varName = m_floatVariables[a_index];
		const RE::BSFixedString fixedName{ varName };

		return a_holder.GetGraphVariableImplFloat(fixedName, a_outValue);
	}

	bool F4AnimationDescriptor::TryReadBoolVariable(
		RE::IAnimationGraphManagerHolder& a_holder,
		std::size_t a_index,
		bool& a_outValue) const
	{
		if (a_index >= m_boolVariables.size()) {
			return false;
		}

		const auto& varName = m_boolVariables[a_index];
		const RE::BSFixedString fixedName{ varName };

		return a_holder.GetGraphVariableImplBool(fixedName, a_outValue);
	}

	bool F4AnimationDescriptor::TryReadIntVariable(
		RE::IAnimationGraphManagerHolder& a_holder,
		std::size_t a_index,
		std::uint32_t& a_outValue) const
	{
		if (a_index >= m_intVariables.size()) {
			return false;
		}

		const auto& varName = m_intVariables[a_index];
		const RE::BSFixedString fixedName{ varName };

		std::int32_t tempValue = 0;
		const auto result = a_holder.GetGraphVariableImplInt(fixedName, tempValue);
		a_outValue = static_cast<std::uint32_t>(tempValue);
		return result;
	}

	bool F4AnimationDescriptor::TryWriteFloatVariable(
		RE::IAnimationGraphManagerHolder& a_holder,
		std::size_t a_index,
		float a_value) const
	{
		if (a_index >= m_floatVariables.size()) {
			return false;
		}

		const auto& varName = m_floatVariables[a_index];
		const RE::BSFixedString fixedName{ varName };

		return a_holder.SetGraphVariableFloat(fixedName, a_value);
	}

	bool F4AnimationDescriptor::TryWriteBoolVariable(
		RE::IAnimationGraphManagerHolder& a_holder,
		std::size_t a_index,
		bool a_value) const
	{
		if (a_index >= m_boolVariables.size()) {
			return false;
		}

		const auto& varName = m_boolVariables[a_index];
		const RE::BSFixedString fixedName{ varName };

		return a_holder.SetGraphVariableBool(fixedName, a_value);
	}

	bool F4AnimationDescriptor::TryWriteIntVariable(
		RE::IAnimationGraphManagerHolder& a_holder,
		std::size_t a_index,
		std::uint32_t a_value) const
	{
		if (a_index >= m_intVariables.size()) {
			return false;
		}

		const auto& varName = m_intVariables[a_index];
		const RE::BSFixedString fixedName{ varName };

		return a_holder.SetGraphVariableInt(fixedName, static_cast<std::int32_t>(a_value));
	}
}
