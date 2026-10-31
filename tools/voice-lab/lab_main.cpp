#include "lab_bench.h"
#include "lab_gns.h"
#include "lab_loss.h"
#include "lab_loop.h"
#include "lab_tload.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
	int Usage()
	{
		std::fprintf(stderr, "usage: voice-lab bench <wav> [--minutes 10] [--complexity 5] [--frame-ms 20|40] [--decoders 4] [--loss 0.10]\n"
			"usage: voice-lab loss <wav> --profile a|b|c|d|e [--seconds 180] [--warmup 30] [--seed N]\n"
			"usage: voice-lab gns --bridge <dll> [--seconds 60] [--senders 4 (max 63)] [--rate 50] [--seed N]\n"
			"usage: voice-lab loop [--seconds 60] [--complexity 5] [--simulate <wav>]\n"
			"usage: voice-lab latency [--seconds 60] [--threshold-dbfs -30] [--simulate <wav>]\n"
			"usage: voice-lab tload --bridge <dll> --port <server port> --server-pid <pid> --voice on|off [--relay-cap-bps N] [--clients 16] [--seconds 600] [--seed N] [--admin-port P --admin-token-file <path>] [--baseline <voice-off summary file>]\n");
		return 2;
	}

	bool ParseBench(int a_argc, char** a_argv, BenchOptions& a_options)
	{
		if (a_argc < 3 || (a_argc - 3) % 2 != 0)
		{
			return false;
		}
		a_options.wavPath = a_argv[2];
		for (int i = 3; i < a_argc; i += 2)
		{
			const std::string flag = a_argv[i];
			const char* value = a_argv[i + 1];
			if (flag == "--minutes")
			{
				a_options.minutes = std::atof(value);
			}
			else if (flag == "--complexity")
			{
				a_options.complexity = std::atoi(value);
			}
			else if (flag == "--frame-ms")
			{
				a_options.frameMs = std::atoi(value);
			}
			else if (flag == "--decoders")
			{
				a_options.decoders = std::atoi(value);
			}
			else if (flag == "--loss")
			{
				a_options.loss = std::atof(value);
			}
			else
			{
				return false;
			}
		}
		return a_options.minutes > 0.0 && a_options.complexity >= 0 && a_options.complexity <= 10 &&
			(a_options.frameMs == 20 || a_options.frameMs == 40) && a_options.decoders >= 1 &&
			a_options.loss >= 0.0 && a_options.loss < 1.0;
	}

	int Dispatch(int a_argc, char** a_argv)
	{
		if (a_argc < 2)
		{
			return Usage();
		}
		if (std::strcmp(a_argv[1], "bench") == 0)
		{
			BenchOptions options;
			if (!ParseBench(a_argc, a_argv, options))
			{
				return Usage();
			}
			return RunBench(options);
		}
		if (std::strcmp(a_argv[1], "loss") == 0)
		{
			return VoiceLab::Loss::RunLoss(a_argc, a_argv);
		}
		if (std::strcmp(a_argv[1], "gns") == 0)
		{
			return RunGns(a_argc, a_argv);
		}
		if (std::strcmp(a_argv[1], "tload") == 0)
		{
			return RunTload(a_argc, a_argv);
		}
		if (std::strcmp(a_argv[1], "loop") == 0)
		{
			return RunLoop(a_argc, a_argv, false);
		}
		if (std::strcmp(a_argv[1], "latency") == 0)
		{
			return RunLoop(a_argc, a_argv, true);
		}
		return Usage();
	}
}

int main(int a_argc, char** a_argv)
{
	return Dispatch(a_argc, a_argv);
}
