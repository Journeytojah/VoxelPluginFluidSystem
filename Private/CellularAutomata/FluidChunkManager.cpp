#include "CellularAutomata/FluidChunkManager.h"
#include "VoxelFluidStats.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Async/ParallelFor.h"

UFluidChunkManager::UFluidChunkManager()
{
	ChunkSize = 32;
	CellSize = 100.0f;
	WorldOrigin = FVector::ZeroVector;
	WorldSize = FVector(100000.0f, 100000.0f, 10000.0f);
	
	StreamingConfig.ActiveDistance = 5000.0f;
	StreamingConfig.LoadDistance = 8000.0f;
	StreamingConfig.UnloadDistance = 10000.0f;
	StreamingConfig.MaxActiveChunks = 64;
	StreamingConfig.MaxLoadedChunks = 128;
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
	
	UE_LOG(LogTemp, Log, TEXT("FluidChunkManager: Initialized with chunk size %d, cell size %.1f"), 
		   ChunkSize, CellSize);
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
		
		ProcessChunkLoadQueue();
		ProcessChunkUnloadQueue();
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
		
		// Calculate additional fluid statistics
		int32 SignificantCells = 0;
		float TotalFluidLevels = 0.0f;
		int32 CellsWithFluid = 0;
		
		for (const auto& ChunkPair : LoadedChunks)
		{
			UFluidChunk* Chunk = ChunkPair.Value;
			if (Chunk && Chunk->State != EChunkState::Unloaded)
			{
				for (int32 i = 0; i < Chunk->Cells.Num(); ++i)
				{
					const float FluidLevel = Chunk->Cells[i].FluidLevel;
					if (FluidLevel > 0.0f)
					{
						TotalFluidLevels += FluidLevel;
						CellsWithFluid++;
						
						if (FluidLevel > 0.1f)
						{
							SignificantCells++;
						}
					}
				}
			}
		}
		
		SET_DWORD_STAT(STAT_VoxelFluid_SignificantCells, SignificantCells);
		SET_FLOAT_STAT(STAT_VoxelFluid_AvgFluidLevel, CellsWithFluid > 0 ? TotalFluidLevels / CellsWithFluid : 0.0f);
		
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
	}
	
	// Update debug timer (debug drawing is now called externally)
	DebugUpdateTimer += DeltaTime;
}

