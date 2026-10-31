#include "F4TFormIdentity.h"

namespace F4T::FormIdentity
{
	Split SplitFormId(std::uint32_t a_formId) noexcept
	{
		const auto topByte = static_cast<std::uint8_t>(a_formId >> 24);
		if (topByte == RuntimeFileMarker) {
			return Split{ Kind::Runtime, 0, 0 };
		}
		if (topByte == LightFileMarker) {
			return Split{ Kind::Light, (a_formId >> 12) & MaximumLightFileIndex, a_formId & MaximumLightLocalId };
		}
		return Split{ Kind::Regular, topByte, a_formId & MaximumRegularLocalId };
	}

	bool IsValidRecordIdentity(const RecordIdentity& a_identity, Kind a_kind) noexcept
	{
		if (a_identity.pluginName.empty()) {
			return false;
		}
		switch (a_kind) {
		case Kind::Regular:
			return a_identity.localId <= MaximumRegularLocalId;
		case Kind::Light:
			return a_identity.localId <= MaximumLightLocalId;
		case Kind::Runtime:
			return false;
		}
		return false;
	}
}
