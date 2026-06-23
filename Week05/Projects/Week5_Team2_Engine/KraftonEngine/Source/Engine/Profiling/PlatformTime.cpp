#include "Profiling/PlatformTime.h"

double FWindowsPlatformTime::GSecondsPerCycle = 0.0;
bool   FWindowsPlatformTime::bInitialized     = false;
