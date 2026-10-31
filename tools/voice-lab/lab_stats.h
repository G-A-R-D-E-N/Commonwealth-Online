#pragma once

#include <cstddef>
#include <cstdio>
#include <vector>

namespace VoiceLab
{
	double Sum(const std::vector<double>& a_values);
	double Mean(const std::vector<double>& a_values);
	double Max(const std::vector<double>& a_values);
	double Percentile(std::vector<double> a_values, double a_fraction);
	std::vector<std::size_t> Histogram(const std::vector<double>& a_values, double a_binWidth, std::size_t a_bins);

	inline bool PrintCriterion(const char* a_name, bool a_pass)
	{
		std::printf("criterion %s: %s\n", a_name, a_pass ? "PASS" : "FAIL");
		return a_pass;
	}
}
