#include "CellularAutomata/FluidChunkManager.h"
#include "CellularAutomata/StaticWaterBody.h"
#include "VoxelFluidStats.h"
#include "VoxelFluidDebug.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Async/ParallelFor.h"
#include "Actors/VoxelFluidActor.h"
#include "VoxelIntegration/VoxelFluidIntegration.h"

UFluidChunkManager::UFluidChunkManager()
{
	ChunkSize = 32;
	CellSize = 100.0f;
	WorldOrigin = FVector::ZeroVector;
	WorldSize = FVector(100000.0f, 100000.0f, 10000.0f);

	StreamingConfig.ActiveDistance = 8000.0f;
	StreamingConfig.LoadDistance = 15000.0f;
	StreamingConfig.UnloadDistance = 20000.0f;
	StreamingConfig.MaxActiveChunks = 80;
	StreamingConfig.MaxLoadedChunks = 160;
	StreamingConfig.ChunkUpdateInterval = 0.1f;
	StreamingConfig.LOD1Distance = 2000.0f;
	StreamingConfig.LOD2Distance = 4000.0f;
	StreamingConfig.bUseAsyncLoading = true;
	StreamingConfig.MaxChunksToProcessPerFrame = 8;
}

void UFluidChunkManager::Initialize(int32 InChunkSize, float InCellSize, const FVector& InWorldOrigin, const FVector& InWorldSize)
{
	if (bIsInitialized)
	{
		ClearAllChunks();
	}

	ChunkSize = FMath::Max(1, InChunkSize);
	CellSize = FMath::Max(1.0f, InCellSize);
	WorldOrigin = InWorldOrigin;
	WorldSize = InWorldSize;

	// Clear all tracking sets
	ActiveChunkCoords.Empty();
	InactiveChunkCoords.Empty();
	BorderOnlyChunkCoords.Empty();

	// Clear queues
	while (!ChunkLoadQueue.IsEmpty())
	{
		FFluidChunkCoord Dummy;
		ChunkLoadQueue.Dequeue(Dummy);
	}
	while (!ChunkUnloadQueue.IsEmpty())
	{
		FFluidChunkCoord Dummy;
		ChunkUnloadQueue.Dequeue(Dummy);
	}

	ChunkUpdateTimer = 0.0f;
	StatsUpdateTimer = 0.0f;


	bIsInitialized = true;

}

void UFluidChunkManager::UpdateChunks(float DeltaTime, const TArray<FVector>& ViewerPositions)
{
	if (!bIsInitialized || !IsValidLowLevel())
		return;

	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_ChunkManagerUpdate);

	ChunkUpdateTimer += DeltaTime;
	if (ChunkUpdateTimer >= StreamingConfig.ChunkUpdateInterval)
	{
		ChunkUpdateTimer = 0.0f;

		UpdateChunkStates(ViewerPositions);
		UpdateChunkLODs(ViewerPositions);

		// Track queues for performance monitoring
		int32 LoadQueueBefore = ChunkLoadQueue.IsEmpty() ? 0 : 1;
		int32 UnloadQueueBefore = ChunkUnloadQueue.IsEmpty() ? 0 : 1;

		// Process queues with improved performance
		{
			SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_ChunkStateChange);
			ProcessChunkLoadQueue();
			ProcessChunkUnloadQueue();
		}

		if (LoadQueueBefore > 0 || UnloadQueueBefore > 0)
		{
		}

		SET_DWORD_STAT(STAT_VoxelFluid_StateChangesPerFrame, LoadQueueBefore + UnloadQueueBefore);
	}

	// Check for settled chunks if using edit-triggered activation
	if (StreamingConfig.ActivationMode != EChunkActivationMode::DistanceBased)
	{
		SettledChunkCheckTimer += DeltaTime;
		if (SettledChunkCheckTimer >= SettledChunkCheckInterval)
		{
			SettledChunkCheckTimer = 0.0f;
			CheckForSettledChunks();
		}
	}

	StatsUpdateTimer += DeltaTime;
	if (StatsUpdateTimer >= 1.0f)
	{
		StatsUpdateTimer = 0.0f;
		CachedStats = GetStats();

		// === Chunk System Statistics ===
		SET_DWORD_STAT(STAT_VoxelFluid_LoadedChunks, CachedStats.TotalChunks);
		SET_DWORD_STAT(STAT_VoxelFluid_ActiveChunks, CachedStats.ActiveChunks);
		SET_DWORD_STAT(STAT_VoxelFluid_InactiveChunks, CachedStats.InactiveChunks);
		SET_DWORD_STAT(STAT_VoxelFluid_BorderOnlyChunks, CachedStats.BorderOnlyChunks);
		SET_DWORD_STAT(STAT_VoxelFluid_ChunkLoadQueueSize, CachedStats.ChunkLoadQueueSize);
		SET_DWORD_STAT(STAT_VoxelFluid_ChunkUnloadQueueSize, CachedStats.ChunkUnloadQueueSize);
		SET_FLOAT_STAT(STAT_VoxelFluid_AvgChunkUpdateTime, CachedStats.AverageChunkUpdateTime);

		// === Fluid Cell Statistics ===
		SET_DWORD_STAT(STAT_VoxelFluid_ActiveCells, CachedStats.TotalActiveCells);
		SET_DWORD_STAT(STAT_VoxelFluid_TotalCells, CachedStats.TotalChunks * ChunkSize * ChunkSize * ChunkSize);
		SET_FLOAT_STAT(STAT_VoxelFluid_TotalVolume, CachedStats.TotalFluidVolume);

		// Disabled expensive cell iteration that causes frame hitches
		// This was iterating through every cell of every loaded chunk every second
		// // Calculate additional fluid statistics
		// int32 SignificantCells = 0;
		// float TotalFluidLevels = 0.0f;
		// int32 CellsWithFluid = 0;
		//
		// for (const auto& ChunkPair : LoadedChunks)
		// {
		// 	UFluidChunk* Chunk = ChunkPair.Value;
		// 	if (Chunk && Chunk->State != EChunkState::Unloaded)
		// 	{
		// 		for (int32 i = 0; i < Chunk->Cells.Num(); ++i)
		// 		{
		// 			const float FluidLevel = Chunk->Cells[i].FluidLevel;
		// 			if (FluidLevel > 0.0f)
		// 			{
		// 				TotalFluidLevels += FluidLevel;
		// 				CellsWithFluid++;
		//
		// 				if (FluidLevel > 0.1f)
		// 				{
		// 					SignificantCells++;
		// 				}
		// 			}
		// 		}
		// 	}
		// }

		SET_DWORD_STAT(STAT_VoxelFluid_SignificantCells, 0);
		SET_FLOAT_STAT(STAT_VoxelFluid_AvgFluidLevel, 0.0f);

		// === Player & World Information ===
		if (ViewerPositions.Num() > 0)
		{
			const FVector& PlayerPos = ViewerPositions[0];
			SET_FLOAT_STAT(STAT_VoxelFluid_PlayerPosX, PlayerPos.X);
			SET_FLOAT_STAT(STAT_VoxelFluid_PlayerPosY, PlayerPos.Y);
			SET_FLOAT_STAT(STAT_VoxelFluid_PlayerPosZ, PlayerPos.Z);
		}
		else
		{
			// Clear player position if no viewers
			SET_FLOAT_STAT(STAT_VoxelFluid_PlayerPosX, 0.0f);
			SET_FLOAT_STAT(STAT_VoxelFluid_PlayerPosY, 0.0f);
			SET_FLOAT_STAT(STAT_VoxelFluid_PlayerPosZ, 0.0f);
		}

		SET_FLOAT_STAT(STAT_VoxelFluid_ActiveDistance, StreamingConfig.ActiveDistance);
		SET_FLOAT_STAT(STAT_VoxelFluid_LoadDistance, StreamingConfig.LoadDistance);
		SET_DWORD_STAT(STAT_VoxelFluid_CrossChunkFlow, bDebugCrossChunkFlow ? 1 : 0);

		// === Persistence & Cache Statistics ===
		SET_DWORD_STAT(STAT_VoxelFluid_CacheEntries, GetCacheSize());
		SET_DWORD_STAT(STAT_VoxelFluid_CacheMemoryKB, GetCacheMemoryUsage());
		SET_DWORD_STAT(STAT_VoxelFluid_ChunksSaved, ChunksSavedThisFrame);
		SET_DWORD_STAT(STAT_VoxelFluid_ChunksLoaded, ChunksLoadedThisFrame);

		// === Fluid Properties ===
		SET_FLOAT_STAT(STAT_VoxelFluid_EvaporationRate, EvaporationRate);

		// Reset frame counters
		ChunksSavedThisFrame = 0;
		ChunksLoadedThisFrame = 0;
	}

	// Update debug timer (debug drawing is now called externally)
	DebugUpdateTimer += DeltaTime;
}

