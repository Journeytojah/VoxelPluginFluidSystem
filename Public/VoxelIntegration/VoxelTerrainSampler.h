#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "VoxelTerrainSampler.generated.h"

UCLASS(BlueprintType)
class VOXELFLUIDSYSTEM_API UVoxelTerrainSampler : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Voxel Sampling", meta = (CallInEditor = "true"))
	static float SampleTerrainHeightAtLocation(UObject* WorldContextObject, const FVector& WorldLocation);

	UFUNCTION(BlueprintCallable, Category = "Voxel Sampling")
	static bool GetTerrainNormalAtLocation(UObject* WorldContextObject, const FVector& WorldLocation, FVector& OutNormal);

	UFUNCTION(BlueprintCallable, Category = "Voxel Sampling")
	static void SampleTerrainInBounds(UObject* WorldContextObject, const FVector& BoundsMin, const FVector& BoundsMax, 
									   float SampleResolution, TArray<float>& OutHeights, TArray<FVector>& OutPositions);

	UFUNCTION(BlueprintCallable, Category = "Voxel Sampling")
	static bool IsPointInsideTerrain(UObject* WorldContextObject, const FVector& WorldLocation);

private:
	static bool PerformLineTrace(UWorld* World, const FVector& StartLocation, const FVector& EndLocation, FHitResult& OutHit);
};