#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace F4T::SoloCell
{
	inline constexpr auto kCharacterSetupCellEditorId = "Vault111Cryo";

	inline std::optional<std::uint32_t>& GetCharacterSetupCellIdCache()
	{
		static std::optional<std::uint32_t> s_characterSetupCellId;
		return s_characterSetupCellId;
	}

	inline bool HasEditorId(const RE::TESObjectCELL& a_cell, std::string_view a_editorId)
	{
		const auto* cellEditorId = a_cell.GetFormEditorID();
		return cellEditorId && a_editorId == cellEditorId;
	}

	inline bool IsSoloCell(const RE::TESObjectCELL& a_cell)
	{
		if (HasEditorId(a_cell, kCharacterSetupCellEditorId)) {
			return true;
		}

		auto& characterSetupCellId = GetCharacterSetupCellIdCache();
		if (!characterSetupCellId) {
			const auto* characterSetupCell = RE::TESForm::GetFormByEditorID<RE::TESObjectCELL>(
				RE::BSFixedString{ kCharacterSetupCellEditorId });
			if (characterSetupCell) {
				characterSetupCellId = characterSetupCell->GetFormID();
			}
		}

		return characterSetupCellId && a_cell.GetFormID() == *characterSetupCellId;
	}
}
