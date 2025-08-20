#pragma once

#include "CoreMinimal.h"

// Forward declaration
class AVoxelFluidActor;

// Debug logging macros that respect the debug toggle
#define UE_LOG_VOXELFLUID_DEBUG(FluidActor, Verbosity, Format, ...) \
	do { \
		if (FluidActor && FluidActor->bEnableDebugLogging) \
		{ \
			UE_LOG(LogTemp, Verbosity, Format, ##__VA_ARGS__); \
		} \
	} while(0)

#define UE_LOG_VOXELFLUID_DEBUG_CONDITIONAL(FluidActor, Condition, Verbosity, Format, ...) \
	do { \
		if (FluidActor && FluidActor->bEnableDebugLogging && (Condition)) \
		{ \
			UE_LOG(LogTemp, Verbosity, Format, ##__VA_ARGS__); \
		} \
	} while(0)

// For components that don't have direct access to FluidActor, use a global debug flag
DECLARE_LOG_CATEGORY_EXTERN(LogVoxelFluidDebug, Log, All);

#define UE_LOG_VOXELFLUID_COMPONENT_DEBUG(Verbosity, Format, ...) \
	UE_CLOG(CVarEnableVoxelFluidDebugLogging.GetValueOnGameThread(), LogVoxelFluidDebug, Verbosity, Format, ##__VA_ARGS__)

// Console variable for global debug logging
extern TAutoConsoleVariable<bool> CVarEnableVoxelFluidDebugLogging;