void UFluidChunkManager::UpdateSimulation(float DeltaTime)
{
	if (!bIsInitialized || !IsValidLowLevel())
		return;

	// Skip fluid simulation if we're in the middle of chunk operations
	if (bFreezeFluidForChunkOps)
	{
		ChunkOpsFreezeTimer -= DeltaTime;
		if (ChunkOpsFreezeTimer <= 0.0f)
		{
			bFreezeFluidForChunkOps = false;
		}
		return; // Don't update fluid while frozen
	}

	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_UpdateSimulation);

	TArray<UFluidChunk*> ActiveChunkArray = GetActiveChunks();

	// Smart chunk filtering: Only simulate chunks that actually need updates
	TArray<UFluidChunk*> ChunksNeedingUpdate;
	ChunksNeedingUpdate.Reserve(ActiveChunkArray.Num());

	for (UFluidChunk* Chunk : ActiveChunkArray)
	{
		if (Chunk && ShouldUpdateChunk(Chunk))
		{
			ChunksNeedingUpdate.Add(Chunk);
		}
	}

	// Update critical performance stats
	SET_DWORD_STAT(STAT_VoxelFluid_ActiveChunks, ChunksNeedingUpdate.Num());
	SET_DWORD_STAT(STAT_VoxelFluid_LoadedChunks, LoadedChunks.Num());

	int32 TotalCells = 0, ActiveCells = 0, StaticWaterCells = 0;
	float TotalVolume = 0.0f;
	for (UFluidChunk* Chunk : ActiveChunkArray)
	{
		if (Chunk)
		{
			TotalCells += Chunk->Cells.Num();
			for (const auto& Cell : Chunk->Cells)
			{
				if (Cell.FluidLevel > 0.01f)
				{
					ActiveCells++;
					TotalVolume += Cell.FluidLevel;
					if (Cell.bSourceBlock)
						StaticWaterCells++;
				}
			}
		}
	}

	SET_DWORD_STAT(STAT_VoxelFluid_TotalCells, TotalCells);
	SET_DWORD_STAT(STAT_VoxelFluid_ActiveCells, ActiveCells);
	SET_DWORD_STAT(STAT_VoxelFluid_StaticWaterCells, StaticWaterCells);
	SET_FLOAT_STAT(STAT_VoxelFluid_TotalVolume, TotalVolume);


	// Use optimized parallel processing
	if (ChunksNeedingUpdate.Num() > 2)
	{
		// Process all chunks in parallel with optimized thread count
		const int32 OptimalThreads = FMath::Min(8, FMath::Max(1, FPlatformMisc::NumberOfCoresIncludingHyperthreads() * 3 / 4));
		const int32 BatchSize = FMath::Max(1, ChunksNeedingUpdate.Num() / OptimalThreads);

		ParallelFor(TEXT("FluidChunkUpdate"), ChunksNeedingUpdate.Num(), BatchSize, [&](int32 Index)
		{
			if (ChunksNeedingUpdate[Index])
			{
				ChunksNeedingUpdate[Index]->UpdateSimulation(DeltaTime);
			}
		}, EParallelForFlags::None);

		// Synchronize borders - can also be done in parallel for non-conflicting chunks
		// For now, using serial synchronization to avoid race conditions
		SynchronizeChunkBorders();

		// Finalize simulation step by swapping buffers
		for (UFluidChunk* Chunk : ActiveChunkArray)
		{
			if (Chunk)
			{
				Chunk->FinalizeSimulationStep();
			}
		}
	}
	else
	{
		// Fallback to original processing for small chunk counts or if optimization disabled
		TArray<UFluidChunk*> HighActivityChunks;
		TArray<UFluidChunk*> LowActivityChunks;
		TArray<UFluidChunk*> SettledChunks;

		HighActivityChunks.Reserve(ActiveChunkArray.Num() / 2);
		LowActivityChunks.Reserve(ActiveChunkArray.Num() / 2);
		SettledChunks.Reserve(ActiveChunkArray.Num() / 4);

		// Process all chunks equally for consistent updates
		for (UFluidChunk* Chunk : ActiveChunkArray)
		{
			if (!Chunk) continue;
			HighActivityChunks.Add(Chunk);
		}

		// Process high activity chunks
		if (StreamingConfig.bUseAsyncLoading && HighActivityChunks.Num() > 4)
		{
			ParallelFor(HighActivityChunks.Num(), [&](int32 Index)
			{
				if (HighActivityChunks[Index])
				{
					HighActivityChunks[Index]->UpdateSimulation(DeltaTime);
				}
			});
		}
		else
		{
			for (UFluidChunk* Chunk : HighActivityChunks)
			{
				if (Chunk)
				{
					Chunk->UpdateSimulation(DeltaTime);
				}
			}
		}

		// Process low activity chunks
		for (UFluidChunk* Chunk : LowActivityChunks)
		{
			if (Chunk)
			{
				Chunk->UpdateSimulation(DeltaTime);
			}
		}

		// Process settled chunks
		for (UFluidChunk* Chunk : SettledChunks)
		{
			if (Chunk)
			{
				Chunk->UpdateSimulation(DeltaTime);
			}
		}

		// Synchronize borders
		SynchronizeChunkBorders();

		// Finalize simulation step by swapping buffers for all chunks
		for (UFluidChunk* Chunk : ActiveChunkArray)
		{
			if (Chunk)
			{
				Chunk->FinalizeSimulationStep();
			}
		}
	}
}

UFluidChunk* UFluidChunkManager::GetChunk(const FFluidChunkCoord& Coord)
{
	FScopeLock Lock(&ChunkMapMutex);

	if (UFluidChunk** ChunkPtr = LoadedChunks.Find(Coord))
	{
		return *ChunkPtr;
	}

	return nullptr;
}

UFluidChunk* UFluidChunkManager::GetOrCreateChunk(const FFluidChunkCoord& Coord)
{
	UFluidChunk* Chunk = GetChunk(Coord);
	if (Chunk)
		return Chunk;

	FScopeLock Lock(&ChunkMapMutex);

	Chunk = NewObject<UFluidChunk>(this);
	Chunk->Initialize(Coord, ChunkSize, CellSize, WorldOrigin);
	Chunk->FlowRate = FlowRate;
	Chunk->Viscosity = Viscosity;
	Chunk->Gravity = Gravity;
	Chunk->EvaporationRate = EvaporationRate;

	// Enable sparse representation if configured

	LoadedChunks.Add(Coord, Chunk);
	InactiveChunkCoords.Add(Coord);


	return Chunk;
}

bool UFluidChunkManager::IsChunkLoaded(const FFluidChunkCoord& Coord) const
{
	return LoadedChunks.Contains(Coord);
}

bool UFluidChunkManager::IsChunkActive(const FFluidChunkCoord& Coord) const
{
	return ActiveChunkCoords.Contains(Coord);
}

void UFluidChunkManager::RequestChunkLoad(const FFluidChunkCoord& Coord)
{
	if (!IsChunkLoaded(Coord))
	{
		ChunkLoadQueue.Enqueue(Coord);
	}
}

void UFluidChunkManager::RequestChunkUnload(const FFluidChunkCoord& Coord)
{
	if (IsChunkLoaded(Coord))
	{
		ChunkUnloadQueue.Enqueue(Coord);
	}
}

FFluidChunkCoord UFluidChunkManager::GetChunkCoordFromWorldPosition(const FVector& WorldPos) const
{
	const FVector LocalPos = WorldPos - WorldOrigin;
	const float ChunkWorldSize = ChunkSize * CellSize;

	return FFluidChunkCoord(
		FMath::FloorToInt(LocalPos.X / ChunkWorldSize),
		FMath::FloorToInt(LocalPos.Y / ChunkWorldSize),
		FMath::FloorToInt(LocalPos.Z / ChunkWorldSize)
	);
}

bool UFluidChunkManager::GetCellFromWorldPosition(const FVector& WorldPos, FFluidChunkCoord& OutChunkCoord, int32& OutLocalX, int32& OutLocalY, int32& OutLocalZ) const
{
	// Validate this pointer for async access
	if (!IsValid(this))
		return false;

	OutChunkCoord = GetChunkCoordFromWorldPosition(WorldPos);

	// Thread-safe chunk access
	FScopeLock Lock(&const_cast<UFluidChunkManager*>(this)->ChunkMapMutex);
	UFluidChunk* Chunk = const_cast<UFluidChunkManager*>(this)->GetChunk(OutChunkCoord);
	if (!Chunk || !IsValid(Chunk))
		return false;

	return Chunk->GetLocalFromWorldPosition(WorldPos, OutLocalX, OutLocalY, OutLocalZ);
}

void UFluidChunkManager::AddFluidAtWorldPosition(const FVector& WorldPos, float Amount)
{
	FFluidChunkCoord ChunkCoord;
	int32 LocalX, LocalY, LocalZ;

	if (GetCellFromWorldPosition(WorldPos, ChunkCoord, LocalX, LocalY, LocalZ))
	{
		UFluidChunk* Chunk = GetOrCreateChunk(ChunkCoord);
		if (Chunk)
		{
			if (Chunk->State == EChunkState::Unloaded)
			{
				Chunk->LoadChunk();
			}
			Chunk->AddFluid(LocalX, LocalY, LocalZ, Amount);

			if (Chunk->State == EChunkState::Inactive)
			{
				ActivateChunk(Chunk);
			}
		}
	}
}

void UFluidChunkManager::RemoveFluidAtWorldPosition(const FVector& WorldPos, float Amount)
{
	FFluidChunkCoord ChunkCoord;
	int32 LocalX, LocalY, LocalZ;

	if (GetCellFromWorldPosition(WorldPos, ChunkCoord, LocalX, LocalY, LocalZ))
	{
		UFluidChunk* Chunk = GetChunk(ChunkCoord);
		if (Chunk && Chunk->State != EChunkState::Unloaded)
		{
			Chunk->RemoveFluid(LocalX, LocalY, LocalZ, Amount);
		}
	}
}

float UFluidChunkManager::GetFluidAtWorldPosition(const FVector& WorldPos) const
{
	// Validate this pointer for async access
	if (!IsValid(this))
		return 0.0f;

	FFluidChunkCoord ChunkCoord;
	int32 LocalX, LocalY, LocalZ;

	// GetCellFromWorldPosition already handles thread safety and validation
	if (GetCellFromWorldPosition(WorldPos, ChunkCoord, LocalX, LocalY, LocalZ))
	{
		// Thread-safe chunk access
		FScopeLock Lock(&const_cast<UFluidChunkManager*>(this)->ChunkMapMutex);

		UFluidChunk* Chunk = const_cast<UFluidChunkManager*>(this)->GetChunk(ChunkCoord);
		if (Chunk && IsValid(Chunk) && Chunk->State != EChunkState::Unloaded)
		{
			return Chunk->GetFluidAt(LocalX, LocalY, LocalZ);
		}
	}

	return 0.0f;
}

void UFluidChunkManager::SetTerrainHeightAtWorldPosition(const FVector& WorldPos, float Height)
{
	FFluidChunkCoord ChunkCoord;
	int32 LocalX, LocalY, LocalZ;

	if (GetCellFromWorldPosition(WorldPos, ChunkCoord, LocalX, LocalY, LocalZ))
	{
		UFluidChunk* Chunk = GetOrCreateChunk(ChunkCoord);
		if (Chunk)
		{
			if (Chunk->State == EChunkState::Unloaded)
			{
				Chunk->LoadChunk();
			}
			Chunk->SetTerrainHeight(LocalX, LocalY, Height);
		}
	}
}

void UFluidChunkManager::ClearAllChunks()
{
	FScopeLock Lock(&ChunkMapMutex);

	for (auto& Pair : LoadedChunks)
	{
		if (Pair.Value)
		{
			Pair.Value->ClearChunk();
		}
	}
}

TArray<UFluidChunk*> UFluidChunkManager::GetActiveChunks() const
{
	TArray<UFluidChunk*> Result;
	Result.Reserve(ActiveChunkCoords.Num());

	for (const FFluidChunkCoord& Coord : ActiveChunkCoords)
	{
		if (UFluidChunk* const* ChunkPtr = LoadedChunks.Find(Coord))
		{
			Result.Add(*ChunkPtr);
		}
	}

	return Result;
}

