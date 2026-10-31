#include "F4TCharacterCreatorInternal.h"

using namespace F4T::CharacterCreator::Commands;

namespace F4T::CharacterCreator
{
	namespace
	{
		bool SendMovieInput(std::string_view a_control, bool a_keyUp)
		{
			if (!g_state.menuOpen) {
				LogWarning(std::format("Dropped input '{}': LooksMenu is not open.", a_control));
				return false;
			}
			if (!IsKnownControl(a_control)) {
				LogWarning(std::format("Rejected unknown control '{}'.", a_control));
				return false;
			}

			auto* ui = RE::UI::GetSingleton();
			auto  menu = ui ? ui->GetMenu(kMenuName) : nullptr;
			if (!menu || !menu->uiMovie) {
				return false;
			}

			const std::string     control{ a_control };
			Scaleform::GFx::Value args[2];
			args[0] = Scaleform::GFx::Value(control.c_str());
			args[1] = Scaleform::GFx::Value(a_keyUp);

			Scaleform::GFx::Value result;
			const bool            invoked =
				(menu->menuObj.IsObject() && menu->menuObj.Invoke("ProcessUserEvent", &result, args, 2)) ||
				menu->uiMovie->Invoke("ProcessUserEvent", &result, args, 2);
			const bool handled = invoked && result.IsBoolean() && result.GetBoolean();

			LogInfo(std::format(
				"ProcessUserEvent(\"{}\", keyUp={}) generation={} invoked={} handled={}",
				control,
				a_keyUp,
				g_state.generation.load(),
				invoked,
				handled));

			PushEvent(nlohmann::json{
				{ "type", "creatorInput" },
				{ "control", control },
				{ "handled", handled }
			});
			return handled;
		}

		bool ResolveMenuMember(RE::IMenu& a_menu, Scaleform::GFx::Value& a_out, const char* a_name)
		{
			if (a_menu.menuObj.IsObject() && a_menu.menuObj.GetMember(a_name, &a_out)) {
				return true;
			}
			if (!a_menu.uiMovie) {
				return false;
			}
			if (a_menu.uiMovie->GetVariable(&a_out, a_name)) {
				return true;
			}
			return a_menu.uiMovie->GetVariable(&a_out, std::format("_root.{}", a_name).c_str());
		}

		bool ResolveCodeObject(RE::IMenu& a_menu, Scaleform::GFx::Value& a_out)
		{
			return ResolveMenuMember(a_menu, a_out, "BGSCodeObj") && a_out.IsObject();
		}

		struct CreatorRequest
		{
			const char*   member{ nullptr };
			std::uint32_t arity{ 0 };
			ArgKind       argKind{ ArgKind::None };
			std::int32_t  undoType{ -1 };
			bool          undoWithBone{ false };
			double        x{ 0.0 };
			double        y{ 0.0 };
			std::uint32_t group{ 0 };
			bool          flag{ false };
			std::uint64_t generation{ 0 };
		};

		std::uint32_t ReadMovieBoneId(RE::IMenu& a_menu)
		{
			Scaleform::GFx::Value current;
			if (!ResolveMenuMember(a_menu, current, "CurrentBoneID")) {
				return 0;
			}
			if (current.IsUInt()) {
				return current.GetUInt();
			}
			if (current.IsInt()) {
				return static_cast<std::uint32_t>(current.GetInt());
			}
			return 0;
		}

		bool InvokeCodeObject(const CreatorRequest& a_request)
		{
			if (!g_state.menuOpen || a_request.generation != g_state.generation.load() || !a_request.member) {
				LogWarning(std::format(
					"Dropped stale BGSCodeObj.{} request generation={} (open={}, current={}).",
					a_request.member ? a_request.member : "?",
					a_request.generation,
					g_state.menuOpen,
					g_state.generation.load()));
				return false;
			}

			auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return false;
			}

			auto menu = ui->GetMenu(kMenuName);
			if (!menu || !menu->uiMovie) {
				return false;
			}

