#include "VoxelIntegration/VoxelTerrainSampler.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "Components/PrimitiveComponent.h"
#include "VoxelLayersBlueprintLibrary.h"

float UVoxelTerrainSampler::SampleTerrainHeightAtLocation(UObject* WorldContextObject, const FVector& WorldLocation)
{
	if (!WorldContextObject)
		return 0.0f;

	UWorld* World = WorldContextObject->GetWorld();
	if (!World)
		return 0.0f;

	FHitResult Hit;
	FVector StartLocation = WorldLocation + FVector(0, 0, 10000.0f);
	FVector EndLocation = WorldLocation - FVector(0, 0, 10000.0f);

	if (PerformLineTrace(World, StartLocation, EndLocation, Hit))
	{
		UPrimitiveComponent* HitComponent = Hit.GetComponent();
		if (HitComponent)
		{
			FString ComponentName = HitComponent->GetName();
			FString ActorName = HitComponent->GetOwner() ? HitComponent->GetOwner()->GetName() : TEXT("Unknown");
			
			if (ComponentName.Contains(TEXT("Voxel")) || ActorName.Contains(TEXT("Voxel")))
			{
				return Hit.Location.Z;
			}
		}
		
		return Hit.Location.Z;
	}

	return WorldLocation.Z;
}

bool UVoxelTerrainSampler::GetTerrainNormalAtLocation(UObject* WorldContextObject, const FVector& WorldLocation, FVector& OutNormal)
{
	if (!WorldContextObject)
	{
		OutNormal = FVector::UpVector;
		return false;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World)
	{
		OutNormal = FVector::UpVector;
		return false;
	}

	FHitResult Hit;
	FVector StartLocation = WorldLocation + FVector(0, 0, 1000.0f);
	FVector EndLocation = WorldLocation - FVector(0, 0, 1000.0f);

	if (PerformLineTrace(World, StartLocation, EndLocation, Hit))
	{
		OutNormal = Hit.Normal;
		return true;
	}

	OutNormal = FVector::UpVector;
	return false;
}

void UVoxelTerrainSampler::SampleTerrainInBounds(UObject* WorldContextObject, const FVector& BoundsMin, const FVector& BoundsMax, 
												   float SampleResolution, TArray<float>& OutHeights, TArray<FVector>& OutPositions)
{
	OutHeights.Empty();
	OutPositions.Empty();

	if (!WorldContextObject || SampleResolution <= 0)
		return;

	int32 SamplesX = FMath::CeilToInt((BoundsMax.X - BoundsMin.X) / SampleResolution);
	int32 SamplesY = FMath::CeilToInt((BoundsMax.Y - BoundsMin.Y) / SampleResolution);

	OutHeights.Reserve(SamplesX * SamplesY);
	OutPositions.Reserve(SamplesX * SamplesY);

	for (int32 x = 0; x < SamplesX; ++x)
	{
		for (int32 y = 0; y < SamplesY; ++y)
		{
			FVector SamplePos = FVector(
				BoundsMin.X + x * SampleResolution,
				BoundsMin.Y + y * SampleResolution,
				(BoundsMin.Z + BoundsMax.Z) * 0.5f
			);

			float Height = SampleTerrainHeightAtLocation(WorldContextObject, SamplePos);
			
			OutPositions.Add(SamplePos);
			OutHeights.Add(Height);
		}
	}
}

bool UVoxelTerrainSampler::IsPointInsideTerrain(UObject* WorldContextObject, const FVector& WorldLocation)
{
	if (!WorldContextObject)
		return false;

	float TerrainHeight = SampleTerrainHeightAtLocation(WorldContextObject, WorldLocation);
	return WorldLocation.Z < TerrainHeight;
}

