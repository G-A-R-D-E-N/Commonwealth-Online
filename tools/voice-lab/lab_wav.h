#pragma once

#include <cstdint>
#include <string>
#include <vector>

bool LoadCorpus(const std::string& a_path, std::vector<std::int16_t>& a_pcm);
