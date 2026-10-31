#pragma once

#include <cstdint>

using PrismaView = std::uint64_t;

namespace PRISMA_UI_API
{
	using JSCallback = void (*)(const char*);

	class IVPrismaUI4
	{
	public:
		virtual void Invoke(PrismaView a_view, const char* a_script, JSCallback a_callback = nullptr) noexcept = 0;
		virtual bool IsHidden(PrismaView a_view) noexcept = 0;
		virtual bool IsValid(PrismaView a_view) noexcept = 0;
	};
}