float UVoxelTerrainSampler::SampleTerrainHeightAtLocationWithLayer(UObject* WorldContextObject, const FVector& WorldLocation, 
																	   const FVoxelStackLayer& VoxelLayer, EVoxelSamplingMethod SamplingMethod)
{
	if (!WorldContextObject)
		return 0.0f;

	if (SamplingMethod == EVoxelSamplingMethod::VoxelQuery)
	{
		FVoxelQueryResult QueryResult;
		TArray<UVoxelFloatMetadata*> EmptyMetadata;
		
		if (UVoxelLayersBlueprintLibrary::QueryVoxelLayer(WorldContextObject, VoxelLayer, WorldLocation, false, EmptyMetadata, 0, QueryResult))
		{
			return QueryResult.Value;
		}
	}
	
	// Fallback to line trace
	return SampleTerrainHeightAtLocation(WorldContextObject, WorldLocation);
}

bool UVoxelTerrainSampler::GetTerrainNormalAtLocationWithLayer(UObject* WorldContextObject, const FVector& WorldLocation, 
															   const FVoxelStackLayer& VoxelLayer, FVector& OutNormal, EVoxelSamplingMethod SamplingMethod)
{
	if (!WorldContextObject)
	{
		OutNormal = FVector::UpVector;
		return false;
	}

	if (SamplingMethod == EVoxelSamplingMethod::VoxelQuery)
	{
		const float SampleDistance = 1.0f;
		
		// Sample height at center and surrounding points to calculate normal
		const float CenterHeight = SampleTerrainHeightAtLocationWithLayer(WorldContextObject, WorldLocation, VoxelLayer, SamplingMethod);
		const float RightHeight = SampleTerrainHeightAtLocationWithLayer(WorldContextObject, WorldLocation + FVector(SampleDistance, 0, 0), VoxelLayer, SamplingMethod);
		const float ForwardHeight = SampleTerrainHeightAtLocationWithLayer(WorldContextObject, WorldLocation + FVector(0, SampleDistance, 0), VoxelLayer, SamplingMethod);
		
		// Calculate normal from height differences
		const FVector Right = FVector(SampleDistance, 0, RightHeight - CenterHeight);
		const FVector Forward = FVector(0, SampleDistance, ForwardHeight - CenterHeight);
		
		OutNormal = FVector::CrossProduct(Forward, Right).GetSafeNormal();
		if (OutNormal.IsZero())
		{
			OutNormal = FVector::UpVector;
		}
		
		return true;
	}
	
	// Fallback to line trace
	return GetTerrainNormalAtLocation(WorldContextObject, WorldLocation, OutNormal);
}

void UVoxelTerrainSampler::SampleTerrainInBoundsWithLayer(UObject* WorldContextObject, const FVector& BoundsMin, const FVector& BoundsMax, 
														  float SampleResolution, const FVoxelStackLayer& VoxelLayer,
														  TArray<float>& OutHeights, TArray<FVector>& OutPositions, EVoxelSamplingMethod SamplingMethod)
{
	OutHeights.Empty();
	OutPositions.Empty();

	if (!WorldContextObject || SampleResolution <= 0)
		return;

	int32 SamplesX = FMath::CeilToInt((BoundsMax.X - BoundsMin.X) / SampleResolution);
	int32 SamplesY = FMath::CeilToInt((BoundsMax.Y - BoundsMin.Y) / SampleResolution);

	OutHeights.Reserve(SamplesX * SamplesY);
	OutPositions.Reserve(SamplesX * SamplesY);

	if (SamplingMethod == EVoxelSamplingMethod::VoxelQuery)
	{
		// Collect all positions for multi-query
		TArray<FVector> SamplePositions;
		SamplePositions.Reserve(SamplesX * SamplesY);
		
		for (int32 x = 0; x < SamplesX; ++x)
		{
			for (int32 y = 0; y < SamplesY; ++y)
			{
				FVector SamplePos = FVector(
					BoundsMin.X + x * SampleResolution,
					BoundsMin.Y + y * SampleResolution,
					(BoundsMin.Z + BoundsMax.Z) * 0.5f
				);
				SamplePositions.Add(SamplePos);
				OutPositions.Add(SamplePos);
			}
		}
		
		// Use multi-query for better performance
		TArray<FVoxelQueryResult> QueryResults;
		TArray<UVoxelFloatMetadata*> EmptyMetadata;
		
		if (UVoxelLayersBlueprintLibrary::MultiQueryVoxelLayer(WorldContextObject, VoxelLayer, SamplePositions, false, EmptyMetadata, 0, QueryResults))
		{
			for (const FVoxelQueryResult& Result : QueryResults)
			{
				OutHeights.Add(Result.Value);
			}
			return;
		}
	}
	
	// Fallback to single queries or line traces
	for (int32 x = 0; x < SamplesX; ++x)
	{
		for (int32 y = 0; y < SamplesY; ++y)
		{
			FVector SamplePos = FVector(
				BoundsMin.X + x * SampleResolution,
				BoundsMin.Y + y * SampleResolution,
				(BoundsMin.Z + BoundsMax.Z) * 0.5f
			);

			float Height = SampleTerrainHeightAtLocationWithLayer(WorldContextObject, SamplePos, VoxelLayer, SamplingMethod);
			
			OutPositions.Add(SamplePos);
			OutHeights.Add(Height);
		}
	}
}

