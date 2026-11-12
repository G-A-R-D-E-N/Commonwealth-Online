#pragma once
#include <cstdint>
namespace F4SE { class SerializationInterface; }
namespace F4T::NpcLootLedger
{
	inline constexpr std::uint32_t RecordType = 'NLTK';
	inline constexpr std::uint32_t RecordVersion = 1;
	void SaveRecords(const F4SE::SerializationInterface* a_intfc);
	void LoadRecord(const F4SE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length);
	void PublishLoadedRecords();
	void ClearStaged();
}
