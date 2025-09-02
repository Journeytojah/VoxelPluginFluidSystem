#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "VoxelStackLayer.h"
#include "VoxelTerrainSampler.generated.h"

UENUM(BlueprintType)
enum class EVoxelSamplingMethod : uint8
{
	LineTrace UMETA(DisplayName = "Line Trace"),
	VoxelQuery UMETA(DisplayName = "Voxel Query")
};

UCLASS(BlueprintType)
class VOXELFLUIDSYSTEM_API UVoxelTerrainSampler : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Voxel Sampling", meta = (CallInEditor = "true"))
	static float SampleTerrainHeightAtLocation(UObject* WorldContextObject, const FVector& WorldLocation);

	UFUNCTION(BlueprintCallable, Category = "Voxel Sampling", meta = (CallInEditor = "true"))
	static float SampleTerrainHeightAtLocationWithLayer(UObject* WorldContextObject, const FVector& WorldLocation, 
														 const FVoxelStackLayer& VoxelLayer, EVoxelSamplingMethod SamplingMethod = EVoxelSamplingMethod::VoxelQuery);

	UFUNCTION(BlueprintCallable, Category = "Voxel Sampling")
	static bool GetTerrainNormalAtLocation(UObject* WorldContextObject, const FVector& WorldLocation, FVector& OutNormal);

	UFUNCTION(BlueprintCallable, Category = "Voxel Sampling")
	static bool GetTerrainNormalAtLocationWithLayer(UObject* WorldContextObject, const FVector& WorldLocation, 
													const FVoxelStackLayer& VoxelLayer, FVector& OutNormal, EVoxelSamplingMethod SamplingMethod = EVoxelSamplingMethod::VoxelQuery);

	UFUNCTION(BlueprintCallable, Category = "Voxel Sampling")
	static void SampleTerrainInBounds(UObject* WorldContextObject, const FVector& BoundsMin, const FVector& BoundsMax, 
									   float SampleResolution, TArray<float>& OutHeights, TArray<FVector>& OutPositions);

	UFUNCTION(BlueprintCallable, Category = "Voxel Sampling")
	static void SampleTerrainInBoundsWithLayer(UObject* WorldContextObject, const FVector& BoundsMin, const FVector& BoundsMax, 
												float SampleResolution, const FVoxelStackLayer& VoxelLayer,
												TArray<float>& OutHeights, TArray<FVector>& OutPositions, EVoxelSamplingMethod SamplingMethod = EVoxelSamplingMethod::VoxelQuery);

	// Batch sampling for better performance
	UFUNCTION(BlueprintCallable, Category = "Voxel Sampling")
	static void SampleTerrainAtPositions(UObject* WorldContextObject, const TArray<FVector>& Positions, TArray<float>& OutHeights);

	UFUNCTION(BlueprintCallable, Category = "Voxel Sampling")
	static void SampleTerrainAtPositionsWithLayer(UObject* WorldContextObject, const TArray<FVector>& Positions, 
													const FVoxelStackLayer& VoxelLayer, TArray<float>& OutHeights, 
													EVoxelSamplingMethod SamplingMethod = EVoxelSamplingMethod::VoxelQuery);

	UFUNCTION(BlueprintCallable, Category = "Voxel Sampling")
	static bool IsPointInsideTerrain(UObject* WorldContextObject, const FVector& WorldLocation);

	UFUNCTION(BlueprintCallable, Category = "Voxel Sampling")
	static bool IsPointInsideTerrainWithLayer(UObject* WorldContextObject, const FVector& WorldLocation, 
											   const FVoxelStackLayer& VoxelLayer, EVoxelSamplingMethod SamplingMethod = EVoxelSamplingMethod::VoxelQuery);

private:
	static bool PerformLineTrace(UWorld* World, const FVector& StartLocation, const FVector& EndLocation, FHitResult& OutHit);
};