bool UVoxelTerrainSampler::IsPointInsideTerrainWithLayer(UObject* WorldContextObject, const FVector& WorldLocation, 
														 const FVoxelStackLayer& VoxelLayer, EVoxelSamplingMethod SamplingMethod)
{
	if (!WorldContextObject)
		return false;

	if (SamplingMethod == EVoxelSamplingMethod::VoxelQuery)
	{
		FVoxelQueryResult QueryResult;
		TArray<UVoxelFloatMetadata*> EmptyMetadata;
		
		if (UVoxelLayersBlueprintLibrary::QueryVoxelLayer(WorldContextObject, VoxelLayer, WorldLocation, false, EmptyMetadata, 0, QueryResult))
		{
			// For volume queries, negative values typically mean inside the volume
			return QueryResult.Value < 0.0f;
		}
	}
	
	// Fallback to line trace method
	return IsPointInsideTerrain(WorldContextObject, WorldLocation);
}

bool UVoxelTerrainSampler::PerformLineTrace(UWorld* World, const FVector& StartLocation, const FVector& EndLocation, FHitResult& OutHit)
{
	if (!World)
		return false;

	FCollisionQueryParams QueryParams;
	QueryParams.bTraceComplex = true;
	QueryParams.bReturnPhysicalMaterial = false;

	ECollisionChannel TraceChannel = ECC_Visibility;

	return World->LineTraceSingleByChannel(
		OutHit,
		StartLocation,
		EndLocation,
		TraceChannel,
		QueryParams
	);
}

void UVoxelTerrainSampler::SampleTerrainAtPositions(UObject* WorldContextObject, const TArray<FVector>& Positions, TArray<float>& OutHeights)
{
	OutHeights.Empty();
	OutHeights.Reserve(Positions.Num());
	
	if (!WorldContextObject || Positions.Num() == 0)
		return;
	
	for (const FVector& Position : Positions)
	{
		float Height = SampleTerrainHeightAtLocation(WorldContextObject, Position);
		OutHeights.Add(Height);
	}
}

void UVoxelTerrainSampler::SampleTerrainAtPositionsWithLayer(UObject* WorldContextObject, const TArray<FVector>& Positions, 
	const FVoxelStackLayer& VoxelLayer, TArray<float>& OutHeights, EVoxelSamplingMethod SamplingMethod)
{
	OutHeights.Empty();
	OutHeights.Reserve(Positions.Num());
	
	if (!WorldContextObject || Positions.Num() == 0)
		return;
	
	if (SamplingMethod == EVoxelSamplingMethod::VoxelQuery && Positions.Num() > 1)
	{
		TArray<FVoxelQueryResult> QueryResults;
		TArray<UVoxelFloatMetadata*> EmptyMetadata;
		
		if (UVoxelLayersBlueprintLibrary::MultiQueryVoxelLayer(WorldContextObject, VoxelLayer, Positions, false, EmptyMetadata, 0, QueryResults))
		{
			for (const FVoxelQueryResult& Result : QueryResults)
			{
				OutHeights.Add(Result.Value);
			}
			return;
		}
	}
	
	for (const FVector& Position : Positions)
	{
		float Height = SampleTerrainHeightAtLocationWithLayer(WorldContextObject, Position, VoxelLayer, SamplingMethod);
		OutHeights.Add(Height);
	}
}