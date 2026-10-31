#pragma once

		bool ResolveF4EE(Scaleform::GFx::Movie& a_movie, Scaleform::GFx::Value* a_out)
		{
			if (!a_movie.GetVariable(a_out, "root.f4se.plugins.F4EE") || !a_out->IsObject()) {
				return false;
			}
			return true;
		}

		void SendCreatorPresets()
		{
			if (!g_state.menuOpen) {
				LogWarning("Dropped getPresets: LooksMenu is not open.");
				return;
			}

			auto* ui = RE::UI::GetSingleton();
			auto  menu = ui ? ui->GetMenu(kMenuName) : nullptr;
			if (!menu || !menu->uiMovie) {
				return;
			}
			auto& movie = *menu->uiMovie;

			Scaleform::GFx::Value f4ee;
			if (!ResolveF4EE(movie, &f4ee) || !f4ee.HasMember("GetExternalFiles")) {
				LogWarning(
					"F4EE is not present on the live movie; presets are unavailable. LooksMenu presets "
					"are an F4EE feature, so this is expected without that plugin installed.");
				PushEvent(nlohmann::json{
					{ "type", "creatorPresets" },
					{ "available", false },
					{ "count", 0 },
					{ "presets", nlohmann::json::array() }
				});
				return;
			}

			Scaleform::GFx::Value patterns;
			movie.CreateArray(&patterns);
			patterns.PushBack(Scaleform::GFx::Value("*.json"));

			Scaleform::GFx::Value args[3];
			args[0] = Scaleform::GFx::Value(kPresetDirectory);
			args[1] = patterns;
			args[2] = Scaleform::GFx::Value(false);

			Scaleform::GFx::Value files;
			if (!f4ee.Invoke("GetExternalFiles", &files, args, 3) || !files.IsArray()) {
				LogWarning("F4EE.GetExternalFiles failed; presets are unavailable.");
				PushEvent(nlohmann::json{
					{ "type", "creatorPresets" },
					{ "available", false },
					{ "count", 0 },
					{ "presets", nlohmann::json::array() }
				});
				return;
			}

			nlohmann::json presets = nlohmann::json::array();
			const auto     fileCount = files.GetArraySize();
			for (std::uint32_t i = 0; i < fileCount; ++i) {
				Scaleform::GFx::Value entry;
				if (!files.GetElement(i, &entry) || !entry.IsObject()) {
					continue;
				}

				Scaleform::GFx::Value isDirectory;
				if (entry.GetMember("directory", &isDirectory) && isDirectory.IsBoolean() &&
					isDirectory.GetBoolean()) {
					continue;
				}

				nlohmann::json preset;
				Scaleform::GFx::Value member;
				if (entry.GetMember("name", &member) && member.IsString()) {
					preset["name"] = member.GetString();
				}
				if (entry.GetMember("path", &member) && member.IsString()) {
					preset["path"] = member.GetString();
				}
				if (preset.contains("path")) {
					presets.push_back(std::move(preset));
				}
			}

			LogInfo(std::format(
				"F4EE.GetExternalFiles(\"{}\") -> {} preset files.",
				kPresetDirectory,
				presets.size()));

			PushEvent(nlohmann::json{
				{ "type", "creatorPresets" },
				{ "available", true },
				{ "count", presets.size() },
				{ "presets", std::move(presets) }
			});
		}

		void LoadCreatorPreset(std::string_view a_path)
		{
			if (!g_state.menuOpen) {
				LogWarning("Dropped loadPreset: LooksMenu is not open.");
				return;
			}
			if (!IsPresetPathAllowed(a_path)) {
				LogWarning(std::format(
					"Rejected preset path '{}'; it must stay inside the F4EE preset directory.",
					a_path));
				return;
			}

			auto* ui = RE::UI::GetSingleton();
			auto  menu = ui ? ui->GetMenu(kMenuName) : nullptr;
			if (!menu || !menu->uiMovie) {
				return;
			}

			Scaleform::GFx::Value f4ee;
			if (!ResolveF4EE(*menu->uiMovie, &f4ee) || !f4ee.HasMember("LoadPreset")) {
				LogWarning("F4EE.LoadPreset is absent on the live movie; refused.");
				return;
			}

			const std::string     path{ a_path };
			Scaleform::GFx::Value args[1];
			args[0] = Scaleform::GFx::Value(path.c_str());

			Scaleform::GFx::Value status;
			const bool            invoked = f4ee.Invoke("LoadPreset", &status, args, 1);
			LogInfo(std::format(
				"F4EE.LoadPreset(\"{}\") -> invoked={} status={}",
				path,
				invoked,
				status.IsInt() ? status.GetInt() : 0));

			PushState();
		}
