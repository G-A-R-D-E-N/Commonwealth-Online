#include "F4TVoiceTransmitRig.h"

#include <cassert>
#include <optional>

using namespace TransmitRig;

namespace Testing = F4T::Voice::Testing;

namespace
{
	using F4T::Voice::DeviceWatcher;
	using F4T::Voice::FrameLevelDbfs;
	using F4T::Voice::ReopenDelayMs;

	void TestMicHealthTransitions()
	{
		Rig rig;
		rig.now = 0.49;
		assert(!rig.tx.Status().inputs.micFailed);
		rig.now = 0.6;
		assert(rig.tx.Status().inputs.micFailed);
		assert(!rig.tx.Status().inputs.idle);
		Run(rig, 1, kSilence);
		assert(!rig.tx.Status().inputs.micFailed);
		assert(rig.tx.Status().inputs.idle);

		rig.tx.OnDeviceError();
		assert(rig.tx.Status().inputs.micFailed);
		Run(rig, 1, kSilence);
		assert(!rig.tx.Status().inputs.micFailed);

		rig.tx.OnDeviceError();
		rig.tx.OnReopenAttempt();
		assert(rig.tx.Status().inputs.micRecovering);
		Run(rig, 1, kSilence);
		assert(!rig.tx.Status().inputs.micRecovering);
	}

	void TestSilentHintAfterQuietHold()
	{
		Rig rig;
		rig.keys[kPttKey] = true;
		Run(rig, 148, kSilence);
		assert(!rig.tx.Status().inputs.micSilentHint);
		Run(rig, 4, kSilence);
		assert(rig.tx.Status().inputs.micSilentHint);
		Run(rig, 1, kTone);
		assert(!rig.tx.Status().inputs.micSilentHint);
	}

	void TestMicCallbackTimeoutBoundary()
	{
		Rig rig;
		rig.now = 0.49;
		assert(!rig.tx.Status().inputs.micFailed);
		rig.now = 0.5;
		assert(rig.tx.Status().inputs.micFailed);
	}

	void TestSilentPeakBoundary()
	{
		const Frame above = PeakFrame(4);
		const Frame below = PeakFrame(3);
		assert(FrameLevelDbfs(above).peakDbfs > -80.0);
		assert(FrameLevelDbfs(below).peakDbfs < -80.0);
		Rig audible;
		audible.keys[kPttKey] = true;
		for (int tick = 0; tick < 160; ++tick) {
			RunFrame(audible, above);
		}
		assert(!audible.tx.Status().inputs.micSilentHint);
		Rig quiet;
		quiet.keys[kPttKey] = true;
		for (int tick = 0; tick < 160; ++tick) {
			RunFrame(quiet, below);
		}
		assert(quiet.tx.Status().inputs.micSilentHint);
	}

	void TestSetupFailureLeavesTransmittingOff()
	{
		Testing::MutableFaults().failCreate = true;
		Rig rig;
		rig.keys[kPttKey] = true;
		Run(rig, 3, kTone);
		assert(!rig.tx.Status().inputs.transmitting);
		assert(rig.packets.empty());
		Testing::MutableFaults().failCreate = false;
		rig.tx.OnReopenAttempt();
		Run(rig, 2, kTone);
		assert(rig.tx.Status().inputs.transmitting);
	}

	void TestCodecFaults()
	{
		Testing::MutableFaults().failCreate = true;
		Rig rig;
		assert(rig.tx.Status().inputs.codecFailed);
		Testing::MutableFaults().failCreate = false;
		rig.tx.OnReopenAttempt();
		assert(!rig.tx.Status().inputs.codecFailed);

		Testing::MutableFaults().encodeReturn = -1;
		rig.keys[kPttKey] = true;
		Run(rig, 5, kTone);
		assert(rig.tx.Status().codecFailures == 3);
		assert(rig.tx.Status().dropped == 3);
		assert(rig.tx.Status().inputs.codecFailed);
		assert(rig.packets.empty());

		Testing::MutableFaults().encodeReturn = std::nullopt;
		Run(rig, 1, kTone);
		assert(!rig.tx.Status().inputs.codecFailed);
		assert(rig.tx.Status().sent == 1);
	}

	void TestReopenDelaySchedule()
	{
		assert(ReopenDelayMs(0) == 1000);
		assert(ReopenDelayMs(1) == 2000);
		assert(ReopenDelayMs(7) == 2000);
		assert(ReopenDelayMs(1000) == 2000);
	}

