#pragma once

#include "Manifest.h"

void publish(const std::filesystem::path& input, const std::filesystem::path& output, const Manifest& manifest);