void UFluidChunkManager::UpdateSimulation(float DeltaTime)
{
	if (!bIsInitialized || !IsValidLowLevel())
		return;
	
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_UpdateSimulation);
	
	TArray<UFluidChunk*> ActiveChunkArray = GetActiveChunks();
	
	// First, update individual chunk simulations
	if (StreamingConfig.bUseAsyncLoading && ActiveChunkArray.Num() > 4)
	{
		ParallelFor(ActiveChunkArray.Num(), [&](int32 Index)
		{
			if (ActiveChunkArray[Index])
			{
				ActiveChunkArray[Index]->UpdateSimulation(DeltaTime);
			}
		});
	}
	else
	{
		for (UFluidChunk* Chunk : ActiveChunkArray)
		{
			if (Chunk)
			{
				Chunk->UpdateSimulation(DeltaTime);
			}
		}
	}
	
	// Then synchronize borders between chunks
	SynchronizeChunkBorders();
	
	// Finally, apply the NextCells to Cells for all chunks after border sync
	for (UFluidChunk* Chunk : ActiveChunkArray)
	{
		if (Chunk)
		{
			// Swap the cell buffers to apply border changes
			Chunk->Cells = Chunk->NextCells;
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
	OutChunkCoord = GetChunkCoordFromWorldPosition(WorldPos);
	
	UFluidChunk* Chunk = const_cast<UFluidChunkManager*>(this)->GetChunk(OutChunkCoord);
	if (!Chunk)
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
	FFluidChunkCoord ChunkCoord;
	int32 LocalX, LocalY, LocalZ;
	
	if (GetCellFromWorldPosition(WorldPos, ChunkCoord, LocalX, LocalY, LocalZ))
	{
		UFluidChunk* Chunk = const_cast<UFluidChunkManager*>(this)->GetChunk(ChunkCoord);
		if (Chunk && Chunk->State != EChunkState::Unloaded)
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

TArray<UFluidChunk*> UFluidChunkManager::GetChunksInRadius(const FVector& Center, float Radius) const
{
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
	
	while (ProcessedCount < StreamingConfig.MaxChunksToProcessPerFrame && ChunkLoadQueue.Dequeue(Coord))
	{
		LoadChunk(Coord);
		ProcessedCount++;
	}
}

void UFluidChunkManager::ProcessChunkUnloadQueue()
{
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_ChunkStreaming);
	
	int32 ProcessedCount = 0;
	FFluidChunkCoord Coord;
	
	while (ProcessedCount < StreamingConfig.MaxChunksToProcessPerFrame && ChunkUnloadQueue.Dequeue(Coord))
	{
		UnloadChunk(Coord);
		ProcessedCount++;
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
						ChunksToActivate.Add(Coord);
						if (!IsChunkLoaded(Coord))
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
	
	for (const auto& Pair : LoadedChunks)
	{
		const FFluidChunkCoord& Coord = Pair.Key;
		const float Distance = GetDistanceToChunk(Coord, ViewerPositions);
		
		if (Distance > StreamingConfig.UnloadDistance)
		{
			ChunksToUnload.Add(Coord);
		}
		else if (Distance > StreamingConfig.ActiveDistance && ActiveChunkCoords.Contains(Coord))
		{
			ChunksToDeactivate.Add(Coord);
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
				// Process bidirectional flow between chunks
				ProcessCrossChunkFlow(Chunk, Neighbor, 0.016f);
				ProcessCrossChunkFlow(Neighbor, Chunk, 0.016f);
				ProcessedPairs.Add(PairKey);
			}
		}
		
		// Clear the border dirty flag after processing
		Chunk->bBorderDirty = false;
	}
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
					
					if (!CellA.bIsSolid && !CellB.bIsSolid && CellA.FluidLevel > 0.01f)
					{
						// Calculate flow based on fluid height difference
						const float HeightA = CellA.TerrainHeight + CellA.FluidLevel;
						const float HeightB = CellB.TerrainHeight + CellB.FluidLevel;
						const float HeightDiff = HeightA - HeightB;
						
						if (HeightDiff > 0.01f)
						{
							const float SpaceInB = ChunkA->MaxFluidLevel - CellB.FluidLevel;
							const float PossibleFlow = FMath::Min(CellA.FluidLevel * FlowAmount, HeightDiff * 0.5f);
							const float ActualFlow = FMath::Min(PossibleFlow, SpaceInB);
							
							if (ActualFlow > 0.0f)
							{
								CellA.FluidLevel -= ActualFlow;
								CellB.FluidLevel += ActualFlow;
								
								// Wake up the border cells
								CellA.bSettled = false;
								CellA.SettledCounter = 0;
								CellB.bSettled = false;
								CellB.SettledCounter = 0;
								
								ChunkA->bDirty = true;
								ChunkB->bDirty = true;
								ChunkA->ConsiderMeshUpdate(ActualFlow);
								ChunkB->ConsiderMeshUpdate(ActualFlow);
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
					
					if (!CellA.bIsSolid && !CellB.bIsSolid && CellA.FluidLevel > 0.01f)
					{
						const float HeightA = CellA.TerrainHeight + CellA.FluidLevel;
						const float HeightB = CellB.TerrainHeight + CellB.FluidLevel;
						const float HeightDiff = HeightA - HeightB;
						
						if (HeightDiff > 0.01f)
						{
							const float SpaceInB = ChunkA->MaxFluidLevel - CellB.FluidLevel;
							const float PossibleFlow = FMath::Min(CellA.FluidLevel * FlowAmount, HeightDiff * 0.5f);
							const float ActualFlow = FMath::Min(PossibleFlow, SpaceInB);
							
							if (ActualFlow > 0.0f)
							{
								CellA.FluidLevel -= ActualFlow;
								CellB.FluidLevel += ActualFlow;
								
								// Wake up the border cells
								CellA.bSettled = false;
								CellA.SettledCounter = 0;
								CellB.bSettled = false;
								CellB.SettledCounter = 0;
								
								ChunkA->bDirty = true;
								ChunkB->bDirty = true;
								ChunkA->ConsiderMeshUpdate(ActualFlow);
								ChunkB->ConsiderMeshUpdate(ActualFlow);
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
					
					if (!CellA.bIsSolid && !CellB.bIsSolid && CellA.FluidLevel > 0.01f)
					{
						const float HeightA = CellA.TerrainHeight + CellA.FluidLevel;
						const float HeightB = CellB.TerrainHeight + CellB.FluidLevel;
						const float HeightDiff = HeightA - HeightB;
						
						if (HeightDiff > 0.01f)
						{
							const float SpaceInB = ChunkA->MaxFluidLevel - CellB.FluidLevel;
							const float PossibleFlow = FMath::Min(CellA.FluidLevel * FlowAmount, HeightDiff * 0.5f);
							const float ActualFlow = FMath::Min(PossibleFlow, SpaceInB);
							
							if (ActualFlow > 0.0f)
							{
								CellA.FluidLevel -= ActualFlow;
								CellB.FluidLevel += ActualFlow;
								
								// Wake up the border cells
								CellA.bSettled = false;
								CellA.SettledCounter = 0;
								CellB.bSettled = false;
								CellB.SettledCounter = 0;
								
								ChunkA->bDirty = true;
								ChunkB->bDirty = true;
								ChunkA->ConsiderMeshUpdate(ActualFlow);
								ChunkB->ConsiderMeshUpdate(ActualFlow);
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
					
					if (!CellA.bIsSolid && !CellB.bIsSolid && CellA.FluidLevel > 0.01f)
					{
						const float HeightA = CellA.TerrainHeight + CellA.FluidLevel;
						const float HeightB = CellB.TerrainHeight + CellB.FluidLevel;
						const float HeightDiff = HeightA - HeightB;
						
						if (HeightDiff > 0.01f)
						{
							const float SpaceInB = ChunkA->MaxFluidLevel - CellB.FluidLevel;
							const float PossibleFlow = FMath::Min(CellA.FluidLevel * FlowAmount, HeightDiff * 0.5f);
							const float ActualFlow = FMath::Min(PossibleFlow, SpaceInB);
							
							if (ActualFlow > 0.0f)
							{
								CellA.FluidLevel -= ActualFlow;
								CellB.FluidLevel += ActualFlow;
								
								// Wake up the border cells
								CellA.bSettled = false;
								CellA.SettledCounter = 0;
								CellB.bSettled = false;
								CellB.SettledCounter = 0;
								
								ChunkA->bDirty = true;
								ChunkB->bDirty = true;
								ChunkA->ConsiderMeshUpdate(ActualFlow);
								ChunkB->ConsiderMeshUpdate(ActualFlow);
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
	if (!World || (!bShowChunkBorders && !bShowChunkStates))
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
				ChunkColor = FColor::Green;
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
				ChunkColor = FColor::Blue;
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
			
			// Get state history
			FString StateHistory = TEXT("No History");
			if (const FString* HistoryPtr = ChunkStateHistory.Find(Coord))
			{
				StateHistory = *HistoryPtr;
			}
			
			// Build detailed info string with distance information
			const FString ChunkInfo = FString::Printf(
				TEXT("Chunk [%d,%d,%d] (%.0fm)\n")
				TEXT("State: %s\n")
				TEXT("LOD: %d | Cells: %d\n")
				TEXT("Fluid: %.2f units\n")
				TEXT("Load Time: %.1fs\n")
				TEXT("%s"),
				Coord.X, Coord.Y, Coord.Z, Distance,
				*StateText,
				Chunk->CurrentLOD,
				Chunk->GetActiveCellCount(),
				Chunk->GetTotalFluidVolume(),
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