			Scaleform::GFx::Value codeObj;
			if (!ResolveCodeObject(*menu, codeObj)) {
				LogWarning("BGSCodeObj is not resolvable on the live LooksMenu movie; refused.");
				return false;
			}

			if (!codeObj.HasMember(a_request.member)) {
				LogWarning(std::format(
					"BGSCodeObj.{} is absent on the live movie; refused.",
					a_request.member));
				return false;
			}

			if (a_request.undoType >= 0) {
				if (!codeObj.HasMember("CreateUndoPoint")) {
					LogWarning(
						"BGSCodeObj.CreateUndoPoint is absent; the change will be applied without an "
						"undo point of its own.");
				} else {
					Scaleform::GFx::Value undoArgs[2];
					undoArgs[0] = Scaleform::GFx::Value(static_cast<std::uint32_t>(a_request.undoType));
					std::uint32_t undoArgc = 1;
					if (a_request.undoWithBone) {
						undoArgs[1] = Scaleform::GFx::Value(ReadMovieBoneId(*menu));
						undoArgc = 2;
					}

					Scaleform::GFx::Value undoResult;
					const bool             undoCreated =
						codeObj.Invoke("CreateUndoPoint", &undoResult, undoArgs, undoArgc);
					LogInfo(std::format(
						"BGSCodeObj.CreateUndoPoint({}{}) -> {}",
						a_request.undoType,
						a_request.undoWithBone ? ", CurrentBoneID" : "",
						undoCreated));
				}
			}

			Scaleform::GFx::Value args[4];
			std::uint32_t         argc = 0;
			switch (a_request.argKind) {
			case ArgKind::Bool:
				args[0] = Scaleform::GFx::Value(a_request.flag);
				argc = 1;
				break;
			case ArgKind::Index:
				args[0] = Scaleform::GFx::Value(static_cast<std::uint32_t>(a_request.x));
				argc = 1;
				break;
			case ArgKind::MorphPreset:
				args[0] = Scaleform::GFx::Value(ReadMovieBoneId(*menu));
				args[1] = Scaleform::GFx::Value(static_cast<std::uint32_t>(a_request.x));
				argc = 2;
				break;
			case ArgKind::UnitPair:
				args[0] = Scaleform::GFx::Value(a_request.x);
				args[1] = Scaleform::GFx::Value(a_request.y);
				argc = 2;
				break;
			case ArgKind::TintApply:
				args[0] = Scaleform::GFx::Value(a_request.group);
				args[1] = Scaleform::GFx::Value(static_cast<std::uint32_t>(a_request.x));
				args[2] = Scaleform::GFx::Value(1.0);
				args[3] = Scaleform::GFx::Value(true);
				argc = 4;
				break;
			case ArgKind::TintRemove:
				args[0] = Scaleform::GFx::Value(a_request.group);
				argc = 1;
				break;
			case ArgKind::SignedUnit:
				args[0] = Scaleform::GFx::Value(a_request.x);
				argc = 1;
				break;
			case ArgKind::AxisValue:
				args[0] = Scaleform::GFx::Value(ReadMovieBoneId(*menu));
				args[1] = Scaleform::GFx::Value(static_cast<std::uint32_t>(a_request.x));
				args[2] = Scaleform::GFx::Value(a_request.y);
				argc = 3;
				break;
			case ArgKind::None:
			default:
				break;
			}

			Scaleform::GFx::Value result;
			const bool             invoked =
				codeObj.Invoke(a_request.member, &result, args, argc);
			LogInfo(std::format(
				"BGSCodeObj.{}(argc={}) generation={} -> {}",
				a_request.member,
				argc,
				a_request.generation,
				invoked));
			return invoked;
		}

