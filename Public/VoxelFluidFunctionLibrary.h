#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VoxelFluidFunctionLibrary.generated.h"

class AVoxelFluidActor;

UCLASS()
class VOXELFLUIDSYSTEM_API UVoxelFluidFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid", meta = (WorldContext = "WorldContextObject"))
	static AVoxelFluidActor* SpawnFluidSystem(UObject* WorldContextObject, const FVector& Location, 
											   int32 GridSizeX = 128, int32 GridSizeY = 128, int32 GridSizeZ = 32);

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	static void AddRainToFluidSystem(AVoxelFluidActor* FluidActor, float Intensity = 0.1f, float Radius = 1000.0f);

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	static void CreateFluidSource(AVoxelFluidActor* FluidActor, const FVector& SourceLocation, float FlowRate = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	static void CreateFluidSplash(AVoxelFluidActor* FluidActor, const FVector& ImpactLocation, float SplashRadius = 200.0f, float SplashAmount = 0.5f);

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid", meta = (WorldContext = "WorldContextObject"))
	static void SyncAllFluidActorsWithTerrain(UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "Voxel Fluid")
	static float GetFluidDepthAtLocation(AVoxelFluidActor* FluidActor, const FVector& WorldLocation);

	UFUNCTION(BlueprintPure, Category = "Voxel Fluid")
	static bool IsLocationSubmerged(AVoxelFluidActor* FluidActor, const FVector& WorldLocation, float MinDepth = 10.0f);

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid", meta = (CallInEditor = "true"))
	static void TestFluidOnTerrain(AVoxelFluidActor* FluidActor, int32 NumTestPoints = 10);
};