TArray<UFluidChunk*> UFluidChunkManager::GetVisibleChunks() const
{
	// For now, just return the same as GetActiveChunks - no optimizations
	return GetActiveChunks();
}

TArray<UFluidChunk*> UFluidChunkManager::GetChunksInRadius(const FVector& Center, float Radius) const
{
	// Linear search
	TArray<UFluidChunk*> Result;
	const float RadiusSq = Radius * Radius;

	for (const auto& Pair : LoadedChunks)
	{
		if (Pair.Value)
		{
			const FBox ChunkBounds = Pair.Value->GetWorldBounds();
			const float DistSq = ChunkBounds.ComputeSquaredDistanceToPoint(Center);

			if (DistSq <= RadiusSq)
			{
				Result.Add(Pair.Value);
			}
		}
	}

	return Result;
}

TArray<FFluidChunkCoord> UFluidChunkManager::GetChunksInBounds(const FBox& Bounds) const
{
	TArray<FFluidChunkCoord> Result;

	if (!bIsInitialized)
		return Result;

	// Grid-based calculation
	// Calculate chunk range from bounds
	const float ChunkWorldSize = ChunkSize * CellSize;

	const int32 MinChunkX = FMath::FloorToInt((Bounds.Min.X - WorldOrigin.X) / ChunkWorldSize);
	const int32 MaxChunkX = FMath::CeilToInt((Bounds.Max.X - WorldOrigin.X) / ChunkWorldSize);
	const int32 MinChunkY = FMath::FloorToInt((Bounds.Min.Y - WorldOrigin.Y) / ChunkWorldSize);
	const int32 MaxChunkY = FMath::CeilToInt((Bounds.Max.Y - WorldOrigin.Y) / ChunkWorldSize);
	const int32 MinChunkZ = FMath::FloorToInt((Bounds.Min.Z - WorldOrigin.Z) / ChunkWorldSize);
	const int32 MaxChunkZ = FMath::CeilToInt((Bounds.Max.Z - WorldOrigin.Z) / ChunkWorldSize);

	// Iterate through all possible chunks in the bounds
	for (int32 X = MinChunkX; X <= MaxChunkX; ++X)
	{
		for (int32 Y = MinChunkY; Y <= MaxChunkY; ++Y)
		{
			for (int32 Z = MinChunkZ; Z <= MaxChunkZ; ++Z)
			{
				FFluidChunkCoord Coord(X, Y, Z);

				// Check if chunk actually overlaps with bounds
				const FVector ChunkMin = WorldOrigin + FVector(X * ChunkWorldSize, Y * ChunkWorldSize, Z * ChunkWorldSize);
				const FVector ChunkMax = ChunkMin + FVector(ChunkWorldSize, ChunkWorldSize, ChunkWorldSize);
				const FBox ChunkBounds(ChunkMin, ChunkMax);

				if (Bounds.Intersect(ChunkBounds))
				{
					Result.Add(Coord);
				}
			}
		}
	}

	return Result;
}

FChunkManagerStats UFluidChunkManager::GetStats() const
{
	FChunkManagerStats Stats;

	Stats.TotalChunks = LoadedChunks.Num();
	Stats.ActiveChunks = ActiveChunkCoords.Num();
	Stats.InactiveChunks = InactiveChunkCoords.Num();
	Stats.BorderOnlyChunks = BorderOnlyChunkCoords.Num();
	Stats.ChunkLoadQueueSize = ChunkLoadQueue.IsEmpty() ? 0 : 1; // TQueue doesn't have size method
	Stats.ChunkUnloadQueueSize = ChunkUnloadQueue.IsEmpty() ? 0 : 1;

	float TotalUpdateTime = 0.0f;
	int32 ActiveChunkCount = 0;

	for (const auto& Pair : LoadedChunks)
	{
		if (Pair.Value)
		{
			Stats.TotalFluidVolume += Pair.Value->GetTotalFluidVolume();
			Stats.TotalActiveCells += Pair.Value->GetActiveCellCount();

			if (Pair.Value->State == EChunkState::Active)
			{
				TotalUpdateTime += Pair.Value->LastUpdateTime;
				ActiveChunkCount++;
			}
		}
	}

	Stats.AverageChunkUpdateTime = ActiveChunkCount > 0 ? TotalUpdateTime / ActiveChunkCount : 0.0f;

	return Stats;
}

void UFluidChunkManager::SetStreamingConfig(const FChunkStreamingConfig& NewConfig)
{
	StreamingConfig = NewConfig;
}

void UFluidChunkManager::ForceUpdateChunkStates()
{
	TArray<FVector> ViewerPositions;
	UpdateChunkStates(ViewerPositions);
}

void UFluidChunkManager::EnableChunkDebugVisualization(bool bEnable)
{
	bShowChunkBorders = bEnable;
	bShowChunkStates = bEnable;
}

void UFluidChunkManager::ProcessChunkLoadQueue()
{
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_ChunkStreaming);

	int32 ProcessedCount = 0;
	FFluidChunkCoord Coord;
	bool bProcessedAny = false;

	while (ProcessedCount < StreamingConfig.MaxChunksToProcessPerFrame && ChunkLoadQueue.Dequeue(Coord))
	{
		// Freeze fluid for a moment when loading chunks to ensure consistent state
		if (!bFreezeFluidForChunkOps)
		{
			bFreezeFluidForChunkOps = true;
			ChunkOpsFreezeTimer = 0.1f; // Freeze for 100ms
		}

		LoadChunk(Coord);
		ProcessedCount++;
		bProcessedAny = true;
	}

	// Extend freeze timer if we processed chunks
	if (bProcessedAny && bFreezeFluidForChunkOps)
	{
		ChunkOpsFreezeTimer = FMath::Max(ChunkOpsFreezeTimer, 0.1f);
	}
}

void UFluidChunkManager::ProcessChunkUnloadQueue()
{
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_ChunkUnload);

	int32 ProcessedCount = 0;
	FFluidChunkCoord Coord;
	bool bProcessedAny = false;

	while (ProcessedCount < StreamingConfig.MaxChunksToProcessPerFrame && ChunkUnloadQueue.Dequeue(Coord))
	{
		// Freeze fluid for a moment when unloading chunks to save consistent state
		if (!bFreezeFluidForChunkOps)
		{
			bFreezeFluidForChunkOps = true;
			ChunkOpsFreezeTimer = 0.1f; // Freeze for 100ms
		}

		UnloadChunk(Coord);
		ProcessedCount++;
		bProcessedAny = true;
	}

	// Extend freeze timer if we processed chunks
	if (bProcessedAny && bFreezeFluidForChunkOps)
	{
		ChunkOpsFreezeTimer = FMath::Max(ChunkOpsFreezeTimer, 0.1f);
	}
}

void UFluidChunkManager::UpdateChunkStates(const TArray<FVector>& ViewerPositions)
{
	if (ViewerPositions.Num() == 0)
		return;

	TSet<FFluidChunkCoord> ChunksToActivate;
	TSet<FFluidChunkCoord> ChunksToDeactivate;
	TSet<FFluidChunkCoord> ChunksToLoad;
	TSet<FFluidChunkCoord> ChunksToUnload;

	// In edit-triggered mode, we only load/activate chunks that have been explicitly triggered
	bool bShouldLoadByDistance = (StreamingConfig.ActivationMode == EChunkActivationMode::DistanceBased ||
								   StreamingConfig.ActivationMode == EChunkActivationMode::Hybrid);
	bool bShouldActivateByDistance = (StreamingConfig.ActivationMode == EChunkActivationMode::DistanceBased ||
									   StreamingConfig.ActivationMode == EChunkActivationMode::Hybrid);

	for (const FVector& ViewerPos : ViewerPositions)
	{
		const float ChunkWorldSize = ChunkSize * CellSize;
		const int32 LoadRadiusInChunks = FMath::CeilToInt(StreamingConfig.LoadDistance / ChunkWorldSize);
		const int32 ActiveRadiusInChunks = FMath::CeilToInt(StreamingConfig.ActiveDistance / ChunkWorldSize);

		const FFluidChunkCoord ViewerChunk = GetChunkCoordFromWorldPosition(ViewerPos);

		for (int32 dx = -LoadRadiusInChunks; dx <= LoadRadiusInChunks; ++dx)
		{
			for (int32 dy = -LoadRadiusInChunks; dy <= LoadRadiusInChunks; ++dy)
			{
				for (int32 dz = -2; dz <= 2; ++dz)
				{
					const FFluidChunkCoord Coord(ViewerChunk.X + dx, ViewerChunk.Y + dy, ViewerChunk.Z + dz);
					const float Distance = GetDistanceToChunk(Coord, ViewerPositions);

					if (Distance <= StreamingConfig.ActiveDistance)
					{
						// Only activate by distance if mode allows it
						if (bShouldActivateByDistance)
						{
							ChunksToActivate.Add(Coord);
						}
						
						// Only load chunks by distance if not in pure edit-triggered mode
						if (bShouldLoadByDistance && !IsChunkLoaded(Coord))
						{
							ChunksToLoad.Add(Coord);
						}
					}
					else if (Distance <= StreamingConfig.LoadDistance)
					{
						if (!IsChunkLoaded(Coord))
						{
							ChunksToLoad.Add(Coord);
						}
					}
				}
			}
		}
	}

	// Log detailed chunk state periodically
	static float ChunkStateLogTimer = 0.0f;
	ChunkStateLogTimer += 0.1f; // Approximation since we're called every 0.1s
	bool bShouldLogDetails = (ChunkStateLogTimer > 5.0f); // Log every 5 seconds
	{
		ChunkStateLogTimer = 0.0f;
	}

	for (const auto& Pair : LoadedChunks)
	{
		const FFluidChunkCoord& Coord = Pair.Key;
		const float Distance = GetDistanceToChunk(Coord, ViewerPositions);

		if (bShouldLogDetails && Pair.Value && Pair.Value->HasFluid())
		{
		}

		if (Distance > StreamingConfig.UnloadDistance)
		{
			ChunksToUnload.Add(Coord);
			{
			}
		}
		else if (Distance > StreamingConfig.ActiveDistance && ActiveChunkCoords.Contains(Coord))
		{
			// In edit-triggered mode, don't deactivate edit-activated chunks based on distance
			if (StreamingConfig.ActivationMode == EChunkActivationMode::EditTriggered)
			{
				if (!IsChunkEditActivated(Coord))
				{
					// This chunk was somehow activated but not by edits, deactivate it
					ChunksToDeactivate.Add(Coord);
				}
				// Edit-activated chunks are handled by CheckForSettledChunks()
			}
			else
			{
				// Normal distance-based deactivation
				ChunksToDeactivate.Add(Coord);
			}
		}
	}

	for (const FFluidChunkCoord& Coord : ChunksToLoad)
	{
		RequestChunkLoad(Coord);
	}

	for (const FFluidChunkCoord& Coord : ChunksToUnload)
	{
		RequestChunkUnload(Coord);
	}

	for (const FFluidChunkCoord& Coord : ChunksToActivate)
	{
		if (UFluidChunk* Chunk = GetChunk(Coord))
		{
			ActivateChunk(Chunk);
		}
	}

	for (const FFluidChunkCoord& Coord : ChunksToDeactivate)
	{
		if (UFluidChunk* Chunk = GetChunk(Coord))
		{
			DeactivateChunk(Chunk);
		}
	}
}