	void TestTransmittingClearsOnOutageAndStop()
	{
		Rig rig;
		rig.keys[kPttKey] = true;
		Run(rig, 3, kTone);
		assert(rig.tx.Status().inputs.transmitting);
		rig.tx.OnDeviceError();
		assert(rig.tx.Status().inputs.micFailed);
		assert(!rig.tx.Status().inputs.transmitting);
		rig.tx.OnReopenAttempt();
		Run(rig, 2, kTone);
		assert(rig.tx.Status().inputs.transmitting);
		rig.tx.Stop();
		assert(!rig.tx.Status().inputs.transmitting);
	}

	void TestCodecFailureClearsOnReopen()
	{
		Testing::MutableFaults().encodeReturn = -1;
		Rig rig;
		rig.keys[kPttKey] = true;
		Run(rig, 5, kTone);
		assert(rig.tx.Status().inputs.codecFailed);
		rig.tx.OnDeviceError();
		rig.tx.OnReopenAttempt();
		assert(!rig.tx.Status().inputs.codecFailed);
		Testing::MutableFaults().encodeReturn = std::nullopt;
		Run(rig, 2, kTone);
		assert(!rig.tx.Status().inputs.codecFailed);
	}

	void TestCodecResetFailureWaitsForReopen()
	{
		Rig rig;
		rig.keys[kPttKey] = true;
		Testing::MutableFaults().failCtl = true;
		Run(rig, 5, kTone);
		assert(rig.tx.Status().inputs.codecFailed);
		assert(rig.packets.empty());
		assert(rig.tx.Status().dropped == 3);
		assert(rig.tx.Status().codecFailures == 3);
		Testing::MutableFaults().failCtl = false;
		rig.keys[kPttKey] = false;
		for (int burst = 0; burst < 3; ++burst) {
			Run(rig, 12, kTone);
			rig.keys[kPttKey] = true;
			Run(rig, 5, kTone);
			rig.keys[kPttKey] = false;
		}
		Run(rig, 12, kTone);
		assert(rig.tx.Status().inputs.codecFailed);
		assert(rig.packets.empty());
		rig.tx.OnReopenAttempt();
		assert(!rig.tx.Status().inputs.codecFailed);
		rig.keys[kPttKey] = true;
		Run(rig, 3, kTone);
		assert(rig.packets.size() == 1);
		assert(rig.packets.front().burstStart);
	}

	void TestDeviceWatcherCallbacks()
	{
		int changes = 0;
		DeviceWatcher watcher(eCapture, [&changes] { ++changes; });
		watcher.OnDefaultDeviceChanged(eRender, eConsole, nullptr);
		assert(changes == 0);
		watcher.OnDefaultDeviceChanged(eCapture, eCommunications, nullptr);
		assert(changes == 0);
		watcher.OnDefaultDeviceChanged(eCapture, eConsole, nullptr);
		assert(changes == 1);
		watcher.OnDeviceStateChanged(nullptr, DEVICE_STATE_ACTIVE);
		assert(changes == 2);
		watcher.OnDeviceStateChanged(nullptr, DEVICE_STATE_UNPLUGGED);
		watcher.OnDeviceStateChanged(nullptr, DEVICE_STATE_DISABLED);
		watcher.OnDeviceStateChanged(nullptr, DEVICE_STATE_NOTPRESENT);
		assert(changes == 2);
		watcher.OnDeviceAdded(nullptr);
		watcher.OnDeviceRemoved(nullptr);
		assert(changes == 2);

		IMMNotificationClient* client = nullptr;
		const HRESULT queried = watcher.QueryInterface(__uuidof(IMMNotificationClient), reinterpret_cast<void**>(&client));
		assert(queried == S_OK);
		assert(client == &watcher);
		const ULONG remaining = watcher.Release();
		assert(remaining == 1);
	}
}

namespace TransmitRig
{
	void RunDeviceTests()
	{
		TestMicHealthTransitions();
		TestSilentHintAfterQuietHold();
		TestMicCallbackTimeoutBoundary();
		TestSilentPeakBoundary();
		TestSetupFailureLeavesTransmittingOff();
		TestCodecFaults();
		TestReopenDelaySchedule();
		TestTransmittingClearsOnOutageAndStop();
		TestCodecFailureClearsOnReopen();
		TestCodecResetFailureWaitsForReopen();
		TestDeviceWatcherCallbacks();
	}
}
