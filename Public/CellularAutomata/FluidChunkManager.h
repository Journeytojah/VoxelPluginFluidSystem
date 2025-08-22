#pragma once

#include "CoreMinimal.h"
#include "FluidChunk.h"
#include "Engine/World.h"
#include "Optimization/FluidOctree.h"
#include "FluidChunkManager.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnChunkLoaded, const FFluidChunkCoord&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnChunkUnloaded, const FFluidChunkCoord&);

USTRUCT(BlueprintType)
struct VOXELFLUIDSYSTEM_API FChunkStreamingConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	float ActiveDistance = 6000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	float LoadDistance = 10000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	float UnloadDistance = 12000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	int32 MaxActiveChunks = 80;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	int32 MaxLoadedChunks = 160;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	float ChunkUpdateInterval = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD")
	float LOD1Distance = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD")
	float LOD2Distance = 4000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
	bool bUseAsyncLoading = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
	int32 MaxChunksToProcessPerFrame = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Persistence")
	bool bEnablePersistence = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Persistence")
	int32 MaxCachedChunks = 256;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Persistence")
	float CacheExpirationTime = 300.0f; // 5 minutes
};

USTRUCT(BlueprintType)
struct VOXELFLUIDSYSTEM_API FChunkManagerStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 TotalChunks = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 ActiveChunks = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 InactiveChunks = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 BorderOnlyChunks = 0;

	UPROPERTY(BlueprintReadOnly)
	float TotalFluidVolume = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	int32 TotalActiveCells = 0;

	UPROPERTY(BlueprintReadOnly)
	float AverageChunkUpdateTime = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float LastFrameUpdateTime = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	int32 ChunkLoadQueueSize = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 ChunkUnloadQueueSize = 0;
};

UCLASS(BlueprintType, Blueprintable)
class VOXELFLUIDSYSTEM_API UFluidChunkManager : public UObject
{
	GENERATED_BODY()

public:
	UFluidChunkManager();

	void Initialize(int32 InChunkSize, float InCellSize, const FVector& InWorldOrigin, const FVector& InWorldSize);
	
	void SetStaticWaterManager(class UStaticWaterManager* InStaticWaterManager) { StaticWaterManager = InStaticWaterManager; }
	
	void UpdateChunks(float DeltaTime, const TArray<FVector>& ViewerPositions);
	
	void UpdateSimulation(float DeltaTime);
	
	UFluidChunk* GetChunk(const FFluidChunkCoord& Coord);
	UFluidChunk* GetOrCreateChunk(const FFluidChunkCoord& Coord);
	
	bool IsChunkLoaded(const FFluidChunkCoord& Coord) const;
	bool IsChunkActive(const FFluidChunkCoord& Coord) const;
	
	void RequestChunkLoad(const FFluidChunkCoord& Coord);
	void RequestChunkUnload(const FFluidChunkCoord& Coord);
	
	FFluidChunkCoord GetChunkCoordFromWorldPosition(const FVector& WorldPos) const;
	bool GetCellFromWorldPosition(const FVector& WorldPos, FFluidChunkCoord& OutChunkCoord, int32& OutLocalX, int32& OutLocalY, int32& OutLocalZ) const;
	
	void AddFluidAtWorldPosition(const FVector& WorldPos, float Amount);
	void RemoveFluidAtWorldPosition(const FVector& WorldPos, float Amount);
	float GetFluidAtWorldPosition(const FVector& WorldPos) const;
	
	void SetTerrainHeightAtWorldPosition(const FVector& WorldPos, float Height);
	
	void ClearAllChunks();
	
	TArray<UFluidChunk*> GetActiveChunks() const;
	TArray<UFluidChunk*> GetChunksInRadius(const FVector& Center, float Radius) const;
	TArray<FFluidChunkCoord> GetChunksInBounds(const FBox& Bounds) const;
	
	FChunkManagerStats GetStats() const;
	
	UFUNCTION(BlueprintCallable, Category = "Chunk System")
	int32 GetLoadedChunkCount() const { return LoadedChunks.Num(); }
	
	UFUNCTION(BlueprintCallable, Category = "Chunk System")
	int32 GetActiveChunkCount() const { return ActiveChunkCoords.Num(); }
	
	void SetStreamingConfig(const FChunkStreamingConfig& NewConfig);
	const FChunkStreamingConfig& GetStreamingConfig() const { return StreamingConfig; }
	
	void ForceUpdateChunkStates();
	
	void EnableChunkDebugVisualization(bool bEnable);
	
	void DrawDebugChunks(UWorld* World) const;
	bool ShouldUpdateDebugVisualization() const;
	
	// Persistence methods
	void SaveChunkData(const FFluidChunkCoord& Coord, const FChunkPersistentData& Data);
	bool LoadChunkData(const FFluidChunkCoord& Coord, FChunkPersistentData& OutData);
	void ClearChunkCache();
	void PruneExpiredCache();
	int32 GetCacheMemoryUsage() const;
	int32 GetCacheSize() const;
	void SaveCacheToDisk();
	void LoadCacheFromDisk();
	
	// Debug methods
	UFUNCTION(BlueprintCallable, Category = "Debug")
	void TestPersistence(const FVector& WorldPos);
	
	UFUNCTION(BlueprintCallable, Category = "Debug") 
	void ForceUnloadAllChunks();
	
	// Public method to activate a chunk for testing/restoration
	void ForceActivateChunk(UFluidChunk* Chunk);
	