void UFluidChunkManager::UpdateChunkLODs(const TArray<FVector>& ViewerPositions)
{
	for (const auto& Pair : LoadedChunks)
	{
		if (Pair.Value && Pair.Value->State == EChunkState::Active)
		{
			const float Distance = GetDistanceToChunk(Pair.Key, ViewerPositions);

			int32 LODLevel = 0;
			if (Distance > StreamingConfig.LOD2Distance)
			{
				LODLevel = 2;
			}
			else if (Distance > StreamingConfig.LOD1Distance)
			{
				LODLevel = 1;
			}

			Pair.Value->SetLODLevel(LODLevel);
		}
	}
}

void UFluidChunkManager::SynchronizeChunkBorders()
{
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_BorderSync);

	// Removed terrain synchronization - too expensive and not solving the problem
	// SynchronizeChunkBorderTerrain();

	// Create a set to track processed chunk pairs to avoid duplicate processing
	TSet<FString> ProcessedPairs;

	for (const FFluidChunkCoord& Coord : ActiveChunkCoords)
	{
		UFluidChunk* Chunk = GetChunk(Coord);
		if (!Chunk)
			continue;

		// Process each neighbor direction
		const TArray<FFluidChunkCoord> NeighborCoords = {
			FFluidChunkCoord(Coord.X + 1, Coord.Y, Coord.Z),
			FFluidChunkCoord(Coord.X - 1, Coord.Y, Coord.Z),
			FFluidChunkCoord(Coord.X, Coord.Y + 1, Coord.Z),
			FFluidChunkCoord(Coord.X, Coord.Y - 1, Coord.Z),
			FFluidChunkCoord(Coord.X, Coord.Y, Coord.Z + 1),
			FFluidChunkCoord(Coord.X, Coord.Y, Coord.Z - 1)
		};

		for (const FFluidChunkCoord& NeighborCoord : NeighborCoords)
		{
			// Create a unique key for this chunk pair
			FString PairKey = FString::Printf(TEXT("%d_%d_%d-%d_%d_%d"),
				FMath::Min(Coord.X, NeighborCoord.X),
				FMath::Min(Coord.Y, NeighborCoord.Y),
				FMath::Min(Coord.Z, NeighborCoord.Z),
				FMath::Max(Coord.X, NeighborCoord.X),
				FMath::Max(Coord.Y, NeighborCoord.Y),
				FMath::Max(Coord.Z, NeighborCoord.Z));

			// Skip if we've already processed this pair
			if (ProcessedPairs.Contains(PairKey))
				continue;

			UFluidChunk* Neighbor = GetChunk(NeighborCoord);
			if (Neighbor && Neighbor->State == EChunkState::Active)
			{
				// Process flow only once per chunk pair
				// The ProcessCrossChunkFlow function handles bidirectional flow internally
				ProcessCrossChunkFlow(Chunk, Neighbor, 0.016f);
				ProcessedPairs.Add(PairKey);
			}
		}

		// Clear the border dirty flag after processing
		Chunk->bBorderDirty = false;
	}
}

void UFluidChunkManager::SynchronizeChunkBorderTerrain()
{
	// Ensure terrain heights are consistent at chunk borders by using the same world position sampling
	TSet<FString> ProcessedPairs;

	for (const FFluidChunkCoord& Coord : ActiveChunkCoords)
	{
		UFluidChunk* Chunk = GetChunk(Coord);
		if (!Chunk)
			continue;

		// Check all 6 neighboring chunks (X+1, X-1, Y+1, Y-1, Z+1, Z-1)
		const TArray<FFluidChunkCoord> NeighborCoords = {
			FFluidChunkCoord(Coord.X + 1, Coord.Y, Coord.Z),
			FFluidChunkCoord(Coord.X - 1, Coord.Y, Coord.Z),
			FFluidChunkCoord(Coord.X, Coord.Y + 1, Coord.Z),
			FFluidChunkCoord(Coord.X, Coord.Y - 1, Coord.Z),
			FFluidChunkCoord(Coord.X, Coord.Y, Coord.Z + 1),
			FFluidChunkCoord(Coord.X, Coord.Y, Coord.Z - 1)
		};

		for (const FFluidChunkCoord& NeighborCoord : NeighborCoords)
		{
			UFluidChunk* Neighbor = GetChunk(NeighborCoord);
			if (!Neighbor)
				continue;

			// Create pair key to avoid duplicate processing
			// Sort the coords to ensure consistent key regardless of order
			FString PairKey;
			if (Coord.X < NeighborCoord.X ||
				(Coord.X == NeighborCoord.X && Coord.Y < NeighborCoord.Y) ||
				(Coord.X == NeighborCoord.X && Coord.Y == NeighborCoord.Y && Coord.Z < NeighborCoord.Z))
			{
				PairKey = FString::Printf(TEXT("%s_%s"), *Coord.ToString(), *NeighborCoord.ToString());
			}
			else
			{
				PairKey = FString::Printf(TEXT("%s_%s"), *NeighborCoord.ToString(), *Coord.ToString());
			}

			if (ProcessedPairs.Contains(PairKey))
				continue;

			ProcessedPairs.Add(PairKey);

			// Synchronize terrain heights at the border between these chunks
			SynchronizeTerrainBetweenChunks(Chunk, Neighbor);
		}
	}
}

void UFluidChunkManager::SynchronizeTerrainBetweenChunks(UFluidChunk* ChunkA, UFluidChunk* ChunkB)
{
	if (!ChunkA || !ChunkB)
		return;

	const FFluidChunkCoord& CoordA = ChunkA->ChunkCoord;
	const FFluidChunkCoord& CoordB = ChunkB->ChunkCoord;

	const int32 DiffX = CoordB.X - CoordA.X;
	const int32 DiffY = CoordB.Y - CoordA.Y;
	const int32 DiffZ = CoordB.Z - CoordA.Z;

	const int32 LocalChunkSize = ChunkA->ChunkSize;

	// Synchronize terrain heights at borders based on chunk adjacency
	if (DiffX == 1 && DiffY == 0 && DiffZ == 0) // ChunkB is to the positive X of ChunkA
	{
		// Synchronize terrain heights along the X border
		for (int32 LocalY = 0; LocalY < LocalChunkSize; ++LocalY)
		{
			// Get world position at the border (use ChunkA's positive X edge)
			FVector BorderWorldPos = ChunkA->GetWorldPositionFromLocal(LocalChunkSize - 1, LocalY, 0);
			BorderWorldPos.Z = 0; // We only care about X,Y for terrain height sampling

			// Sample terrain height at this exact world position
			float TerrainHeight = 0.0f;
			if (AActor* Owner = GetTypedOuter<AActor>())
			{
				if (AVoxelFluidActor* FluidActor = Cast<AVoxelFluidActor>(Owner))
				{
					if (UVoxelFluidIntegration* Integration = FluidActor->VoxelIntegration)
					{
						if (Integration->IsVoxelWorldValid())
						{
							TerrainHeight = Integration->SampleVoxelHeight(BorderWorldPos.X, BorderWorldPos.Y);
						}
					}
				}
			}

			// Set the same terrain height for both chunks at this border
			ChunkA->SetTerrainHeight(LocalChunkSize - 1, LocalY, TerrainHeight);
			ChunkB->SetTerrainHeight(0, LocalY, TerrainHeight);
		}
	}
	else if (DiffX == -1 && DiffY == 0 && DiffZ == 0) // ChunkB is to the negative X of ChunkA
	{
		// Synchronize terrain heights along the X border (other direction)
		for (int32 LocalY = 0; LocalY < LocalChunkSize; ++LocalY)
		{
			FVector BorderWorldPos = ChunkA->GetWorldPositionFromLocal(0, LocalY, 0);
			BorderWorldPos.Z = 0;

			float TerrainHeight = 0.0f;
			if (AActor* Owner = GetTypedOuter<AActor>())
			{
				if (AVoxelFluidActor* FluidActor = Cast<AVoxelFluidActor>(Owner))
				{
					if (UVoxelFluidIntegration* Integration = FluidActor->VoxelIntegration)
					{
						if (Integration->IsVoxelWorldValid())
						{
							TerrainHeight = Integration->SampleVoxelHeight(BorderWorldPos.X, BorderWorldPos.Y);
						}
					}
				}
			}

			ChunkA->SetTerrainHeight(0, LocalY, TerrainHeight);
			ChunkB->SetTerrainHeight(LocalChunkSize - 1, LocalY, TerrainHeight);
		}
	}
	else if (DiffY == 1 && DiffX == 0 && DiffZ == 0) // ChunkB is to the positive Y of ChunkA
	{
		// Synchronize terrain heights along the Y border
		for (int32 LocalX = 0; LocalX < LocalChunkSize; ++LocalX)
		{
			FVector BorderWorldPos = ChunkA->GetWorldPositionFromLocal(LocalX, LocalChunkSize - 1, 0);
			BorderWorldPos.Z = 0;

			float TerrainHeight = 0.0f;
			if (AActor* Owner = GetTypedOuter<AActor>())
			{
				if (AVoxelFluidActor* FluidActor = Cast<AVoxelFluidActor>(Owner))
				{
					if (UVoxelFluidIntegration* Integration = FluidActor->VoxelIntegration)
					{
						if (Integration->IsVoxelWorldValid())
						{
							TerrainHeight = Integration->SampleVoxelHeight(BorderWorldPos.X, BorderWorldPos.Y);
						}
					}
				}
			}

			ChunkA->SetTerrainHeight(LocalX, LocalChunkSize - 1, TerrainHeight);
			ChunkB->SetTerrainHeight(LocalX, 0, TerrainHeight);
		}
	}
	else if (DiffY == -1 && DiffX == 0 && DiffZ == 0) // ChunkB is to the negative Y of ChunkA
	{
		// Synchronize terrain heights along the Y border (other direction)
		for (int32 LocalX = 0; LocalX < LocalChunkSize; ++LocalX)
		{
			FVector BorderWorldPos = ChunkA->GetWorldPositionFromLocal(LocalX, 0, 0);
			BorderWorldPos.Z = 0;

			float TerrainHeight = 0.0f;
			if (AActor* Owner = GetTypedOuter<AActor>())
			{
				if (AVoxelFluidActor* FluidActor = Cast<AVoxelFluidActor>(Owner))
				{
					if (UVoxelFluidIntegration* Integration = FluidActor->VoxelIntegration)
					{
						if (Integration->IsVoxelWorldValid())
						{
							TerrainHeight = Integration->SampleVoxelHeight(BorderWorldPos.X, BorderWorldPos.Y);
						}
					}
				}
			}

			ChunkA->SetTerrainHeight(LocalX, 0, TerrainHeight);
			ChunkB->SetTerrainHeight(LocalX, LocalChunkSize - 1, TerrainHeight);
		}
	}
	// Note: We don't synchronize Z borders as terrain height is 2D (only X,Y dependent)
}

