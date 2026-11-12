#include "lab_stats.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace VoiceLab
{
	double Sum(const std::vector<double>& a_values)
	{
		return std::accumulate(a_values.begin(), a_values.end(), 0.0);
	}

	double Mean(const std::vector<double>& a_values)
	{
		if (a_values.empty())
		{
			return 0.0;
		}
		return Sum(a_values) / static_cast<double>(a_values.size());
	}

	double Max(const std::vector<double>& a_values)
	{
		return a_values.empty() ? 0.0 : *std::max_element(a_values.begin(), a_values.end());
	}

	double Percentile(std::vector<double> a_values, double a_fraction)
	{
		if (a_values.empty())
		{
			return 0.0;
		}
		std::sort(a_values.begin(), a_values.end());
		const double rank = std::ceil(a_fraction * static_cast<double>(a_values.size()));
		const std::size_t index = static_cast<std::size_t>(std::clamp(rank, 1.0, static_cast<double>(a_values.size())) - 1.0);
		return a_values[index];
	}

	std::vector<std::size_t> Histogram(const std::vector<double>& a_values, double a_binWidth, std::size_t a_bins)
	{
		std::vector<std::size_t> counts(a_bins, 0);
		for (const double value : a_values)
		{
			const double position = std::floor(value / a_binWidth);
			const std::size_t bin = static_cast<std::size_t>(std::clamp(position, 0.0, static_cast<double>(a_bins - 1)));
			++counts[bin];
		}
		return counts;
	}
}
