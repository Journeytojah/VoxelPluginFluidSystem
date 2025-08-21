#pragma once

#include "CoreMinimal.h"
#include "CAFluidGrid.h"
#include "FluidChunk.h"
#include "StaticWaterBody.generated.h"

UENUM(BlueprintType)
enum class EStaticWaterType : uint8
{
	None,
	Ocean,
	Lake,
	River
};

USTRUCT(BlueprintType)
struct VOXELFLUIDSYSTEM_API FStaticWaterRegion
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Water")
	FBox Bounds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Water")
	float WaterLevel = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Water")
	EStaticWaterType WaterType = EStaticWaterType::Lake;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Water")
	bool bInfiniteDepth = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Water")
	float MinDepth = 100.0f;

	FStaticWaterRegion()
	{
		Bounds = FBox(EForceInit::ForceInit);
		WaterLevel = 0.0f;
		WaterType = EStaticWaterType::Lake;
		bInfiniteDepth = false;
		MinDepth = 100.0f;
	}

	FStaticWaterRegion(const FBox& InBounds, float InWaterLevel, EStaticWaterType InType = EStaticWaterType::Lake)
		: Bounds(InBounds)
		, WaterLevel(InWaterLevel)
		, WaterType(InType)
		, bInfiniteDepth(InType == EStaticWaterType::Ocean)
		, MinDepth(100.0f)
	{
	}

	bool ContainsPoint(const FVector& Point) const
	{
		return Bounds.IsInsideXY(Point) && Point.Z <= WaterLevel;
	}

	bool IntersectsChunk(const FBox& ChunkBounds) const
	{
		return Bounds.Intersect(ChunkBounds);
	}

	float GetWaterDepthAtPoint(const FVector& Point) const
	{
		if (!Bounds.IsInsideXY(Point) || Point.Z > WaterLevel)
		{
			return 0.0f;
		}

		if (bInfiniteDepth)
		{
			return FMath::Max(WaterLevel - Point.Z, MinDepth);
		}

		return FMath::Clamp(WaterLevel - Point.Z, 0.0f, WaterLevel - Bounds.Min.Z);
	}
};

USTRUCT(BlueprintType)
struct VOXELFLUIDSYSTEM_API FStaticWaterChunkData
{
	GENERATED_BODY()

	UPROPERTY()
	FFluidChunkCoord ChunkCoord;

	UPROPERTY()
	bool bHasStaticWater = false;

	UPROPERTY()
	float StaticWaterLevel = 0.0f;

	UPROPERTY()
	EStaticWaterType WaterType = EStaticWaterType::None;

	UPROPERTY()
	TArray<int32> StaticWaterCells;

	void Clear()
	{
		bHasStaticWater = false;
		StaticWaterLevel = 0.0f;
		WaterType = EStaticWaterType::None;
		StaticWaterCells.Empty();
	}

	bool IsStaticWaterCell(int32 LocalX, int32 LocalY, int32 LocalZ, int32 ChunkSize) const
	{
		if (!bHasStaticWater)
			return false;

		int32 Index = LocalX + LocalY * ChunkSize + LocalZ * ChunkSize * ChunkSize;
		return StaticWaterCells.Contains(Index);
	}

	void AddStaticWaterCell(int32 LocalX, int32 LocalY, int32 LocalZ, int32 ChunkSize)
	{
		int32 Index = LocalX + LocalY * ChunkSize + LocalZ * ChunkSize * ChunkSize;
		StaticWaterCells.AddUnique(Index);
		bHasStaticWater = true;
	}
};

UCLASS(BlueprintType)
class VOXELFLUIDSYSTEM_API UStaticWaterManager : public UObject
{
	GENERATED_BODY()

public:
	UStaticWaterManager();

	UFUNCTION(BlueprintCallable, Category = "Static Water")
	void AddStaticWaterRegion(const FStaticWaterRegion& Region);

	UFUNCTION(BlueprintCallable, Category = "Static Water")
	void RemoveStaticWaterRegion(int32 RegionIndex);

	UFUNCTION(BlueprintCallable, Category = "Static Water")
	void ClearAllStaticWaterRegions();

	UFUNCTION(BlueprintCallable, Category = "Static Water")
	FStaticWaterChunkData GenerateStaticWaterForChunk(const FFluidChunkCoord& ChunkCoord, int32 ChunkSize, float CellSize, const FVector& WorldOrigin) const;

	UFUNCTION(BlueprintCallable, Category = "Static Water")
	bool IsPointInStaticWater(const FVector& WorldPosition) const;

	UFUNCTION(BlueprintCallable, Category = "Static Water")
	float GetStaticWaterLevelAtPoint(const FVector& WorldPosition) const;

	UFUNCTION(BlueprintCallable, Category = "Static Water")
	void CreateOcean(float WaterLevel, const FBox& OceanBounds);

	UFUNCTION(BlueprintCallable, Category = "Static Water")
	void CreateLake(const FVector& Center, float Radius, float WaterLevel, float Depth);

	UFUNCTION(BlueprintCallable, Category = "Static Water")
	void CreateRectangularLake(const FBox& LakeBounds, float WaterLevel);

	void ApplyStaticWaterToChunk(UFluidChunk* Chunk) const;
	
	// Apply static water after terrain has been updated
	void ApplyStaticWaterToChunkWithTerrain(UFluidChunk* Chunk) const;
	
	// Seal chunk borders to prevent water leaking through gaps
	void SealChunkBordersAgainstTerrain(UFluidChunk* Chunk) const;

	bool ChunkIntersectsStaticWater(const FBox& ChunkBounds) const;
	
	UFUNCTION(BlueprintCallable, Category = "Static Water")
	const TArray<FStaticWaterRegion>& GetStaticWaterRegions() const { return StaticWaterRegions; }
	
	// Dynamic refill system functions
	void CreateDynamicFluidSourcesInRadius(UFluidChunk* Chunk, const FVector& Center, float Radius) const;
	bool ShouldHaveStaticWaterAt(const FVector& WorldPosition, float& OutWaterLevel) const;

protected:
	UPROPERTY()
	TArray<FStaticWaterRegion> StaticWaterRegions;

	TMap<FFluidChunkCoord, FStaticWaterChunkData> CachedChunkData;

	void InvalidateChunkCache();
};