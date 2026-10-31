#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

#include "RE/I/IAnimationGraphManagerHolder.h"

namespace F4T::AnimationDescriptor
{
	struct AnimationVariableSnapshot;
	class F4AnimationDescriptor;

	enum class VariableType : std::uint8_t
	{
		Float,
		Bool,
		Int,
	};

	struct AnimationVariableSnapshot
	{
		std::vector<float> floats;
		std::vector<bool> bools;
		std::vector<std::uint32_t> ints;
	};

	class F4AnimationDescriptor
	{
	public:
		F4AnimationDescriptor() = default;

		static const F4AnimationDescriptor& GetHumanoidDescriptor();

		F4AnimationDescriptor(const F4AnimationDescriptor&) = delete;
		F4AnimationDescriptor& operator=(const F4AnimationDescriptor&) = delete;

		const std::string& GetFloatVariableName(std::size_t a_index) const;
		const std::string& GetBoolVariableName(std::size_t a_index) const;
		const std::string& GetIntVariableName(std::size_t a_index) const;

		std::size_t GetFloatVariableIndex(const std::string& a_name) const;
		std::size_t GetBoolVariableIndex(const std::string& a_name) const;
		std::size_t GetIntVariableIndex(const std::string& a_name) const;

		bool HasFloatVariable(const std::string& a_name) const;
		bool HasBoolVariable(const std::string& a_name) const;
		bool HasIntVariable(const std::string& a_name) const;

		bool SaveAnimationVariablesFromCache(
			RE::IAnimationGraphManagerHolder& a_holder,
			AnimationVariableSnapshot& a_snapshot) const;

		bool LoadAnimationVariablesToCache(
			RE::IAnimationGraphManagerHolder& a_holder,
			const AnimationVariableSnapshot& a_snapshot) const;

		std::size_t GetFloatVariableCount() const { return m_floatVariables.size(); }
		std::size_t GetBoolVariableCount() const { return m_boolVariables.size(); }
		std::size_t GetIntVariableCount() const { return m_intVariables.size(); }

	private:
		std::vector<std::string> m_floatVariables;
		std::vector<std::string> m_boolVariables;
		std::vector<std::string> m_intVariables;

		std::unordered_map<std::string, std::size_t> m_floatVariableIndexMap;
		std::unordered_map<std::string, std::size_t> m_boolVariableIndexMap;
		std::unordered_map<std::string, std::size_t> m_intVariableIndexMap;

		void InitializeHumanoidVariables();

		bool TryReadFloatVariable(
			RE::IAnimationGraphManagerHolder& a_holder,
			std::size_t a_index,
			float& a_outValue) const;

		bool TryReadBoolVariable(
			RE::IAnimationGraphManagerHolder& a_holder,
			std::size_t a_index,
			bool& a_outValue) const;

		bool TryReadIntVariable(
			RE::IAnimationGraphManagerHolder& a_holder,
			std::size_t a_index,
			std::uint32_t& a_outValue) const;

		bool TryWriteFloatVariable(
			RE::IAnimationGraphManagerHolder& a_holder,
			std::size_t a_index,
			float a_value) const;

		bool TryWriteBoolVariable(
			RE::IAnimationGraphManagerHolder& a_holder,
			std::size_t a_index,
			bool a_value) const;

		bool TryWriteIntVariable(
			RE::IAnimationGraphManagerHolder& a_holder,
			std::size_t a_index,
			std::uint32_t a_value) const;

		friend class F4TProxyAnimationSync;
	};
}
