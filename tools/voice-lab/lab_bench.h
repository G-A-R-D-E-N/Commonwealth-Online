#pragma once

#include <string>

struct BenchOptions
{
	std::string wavPath;
	double minutes{ 10.0 };
	int complexity{ 5 };
	int frameMs{ 20 };
	int decoders{ 4 };
	double loss{ 0.10 };
};

int RunBench(const BenchOptions& a_options);
