#include "F4TCharacterCreatorInternal.h"

namespace F4T::CharacterCreator
{
	namespace
	{
		nlohmann::json BuildStateJson()
		{
			nlohmann::json state;
			state["type"] = "creatorState";
			state["generation"] = g_state.generation.load();
			state["menuOpen"] = g_state.menuOpen;
			state["available"] = false;

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return state;
			}

			auto* npc = player->GetNPC();
			if (!npc) {
				return state;
			}

			state["available"] = true;
			state["byCharGenFlag"] = player->byCharGenFlag;
			state["charGenHUDMode"] = player->charGenHUDMode;
			state["isFemale"] = npc->GetSex() == RE::SEX::kFemale;
			state["height"] = npc->height;

			auto* race = npc->GetFormRace();
			state["raceFormId"] = race ? race->GetFormID() : 0u;

			state["morphWeight"] = nlohmann::json{
				{ "thin", npc->morphWeight.x },
				{ "muscular", npc->morphWeight.y },
				{ "large", npc->morphWeight.z }
			};
			state["bodyTintColor"] = nlohmann::json{
				{ "r", npc->bodyTintColorR },
				{ "g", npc->bodyTintColorG },
				{ "b", npc->bodyTintColorB },
				{ "a", npc->bodyTintColorA }
			};

			state["hairColorFormId"] = 0u;
			state["facialHairColorFormId"] = 0u;
			if (auto* headData = npc->headRelatedData) {
				state["hairColorFormId"] = headData->hairColor ? headData->hairColor->GetFormID() : 0u;
				state["facialHairColorFormId"] =
					headData->facialHairColor ? headData->facialHairColor->GetFormID() : 0u;
			}

			auto          headParts = nlohmann::json::array();
			std::uint32_t hairPartFormId = 0;
			std::uint32_t facialHairPartFormId = 0;
			std::uint32_t eyesPartFormId = 0;

			for (auto* part : npc->GetHeadParts()) {
				if (!part) {
					continue;
				}

				headParts.push_back({
					// TODO-verify(#366): keep the "formId" key byte-identical with the UI contract.
					{ "formId", part->GetFormID() },
					{ "type", std::to_underlying(part->type.get()) },
					{ "editorId", part->formEditorID.c_str() }
				});

				switch (part->type.get()) {
				case RE::BGSHeadPart::HeadPartType::kHair:
					hairPartFormId = part->GetFormID();
					break;
				case RE::BGSHeadPart::HeadPartType::kFacialHair:
					facialHairPartFormId = part->GetFormID();
					break;
				case RE::BGSHeadPart::HeadPartType::kEyes:
					eyesPartFormId = part->GetFormID();
					break;
				default:
					break;
				}
			}

			state["headParts"] = std::move(headParts);
			state["hairPartFormId"] = hairPartFormId;
			state["facialHairPartFormId"] = facialHairPartFormId;
			state["eyesPartFormId"] = eyesPartFormId;
			state["tintCount"] = npc->tintingData ? npc->tintingData->entriesA.size() : 0;

			auto* ui = RE::UI::GetSingleton();
			auto  menu = ui ? ui->GetMenu(kMenuName) : nullptr;
			if (menu && menu->uiMovie) {
				Scaleform::GFx::Value value;
				if (menu->uiMovie->GetVariable(&value, "CurrentBoneID")) {
					if (value.IsUInt()) {
						state["currentBoneId"] = value.GetUInt();
					} else if (value.IsInt()) {
						state["currentBoneId"] = value.GetInt();
					}
				}
				if (menu->uiMovie->GetVariable(&value, "uiCharacterPresetCount")) {
					if (value.IsNumber()) {
						state["characterPresetCount"] = value.GetNumber();
					} else if (value.IsUInt()) {
						state["characterPresetCount"] = value.GetUInt();
					} else if (value.IsInt()) {
						state["characterPresetCount"] = value.GetInt();
					}
				}
				if (menu->uiMovie->GetVariable(&value, "menuMode") && value.IsUInt()) {
					state["menuMode"] = value.GetUInt();
				}
				if (menu->uiMovie->GetVariable(&value, "sculpting") && value.IsBoolean()) {
					state["sculpting"] = value.GetBoolean();
				}
				if (menu->uiMovie->GetVariable(&value, "dirty") && value.IsBoolean()) {
					state["dirty"] = value.GetBoolean();
				}
			}

			return state;
		}
	}

	void PushEvent(const nlohmann::json& a_event)
	{
		if (!Internal::g_api || !g_state.controller.IsHandleValid() ||
			!g_state.controller.IsDomReady()) {
			return;
		}

		const auto script = std::format(
			"if(window.dispatchCoCreatorEvent){{window.dispatchCoCreatorEvent({});}}",
			a_event.dump());
		Internal::g_api->Invoke(g_state.controller.GetHandle(), script.c_str());
	}

	void PushState()
	{
		PushEvent(BuildStateJson());
	}
}
