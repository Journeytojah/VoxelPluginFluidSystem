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
		SET_DWORD_STAT(STAT_VoxelFluid_LoadedChunks, CachedStats.TotalChunks);
		SET_DWORD_STAT(STAT_VoxelFluid_ActiveChunks, CachedStats.ActiveChunks);
		SET_DWORD_STAT(STAT_VoxelFluid_InactiveChunks, CachedStats.InactiveChunks);
		SET_DWORD_STAT(STAT_VoxelFluid_BorderOnlyChunks, CachedStats.BorderOnlyChunks);
		SET_DWORD_STAT(STAT_VoxelFluid_ChunkLoadQueueSize, CachedStats.ChunkLoadQueueSize);
		SET_DWORD_STAT(STAT_VoxelFluid_ChunkUnloadQueueSize, CachedStats.ChunkUnloadQueueSize);
		SET_FLOAT_STAT(STAT_VoxelFluid_AvgChunkUpdateTime, CachedStats.AverageChunkUpdateTime);
	}
}

void UFluidChunkManager::UpdateSimulation(float DeltaTime)
{
	if (!bIsInitialized || !IsValidLowLevel())
		return;
	
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_UpdateSimulation);
	
	TArray<UFluidChunk*> ActiveChunkArray = GetActiveChunks();
	
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
	
	SynchronizeChunkBorders();
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
	
	for (const FFluidChunkCoord& Coord : ActiveChunkCoords)
	{
		UFluidChunk* Chunk = GetChunk(Coord);
		if (!Chunk || !Chunk->bBorderDirty)
			continue;
		
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
			if (Neighbor && Neighbor->State == EChunkState::Active)
			{
				ProcessCrossChunkFlow(Chunk, Neighbor, 0.016f);
			}
		}
	}
}

void UFluidChunkManager::ProcessCrossChunkFlow(UFluidChunk* ChunkA, UFluidChunk* ChunkB, float DeltaTime)
{
	if (!ChunkA || !ChunkB)
		return;
	
	const FFluidChunkCoord& CoordA = ChunkA->ChunkCoord;
	const FFluidChunkCoord& CoordB = ChunkB->ChunkCoord;
	
	const int32 DiffX = CoordB.X - CoordA.X;
	const int32 DiffY = CoordB.Y - CoordA.Y;
	const int32 DiffZ = CoordB.Z - CoordA.Z;
	
	if (FMath::Abs(DiffX) + FMath::Abs(DiffY) + FMath::Abs(DiffZ) != 1)
		return;
	
	const FChunkBorderData BorderA = ChunkA->ExtractBorderData();
	const FChunkBorderData BorderB = ChunkB->ExtractBorderData();
	
	if (DiffX == 1)
	{
		ChunkA->ApplyBorderData(BorderB);
		ChunkB->ApplyBorderData(BorderA);
	}
	else if (DiffX == -1)
	{
		ChunkA->ApplyBorderData(BorderB);
		ChunkB->ApplyBorderData(BorderA);
	}
	else if (DiffY == 1)
	{
		ChunkA->ApplyBorderData(BorderB);
		ChunkB->ApplyBorderData(BorderA);
	}
	else if (DiffY == -1)
	{
		ChunkA->ApplyBorderData(BorderB);
		ChunkB->ApplyBorderData(BorderA);
	}
	else if (DiffZ == 1)
	{
		ChunkA->ApplyBorderData(BorderB);
		ChunkB->ApplyBorderData(BorderA);
	}
	else if (DiffZ == -1)
	{
		ChunkA->ApplyBorderData(BorderB);
		ChunkB->ApplyBorderData(BorderA);
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
	}
}

void UFluidChunkManager::DrawDebugChunks(UWorld* World) const
{
	if (!World || (!bShowChunkBorders && !bShowChunkStates))
		return;
	
	for (const auto& Pair : LoadedChunks)
	{
		if (!Pair.Value)
			continue;
		
		const FBox ChunkBounds = Pair.Value->GetWorldBounds();
		FColor ChunkColor = FColor::White;
		
		if (bShowChunkStates)
		{
			switch (Pair.Value->State)
			{
				case EChunkState::Active:
					ChunkColor = FColor::Green;
					break;
				case EChunkState::Inactive:
					ChunkColor = FColor::Yellow;
					break;
				case EChunkState::BorderOnly:
					ChunkColor = FColor::Orange;
					break;
				default:
					ChunkColor = FColor::Red;
					break;
			}
		}
		
		if (bShowChunkBorders)
		{
			DrawDebugBox(World, ChunkBounds.GetCenter(), ChunkBounds.GetExtent(), ChunkColor, false, -1.0f, 0, 2.0f);
		}
	}
}