void UFluidChunkManager::ProcessCrossChunkFlow(UFluidChunk* ChunkA, UFluidChunk* ChunkB, float DeltaTime)
{
	if (!ChunkA || !ChunkB || ChunkA->State != EChunkState::Active || ChunkB->State != EChunkState::Active)
		return;

	const FFluidChunkCoord& CoordA = ChunkA->ChunkCoord;
	const FFluidChunkCoord& CoordB = ChunkB->ChunkCoord;

	const int32 DiffX = CoordB.X - CoordA.X;
	const int32 DiffY = CoordB.Y - CoordA.Y;
	const int32 DiffZ = CoordB.Z - CoordA.Z;

	// Only process direct neighbors
	if (FMath::Abs(DiffX) + FMath::Abs(DiffY) + FMath::Abs(DiffZ) != 1)
		return;

	const int32 LocalChunkSize = ChunkA->ChunkSize;
	const float LocalFlowRate = ChunkA->FlowRate;
	const float FlowAmount = LocalFlowRate * DeltaTime;

	// Process flow between chunks based on their relative positions
	if (DiffX == 1) // ChunkB is to the positive X of ChunkA
	{
		// Process flow from ChunkA's positive X border to ChunkB's negative X border
		for (int32 y = 0; y < LocalChunkSize; ++y)
		{
			for (int32 z = 0; z < LocalChunkSize; ++z)
			{
				// Get cells at the border
				const int32 IdxA = ChunkA->GetLocalCellIndex(LocalChunkSize - 1, y, z);
				const int32 IdxB = ChunkB->GetLocalCellIndex(0, y, z);

				if (IdxA != -1 && IdxB != -1)
				{
					FCAFluidCell& CellA = ChunkA->NextCells[IdxA]; // Use NextCells for consistency
					FCAFluidCell& CellB = ChunkB->NextCells[IdxB];

					if (!CellA.bIsSolid && !CellB.bIsSolid)
					{
						// Calculate flow based on fluid height difference
						const float HeightA = CellA.TerrainHeight + CellA.FluidLevel;
						const float HeightB = CellB.TerrainHeight + CellB.FluidLevel;
						const float HeightDiff = HeightA - HeightB;

						// Allow bidirectional flow - flow from higher to lower
						if (FMath::Abs(HeightDiff) > 0.01f)
						{
							FCAFluidCell* SourceCell = HeightDiff > 0 ? &CellA : &CellB;
							FCAFluidCell* TargetCell = HeightDiff > 0 ? &CellB : &CellA;
							UFluidChunk* SourceChunk = HeightDiff > 0 ? ChunkA : ChunkB;
							UFluidChunk* TargetChunk = HeightDiff > 0 ? ChunkB : ChunkA;

							if (SourceCell->FluidLevel > 0.01f)
							{
								const float SpaceInTarget = SourceChunk->MaxFluidLevel - TargetCell->FluidLevel;
								const float PossibleFlow = FMath::Min(SourceCell->FluidLevel * FlowAmount, FMath::Abs(HeightDiff) * 0.5f);
								const float ActualFlow = FMath::Min(PossibleFlow, SpaceInTarget);

								if (ActualFlow > 0.0f)
								{
									SourceCell->FluidLevel -= ActualFlow;
									TargetCell->FluidLevel += ActualFlow;

									// Wake up the border cells
									SourceCell->bSettled = false;
									SourceCell->SettledCounter = 0;
									TargetCell->bSettled = false;
									TargetCell->SettledCounter = 0;

									SourceChunk->bDirty = true;
									TargetChunk->bDirty = true;
									SourceChunk->ConsiderMeshUpdate(ActualFlow);
									TargetChunk->ConsiderMeshUpdate(ActualFlow);
								}
							}
						}
					}
				}
			}
		}
	}
	else if (DiffX == -1) // ChunkB is to the negative X of ChunkA
	{
		// Process flow from ChunkA's negative X border to ChunkB's positive X border
		for (int32 y = 0; y < LocalChunkSize; ++y)
		{
			for (int32 z = 0; z < LocalChunkSize; ++z)
			{
				const int32 IdxA = ChunkA->GetLocalCellIndex(0, y, z);
				const int32 IdxB = ChunkB->GetLocalCellIndex(LocalChunkSize - 1, y, z);

				if (IdxA != -1 && IdxB != -1)
				{
					FCAFluidCell& CellA = ChunkA->NextCells[IdxA];
					FCAFluidCell& CellB = ChunkB->NextCells[IdxB];

					if (!CellA.bIsSolid && !CellB.bIsSolid)
					{
						const float HeightA = CellA.TerrainHeight + CellA.FluidLevel;
						const float HeightB = CellB.TerrainHeight + CellB.FluidLevel;
						const float HeightDiff = HeightA - HeightB;

						if (FMath::Abs(HeightDiff) > 0.01f)
						{
							FCAFluidCell* SourceCell = HeightDiff > 0 ? &CellA : &CellB;
							FCAFluidCell* TargetCell = HeightDiff > 0 ? &CellB : &CellA;
							UFluidChunk* SourceChunk = HeightDiff > 0 ? ChunkA : ChunkB;
							UFluidChunk* TargetChunk = HeightDiff > 0 ? ChunkB : ChunkA;

							if (SourceCell->FluidLevel > 0.01f)
							{
								const float SpaceInTarget = SourceChunk->MaxFluidLevel - TargetCell->FluidLevel;
								const float PossibleFlow = FMath::Min(SourceCell->FluidLevel * FlowAmount, FMath::Abs(HeightDiff) * 0.5f);
								const float ActualFlow = FMath::Min(PossibleFlow, SpaceInTarget);

								if (ActualFlow > 0.0f)
								{
									SourceCell->FluidLevel -= ActualFlow;
									TargetCell->FluidLevel += ActualFlow;

									SourceCell->bSettled = false;
									SourceCell->SettledCounter = 0;
									TargetCell->bSettled = false;
									TargetCell->SettledCounter = 0;

									SourceChunk->bDirty = true;
									TargetChunk->bDirty = true;
									SourceChunk->ConsiderMeshUpdate(ActualFlow);
									TargetChunk->ConsiderMeshUpdate(ActualFlow);
								}
							}
						}
					}
				}
			}
		}
	}
	else if (DiffY == 1) // ChunkB is to the positive Y of ChunkA
	{
		// Process flow from ChunkA's positive Y border to ChunkB's negative Y border
		for (int32 x = 0; x < LocalChunkSize; ++x)
		{
			for (int32 z = 0; z < LocalChunkSize; ++z)
			{
				const int32 IdxA = ChunkA->GetLocalCellIndex(x, LocalChunkSize - 1, z);
				const int32 IdxB = ChunkB->GetLocalCellIndex(x, 0, z);

				if (IdxA != -1 && IdxB != -1)
				{
					FCAFluidCell& CellA = ChunkA->NextCells[IdxA];
					FCAFluidCell& CellB = ChunkB->NextCells[IdxB];

					if (!CellA.bIsSolid && !CellB.bIsSolid)
					{
						const float HeightA = CellA.TerrainHeight + CellA.FluidLevel;
						const float HeightB = CellB.TerrainHeight + CellB.FluidLevel;
						const float HeightDiff = HeightA - HeightB;

						if (FMath::Abs(HeightDiff) > 0.01f)
						{
							FCAFluidCell* SourceCell = HeightDiff > 0 ? &CellA : &CellB;
							FCAFluidCell* TargetCell = HeightDiff > 0 ? &CellB : &CellA;
							UFluidChunk* SourceChunk = HeightDiff > 0 ? ChunkA : ChunkB;
							UFluidChunk* TargetChunk = HeightDiff > 0 ? ChunkB : ChunkA;

							if (SourceCell->FluidLevel > 0.01f)
							{
								const float SpaceInTarget = SourceChunk->MaxFluidLevel - TargetCell->FluidLevel;
								const float PossibleFlow = FMath::Min(SourceCell->FluidLevel * FlowAmount, FMath::Abs(HeightDiff) * 0.5f);
								const float ActualFlow = FMath::Min(PossibleFlow, SpaceInTarget);

								if (ActualFlow > 0.0f)
								{
									SourceCell->FluidLevel -= ActualFlow;
									TargetCell->FluidLevel += ActualFlow;

									SourceCell->bSettled = false;
									SourceCell->SettledCounter = 0;
									TargetCell->bSettled = false;
									TargetCell->SettledCounter = 0;

									SourceChunk->bDirty = true;
									TargetChunk->bDirty = true;
									SourceChunk->ConsiderMeshUpdate(ActualFlow);
									TargetChunk->ConsiderMeshUpdate(ActualFlow);
								}
							}
						}
					}
				}
			}
		}
	}
	else if (DiffY == -1) // ChunkB is to the negative Y of ChunkA
	{
		// Process flow from ChunkA's negative Y border to ChunkB's positive Y border
		for (int32 x = 0; x < LocalChunkSize; ++x)
		{
			for (int32 z = 0; z < LocalChunkSize; ++z)
			{
				const int32 IdxA = ChunkA->GetLocalCellIndex(x, 0, z);
				const int32 IdxB = ChunkB->GetLocalCellIndex(x, LocalChunkSize - 1, z);

				if (IdxA != -1 && IdxB != -1)
				{
					FCAFluidCell& CellA = ChunkA->NextCells[IdxA];
					FCAFluidCell& CellB = ChunkB->NextCells[IdxB];

					if (!CellA.bIsSolid && !CellB.bIsSolid)
					{
						const float HeightA = CellA.TerrainHeight + CellA.FluidLevel;
						const float HeightB = CellB.TerrainHeight + CellB.FluidLevel;
						const float HeightDiff = HeightA - HeightB;

						if (FMath::Abs(HeightDiff) > 0.01f)
						{
							FCAFluidCell* SourceCell = HeightDiff > 0 ? &CellA : &CellB;
							FCAFluidCell* TargetCell = HeightDiff > 0 ? &CellB : &CellA;
							UFluidChunk* SourceChunk = HeightDiff > 0 ? ChunkA : ChunkB;
							UFluidChunk* TargetChunk = HeightDiff > 0 ? ChunkB : ChunkA;

							if (SourceCell->FluidLevel > 0.01f)
							{
								const float SpaceInTarget = SourceChunk->MaxFluidLevel - TargetCell->FluidLevel;
								const float PossibleFlow = FMath::Min(SourceCell->FluidLevel * FlowAmount, FMath::Abs(HeightDiff) * 0.5f);
								const float ActualFlow = FMath::Min(PossibleFlow, SpaceInTarget);

								if (ActualFlow > 0.0f)
								{
									SourceCell->FluidLevel -= ActualFlow;
									TargetCell->FluidLevel += ActualFlow;

									SourceCell->bSettled = false;
									SourceCell->SettledCounter = 0;
									TargetCell->bSettled = false;
									TargetCell->SettledCounter = 0;

									SourceChunk->bDirty = true;
									TargetChunk->bDirty = true;
									SourceChunk->ConsiderMeshUpdate(ActualFlow);
									TargetChunk->ConsiderMeshUpdate(ActualFlow);
								}
							}
						}
					}
				}
			}
		}
	}
	else if (DiffZ == 1) // ChunkB is above ChunkA
	{
		// Process flow from ChunkA's top to ChunkB's bottom (usually no upward flow unless pressure)
		for (int32 x = 0; x < LocalChunkSize; ++x)
		{
			for (int32 y = 0; y < LocalChunkSize; ++y)
			{
				const int32 IdxA = ChunkA->GetLocalCellIndex(x, y, LocalChunkSize - 1);
				const int32 IdxB = ChunkB->GetLocalCellIndex(x, y, 0);

				if (IdxA != -1 && IdxB != -1)
				{
					FCAFluidCell& CellA = ChunkA->NextCells[IdxA];
					FCAFluidCell& CellB = ChunkB->NextCells[IdxB];

					// Only allow upward flow if there's significant pressure
					if (!CellA.bIsSolid && !CellB.bIsSolid && CellA.FluidLevel >= ChunkA->MaxFluidLevel * 0.95f)
					{
						const float SpaceInB = ChunkA->MaxFluidLevel - CellB.FluidLevel;
						const float PossibleFlow = FMath::Min(CellA.FluidLevel * FlowAmount * 0.1f, SpaceInB);

						if (PossibleFlow > 0.0f)
						{
							ChunkA->NextCells[IdxA].FluidLevel -= PossibleFlow;
							ChunkB->NextCells[IdxB].FluidLevel += PossibleFlow;
							ChunkA->bDirty = true;
							ChunkB->bDirty = true;
						}
					}
				}
			}
		}
	}
	else if (DiffZ == -1) // ChunkB is below ChunkA
	{
		// Process gravity flow from ChunkA's bottom to ChunkB's top
		for (int32 x = 0; x < LocalChunkSize; ++x)
		{
			for (int32 y = 0; y < LocalChunkSize; ++y)
			{
				const int32 IdxA = ChunkA->GetLocalCellIndex(x, y, 0);
				const int32 IdxB = ChunkB->GetLocalCellIndex(x, y, LocalChunkSize - 1);

				if (IdxA != -1 && IdxB != -1)
				{
					FCAFluidCell& CellA = ChunkA->NextCells[IdxA];
					FCAFluidCell& CellB = ChunkB->NextCells[IdxB];

					if (!CellA.bIsSolid && !CellB.bIsSolid && CellA.FluidLevel > 0.01f)
					{
						const float SpaceInB = ChunkA->MaxFluidLevel - CellB.FluidLevel;
						const float GravityFlow = (ChunkA->Gravity / 1000.0f) * DeltaTime;
						const float PossibleFlow = FMath::Min(CellA.FluidLevel * GravityFlow, SpaceInB);

						if (PossibleFlow > 0.0f)
						{
							ChunkA->NextCells[IdxA].FluidLevel -= PossibleFlow;
							ChunkB->NextCells[IdxB].FluidLevel += PossibleFlow;
							ChunkA->bDirty = true;
							ChunkB->bDirty = true;
						}
					}
				}
			}
		}
	}
}

