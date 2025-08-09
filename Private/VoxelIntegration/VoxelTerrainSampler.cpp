#include "VoxelIntegration/VoxelTerrainSampler.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "Components/PrimitiveComponent.h"

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