	// Check if chunk operations are in progress
	bool IsProcessingChunkOperations() const { return bFreezeFluidForChunkOps; }

public:
	FOnChunkLoaded OnChunkLoadedDelegate;
	FOnChunkUnloaded OnChunkUnloadedDelegate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk Settings")
	int32 ChunkSize = 32;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk Settings")
	float CellSize = 100.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Settings")
	FVector WorldOrigin = FVector::ZeroVector;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Settings")
	FVector WorldSize = FVector(100000.0f, 100000.0f, 10000.0f);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	FChunkStreamingConfig StreamingConfig;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	float FlowRate = 0.5f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	float Viscosity = 0.1f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	float Gravity = 981.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EvaporationRate = 0.0f;
	
	// Optimization settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization")
	bool bUseSleepChains = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization")
	bool bUseSparseGrid = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization", meta = (ClampMin = "0.1", ClampMax = "0.5"))
	float SparseGridThreshold = 0.3f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization")
	bool bUsePredictiveSettling = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization")
	bool bUseOptimizedParallelProcessing = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization")
	float SleepChainMergeDistance = 3.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization")
	float PredictiveSettlingConfidenceThreshold = 0.95f;
	
	// Enable memory compression for fluid cells
	void EnableCompressedMode(bool bEnable);
	
	// Octree optimization methods
	UFUNCTION(BlueprintCallable, Category = "Optimization")
	void EnableOctreeOptimization(bool bEnable);
	
	UFUNCTION(BlueprintCallable, Category = "Optimization")
	bool IsOctreeEnabled() const { return bUseOctree; }
	
	UFUNCTION(BlueprintCallable, Category = "Optimization")
	FString GetOctreeStats() const;
	
	UFUNCTION(BlueprintCallable, Category = "Optimization")
	void OptimizeOctree();
	
	UFUNCTION(BlueprintCallable, Category = "Optimization")
	TArray<UFluidChunk*> QueryChunksInFrustum(const FMatrix& ViewProjectionMatrix, float MaxDistance);
	
	UFUNCTION(BlueprintCallable, Category = "Optimization")
	bool IsOctreeValid() const { return FluidOctree != nullptr; }
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization|Octree")
	bool bUseOctree = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization|Octree")
	bool bDrawOctreeDebug = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization|Octree", meta = (ClampMin = "1000.0", ClampMax = "10000.0"))
	float OctreeDebugDrawDistance = 5000.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bShowChunkBorders = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bShowChunkStates = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bDebugCrossChunkFlow = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float DebugUpdateInterval = 0.5f;

protected:
	// Chunk cache entry
	struct FCachedChunkEntry
	{
		FChunkPersistentData Data;
		float CacheTime;
		int32 AccessCount;
		
		FCachedChunkEntry()
		{
			CacheTime = 0.0f;
			AccessCount = 0;
		}
	};
	
	UPROPERTY()
	TMap<FFluidChunkCoord, UFluidChunk*> LoadedChunks;
	
	TSet<FFluidChunkCoord> ActiveChunkCoords;
	TSet<FFluidChunkCoord> InactiveChunkCoords;
	TSet<FFluidChunkCoord> BorderOnlyChunkCoords;
	
	// Persistence cache
	TMap<FFluidChunkCoord, FCachedChunkEntry> ChunkCache;
	mutable FCriticalSection CacheMutex;
	
	// Track last save time to prevent too frequent saves
	TMap<FFluidChunkCoord, float> ChunkLastSaveTime;
	
	// Fluid freeze state for chunk operations
	bool bFreezeFluidForChunkOps = false;
	float ChunkOpsFreezeTimer = 0.0f;
	
	// Static water manager reference
	class UStaticWaterManager* StaticWaterManager = nullptr;
	
	// Octree for spatial optimization
	UPROPERTY()
	UFluidOctree* FluidOctree = nullptr;
	
	// Statistics tracking
	int32 ChunksSavedThisFrame = 0;
	int32 ChunksLoadedThisFrame = 0;
	
	TQueue<FFluidChunkCoord> ChunkLoadQueue;
	TQueue<FFluidChunkCoord> ChunkUnloadQueue;
	
	float ChunkUpdateTimer = 0.0f;
	
	void ProcessChunkLoadQueue();
	void ProcessChunkUnloadQueue();
	
	void UpdateChunkStates(const TArray<FVector>& ViewerPositions);
	void UpdateChunkLODs(const TArray<FVector>& ViewerPositions);
	
	void SynchronizeChunkBorders();
	void SynchronizeChunkBorderTerrain();
	void SynchronizeTerrainBetweenChunks(UFluidChunk* ChunkA, UFluidChunk* ChunkB);
	void ProcessCrossChunkFlow(UFluidChunk* ChunkA, UFluidChunk* ChunkB, float DeltaTime);
	
	float GetDistanceToChunk(const FFluidChunkCoord& Coord, const TArray<FVector>& ViewerPositions) const;
	
	void LoadChunk(const FFluidChunkCoord& Coord);
	void UnloadChunk(const FFluidChunkCoord& Coord);
	
	void ActivateChunk(UFluidChunk* Chunk);
	void DeactivateChunk(UFluidChunk* Chunk);
	
	bool ShouldUpdateChunk(UFluidChunk* Chunk) const;
	
	FChunkManagerStats CachedStats;
	float StatsUpdateTimer = 0.0f;
	
	// Debug timing and tracking
	float DebugUpdateTimer = 0.0f;
	TMap<FFluidChunkCoord, float> ChunkLoadTimes;
	TMap<FFluidChunkCoord, FString> ChunkStateHistory;
	
	FCriticalSection ChunkMapMutex;
	
	bool bIsInitialized = false;
};