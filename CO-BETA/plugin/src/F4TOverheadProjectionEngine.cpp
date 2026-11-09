#include "pch.h"

#include "F4TOverheadProjectionEngine.h"

namespace F4T::OverheadProjection
{
	std::optional<Pixel> ProjectWorldPoint(float a_x, float a_y, float a_z, float a_width, float a_height)
	{
		auto* camera = RE::Main::WorldRootCamera();
		if (!camera) {
			return std::nullopt;
		}
		const RE::NiPoint3 world{ a_x, a_y, a_z };
		float screenX{};
		float screenY{};
		float screenZ{};
		const bool inFrustum = camera->WorldPtToScreenPt3(world, screenX, screenY, screenZ, 1e-5F);
		const auto& m = camera->worldToCam;
		const float w = m[3][0] * a_x + m[3][1] * a_y + m[3][2] * a_z + m[3][3];
		static bool logged{};
		if (!logged) {
			logged = true;
			REX::INFO("[OverheadProjection] first projection inFrustum={} x={} y={} w={}", inFrustum, screenX, screenY, w);
		}
		if (!inFrustum) {
			return std::nullopt;
		}
		return ToPixels(w, screenX, screenY, a_width, a_height);
	}
}
