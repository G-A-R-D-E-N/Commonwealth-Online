#include "F4TValidationAttempt.h"

#include <cassert>
#include <memory>
#include <thread>

namespace
{
	F4T::ValidationAttempt::ConnectFailure Failure(F4T::ValidationAttempt::ConnectFailureKind a_kind)
	{
		return { a_kind, 0, {}, {} };
	}
}

int main()
{
	F4T::ValidationAttempt isolated;
	isolated.Publish(Failure(F4T::ValidationAttempt::ConnectFailureKind::AdmissionRefused));
	isolated.Publish(Failure(F4T::ValidationAttempt::ConnectFailureKind::SessionRejected));
	assert(isolated.Take()->kind == F4T::ValidationAttempt::ConnectFailureKind::SessionRejected);
	assert(!isolated.Take());

	F4T::ValidationAttempt priority;
	priority.Publish(Failure(F4T::ValidationAttempt::ConnectFailureKind::SessionRejected));
	priority.Publish(Failure(F4T::ValidationAttempt::ConnectFailureKind::LocalHandshakeFailure));
	assert(priority.Take()->kind == F4T::ValidationAttempt::ConnectFailureKind::LocalHandshakeFailure);

	F4T::ValidationAttempt other;
	assert(!other.Take());

	auto surviving = std::make_shared<F4T::ValidationAttempt>();
	{
		auto socketOwner = surviving;
		socketOwner->Publish(Failure(F4T::ValidationAttempt::ConnectFailureKind::LocalHandshakeFailure));
	}
	assert(surviving->Take()->kind == F4T::ValidationAttempt::ConnectFailureKind::LocalHandshakeFailure);

	std::thread publisher([&surviving]() {
		surviving->Publish(Failure(F4T::ValidationAttempt::ConnectFailureKind::AdmissionRefused));
	});
	publisher.join();
	assert(surviving->Take()->kind == F4T::ValidationAttempt::ConnectFailureKind::AdmissionRefused);
	return 0;
}
