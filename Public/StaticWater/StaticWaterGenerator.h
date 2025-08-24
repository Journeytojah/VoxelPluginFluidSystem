#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/World.h"
#include "RHI.h"
#include "RenderResource.h"
#include "StaticWaterGenerator.generated.h"

USTRUCT(BlueprintType)
struct VOXELFLUIDSYSTEM_API FStaticWaterRegionDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Water")
	FBox Bounds = FBox(EForceInit::ForceInit);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Water")
	float WaterLevel = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Water")
	bool bInfiniteDepth = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Water")
	float MinDepth = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Water")
	int32 Priority = 0; // Higher priority overrides lower priority

	bool ContainsPoint(const FVector& Point) const
	{
		// For rendering purposes, we want to render water surfaces where chunks are above the water level
		// The point should be within the XY bounds, and we'll render water if the terrain is below water level
		return Bounds.IsInsideXY(Point);
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
struct VOXELFLUIDSYSTEM_API FStaticWaterTile
{
	GENERATED_BODY()

	UPROPERTY()
	FIntVector TileCoord = FIntVector::ZeroValue;

	UPROPERTY()
	FBox WorldBounds = FBox(EForceInit::ForceInit);

	UPROPERTY()
	float WaterLevel = 0.0f;

	UPROPERTY()
	bool bHasWater = false;

	UPROPERTY()
	bool bNeedsUpdate = true;

	// GPU buffer data
	TArray<float> TerrainHeights;
	TArray<float> WaterDepths;
	
	void Initialize(const FIntVector& InCoord, float TileSize, float CellSize)
	{
		TileCoord = InCoord;
		const int32 CellsPerTile = FMath::CeilToInt(TileSize / CellSize);
		
		WorldBounds.Min = FVector(InCoord.X * TileSize, InCoord.Y * TileSize, -10000.0f);
		WorldBounds.Max = FVector((InCoord.X + 1) * TileSize, (InCoord.Y + 1) * TileSize, 10000.0f);
		
		TerrainHeights.SetNumZeroed(CellsPerTile * CellsPerTile);
		WaterDepths.SetNumZeroed(CellsPerTile * CellsPerTile);
	}

	bool IsValid() const { return TerrainHeights.Num() > 0; }
};

USTRUCT(BlueprintType)
struct VOXELFLUIDSYSTEM_API FStaticWaterGenerationSettings
{
	GENERATED_BODY()

	// Tile system settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tiling", meta = (ClampMin = "1000", ClampMax = "10000"))
	float TileSize = 6400.0f; // 64x64m tiles

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tiling", meta = (ClampMin = "10", ClampMax = "200"))
	float CellSize = 50.0f; // 50cm cells

	// Generation settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation")
	bool bUseGPUGeneration = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta = (ClampMin = "1", ClampMax = "16"))
	int32 GenerationThreads = 4;

	// LOD settings  
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD", meta = (ClampMin = "500", ClampMax = "5000"))
	float LOD0Distance = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD", meta = (ClampMin = "1000", ClampMax = "10000"))
	float LOD1Distance = 5000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD", meta = (ClampMin = "5000", ClampMax = "50000"))
	float MaxGenerationDistance = 20000.0f;

	// Performance settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance", meta = (ClampMin = "1", ClampMax = "32"))
	int32 MaxTilesPerFrame = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance", meta = (ClampMin = "10", ClampMax = "1000"))
	int32 MaxCachedTiles = 256;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float UpdateFrequency = 0.1f;
};

/**
 * Generates static water based on terrain data without simulation dependency
 * Uses GPU compute shaders for parallel terrain sampling and water placement
 */
UCLASS(BlueprintType, Blueprintable, ClassGroup=(VoxelFluidSystem), meta=(BlueprintSpawnableComponent))
class VOXELFLUIDSYSTEM_API UStaticWaterGenerator : public UActorComponent
{
	GENERATED_BODY()

public:
	UStaticWaterGenerator();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Configuration
	UFUNCTION(BlueprintCallable, Category = "Static Water Generation")
	void SetVoxelWorld(AActor* InVoxelWorld);

	UFUNCTION(BlueprintCallable, Category = "Static Water Generation")
	void AddWaterRegion(const FStaticWaterRegionDef& Region);

