#include "pch.h"

#include "F4TFlyOverPathData.h"

#include "F4TGamePaths.h"

#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/T/TESWorldSpace.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>

namespace F4T::FlyOver
{
	namespace
	{
		constexpr std::array<CameraPoint, 8> kCommonwealthOffsets{ {
			{ 0.0F, 0.0F, 350.0F, 0.0F, -0.25F },
			{ 900.0F, 600.0F, 500.0F, 0.55F, -0.30F },
			{ 1800.0F, 1400.0F, 700.0F, 1.05F, -0.35F },
			{ 2400.0F, 2200.0F, 650.0F, 1.45F, -0.30F },
			{ 1500.0F, 3200.0F, 500.0F, 2.40F, -0.20F },
			{ 300.0F, 2400.0F, 400.0F, 3.20F, -0.15F },
			{ -800.0F, 1200.0F, 300.0F, 4.20F, -0.20F },
			{ -400.0F, 300.0F, 280.0F, 5.10F, -0.22F },
		} };

		std::string LowerAscii(std::string a_text)
		{
			std::transform(a_text.begin(), a_text.end(), a_text.begin(), [](unsigned char a_ch) {
				return static_cast<char>(std::tolower(a_ch));
			});
			return a_text;
		}

		bool ReadPointsFromJson(
			const std::filesystem::path& a_file,
			const RE::NiPoint3& a_origin,
			CameraPoint* a_points,
			std::size_t a_capacity,
			std::size_t& a_count,
			float& a_secondsPerSegment)
		{
			std::ifstream stream(a_file);
			if (!stream.is_open()) {
				return false;
			}
			nlohmann::json json;
			try {
				stream >> json;
			} catch (const std::exception&) {
				return false;
			}
			if (!json.is_object()) {
				return false;
			}
			const auto seconds = json.find("secondsPerSegment");
			if (seconds != json.end() && seconds->is_number()) {
				a_secondsPerSegment = std::clamp(seconds->get<float>(), 1.0F, 30.0F);
			}
			const auto points = json.find("points");
			if (points == json.end() || !points->is_array()) {
				return false;
			}
			std::size_t count = 0;
			for (const auto& entry : *points) {
				if (count >= a_capacity) {
					break;
				}
				if (!entry.is_object()) {
					continue;
				}
				const auto read = [&entry](const char* a_key) -> float {
					const auto value = entry.find(a_key);
					return value != entry.end() && value->is_number() ? value->get<float>() : 0.0F;
				};
				CameraPoint point{};
				point.x = a_origin.x + read("dx");
				point.y = a_origin.y + read("dy");
				point.z = a_origin.z + read("dz");
				point.yaw = read("yaw");
				point.pitch = read("pitch");
				a_points[count++] = point;
			}
			a_count = count;
			return count > 0;
		}

		bool TryReadFile(
			const RE::NiPoint3& a_origin,
			std::string_view a_worldName,
			CameraPoint* a_points,
			std::size_t a_capacity,
			std::size_t& a_count,
			float& a_secondsPerSegment)
		{
			if (a_worldName.empty()) {
				return false;
			}
			std::filesystem::path f4seFolder;
			if (!GamePaths::ResolveF4SEFolder(f4seFolder)) {
				return false;
			}
			const auto file = std::string(a_worldName) + ".json";
			const std::array<std::filesystem::path, 2> roots{
				f4seFolder / "CommonwealthOnline" / "flyover" / file,
				f4seFolder / "Plugins" / "flyover" / file,
			};
			for (const auto& root : roots) {
				if (ReadPointsFromJson(root, a_origin, a_points, a_capacity, a_count, a_secondsPerSegment)) {
					return true;
				}
			}
			return false;
		}
	}

	PathLoad BuildPathForPlayer(Path& a_path)
	{
		PathLoad result{};
		const auto* player = RE::PlayerCharacter::GetSingleton();
		const auto* cell = player ? player->GetParentCell() : nullptr;
		const auto* world = cell ? cell->worldSpace : nullptr;
		if (!player || !cell || !world || cell->IsExterior() == false) {
			return result;
		}

		const auto editorId = world->GetFormEditorID();
		const auto worldName = LowerAscii(editorId ? std::string(editorId) : std::string());
		const auto origin = player->GetPosition();

		std::array<CameraPoint, kMaxPathPoints> absolute{};
		std::size_t                             count = 0;
		float                                   secondsPerSegment = 4.0F;
		result.fromFile = TryReadFile(origin, worldName, absolute.data(), absolute.size(), count, secondsPerSegment);
		if (!result.fromFile) {
			if (worldName.find("commonwealth") == std::string::npos) {
				return result;
			}
			for (const auto& offset : kCommonwealthOffsets) {
				CameraPoint point{};
				point.x = origin.x + offset.x;
				point.y = origin.y + offset.y;
				point.z = origin.z + offset.z;
				point.yaw = offset.yaw;
				point.pitch = offset.pitch;
				absolute[count++] = point;
			}
			secondsPerSegment = 4.0F;
		}

		a_path.Assign(absolute.data(), count, secondsPerSegment);
		result.loaded = !a_path.Empty();
		result.pointCount = a_path.PointCount();
		return result;
	}
}
