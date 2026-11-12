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

	struct CreditAck
	{
		std::string creditId;
		std::uint32_t stage{};
	};

	void Enqueue(std::string a_key, std::uint32_t a_resolvedForm, std::int32_t a_stage);
	void EnqueueCredit(std::string a_key, std::uint32_t a_resolvedForm, std::uint32_t a_objectiveId, std::string a_creditId);
	void PublishQueries(std::vector<Query> a_queries);
	[[nodiscard]] std::optional<State> FindReport(std::uint32_t a_resolvedForm);
	[[nodiscard]] std::vector<CreditAck> TakeCreditAcks();
	[[nodiscard]] bool HasScriptContact();
	void Clear();
	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm);
}