		void SetMovieBoneRegion(std::uint32_t a_regionId)
		{
			if (!g_state.menuOpen) {
				LogWarning("Dropped setBoneRegion: LooksMenu is not open.");
				return;
			}

			auto* ui = RE::UI::GetSingleton();
			auto  menu = ui ? ui->GetMenu(kMenuName) : nullptr;
			if (!menu || !menu->uiMovie) {
				return;
			}

			Scaleform::GFx::Value region(static_cast<std::uint32_t>(a_regionId));
			const bool             set = menu->uiMovie->SetVariable("currentBoneRegionID", region);
			LogInfo(std::format(
				"Set currentBoneRegionID={} generation={} -> {}",
				a_regionId,
				g_state.generation.load(),
				set));
		}

		void SendCreatorAxes()
		{
			if (!g_state.menuOpen) {
				LogWarning("Dropped getAxes: LooksMenu is not open.");
				return;
			}

			auto* ui = RE::UI::GetSingleton();
			auto  menu = ui ? ui->GetMenu(kMenuName) : nullptr;
			if (!menu || !menu->uiMovie) {
				return;
			}
			auto& movie = *menu->uiMovie;

			Scaleform::GFx::Value regions;
			if (!movie.GetVariable(&regions, "FacialBoneRegions") || !regions.IsArray()) {
				LogWarning("FacialBoneRegions is not an array on the live movie; axes unavailable.");
				return;
			}

			const auto actorCount = regions.GetArraySize();
			if (actorCount != 1) {
				LogWarning(std::format(
					"FacialBoneRegions holds {} actors; the current actor index is not readable, so "
					"axes are refused rather than read from the wrong entry.",
					actorCount));
				return;
			}

			Scaleform::GFx::Value boneRegion;
			if (!movie.GetVariable(&boneRegion, "CurrentBoneID")) {
				LogWarning("CurrentBoneID is unreadable; axes unavailable.");
				return;
			}

			Scaleform::GFx::Value boneIndexResult;
			if (!movie.Invoke("GetBoneRegionIndexFromCurrentID", &boneIndexResult, nullptr, 0) ||
				!boneIndexResult.IsUInt()) {
				LogWarning("GetBoneRegionIndexFromCurrentID failed; axes unavailable.");
				return;
			}
			const auto boneIndex = boneIndexResult.GetUInt();

			Scaleform::GFx::Value actors;
			if (!regions.GetElement(0, &actors) || !actors.IsArray() ||
				boneIndex >= actors.GetArraySize()) {
				LogWarning(std::format("Bone region index {} is out of range.", boneIndex));
				return;
			}

			Scaleform::GFx::Value region;
			if (!actors.GetElement(boneIndex, &region) || !region.IsObject()) {
				LogWarning("The selected bone region is not an object; axes unavailable.");
				return;
			}

			Scaleform::GFx::Value axes;
			if (!region.GetMember("axisArray", &axes) || !axes.IsArray()) {
				LogWarning("The selected bone region has no axisArray; axes unavailable.");
				return;
			}

			nlohmann::json list = nlohmann::json::array();
			const auto     axisCount = axes.GetArraySize();
			for (std::uint32_t i = 0; i < axisCount; ++i) {
				Scaleform::GFx::Value axis;
				if (!axes.GetElement(i, &axis) || !axis.IsObject()) {
					continue;
				}

				nlohmann::json entry{ { "slot", i } };
				Scaleform::GFx::Value member;
				if (axis.GetMember("axisIndex", &member) && member.IsUInt()) {
					entry["axisIndex"] = member.GetUInt();
				}
				if (axis.GetMember("isSlider", &member) && member.IsBoolean()) {
					entry["isSlider"] = member.GetBoolean();
				}
				if (axis.GetMember("isScalingAxis", &member) && member.IsBoolean()) {
					entry["isScalingAxis"] = member.GetBoolean();
				}
				if (axis.GetMember("axisSliderValue", &member) && member.IsNumber()) {
					entry["sliderValue"] = member.GetNumber();
				}
				if (axis.GetMember("axisScalingValue", &member) && member.IsNumber()) {
					entry["scalingValue"] = member.GetNumber();
				}
				list.push_back(std::move(entry));
			}

			LogInfo(std::format(
				"getAxes boneIndex={} boneId={} -> {} axes.",
				boneIndex,
				boneRegion.IsUInt() ? boneRegion.GetUInt() : 0u,
				axisCount));

			PushEvent(nlohmann::json{
				{ "type", "creatorAxes" },
				{ "boneIndex", boneIndex },
				{ "boneId", boneRegion.IsUInt() ? boneRegion.GetUInt() : 0u },
				{ "count", axisCount },
				{ "axes", std::move(list) }
			});
		}

