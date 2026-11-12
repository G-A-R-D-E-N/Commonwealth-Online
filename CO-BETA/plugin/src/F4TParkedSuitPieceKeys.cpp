#include "pch.h"

#include "F4TParkedSuitInternal.h"

#include "F4TInventoryDropGate.h"

#include "RE/T/TESForm.h"

namespace F4T::ParkedSuitSync::detail
{
	nlohmann::json WithPieceKeys(nlohmann::json a_packet, const std::vector<F4T::PowerArmorSync::Piece>& a_pieces)
	{
		std::vector<std::string> keys;
		for (const auto& piece : a_pieces) {
			const auto* form = RE::TESForm::GetFormByID<RE::TESForm>(piece.formId);
			if (auto key = F4T::InventoryDropGate::TranslateItemKey(form)) {
				keys.push_back(std::move(*key));
			}
		}
		if (!keys.empty()) a_packet["pieceKeys"] = std::move(keys);
		return a_packet;
	}
}
