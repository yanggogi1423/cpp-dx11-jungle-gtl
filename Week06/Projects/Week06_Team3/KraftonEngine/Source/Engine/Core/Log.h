#pragma once

#include <cstdio>

#ifndef UE_LOG
#define UE_LOG(Format, ...) \
	do { \
		std::printf("[Log] " Format "\n", ##__VA_ARGS__); \
	} while (0)
#endif

