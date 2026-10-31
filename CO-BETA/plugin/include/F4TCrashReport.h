// Copyright (C) 2026 FatherLamb. Portions derived from FathersFallout, GPL-3.0 with additional terms. Used with the author's permission.
#pragma once

#include "F4THookThreadTable.h"

#include <cstddef>
#include <cstdint>

namespace F4SE
{
	class LoadInterface;
}

namespace F4T::CrashReport
{
	constexpr std::size_t kMaxModuleName = 64;
	constexpr std::size_t kMaxFrames = 32;
	constexpr std::size_t kRegisterCount = 16;

	struct Location
	{
		char module[kMaxModuleName]{};
		std::uint64_t offset{};
		std::uint64_t address{};
	};

	struct Data
	{
		std::uint16_t year{};
		std::uint8_t month{};
		std::uint8_t day{};
		std::uint8_t hour{};
		std::uint8_t minute{};
		std::uint8_t second{};
		std::uint16_t millisecond{};
		std::uint32_t exceptionCode{};
		Location faulting{};
		std::uint32_t threadId{};
		bool mainThread{};
		bool accessViolation{};
		std::uint8_t accessType{};
		std::uint64_t accessAddress{};
		std::uint64_t registers[kRegisterCount]{};
		std::size_t frameCount{};
		Location frames[kMaxFrames]{};
	};

	namespace detail
	{
		struct Writer
		{
			char* out{};
			std::size_t capacity{};
			std::size_t length{};
		};

		inline void PutChar(Writer& a_writer, char a_char) noexcept
		{
			if (a_writer.length + 1 < a_writer.capacity) {
				a_writer.out[a_writer.length++] = a_char;
			}
		}

		inline void PutText(Writer& a_writer, const char* a_text) noexcept
		{
			while (*a_text != '\0') {
				PutChar(a_writer, *a_text++);
			}
		}

		inline void PutDigits(Writer& a_writer, std::uint64_t a_value, unsigned a_minDigits) noexcept
		{
			char buffer[20]{};
			unsigned count = 0;
			do {
				buffer[count++] = static_cast<char>('0' + a_value % 10);
				a_value /= 10;
			} while (a_value != 0);
			while (count < a_minDigits && count < sizeof(buffer)) {
				buffer[count++] = '0';
			}
			while (count > 0) {
				PutChar(a_writer, buffer[--count]);
			}
		}

		inline void PutHex(Writer& a_writer, std::uint64_t a_value, unsigned a_minDigits) noexcept
		{
			constexpr char kDigits[] = "0123456789ABCDEF";
			char buffer[16]{};
			unsigned count = 0;
			do {
				buffer[count++] = kDigits[a_value & 0xF];
				a_value >>= 4;
			} while (a_value != 0);
			while (count < a_minDigits && count < sizeof(buffer)) {
				buffer[count++] = '0';
			}
			while (count > 0) {
				PutChar(a_writer, buffer[--count]);
			}
		}

		inline void PutLocation(Writer& a_writer, const Location& a_location) noexcept
		{
			if (a_location.module[0] != '\0') {
				for (std::size_t index = 0; index < kMaxModuleName && a_location.module[index] != '\0'; ++index) {
					PutChar(a_writer, a_location.module[index]);
				}
				PutText(a_writer, "+0x");
				PutHex(a_writer, a_location.offset, 1);
			} else {
				PutText(a_writer, "0x");
				PutHex(a_writer, a_location.address, 16);
			}
		}

		inline const char* RegisterName(std::size_t a_index) noexcept
		{
			constexpr const char* kNames[kRegisterCount] = {
				"rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",
				"r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
			};
			return kNames[a_index];
		}

		inline void PutRegisterLine(Writer& a_writer, const Data& a_data, std::size_t a_first) noexcept
		{
			PutText(a_writer, "[CO CRASH] ");
			for (std::size_t index = 0; index < 4; ++index) {
				if (index != 0) {
					PutChar(a_writer, ' ');
				}
				PutText(a_writer, RegisterName(a_first + index));
				PutChar(a_writer, '=');
				PutText(a_writer, "0x");
				PutHex(a_writer, a_data.registers[a_first + index], 16);
			}
			PutText(a_writer, "\r\n");
		}
	}

