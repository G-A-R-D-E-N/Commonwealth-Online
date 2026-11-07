#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace F4T::CrashBreadcrumbs
{
	inline constexpr std::size_t Capacity = 256;

	enum class Code : std::uint16_t
	{
		kUnknown = 0,
		kCellChange = 1,
		kAuthorityEpoch = 2,
		kPuppetSpawn = 3,
		kPuppetDelete = 4,
		kProxyDespawn = 5,
		kPrivateBaseBuilt = 6,
		kPrivateBaseReused = 7,
		kPrivateBaseQueued = 8,
		kPrivateBaseParked = 9,
		kPrivateBaseLeaked = 10,
		kPrivateBaseCapped = 11,
		kLoadDrop = 12,
	};

	[[nodiscard]] constexpr std::string_view Name(Code a_code) noexcept
	{
		switch (a_code) {
		case Code::kCellChange:
			return "cell-change";
		case Code::kAuthorityEpoch:
			return "authority-epoch";
		case Code::kPuppetSpawn:
			return "puppet-spawn";
		case Code::kPuppetDelete:
			return "puppet-delete";
		case Code::kProxyDespawn:
			return "proxy-despawn";
		case Code::kPrivateBaseBuilt:
			return "private-base-built";
		case Code::kPrivateBaseReused:
			return "private-base-reused";
		case Code::kPrivateBaseQueued:
			return "private-base-queued";
		case Code::kPrivateBaseParked:
			return "private-base-parked";
		case Code::kPrivateBaseLeaked:
			return "private-base-leaked";
		case Code::kPrivateBaseCapped:
			return "private-base-capped";
		case Code::kLoadDrop:
			return "load-drop";
		case Code::kUnknown:
			break;
		}
		return "unknown";
	}

	struct Event
	{
		std::uint64_t seq{};
		std::uint64_t tickMs{};
		std::uint32_t threadId{};
		Code code{ Code::kUnknown };
		std::uint32_t refId{};
		std::uint32_t aux{};
	};

	// Lock-free ring. Writers never block; a reader skips slots that are being rewritten.
	class Ring
	{
	public:
		void Record(std::uint64_t a_tickMs, std::uint32_t a_threadId, Code a_code, std::uint32_t a_formId, std::uint32_t a_aux) noexcept
		{
			const auto seq = next_.fetch_add(1, std::memory_order_relaxed);
			auto& slot = slots_[seq % Capacity];
			slot.stamp.store(0, std::memory_order_relaxed);
			slot.tick.store(a_tickMs, std::memory_order_relaxed);
			slot.meta.store((static_cast<std::uint64_t>(a_threadId) << 32) | static_cast<std::uint16_t>(a_code), std::memory_order_relaxed);
			slot.data.store((static_cast<std::uint64_t>(a_formId) << 32) | a_aux, std::memory_order_relaxed);
			slot.stamp.store(seq + 1, std::memory_order_release);
		}

		[[nodiscard]] std::uint64_t Total() const noexcept
		{
			return next_.load(std::memory_order_acquire);
		}

		std::size_t Snapshot(std::span<Event> a_out) const noexcept
		{
			const auto total = Total();
			const auto available = static_cast<std::size_t>(std::min<std::uint64_t>(total, Capacity));
			const auto count = std::min(available, a_out.size());
			std::size_t written = 0;
			for (auto seq = total - count; seq < total; ++seq) {
				const auto& slot = slots_[seq % Capacity];
				const auto before = slot.stamp.load(std::memory_order_acquire);
				Event event{};
				event.seq = seq;
				event.tickMs = slot.tick.load(std::memory_order_relaxed);
				const auto meta = slot.meta.load(std::memory_order_relaxed);
				const auto data = slot.data.load(std::memory_order_relaxed);
				const auto after = slot.stamp.load(std::memory_order_acquire);
				if (before != seq + 1 || after != seq + 1) {
					continue;
				}
				event.threadId = static_cast<std::uint32_t>(meta >> 32);
				event.code = static_cast<Code>(static_cast<std::uint16_t>(meta & 0xFFFFU));
				event.refId = static_cast<std::uint32_t>(data >> 32);
				event.aux = static_cast<std::uint32_t>(data & 0xFFFFFFFFU);
				a_out[written++] = event;
			}
			return written;
		}

	private:
		struct Slot
		{
			std::atomic<std::uint64_t> stamp{};
			std::atomic<std::uint64_t> tick{};
			std::atomic<std::uint64_t> meta{};
			std::atomic<std::uint64_t> data{};
		};

		std::array<Slot, Capacity> slots_{};
		std::atomic<std::uint64_t> next_{};
	};

	class LineWriter
	{
	public:
		explicit LineWriter(std::span<char> a_out) noexcept :
			out_(a_out)
		{
		}

		void Text(std::string_view a_text) noexcept
		{
			for (const char c : a_text) {
				Put(c);
			}
		}

		void Decimal(std::uint64_t a_value) noexcept
		{
			std::array<char, 20> digits{};
			std::size_t count = 0;
			do {
				digits[count++] = static_cast<char>('0' + (a_value % 10));
				a_value /= 10;
			} while (a_value != 0);
			while (count != 0) {
				Put(digits[--count]);
			}
		}

		void Hex(std::uint64_t a_value, std::size_t a_digits) noexcept
		{
			constexpr std::string_view kDigits = "0123456789ABCDEF";
			for (auto index = a_digits; index != 0; --index) {
				Put(kDigits[(a_value >> (4 * (index - 1))) & 0xFU]);
			}
		}

		[[nodiscard]] std::size_t Size() const noexcept
		{
			return used_;
		}

	private:
		void Put(char a_c) noexcept
		{
			if (used_ < out_.size()) {
				out_[used_++] = a_c;
			}
		}

		std::span<char> out_;
		std::size_t used_{ 0 };
	};

	[[nodiscard]] inline std::size_t FormatLine(const Event& a_event, std::span<char> a_out) noexcept
	{
		LineWriter writer{ a_out };
		writer.Text("#");
		writer.Decimal(a_event.seq);
		writer.Text(" +");
		writer.Decimal(a_event.tickMs);
		writer.Text("ms t=");
		writer.Decimal(a_event.threadId);
		writer.Text(" ");
		writer.Text(Name(a_event.code));
		writer.Text(" form=");
		writer.Hex(a_event.refId, 8);
		writer.Text(" aux=");
		writer.Decimal(a_event.aux);
		writer.Text("\n");
		return writer.Size();
	}
}
