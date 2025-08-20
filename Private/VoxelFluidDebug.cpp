#include "VoxelFluidDebug.h"

DEFINE_LOG_CATEGORY(LogVoxelFluidDebug);

// Console variable for global debug logging
TAutoConsoleVariable<bool> CVarEnableVoxelFluidDebugLogging(
	TEXT("voxelfluid.EnableDebugLogging"),
	false,
	TEXT("Enable debug logging for VoxelFluid system components"),
	ECVF_Default
);