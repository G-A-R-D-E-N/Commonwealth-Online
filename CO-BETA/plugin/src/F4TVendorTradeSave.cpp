#include "pch.h"

#include "F4TVendorTradeSave.h"

#include "F4TVendorTradeClient.h"
#include "F4TVendorTradeEscrow.h"

#include <cstdint>
#include <string>
#include <utility>

namespace VE = F4T::VendorTradeEscrow;

namespace F4T::VendorTradeSave
{
	void Save(const F4SE::SerializationInterface* a_intfc)
	{
		const auto text = VE::EncodeParked(F4T::VendorTradeClient::SnapshotParked());
		if (!text) {
			REX::WARN("Vendor trade save skipped: parked trades exceed the save limits.");
			return;
		}
		if (!a_intfc->WriteRecord(kRecordType, kRecordVersion, text->data(), static_cast<std::uint32_t>(text->size()))) {
			REX::WARN("Vendor trade save record could not be written.");
		}
	}

	void Load(const F4SE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length)
	{
		if (a_version != kRecordVersion || a_length > VE::kMaximumParkedBytes) {
			REX::WARN("Vendor trade save record was dropped: unsupported version or size.");
			return;
		}
		std::string text(a_length, '\0');
		if (a_intfc->ReadRecordData(text.data(), a_length) != a_length) {
			REX::WARN("Vendor trade save record could not be read.");
			return;
		}
		auto parked = VE::DecodeParked(text);
		if (!parked) {
			REX::WARN("Vendor trade save record was dropped: it is corrupt.");
			return;
		}
		F4T::VendorTradeClient::RestoreParked(std::move(*parked));
	}

	void Clear()
	{
		F4T::VendorTradeClient::RestoreParked(VE::Parked{});
	}
}
