#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace RE::BSScript
{
	class IVirtualMachine;
}

namespace F4T::QuestPapyrus
{
	struct Query
	{
		std::string key;
		std::uint32_t resolvedForm{};
	};

	struct State
	{
		bool completed{ false };
		bool running{ false };
	};

	void Enqueue(std::string a_key, std::uint32_t a_resolvedForm, std::int32_t a_stage);
	void PublishQueries(std::vector<Query> a_queries);
	[[nodiscard]] std::optional<State> FindReport(std::uint32_t a_resolvedForm);
	[[nodiscard]] bool HasScriptContact();
	void Clear();
	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm);
}