float UFluidChunkManager::GetDistanceToChunk(const FFluidChunkCoord& Coord, const TArray<FVector>& ViewerPositions) const
{
	if (ViewerPositions.Num() == 0)
		return FLT_MAX;

	const float ChunkWorldSize = ChunkSize * CellSize;
	const FVector ChunkCenter = WorldOrigin + FVector(
		(Coord.X + 0.5f) * ChunkWorldSize,
		(Coord.Y + 0.5f) * ChunkWorldSize,
		(Coord.Z + 0.5f) * ChunkWorldSize
	);

	float MinDistance = FLT_MAX;
	for (const FVector& ViewerPos : ViewerPositions)
	{
		const float Distance = FVector::Dist(ChunkCenter, ViewerPos);
		MinDistance = FMath::Min(MinDistance, Distance);
	}

	return MinDistance;
}

void UFluidChunkManager::LoadChunk(const FFluidChunkCoord& Coord)
{
	UFluidChunk* Chunk = GetOrCreateChunk(Coord);
	if (Chunk && Chunk->State == EChunkState::Unloaded)
	{
		Chunk->LoadChunk();

		// Try to restore from cache if persistence is enabled
		if (StreamingConfig.bEnablePersistence)
		{
			FChunkPersistentData PersistentData;
			if (LoadChunkData(Coord, PersistentData))
			{
				float VolumeBefore = Chunk->GetTotalFluidVolume();
				Chunk->DeserializeChunkData(PersistentData);
				float VolumeAfter = Chunk->GetTotalFluidVolume();
				ChunksLoadedThisFrame++;
			}
			else
			{
			}
		}

		// Apply static water if manager is available
		if (StaticWaterManager)
		{
			FBox ChunkBounds = Chunk->GetWorldBounds();
			if (StaticWaterManager->ChunkIntersectsStaticWater(ChunkBounds))
			{
				StaticWaterManager->ApplyStaticWaterToChunk(Chunk);
			}
		}

		// Track load time for debug
		ChunkLoadTimes.Add(Coord, FPlatformTime::Seconds());
		ChunkStateHistory.Add(Coord, FString::Printf(TEXT("Loaded at %.2fs"), FPlatformTime::Seconds()));

		OnChunkLoadedDelegate.Broadcast(Coord);
	}
}

void UFluidChunkManager::UnloadChunk(const FFluidChunkCoord& Coord)
{
	FScopeLock Lock(&ChunkMapMutex);

	if (UFluidChunk** ChunkPtr = LoadedChunks.Find(Coord))
	{
		UFluidChunk* Chunk = *ChunkPtr;
		if (Chunk)
		{
			// Save to cache if persistence is enabled and chunk has fluid
			if (StreamingConfig.bEnablePersistence)
			{
				if (Chunk->HasFluid())
				{
					// Check if enough time has passed since last save (prevent frequent saves)
					const float CurrentTime = FPlatformTime::Seconds();
					const float MinTimeBetweenSaves = 5.0f; // Don't save same chunk more than once per 5 seconds

					bool bShouldSave = true;
					if (float* LastSaveTime = ChunkLastSaveTime.Find(Coord))
					{
						if (CurrentTime - *LastSaveTime < MinTimeBetweenSaves)
						{
							bShouldSave = false;
						}
					}

					if (bShouldSave)
					{
						FChunkPersistentData PersistentData = Chunk->SerializeChunkData();
						SaveChunkData(Coord, PersistentData);
						ChunkLastSaveTime.Add(Coord, CurrentTime);
						ChunksSavedThisFrame++;
					}
				}
				else
				{
				}
			}
			else
			{
			}

			Chunk->UnloadChunk();
			ActiveChunkCoords.Remove(Coord);
			InactiveChunkCoords.Remove(Coord);
			BorderOnlyChunkCoords.Remove(Coord);


			// Track unload time for debug
			ChunkStateHistory.Add(Coord, FString::Printf(TEXT("Unloaded at %.2fs"), FPlatformTime::Seconds()));
			ChunkLoadTimes.Remove(Coord);

			LoadedChunks.Remove(Coord);
			OnChunkUnloadedDelegate.Broadcast(Coord);
		}
	}
}

void UFluidChunkManager::ActivateChunk(UFluidChunk* Chunk)
{
	if (!Chunk)
		return;

	const FFluidChunkCoord& Coord = Chunk->ChunkCoord;

	if (Chunk->State != EChunkState::Active)
	{
		Chunk->ActivateChunk();
		ActiveChunkCoords.Add(Coord);
		InactiveChunkCoords.Remove(Coord);
		BorderOnlyChunkCoords.Remove(Coord);

		// Ensure neighboring chunks are at least loaded for border synchronization
		const TArray<FFluidChunkCoord> NeighborCoords = {
			FFluidChunkCoord(Coord.X + 1, Coord.Y, Coord.Z),
			FFluidChunkCoord(Coord.X - 1, Coord.Y, Coord.Z),
			FFluidChunkCoord(Coord.X, Coord.Y + 1, Coord.Z),
			FFluidChunkCoord(Coord.X, Coord.Y - 1, Coord.Z),
			FFluidChunkCoord(Coord.X, Coord.Y, Coord.Z + 1),
			FFluidChunkCoord(Coord.X, Coord.Y, Coord.Z - 1)
		};

		for (const FFluidChunkCoord& NeighborCoord : NeighborCoords)
		{
			UFluidChunk* NeighborChunk = GetChunk(NeighborCoord);
			if (!NeighborChunk)
			{
				// Create and load the neighbor chunk if it doesn't exist
				NeighborChunk = GetOrCreateChunk(NeighborCoord);
				if (NeighborChunk && NeighborChunk->State == EChunkState::Unloaded)
				{
					NeighborChunk->LoadChunk();
					InactiveChunkCoords.Add(NeighborCoord);
				}
			}
		}

		// Track activation for debug
		ChunkStateHistory.Add(Coord, FString::Printf(TEXT("Activated at %.2fs"), FPlatformTime::Seconds()));

		// Notify that the chunk has been activated (for terrain refresh)
		OnChunkLoadedDelegate.Broadcast(Coord);
	}
}