	UFUNCTION(BlueprintCallable, Category = "Static Water Generation")
	void RemoveWaterRegion(int32 RegionIndex);

	UFUNCTION(BlueprintCallable, Category = "Static Water Generation")
	void ClearAllWaterRegions();

	// Generation control
	UFUNCTION(BlueprintCallable, Category = "Static Water Generation")
	void SetViewerPosition(const FVector& Position);

	UFUNCTION(BlueprintCallable, Category = "Static Water Generation")
	void RegenerateAroundViewer();

	UFUNCTION(BlueprintCallable, Category = "Static Water Generation", meta = (CallInEditor = "true"))
	void ForceRegenerateAll();

	// Terrain interaction
	UFUNCTION(BlueprintCallable, Category = "Static Water Generation")
	void OnTerrainChanged(const FBox& ChangedBounds);

	// Query methods
	UFUNCTION(BlueprintCallable, Category = "Static Water Generation")
	bool HasStaticWaterAtLocation(const FVector& WorldPosition) const;

	UFUNCTION(BlueprintCallable, Category = "Static Water Generation")
	float GetWaterLevelAtLocation(const FVector& WorldPosition) const;

	UFUNCTION(BlueprintCallable, Category = "Static Water Generation")
	float GetWaterDepthAtLocation(const FVector& WorldPosition) const;

	UFUNCTION(BlueprintCallable, Category = "Static Water Generation")
	TArray<FIntVector> GetActiveTileCoords() const;

	// Settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation Settings")
	FStaticWaterGenerationSettings GenerationSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Regions")
	TArray<FStaticWaterRegionDef> WaterRegions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Integration")
	AActor* TargetVoxelWorld = nullptr;

	// Debug settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bShowTileBounds = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bShowWaterRegions = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bEnableLogging = false;

protected:
	// Core generation
	void UpdateTileGeneration(float DeltaTime);
	void GenerateTileData(FStaticWaterTile& Tile);
	void GenerateTileDataGPU(FStaticWaterTile& Tile);
	void GenerateTileDataCPU(FStaticWaterTile& Tile);

	// Terrain sampling
	bool SampleTerrainHeight(const FVector& WorldPosition, float& OutHeight) const;
	void SampleTerrainHeightsInBounds(const FBox& Bounds, int32 Resolution, TArray<float>& OutHeights) const;

	// Tile management
	FIntVector WorldPositionToTileCoord(const FVector& WorldPosition) const;
	FVector TileCoordToWorldPosition(const FIntVector& TileCoord) const;
	void UpdateActiveTiles();
	void LoadTile(const FIntVector& TileCoord);
	void UnloadTile(const FIntVector& TileCoord);
	bool ShouldLoadTile(const FIntVector& TileCoord) const;
	bool ShouldUnloadTile(const FIntVector& TileCoord) const;

	// Water region evaluation
	bool EvaluateWaterAtPosition(const FVector& Position, float& OutWaterLevel) const;
	const FStaticWaterRegionDef* GetHighestPriorityRegionAtPosition(const FVector& Position) const;

	// GPU resources
	void InitializeGPUResources();
	void ReleaseGPUResources();

	// Debug visualization
	void DrawDebugInfo() const;

private:
	// Tile cache
	TMap<FIntVector, FStaticWaterTile> LoadedTiles;
	TSet<FIntVector> ActiveTileCoords;
	TQueue<FIntVector> TileLoadQueue;
	TQueue<FIntVector> TileUnloadQueue;

	// Viewer tracking
	FVector ViewerPosition = FVector::ZeroVector;
	FVector LastViewerPosition = FVector(MAX_flt);

	// Update timers
	float TileUpdateTimer = 0.0f;
	float ViewerUpdateTimer = 0.0f;

	// Voxel world reference
	TWeakObjectPtr<AActor> VoxelWorldPtr;
	class UVoxelFluidIntegration* VoxelIntegration = nullptr;

	// GPU resources
	bool bGPUResourcesInitialized = false;
	FRenderCommandFence RenderFence;

	// Performance tracking
	int32 TilesGeneratedThisFrame = 0;
	float LastGenerationTime = 0.0f;

	// Thread safety
	mutable FCriticalSection TileCacheMutex;

	bool bIsInitialized = false;
};