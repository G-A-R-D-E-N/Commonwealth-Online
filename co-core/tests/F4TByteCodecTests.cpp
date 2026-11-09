#include "F4TByteCodec.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

int main()
{
	using F4T::ByteCodec::GetLittleEndian;
	using F4T::ByteCodec::PutLittleEndian;

	{
		std::array<std::byte, 8> buffer{};
		std::size_t offset = 0;
		PutLittleEndian(buffer, offset, 0x0102030405060708ULL, 8);
		assert(offset == 8);
		assert(buffer[0] == std::byte{ 0x08 });
		assert(buffer[3] == std::byte{ 0x05 });
		assert(buffer[7] == std::byte{ 0x01 });
	}

	{
		std::array<std::byte, 4> buffer{};
		std::size_t offset = 0;
		PutLittleEndian(buffer, offset, 0xAABBCCDDULL, 2);
		assert(offset == 2);
		assert(buffer[0] == std::byte{ 0xDD });
		assert(buffer[1] == std::byte{ 0xCC });
		assert(buffer[2] == std::byte{ 0x00 });
	}

	{
		std::array<std::byte, 8> buffer{};
		std::size_t offset = 0;
		PutLittleEndian(buffer, offset, 0x123456ULL, 3);
		PutLittleEndian(buffer, offset, 0xFFULL, 1);
		assert(offset == 4);

		std::size_t readOffset = 0;
		assert(GetLittleEndian(buffer, readOffset, 3) == 0x123456ULL);
		assert(readOffset == 3);
		assert(GetLittleEndian(buffer, readOffset, 1) == 0xFFULL);
		assert(readOffset == 4);
	}

	{
		std::array<std::byte, 8> buffer{};
		std::size_t offset = 3;
		PutLittleEndian(buffer, offset, 0xFFFFFFFFFFFFFFFFULL, 0);
		assert(offset == 3);
		assert(GetLittleEndian(buffer, offset, 0) == 0);
		assert(offset == 3);
	}

	{
		const std::array<std::byte, 8> bytes{ std::byte{ 0x78 }, std::byte{ 0x56 }, std::byte{ 0x34 }, std::byte{ 0x12 },
			std::byte{ 0xEF }, std::byte{ 0xCD }, std::byte{ 0xAB }, std::byte{ 0x90 } };
		std::size_t offset = 0;
		assert(GetLittleEndian(bytes, offset, 4) == 0x12345678ULL);
		assert(GetLittleEndian(bytes, offset, 4) == 0x90ABCDEFULL);
		assert(offset == 8);
	}

	{
		std::array<std::byte, 8> buffer{};
		std::size_t offset = 0;
		PutLittleEndian(buffer, offset, 0xFFFFFFFFFFFFFFFFULL, 8);
		std::size_t readOffset = 0;
		assert(GetLittleEndian(buffer, readOffset, 8) == 0xFFFFFFFFFFFFFFFFULL);
	}

	return 0;
}