void UFluidChunkManager::DeactivateChunk(UFluidChunk* Chunk)
{
	if (!Chunk)
		return;

	const FFluidChunkCoord& Coord = Chunk->ChunkCoord;

	if (Chunk->State == EChunkState::Active)
	{
		Chunk->DeactivateChunk();
		ActiveChunkCoords.Remove(Coord);
		InactiveChunkCoords.Add(Coord);

		// Track deactivation for debug
		ChunkStateHistory.Add(Coord, FString::Printf(TEXT("Deactivated at %.2fs"), FPlatformTime::Seconds()));
	}
}

void UFluidChunkManager::DrawDebugChunks(UWorld* World) const
{
	if (!World)
		return;


	// Early exit if chunk debug is disabled
	if (!bShowChunkBorders && !bShowChunkStates)
		return;

	const float CurrentTime = FPlatformTime::Seconds();

	// Get viewer positions to prioritize nearby chunks
	TArray<FVector> ViewerPositions;
	if (World->GetNetMode() == NM_Standalone)
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				ViewerPositions.Add(Pawn->GetActorLocation());
			}
		}
	}

	// Fallback to world center if no player found
	if (ViewerPositions.Num() == 0)
	{
		ViewerPositions.Add(WorldOrigin);
	}

	const FVector PrimaryViewerPos = ViewerPositions[0];

	// Summary stats are now integrated into 'stat voxelfluid' display
	// Individual chunk information still shows in world space above each chunk

	// Create sorted list of chunks by distance to player
	struct FChunkDistancePair
	{
		FFluidChunkCoord Coord;
		UFluidChunk* Chunk;
		float Distance;

		FChunkDistancePair(const FFluidChunkCoord& InCoord, UFluidChunk* InChunk, float InDistance)
			: Coord(InCoord), Chunk(InChunk), Distance(InDistance) {}

		bool operator<(const FChunkDistancePair& Other) const
		{
			// Prioritize active chunks first, then sort by distance
			if (Chunk->State == EChunkState::Active && Other.Chunk->State != EChunkState::Active)
				return true;
			if (Chunk->State != EChunkState::Active && Other.Chunk->State == EChunkState::Active)
				return false;
			return Distance < Other.Distance;
		}
	};

	TArray<FChunkDistancePair> SortedChunks;
	for (const auto& Pair : LoadedChunks)
	{
		if (!Pair.Value)
			continue;

		const float Distance = GetDistanceToChunk(Pair.Key, ViewerPositions);
		SortedChunks.Emplace(Pair.Key, Pair.Value, Distance);
	}

	// Sort chunks by priority (active first, then by distance)
	SortedChunks.Sort();

	// Draw the closest/most important chunks first
	int32 DebugIndex = 0;
	const int32 MaxChunksToShow = 15; // Reduced to focus on nearby chunks

	for (const FChunkDistancePair& ChunkPair : SortedChunks)
	{
		if (DebugIndex >= MaxChunksToShow)
			break;

		const UFluidChunk* Chunk = ChunkPair.Chunk;
		const FFluidChunkCoord& Coord = ChunkPair.Coord;
		const float Distance = ChunkPair.Distance;
		const FBox ChunkBounds = Chunk->GetWorldBounds();

		FColor ChunkColor = FColor::White;
		FString StateText = TEXT("Unknown");

		// Determine color and state text based on chunk state
		switch (Chunk->State)
		{
			case EChunkState::Active:
				// Color active chunks based on fluid amount
				{
					const float FluidVolume = Chunk->GetTotalFluidVolume();
					if (FluidVolume > 100.0f)
						ChunkColor = FColor::Cyan; // Lots of fluid
					else if (FluidVolume > 10.0f)
						ChunkColor = FColor::Blue; // Moderate fluid
					else if (FluidVolume > 0.1f)
						ChunkColor = FColor::Green; // Some fluid
					else
						ChunkColor = FColor(0, 128, 0); // Active but dry
				}
				StateText = TEXT("ACTIVE");
				break;
			case EChunkState::Inactive:
				ChunkColor = FColor::Yellow;
				StateText = TEXT("INACTIVE");
				break;
			case EChunkState::BorderOnly:
				ChunkColor = FColor::Orange;
				StateText = TEXT("BORDER");
				break;
			case EChunkState::Loading:
				ChunkColor = FColor::Magenta;
				StateText = TEXT("LOADING");
				break;
			case EChunkState::Unloading:
				ChunkColor = FColor::Purple;
				StateText = TEXT("UNLOADING");
				break;
			default:
				ChunkColor = FColor::Red;
				StateText = TEXT("ERROR");
				break;
		}

		// Draw chunk border
		if (bShowChunkBorders)
		{
			// Make active chunks more prominent
			const float BorderThickness = (Chunk->State == EChunkState::Active) ? 3.0f : 1.0f;
			DrawDebugBox(World, ChunkBounds.GetCenter(), ChunkBounds.GetExtent(), ChunkColor, false, DebugUpdateInterval + 0.1f, 0, BorderThickness);
		}

		// Draw detailed chunk information
		if (bShowChunkStates)
		{
			// Calculate load time
			float LoadTime = 0.0f;
			if (const float* LoadTimePtr = ChunkLoadTimes.Find(Coord))
			{
				LoadTime = CurrentTime - *LoadTimePtr;
			}

			// Get LOD level
			const int32 LODLevel = Chunk->CurrentLOD;

			// Get state history
			FString StateHistory = TEXT("No History");
			if (const FString* HistoryPtr = ChunkStateHistory.Find(Coord))
			{
				StateHistory = *HistoryPtr;
			}

			// Check if chunk has cached mesh data
			const bool bHasCachedMesh = Chunk->StoredMeshData.bIsValid;
			const bool bMeshDirty = Chunk->bMeshDataDirty;

			// Build detailed info string with distance information
			const FString ChunkInfo = FString::Printf(
				TEXT("Chunk [%d,%d,%d] (%.0fm)\n")
				TEXT("State: %s | LOD: %d\n")
				TEXT("Fluid: %.2f units | Cells: %d\n")
				TEXT("Activity: %.4f | Evap: %.3f/s\n")
				TEXT("Mesh: %s%s\n")
				TEXT("Load Time: %.1fs\n")
				TEXT("%s"),
				Coord.X, Coord.Y, Coord.Z, Distance,
				*StateText,
				LODLevel,
				Chunk->GetTotalFluidVolume(),
				Chunk->GetActiveCellCount(),
				Chunk->TotalFluidActivity,
				Chunk->EvaporationRate,
				bHasCachedMesh ? TEXT("Cached") : TEXT("None"),
				bMeshDirty ? TEXT(" [DIRTY]") : TEXT(""),
				LoadTime,
				*StateHistory
			);

			// Position text above chunk center, with larger text for active chunks
			const FVector TextPosition = ChunkBounds.GetCenter() + FVector(0, 0, ChunkBounds.GetExtent().Z + 100);
			const float TextSize = (Chunk->State == EChunkState::Active) ? 1.0f : 0.7f;
			DrawDebugString(World, TextPosition, ChunkInfo, nullptr, ChunkColor, DebugUpdateInterval + 0.1f, true, TextSize);
		}

		DebugIndex++;
	}

	// Overflow information is now available through 'stat voxelfluid' showing total vs active chunk counts

	// Distance circles removed to avoid intrusive view obstruction
	// The distance information is still shown in the text display above
}

bool UFluidChunkManager::ShouldUpdateDebugVisualization() const
{
	if (!bShowChunkBorders && !bShowChunkStates)
		return false;

	if (DebugUpdateTimer >= DebugUpdateInterval)
	{
		// Reset timer (this is a bit hacky but works for our purpose)
		const_cast<UFluidChunkManager*>(this)->DebugUpdateTimer = 0.0f;
		return true;
	}

	return false;
}

// ==================== Persistence Methods ====================

void UFluidChunkManager::SaveChunkData(const FFluidChunkCoord& Coord, const FChunkPersistentData& Data)
{
	FScopeLock Lock(&CacheMutex);

	// Check if we're overwriting an existing entry
	if (ChunkCache.Contains(Coord))
	{
		const FCachedChunkEntry& ExistingEntry = ChunkCache[Coord];
	}

	// Check cache size limit
	if (ChunkCache.Num() >= StreamingConfig.MaxCachedChunks)
	{
		PruneExpiredCache();

		// If still over limit, remove oldest entries
		if (ChunkCache.Num() >= StreamingConfig.MaxCachedChunks)
		{
			// Find and remove the oldest entry
			float OldestTime = FLT_MAX;
			FFluidChunkCoord OldestCoord;

			for (const auto& CachePair : ChunkCache)
			{
				if (CachePair.Value.CacheTime < OldestTime && CachePair.Value.AccessCount == 0)
				{
					OldestTime = CachePair.Value.CacheTime;
					OldestCoord = CachePair.Key;
				}
			}

			if (OldestTime < FLT_MAX)
			{
				ChunkCache.Remove(OldestCoord);
			}
		}
	}

	// Add or update cache entry
	FCachedChunkEntry& Entry = ChunkCache.FindOrAdd(Coord);
	Entry.Data = Data;
	Entry.CacheTime = FPlatformTime::Seconds();
	Entry.AccessCount = 0;
}

bool UFluidChunkManager::LoadChunkData(const FFluidChunkCoord& Coord, FChunkPersistentData& OutData)
{
	FScopeLock Lock(&CacheMutex);

	if (FCachedChunkEntry* Entry = ChunkCache.Find(Coord))
	{
		// Check if expired
		float CurrentTime = FPlatformTime::Seconds();
		if (CurrentTime - Entry->CacheTime > StreamingConfig.CacheExpirationTime)
		{
			ChunkCache.Remove(Coord);
			return false;
		}

		// Update access info
		Entry->AccessCount++;
		Entry->CacheTime = CurrentTime; // Refresh cache time on access

		OutData = Entry->Data;
		return true;
	}

	return false;
}

void UFluidChunkManager::ClearChunkCache()
{
	FScopeLock Lock(&CacheMutex);

	int32 ClearedCount = ChunkCache.Num();
	ChunkCache.Empty();

}

void UFluidChunkManager::PruneExpiredCache()
{
	FScopeLock Lock(&CacheMutex);

	float CurrentTime = FPlatformTime::Seconds();
	TArray<FFluidChunkCoord> ExpiredCoords;

	for (const auto& CachePair : ChunkCache)
	{
		if (CurrentTime - CachePair.Value.CacheTime > StreamingConfig.CacheExpirationTime)
		{
			ExpiredCoords.Add(CachePair.Key);
		}
	}

	for (const FFluidChunkCoord& Coord : ExpiredCoords)
	{
		ChunkCache.Remove(Coord);
	}

	if (ExpiredCoords.Num() > 0)
	{
	}
}