		#include "F4TCharacterCreatorPresets.h"

		void SendCreatorOptions(std::uint32_t a_feature, std::uint32_t a_groupIndex, bool a_groupProvided)
		{
			if (!g_state.menuOpen) {
				LogWarning("Dropped getOptions: LooksMenu is not open.");
				return;
			}

			constexpr std::uint32_t kFeatureExtras = 4;
			if (a_feature == kFeatureExtras && !a_groupProvided) {
				LogWarning(
					"getOptions for the extras feature requires an explicit 'groupIndex': "
					"4294967295 lists the extras categories, and a category index lists that "
					"category's entries.");
				return;
			}

			auto* ui = RE::UI::GetSingleton();
			auto  menu = ui ? ui->GetMenu(kMenuName) : nullptr;
			if (!menu || !menu->uiMovie) {
				return;
			}
			auto& movie = *menu->uiMovie;

			Scaleform::GFx::Value codeObj;
			if (!ResolveCodeObject(*menu, codeObj)) {
				LogWarning("BGSCodeObj is not resolvable; options unavailable.");
				return;
			}
			if (!codeObj.HasMember("GetFeatureData")) {
				LogWarning("BGSCodeObj.GetFeatureData is absent on the live movie; refused.");
				return;
			}

			auto groupIndex = a_groupIndex;
			if (!a_groupProvided) {
				groupIndex = ReadMovieBoneId(*menu);
			}

			Scaleform::GFx::Value list;
			movie.CreateArray(&list);

			Scaleform::GFx::Value args[3];
			args[0] = list;
			args[1] = Scaleform::GFx::Value(a_feature);
			args[2] = Scaleform::GFx::Value(groupIndex);

			Scaleform::GFx::Value result;
			if (!codeObj.Invoke("GetFeatureData", &result, args, 3)) {
				LogWarning(std::format(
					"GetFeatureData(feature={}, group={}) failed.",
					a_feature,
					groupIndex));
				return;
			}

			nlohmann::json options = nlohmann::json::array();
			const auto     size = list.IsArray() ? list.GetArraySize() : 0u;
			for (std::uint32_t i = 0; i < size; ++i) {
				Scaleform::GFx::Value entry;
				if (!list.GetElement(i, &entry) || !entry.IsObject()) {
					continue;
				}

				nlohmann::json option{ { "index", i } };
				Scaleform::GFx::Value member;
				if (entry.GetMember("applied", &member) && member.IsBoolean()) {
					option["applied"] = member.GetBoolean();
				}
				if (entry.GetMember("text", &member) && member.IsString()) {
					option["text"] = member.GetString();
				}
				options.push_back(std::move(option));
			}

			LogInfo(std::format(
				"GetFeatureData(feature={}, group={}) -> return={} options={}",
				a_feature,
				groupIndex,
				result.IsUInt() ? result.GetUInt() : 0u,
				size));

			PushEvent(nlohmann::json{
				{ "type", "creatorOptions" },
				{ "feature", a_feature },
				{ "groupIndex", groupIndex },
				{ "featureData", result.IsUInt() ? result.GetUInt() : 0u },
				{ "count", size },
				{ "options", std::move(options) }
			});
		}