	inline std::size_t Format(const Data& a_data, char* a_out, std::size_t a_capacity) noexcept
	{
		if (a_out == nullptr || a_capacity == 0) {
			return 0;
		}

		detail::Writer writer{ a_out, a_capacity, 0 };
		using detail::PutChar;
		using detail::PutDigits;
		using detail::PutHex;
		using detail::PutLocation;
		using detail::PutText;

		PutText(writer, "\r\n[CO CRASH] ===== game crash report =====\r\n");
		PutText(writer, "[CO CRASH] time=");
		PutDigits(writer, a_data.year, 4);
		PutChar(writer, '-');
		PutDigits(writer, a_data.month, 2);
		PutChar(writer, '-');
		PutDigits(writer, a_data.day, 2);
		PutChar(writer, ' ');
		PutDigits(writer, a_data.hour, 2);
		PutChar(writer, ':');
		PutDigits(writer, a_data.minute, 2);
		PutChar(writer, ':');
		PutDigits(writer, a_data.second, 2);
		PutChar(writer, '.');
		PutDigits(writer, a_data.millisecond, 3);
		PutText(writer, "\r\n");

		PutText(writer, "[CO CRASH] exception=0x");
		PutHex(writer, a_data.exceptionCode, 8);
		PutText(writer, " fault=");
		PutLocation(writer, a_data.faulting);
		PutText(writer, "\r\n");

		PutText(writer, "[CO CRASH] thread=");
		PutDigits(writer, a_data.threadId, 1);
		PutText(writer, a_data.mainThread ? " main-thread=yes\r\n" : " main-thread=no\r\n");

		if (a_data.accessViolation) {
			const char* access = a_data.accessType == 0 ? "read" : a_data.accessType == 1 ? "write" : "execute";
			PutText(writer, "[CO CRASH] access=");
			PutText(writer, access);
			PutText(writer, " address=0x");
			PutHex(writer, a_data.accessAddress, 16);
			PutText(writer, "\r\n");
		}

		detail::PutRegisterLine(writer, a_data, 0);
		detail::PutRegisterLine(writer, a_data, 4);
		detail::PutRegisterLine(writer, a_data, 8);
		detail::PutRegisterLine(writer, a_data, 12);

		if (a_data.frameCount == 0) {
			PutText(writer, "[CO CRASH] frames=unavailable\r\n");
		} else {
			const auto frameCount = a_data.frameCount < kMaxFrames ? a_data.frameCount : kMaxFrames;
			for (std::size_t index = 0; index < frameCount; ++index) {
				PutText(writer, "[CO CRASH] frame ");
				PutDigits(writer, index, 2);
				PutChar(writer, ' ');
				PutLocation(writer, a_data.frames[index]);
				PutText(writer, "\r\n");
			}
		}

		F4T::HookThread::Row hookRows[F4T::HookThread::kCapacity]{};
		const auto hookCount = F4T::HookThread::Snapshot(hookRows, F4T::HookThread::kCapacity);
		for (std::size_t index = 0; index < hookCount; ++index) {
			PutText(writer, "[CO CRASH] hook=");
			PutDigits(writer, hookRows[index].hookId, 1);
			PutText(writer, " thread=");
			PutDigits(writer, hookRows[index].threadId, 1);
			PutText(writer, " count=");
			PutDigits(writer, hookRows[index].count, 1);
			PutText(writer, "\r\n");
		}

		PutText(writer, "[CO CRASH] ===== end crash report =====\r\n");

		writer.out[writer.length] = '\0';
		return writer.length;
	}

	inline std::size_t FormatFirstChance(std::uint32_t a_exceptionCode, const Location& a_location, char* a_out, std::size_t a_capacity) noexcept
	{
		if (a_out == nullptr || a_capacity == 0) {
			return 0;
		}

		detail::Writer writer{ a_out, a_capacity, 0 };
		detail::PutText(writer, "[CO CRASH] first-chance exception=0x");
		detail::PutHex(writer, a_exceptionCode, 8);
		detail::PutText(writer, " fault=");
		detail::PutLocation(writer, a_location);
		detail::PutText(writer, " (the game may still handle it)\r\n");

		writer.out[writer.length] = '\0';
		return writer.length;
	}

	void Install() noexcept;
	void KeepPreviousLog(const F4SE::LoadInterface* a_f4se) noexcept;
	void Reassert() noexcept;
	void Tick() noexcept;
}
