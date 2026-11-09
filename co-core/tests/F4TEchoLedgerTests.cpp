#include "F4TEchoLedger.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

int main()
{
	using F4T::EchoLedger;
	using F4T::EchoLifetimeMilliseconds;
	using F4T::MaximumEchoEntries;

	{
		EchoLedger ledger;
		assert(!ledger.Consume(1, 2, 1, 0));
		ledger.Expect(1, 2, 0, 0);
		assert(!ledger.Consume(1, 2, 1, 0));
	}

	{
		EchoLedger ledger;
		ledger.Expect(1, 2, 3, 1000);
		assert(ledger.Consume(1, 2, 2, 1000));
		assert(ledger.Consume(1, 2, 1, 1000));
		assert(!ledger.Consume(1, 2, 1, 1000));
	}

	{
		EchoLedger ledger;
		ledger.Expect(1, 2, 1, 0);
		assert(!ledger.Consume(9, 2, 1, 0));
		assert(!ledger.Consume(1, 9, 1, 0));
		assert(ledger.Consume(1, 2, 1, 0));
	}

	{
		EchoLedger ledger;
		ledger.Expect(1, 2, 1, 0);
		assert(!ledger.Consume(1, 2, 2, 0));
		assert(!ledger.Consume(1, 2, 1, 0));
	}

	{
		EchoLedger ledger;
		ledger.Expect(1, 2, 1, 1000);
		assert(ledger.Consume(1, 2, 1, 1000 + EchoLifetimeMilliseconds));
	}

	{
		EchoLedger ledger;
		ledger.Expect(1, 2, 1, 0);
		assert(!ledger.Consume(1, 2, 1, EchoLifetimeMilliseconds + 1));
	}

	{
		EchoLedger ledger;
		ledger.Expect(1, 2, 1, 0);
		ledger.Expect(1, 2, 1, 5000);
		ledger.Expire(5000);
		assert(ledger.Consume(1, 2, 1, 5000));
		assert(!ledger.Consume(1, 2, 1, 5000));
	}

	{
		EchoLedger ledger;
		for (std::size_t index = 0; index <= MaximumEchoEntries; ++index) {
			ledger.Expect(1, static_cast<std::uint32_t>(index), 1, 0);
		}
		assert(!ledger.Consume(1, 0, 1, 0));
		assert(ledger.Consume(1, 1, 1, 0));
		assert(ledger.Consume(1, static_cast<std::uint32_t>(MaximumEchoEntries), 1, 0));
	}

	return 0;
}
