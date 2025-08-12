#pragma once

#include "CoreMinimal.h"
#include "FluidChunk.h"
#include "Engine/World.h"
#include "FluidChunkManager.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnChunkLoaded, const FFluidChunkCoord&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnChunkUnloaded, const FFluidChunkCoord&);

USTRUCT(BlueprintType)
struct VOXELFLUIDSYSTEM_API FChunkStreamingConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	float ActiveDistance = 5000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	float LoadDistance = 8000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	float UnloadDistance = 10000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	int32 MaxActiveChunks = 64;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	int32 MaxLoadedChunks = 128;

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
	
	FChunkManagerStats GetStats() const;
	
	void SetStreamingConfig(const FChunkStreamingConfig& NewConfig);
	
	void ForceUpdateChunkStates();
	
	void EnableChunkDebugVisualization(bool bEnable);

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
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bShowChunkBorders = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bShowChunkStates = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bDebugCrossChunkFlow = false;

protected:
	UPROPERTY()
	TMap<FFluidChunkCoord, UFluidChunk*> LoadedChunks;
	
	TSet<FFluidChunkCoord> ActiveChunkCoords;
	TSet<FFluidChunkCoord> InactiveChunkCoords;
	TSet<FFluidChunkCoord> BorderOnlyChunkCoords;
	
	TQueue<FFluidChunkCoord> ChunkLoadQueue;
	TQueue<FFluidChunkCoord> ChunkUnloadQueue;
	
	float ChunkUpdateTimer = 0.0f;
	
	void ProcessChunkLoadQueue();
	void ProcessChunkUnloadQueue();
	
	void UpdateChunkStates(const TArray<FVector>& ViewerPositions);
	void UpdateChunkLODs(const TArray<FVector>& ViewerPositions);
	
	void SynchronizeChunkBorders();
	void ProcessCrossChunkFlow(UFluidChunk* ChunkA, UFluidChunk* ChunkB, float DeltaTime);
	
	float GetDistanceToChunk(const FFluidChunkCoord& Coord, const TArray<FVector>& ViewerPositions) const;
	
	void LoadChunk(const FFluidChunkCoord& Coord);
	void UnloadChunk(const FFluidChunkCoord& Coord);
	
	void ActivateChunk(UFluidChunk* Chunk);
	void DeactivateChunk(UFluidChunk* Chunk);
	
	void DrawDebugChunks(UWorld* World) const;
	
	FChunkManagerStats CachedStats;
	float StatsUpdateTimer = 0.0f;
	
	FCriticalSection ChunkMapMutex;
	
	bool bIsInitialized = false;
};