int32 UFluidChunkManager::GetCacheMemoryUsage() const
{
	FScopeLock Lock(&CacheMutex);

	int32 TotalBytes = 0;
	for (const auto& CachePair : ChunkCache)
	{
		TotalBytes += CachePair.Value.Data.GetMemorySize();
		TotalBytes += sizeof(FCachedChunkEntry);
	}

	return TotalBytes / 1024; // Return in KB
}

int32 UFluidChunkManager::GetCacheSize() const
{
	FScopeLock Lock(&CacheMutex);
	return ChunkCache.Num();
}

void UFluidChunkManager::SaveCacheToDisk()
{
	// Optional: Implement disk persistence
	// This would serialize the cache to a file for long-term storage
	// Could use FArchive or JSON serialization
}

void UFluidChunkManager::LoadCacheFromDisk()
{
	// Optional: Implement disk persistence loading
	// This would deserialize the cache from a file
}

void UFluidChunkManager::TestPersistence(const FVector& WorldPos)
{

	// Get the chunk at this position
	FFluidChunkCoord ChunkCoord = GetChunkCoordFromWorldPosition(WorldPos);
	UFluidChunk* Chunk = GetChunk(ChunkCoord);

	if (!Chunk)
	{
		return;
	}

	// Log current state
	float VolumeBeforeSave = Chunk->GetTotalFluidVolume();

	// Save the chunk data
	if (Chunk->HasFluid())
	{
		FChunkPersistentData SaveData = Chunk->SerializeChunkData();
		SaveChunkData(ChunkCoord, SaveData);

		// Clear the chunk to simulate unloading
		for (int32 i = 0; i < Chunk->Cells.Num(); ++i)
		{
			if (!Chunk->Cells[i].bIsSolid)
			{
				Chunk->Cells[i].FluidLevel = 0.0f;
			}
		}
		Chunk->NextCells = Chunk->Cells;


		// Now reload from cache
		FChunkPersistentData LoadData;
		if (LoadChunkData(ChunkCoord, LoadData))
		{
			Chunk->DeserializeChunkData(LoadData);
			float VolumeAfterLoad = Chunk->GetTotalFluidVolume();

			if (FMath::IsNearlyEqual(VolumeBeforeSave, VolumeAfterLoad, 0.01f))
			{
			}
			else
			{
			}
		}
		else
		{
		}
	}
	else
	{
	}

	// Show cache status
}

void UFluidChunkManager::ForceActivateChunk(UFluidChunk* Chunk)
{
	if (!Chunk)
		return;

	const FFluidChunkCoord& Coord = Chunk->ChunkCoord;

	// Ensure chunk is in loaded chunks
	if (!LoadedChunks.Contains(Coord))
	{
		LoadedChunks.Add(Coord, Chunk);
	}

	// Call protected ActivateChunk method
	ActivateChunk(Chunk);
}


void UFluidChunkManager::ForceUnloadAllChunks()
{

	// Clear last save times to ensure all chunks get saved during force unload
	ChunkLastSaveTime.Empty();

	TArray<FFluidChunkCoord> ChunksToUnload;
	for (const auto& Pair : LoadedChunks)
	{
		ChunksToUnload.Add(Pair.Key);
	}

	int32 SavedCount = 0;
	float TotalSavedVolume = 0.0f;

	for (const FFluidChunkCoord& Coord : ChunksToUnload)
	{
		if (UFluidChunk* Chunk = GetChunk(Coord))
		{
			if (Chunk->HasFluid())
			{
				float Volume = Chunk->GetTotalFluidVolume();
				TotalSavedVolume += Volume;
				SavedCount++;
			}
		}
		UnloadChunk(Coord);
	}

}

bool UFluidChunkManager::ShouldUpdateChunk(UFluidChunk* Chunk) const
{
	if (!Chunk || !Chunk->IsValidLowLevel())
	{
		return false;
	}

	// Always update chunks with high fluid activity
	if (Chunk->TotalFluidActivity > 0.1f)
	{
		return true;
	}

	// Skip chunks that have been fully settled for a while
	if (Chunk->bFullySettled && Chunk->TotalFluidActivity < 0.001f)
	{
		// Only update settled chunks occasionally
		float TimeSinceLastUpdate = GetWorld() ? GetWorld()->GetTimeSeconds() - Chunk->LastUpdateTime : 0.0f;
		return TimeSinceLastUpdate > 0.5f; // Update every 500ms for settled chunks
	}

	// Skip chunks with very low fluid volume
	if (Chunk->GetTotalFluidVolume() < 0.01f)
	{
		return false;
	}

	// Update chunks that haven't been updated recently
	float TimeSinceLastUpdate = GetWorld() ? GetWorld()->GetTimeSeconds() - Chunk->LastUpdateTime : 0.0f;
	return TimeSinceLastUpdate > 0.033f; // Update at least every 33ms (30 FPS)
}

// ==================== Edit-Triggered Activation Methods ====================

void UFluidChunkManager::OnVoxelEditOccurred(const FVector& EditLocation, float EditRadius)
{
	// Only process if we're using edit-triggered activation
	if (StreamingConfig.ActivationMode == EChunkActivationMode::DistanceBased)
	{
		return;
	}

	// Activate chunks in the edit radius
	ActivateChunksForEdit(EditLocation, EditRadius);
}

void UFluidChunkManager::OnVoxelEditOccurredInBounds(const FBox& EditBounds)
{
	// Only process if we're using edit-triggered activation
	if (StreamingConfig.ActivationMode == EChunkActivationMode::DistanceBased)
	{
		return;
	}

	// Get center and radius from bounds
	FVector Center = EditBounds.GetCenter();
	float Radius = EditBounds.GetExtent().GetMax();

	ActivateChunksForEdit(Center, Radius);
}

void UFluidChunkManager::ActivateChunksForEdit(const FVector& EditLocation, float Radius)
{
	// Use configured edit activation radius or the provided radius, whichever is larger
	float ActivationRadius = FMath::Max(Radius, StreamingConfig.EditActivationRadius);
	
	// Find all chunks that could be affected by this edit
	TArray<FFluidChunkCoord> AffectedChunks = GetChunksInBounds(
		FBox(EditLocation - FVector(ActivationRadius), EditLocation + FVector(ActivationRadius))
	);

	float CurrentTime = FPlatformTime::Seconds();

	UE_LOG(LogTemp, Log, TEXT("Voxel edit at %s, activating %d chunks in radius %.0f"), 
		*EditLocation.ToString(), AffectedChunks.Num(), ActivationRadius);

	for (const FFluidChunkCoord& ChunkCoord : AffectedChunks)
	{
		// Get or create the chunk
		UFluidChunk* Chunk = GetOrCreateChunk(ChunkCoord);
		if (!Chunk)
			continue;

		// Load chunk if not loaded
		if (Chunk->State == EChunkState::Unloaded)
		{
			Chunk->LoadChunk();
			
			// Try to restore from cache
			if (StreamingConfig.bEnablePersistence)
			{
				FChunkPersistentData PersistentData;
				if (LoadChunkData(ChunkCoord, PersistentData))
				{
					Chunk->DeserializeChunkData(PersistentData);
				}
			}
		}

		// Activate the chunk if not already active
		if (Chunk->State != EChunkState::Active)
		{
			ActivateChunk(Chunk);
			
			// Mark as edit-activated
			EditActivatedChunks.Add(ChunkCoord, CurrentTime);
			ChunkSettledTimes.Remove(ChunkCoord); // Clear any settled time
			
			UE_LOG(LogTemp, Verbose, TEXT("Edit-activated chunk [%d,%d,%d]"), 
				ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z);
		}
		else
		{
			// Refresh the activation time if already active
			if (EditActivatedChunks.Contains(ChunkCoord))
			{
				EditActivatedChunks[ChunkCoord] = CurrentTime;
				ChunkSettledTimes.Remove(ChunkCoord);
			}
		}
	}
}

void UFluidChunkManager::CheckForSettledChunks()
{
	// Only check if we're using edit-triggered activation
	if (StreamingConfig.ActivationMode == EChunkActivationMode::DistanceBased)
	{
		return;
	}

	float CurrentTime = FPlatformTime::Seconds();
	TArray<FFluidChunkCoord> ChunksToDeactivate;

	// Check all edit-activated chunks
	for (const auto& Pair : EditActivatedChunks)
	{
		const FFluidChunkCoord& ChunkCoord = Pair.Key;
		float ActivationTime = Pair.Value;

		UFluidChunk* Chunk = GetChunk(ChunkCoord);
		if (!Chunk || Chunk->State != EChunkState::Active)
			continue;

		// Check if chunk has settled
		bool bIsSettled = (Chunk->TotalFluidActivity < StreamingConfig.MinActivityForDeactivation) &&
						  (Chunk->bFullySettled || Chunk->GetTotalFluidVolume() < 0.1f);

		if (bIsSettled)
		{
			// Track when chunk became settled
			if (!ChunkSettledTimes.Contains(ChunkCoord))
			{
				ChunkSettledTimes.Add(ChunkCoord, CurrentTime);
				UE_LOG(LogTemp, Verbose, TEXT("Chunk [%d,%d,%d] became settled"), 
					ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z);
			}
			else
			{
				// Check if it's been settled long enough
				float SettledTime = ChunkSettledTimes[ChunkCoord];
				if (CurrentTime - SettledTime >= StreamingConfig.SettledDeactivationDelay)
				{
					ChunksToDeactivate.Add(ChunkCoord);
				}
			}
		}
		else
		{
			// Chunk is active again, remove from settled tracking
			ChunkSettledTimes.Remove(ChunkCoord);
		}
	}

	// Deactivate settled chunks
	for (const FFluidChunkCoord& ChunkCoord : ChunksToDeactivate)
	{
		UFluidChunk* Chunk = GetChunk(ChunkCoord);
		if (Chunk)
		{
			UE_LOG(LogTemp, Log, TEXT("Deactivating settled chunk [%d,%d,%d]"), 
				ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z);
			
			DeactivateChunk(Chunk);
			EditActivatedChunks.Remove(ChunkCoord);
			ChunkSettledTimes.Remove(ChunkCoord);
		}
	}
}

bool UFluidChunkManager::IsChunkEditActivated(const FFluidChunkCoord& Coord) const
{
	return EditActivatedChunks.Contains(Coord);
}