		void DispatchCreatorAction(std::string_view a_json, std::uint64_t a_generation) try
		{
			if (!g_state.menuOpen || a_generation != g_state.generation.load()) {
				return;
			}
			const auto action = nlohmann::json::parse(a_json);

			const auto actionName = action.value("action", "");
			if (actionName != "getState" || action.contains("generation")) {
				const auto generation = action.value("generation", nlohmann::json{});
				if (!generation.is_number_integer() ||
					(!generation.is_number_unsigned() && generation.get<std::int64_t>() < 0) ||
					generation.get<std::uint64_t>() != a_generation) {
					return;
				}
			}
			if (actionName == "getState") {
				PushState();
				return;
			}

			if (actionName == "input") {
				const auto control = action.value("control", "");
				if (control.empty()) {
					LogWarning("input requires a 'control'.");
					return;
				}
				SendMovieInput(control, action.value("keyUp", false));
				return;
			}

			if (actionName == "getOptions") {
				const auto feature = action.value("feature", nlohmann::json{});
				if (!feature.is_number_integer()) {
					LogWarning("getOptions requires an integer 'feature'.");
					return;
				}
				const auto featureId = feature.get<std::int64_t>();
				if (featureId < 0 || featureId > 8) {
					LogWarning(std::format("getOptions feature {} out of range.", featureId));
					return;
				}
				const auto group = action.contains("groupIndex") ? action["groupIndex"] : nlohmann::json{};
				if (action.contains("groupIndex") && !group.is_number_integer()) {
					LogWarning("getOptions 'groupIndex' must be an integer.");
					return;
				}
				const auto groupValue = group.is_number_integer() ? group.get<std::int64_t>() : 0;
				if (group.is_number_integer() && (groupValue < 0 || groupValue > 0xFFFFFFFFLL)) {
					LogWarning(std::format("getOptions groupIndex {} out of range.", groupValue));
					return;
				}
				SendCreatorOptions(
					static_cast<std::uint32_t>(featureId),
					static_cast<std::uint32_t>(groupValue),
					group.is_number_integer());
				return;
			}

			if (actionName == "getPresets") {
				SendCreatorPresets();
				return;
			}

			if (actionName == "loadPreset") {
				const auto path = action.value("path", "");
				if (path.empty()) {
					LogWarning("loadPreset requires a 'path'.");
					return;
				}
				LoadCreatorPreset(path);
				return;
			}

			if (actionName == "getAxes") {
				SendCreatorAxes();
				return;
			}

			if (actionName == "setBoneRegion") {
				const auto value = action.value("value", nlohmann::json{});
				if (!value.is_number_integer()) {
					LogWarning("setBoneRegion requires an integer 'value'.");
					return;
				}
				const auto regionId = value.get<std::int64_t>();
				if (regionId < 0 || regionId > 0xFFFFFFFFLL) {
					LogWarning(std::format("setBoneRegion {} out of range.", regionId));
					return;
				}
				SetMovieBoneRegion(static_cast<std::uint32_t>(regionId));
				return;
			}

			const auto* command = FindCommand(actionName);
			if (!command) {
				LogWarning(std::format("Unknown creator action '{}'.", actionName));
				return;
			}

			CreatorRequest request;
			request.member = command->member;
			request.arity = command->arity;
			request.argKind = command->argKind;
			request.undoType = command->undoType;
			request.undoWithBone = command->undoWithBone;
			request.generation = a_generation;

			switch (command->argKind) {
			case ArgKind::Bool:
				request.flag = action.value("flag", false);
				break;
			case ArgKind::Index:
			case ArgKind::MorphPreset:
			{
				const auto value = action.value("value", nlohmann::json{});
				if (!value.is_number_integer()) {
					LogWarning(std::format("{} requires an integer 'value'.", actionName));
					return;
				}
				const auto index = value.get<std::int64_t>();
				if (index < 0 || index > kMaxIndexArg) {
					LogWarning(std::format("{} index {} out of range.", actionName, index));
					return;
				}
				request.x = static_cast<double>(index);
				break;
			}
			case ArgKind::TintApply:
			case ArgKind::TintRemove:
			{
				const auto group = action.value("group", nlohmann::json{});
				if (!group.is_number_integer()) {
					LogWarning(std::format("{} requires an integer 'group'.", actionName));
					return;
				}
				const auto groupValue = group.get<std::int64_t>();
				if (groupValue < 0 || groupValue > 0xFFFFFFFFLL) {
					LogWarning(std::format("{} group {} out of range.", actionName, groupValue));
					return;
				}
				request.group = static_cast<std::uint32_t>(groupValue);

				if (command->argKind == ArgKind::TintApply) {
					const auto value = action.value("value", nlohmann::json{});
					if (!value.is_number_integer()) {
						LogWarning(std::format("{} requires an integer 'value'.", actionName));
						return;
					}
					const auto index = value.get<std::int64_t>();
					if (index < 0 || index > kMaxIndexArg) {
						LogWarning(std::format("{} index {} out of range.", actionName, index));
						return;
					}
					request.x = static_cast<double>(index);
				}
				break;
			}
			case ArgKind::UnitPair:
			{
				const auto x = action.value("x", nlohmann::json{});
				const auto y = action.value("y", nlohmann::json{});
				if (!x.is_number() || !y.is_number()) {
					LogWarning(std::format("{} requires numeric 'x' and 'y'.", actionName));
					return;
				}
				const auto xv = x.get<double>();
				const auto yv = y.get<double>();
				if (xv < 0.0 || xv > 1.0 || yv < 0.0 || yv > 1.0) {
					LogWarning(std::format("{} x/y must be within [0,1].", actionName));
					return;
				}
				request.x = xv;
				request.y = yv;
				break;
			}
			case ArgKind::SignedUnit:
			{
				const auto value = action.value("value", nlohmann::json{});
				if (!value.is_number()) {
					LogWarning(std::format("{} requires a numeric 'value'.", actionName));
					return;
				}
				const auto signedValue = value.get<double>();
				if (signedValue < -1.0 || signedValue > 1.0) {
					LogWarning(std::format("{} value must be within [-1,1].", actionName));
					return;
				}
				request.x = signedValue;
				break;
			}
			case ArgKind::AxisValue:
			{
				const auto axis = action.value("axis", nlohmann::json{});
				const auto value = action.value("value", nlohmann::json{});
				if (!axis.is_number_integer()) {
					LogWarning(std::format("{} requires an integer 'axis'.", actionName));
					return;
				}
				const auto axisIndex = axis.get<std::int64_t>();
				if (axisIndex < 0 || axisIndex > kMaxIndexArg) {
					LogWarning(std::format("{} axis {} out of range.", actionName, axisIndex));
					return;
				}
				if (!value.is_number()) {
					LogWarning(std::format("{} requires a numeric 'value'.", actionName));
					return;
				}
				const auto axisValue = value.get<double>();
				if (axisValue < -1.0 || axisValue > 1.0) {
					LogWarning(std::format("{} value must be within [-1,1].", actionName));
					return;
				}
				request.x = static_cast<double>(axisIndex);
				request.y = axisValue;
				break;
			}
			case ArgKind::None:
			default:
				break;
			}

			InvokeCodeObject(request);
		}
		catch (const nlohmann::json::exception& ex) {
			LogWarning(std::format("Rejected coCreatorAction JSON: {}", ex.what()));
		}
	}

	void HandleCreatorAction(const char* a_json)
	{
		if (!a_json) {
			return;
		}

		const auto        generation = g_state.generation.load();
		const std::string json{ a_json };
		const auto*       taskInterface = F4SE::GetTaskInterface();
		if (!taskInterface) {
			LogWarning("Dropped coCreatorAction: game-thread task interface unavailable.");
			return;
		}

		taskInterface->AddTask([json, generation]() {
			DispatchCreatorAction(json, generation);
		});
	}
}
