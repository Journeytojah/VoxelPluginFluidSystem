#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CellularAutomata/CAFluidGrid.h"
#include "VoxelStackLayer.h"
#include "VoxelIntegration/VoxelTerrainSampler.h"
#include "VoxelFluidIntegration.generated.h"

class AActor;
class UFluidChunkManager;
struct FFluidChunkCoord;

UENUM(BlueprintType)
enum class E3DVoxelQueryMode : uint8
{
	SingleLayer		UMETA(DisplayName = "Single Layer"),
	CombineLayers	UMETA(DisplayName = "Combine Multiple Layers"),
	MinValue		UMETA(DisplayName = "Use Minimum Value"),
	MaxValue		UMETA(DisplayName = "Use Maximum Value")
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class VOXELFLUIDSYSTEM_API UVoxelFluidIntegration : public UActorComponent
{
	GENERATED_BODY()

public:
	UVoxelFluidIntegration();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:
	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void InitializeFluidSystem(AActor* InVoxelWorld);

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void SetChunkManager(UFluidChunkManager* InChunkManager);

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void SyncWithVoxelTerrain();

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void UpdateTerrainHeights();

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void UpdateChunkedTerrainHeights();

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void UpdateTerrainForChunkCoord(const struct FFluidChunkCoord& ChunkCoord);

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	float SampleVoxelHeight(float WorldX, float WorldY);

	// Batch sampling for performance
	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	TArray<float> SampleVoxelHeightsBatch(const TArray<FVector>& Positions);

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void SampleChunkTerrainBatch(const struct FFluidChunkCoord& ChunkCoord);

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void UpdateTerrainHeightsWithVoxelLayer();

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void Update3DVoxelTerrain();

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void UpdateChunk3DVoxelTerrain(const struct FFluidChunkCoord& ChunkCoord);

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void DetectTerrainChangesAndUpdate();

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void OnVoxelTerrainModified(const FBox& ModifiedBounds);

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid", meta = (DisplayName = "Refresh 3D Terrain After Sculpting"))
	void RefreshTerrainAfterSculpting();

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void RefreshTerrainInRadius(const FVector& Center, float Radius);

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid", meta = (DisplayName = "Force Refresh Voxel Cache"))
	void ForceRefreshVoxelCache();

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid Debug", meta = (DisplayName = "Log Available Voxel Layers"))
	void LogAvailableVoxelLayers();

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void AddFluidAtWorldPosition(const FVector& WorldPosition, float Amount);

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void RemoveFluidAtWorldPosition(const FVector& WorldPosition, float Amount);

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	bool IsVoxelWorldValid() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Fluid")
	AActor* VoxelWorld;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Fluid")
	UCAFluidGrid* FluidGrid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Fluid")
	UFluidChunkManager* ChunkManager;

	// Performance settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
	bool bUseAsyncTerrainSampling = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance", meta = (ClampMin = "1", ClampMax = "1000"))
	int32 MaxTerrainSamplesPerFrame = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance", meta = (ClampMin = "1", ClampMax = "32"))
	int32 TerrainSamplingThreads = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
	bool bEnableTerrainCaching = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance", meta = (ClampMin = "10", ClampMax = "1000"))
	float TerrainCacheGridSize = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	int32 GridResolutionX = 128;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	int32 GridResolutionY = 128;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	int32 GridResolutionZ = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	float CellWorldSize = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	bool bAutoUpdateTerrain = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	float TerrainUpdateInterval = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	float MinFluidToRender = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain")
	FVoxelStackLayer TerrainLayer;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain")
	EVoxelSamplingMethod SamplingMethod = EVoxelSamplingMethod::VoxelQuery;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain")
	bool bUseVoxelLayerSampling = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain", meta = (DisplayName = "Use 3D Voxel Terrain"))
	bool bUse3DVoxelTerrain = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain", meta = (DisplayName = "3D Terrain Layer", EditCondition = "bUse3DVoxelTerrain"))
	FVoxelStackLayer Terrain3DLayer;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain", meta = (DisplayName = "Use Separate 3D Layer", EditCondition = "bUse3DVoxelTerrain"))
	bool bUseSeparate3DLayer = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain", meta = (DisplayName = "3D Query Mode", EditCondition = "bUse3DVoxelTerrain"))
	E3DVoxelQueryMode Terrain3DQueryMode = E3DVoxelQueryMode::SingleLayer;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain", meta = (DisplayName = "Additional 3D Layers", EditCondition = "bUse3DVoxelTerrain && Terrain3DQueryMode != E3DVoxelQueryMode::SingleLayer"))
	TArray<FVoxelStackLayer> Additional3DLayers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain", meta = (EditCondition = "bUse3DVoxelTerrain", ClampMin = "-1.0", ClampMax = "1.0"))
	float SolidThreshold = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain", meta = (EditCondition = "bUse3DVoxelTerrain"))
	float VoxelSampleOffset = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain", meta = (DisplayName = "Auto Refresh After Sculpting", EditCondition = "bUse3DVoxelTerrain"))
	bool bAutoRefreshAfterSculpting = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain", meta = (DisplayName = "Refresh Interval", EditCondition = "bUse3DVoxelTerrain && bAutoRefreshAfterSculpting"))
	float TerrainRefreshInterval = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain", meta = (DisplayName = "Use Multiple Sample Points", EditCondition = "bUse3DVoxelTerrain"))
	bool bUseMultipleSamplePoints = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain Debug", meta = (DisplayName = "Debug Draw Solid Cells", EditCondition = "bUse3DVoxelTerrain"))
	bool bDebugDrawSolidCells = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain Debug", meta = (DisplayName = "Invert Solid Detection", EditCondition = "bUse3DVoxelTerrain"))
	bool bInvertSolidDetection = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain Debug", meta = (DisplayName = "Log Voxel Values", EditCondition = "bUse3DVoxelTerrain"))
	bool bLogVoxelValues = false;

private:
	float TerrainUpdateTimer = 0.0f;
	FVector GridWorldOrigin;
	bool bUseChunkedSystem = false;

	// Terrain change tracking
	TMap<FIntVector, bool> CachedVoxelStates;
	TArray<FBox> PendingTerrainUpdates;
	FCriticalSection TerrainUpdateMutex;
	float LastTerrainRefreshTime = 0.0f;
	bool bTerrainNeedsRefresh = false;

	void DrawDebugFluid();
	void DrawChunkedDebugFluid();
	void DrawDebugSolidCells();
	
	void UpdateTerrainForChunk(const FVector& ChunkWorldMin, const FVector& ChunkWorldMax, int32 ChunkSize, float CellSize);
	void UpdateTerrainInRegion(const FBox& Region);
	void UpdateChunkCellsInRegion(class UFluidChunk* Chunk, const FBox& Region);
	void UpdateGridCellsInRegion(const FBox& Region);
	
	bool CheckIfCellIsSolid(const FVector& CellCenter, int32 GridX, int32 GridY, int32 GridZ);
	bool QueryVoxelAtPosition(const FVector& WorldPosition, float& OutVoxelValue);
	
	int32 UpdateGridCellsInRadius(const FVector& Center, float Radius);
	int32 UpdateChunkCellsInRadius(class UFluidChunk* Chunk, const FVector& Center, float Radius);
	void WakeFluidInRadius(const FVector& Center, float Radius);

	// Terrain caching for performance
	struct FTerrainCacheEntry
	{
		FVector2D Position;
		float Height;
		float CacheTime;
		
		FTerrainCacheEntry() : Position(FVector2D::ZeroVector), Height(0.0f), CacheTime(0.0f) {}
		FTerrainCacheEntry(const FVector2D& InPosition, float InHeight) 
			: Position(InPosition), Height(InHeight), CacheTime(FPlatformTime::Seconds()) {}
	};
	
	TMap<FIntPoint, FTerrainCacheEntry> TerrainHeightCache;
	float TerrainCacheLifetime = 30.0f; // Cache for 30 seconds
	float LastCacheCleanupTime = 0.0f;
	
	// Batch sampling queues
	TQueue<FFluidChunkCoord> PendingChunkSampling;
	TArray<FVector> BatchSamplePositions;
	TArray<FFluidChunkCoord> BatchChunkCoords;
	int32 CurrentBatchSize = 0;
	
	// Async processing
	bool bIsProcessingBatch = false;
	FCriticalSection BatchSamplingMutex;
	
	// Helper functions
	FIntPoint WorldPositionToCacheKey(float WorldX, float WorldY) const;
	void CleanupTerrainCache();
	bool GetCachedHeight(float WorldX, float WorldY, float& OutHeight) const;
	void CacheHeight(float WorldX, float WorldY, float Height);
	void ProcessBatchSampling();
	void OnBatchSamplingComplete(const TArray<float>& Heights);
};