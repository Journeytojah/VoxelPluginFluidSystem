#include "CellularAutomata/FluidChunk.h"
#include "VoxelFluidStats.h"
#include "Math/UnrealMathUtility.h"
#include "HAL/UnrealMemory.h"

// FChunkPersistentData implementation
void FChunkPersistentData::CompressFrom(const TArray<FCAFluidCell>& Cells)
{
	CompressedCells.Empty(Cells.Num());
	NonEmptyCellCount = 0;
	TotalFluidVolume = 0.0f;
	
	for (const FCAFluidCell& Cell : Cells)
	{
		FCompressedFluidCell CompressedCell(Cell);
		CompressedCells.Add(CompressedCell);
		
		if (Cell.FluidLevel > 0.001f && !Cell.bIsSolid)
		{
			NonEmptyCellCount++;
			TotalFluidVolume += Cell.FluidLevel;
		}
	}
	
	bHasFluid = (NonEmptyCellCount > 0);
	Timestamp = FPlatformTime::Seconds();
	Checksum = CalculateChecksum();
}

void FChunkPersistentData::DecompressTo(TArray<FCAFluidCell>& OutCells) const
{
	if (CompressedCells.Num() == 0)
		return;
	
	if (OutCells.Num() != CompressedCells.Num())
	{
		OutCells.SetNum(CompressedCells.Num());
	}
	
	for (int32 i = 0; i < CompressedCells.Num(); ++i)
	{
		CompressedCells[i].Decompress(OutCells[i]);
	}
}

uint32 FChunkPersistentData::CalculateChecksum() const
{
	uint32 Hash = 0;
	for (const FCompressedFluidCell& Cell : CompressedCells)
	{
		Hash = HashCombine(Hash, GetTypeHash(Cell.FluidLevel));
		Hash = HashCombine(Hash, GetTypeHash(Cell.Flags));
	}
	return Hash;
}

bool FChunkPersistentData::ValidateChecksum() const
{
	return Checksum == CalculateChecksum();
}

int32 FChunkPersistentData::GetMemorySize() const
{
	return sizeof(FChunkPersistentData) + 
	       (CompressedCells.Num() * sizeof(FCompressedFluidCell));
}

UFluidChunk::UFluidChunk()
{
	ChunkSize = 32;
	CellSize = 100.0f;
	FlowRate = 0.5f;
	Viscosity = 0.1f;
	Gravity = 981.0f;
	MinFluidLevel = 0.001f;
	MaxFluidLevel = 1.0f;
	CompressionFactor = 0.05f;
	bUseSparseRepresentation = false;
	SparseGridOccupancy = 1.0f;
}

void UFluidChunk::Initialize(const FFluidChunkCoord& InCoord, int32 InChunkSize, float InCellSize, const FVector& InWorldOrigin)
{
	ChunkCoord = InCoord;
	ChunkSize = FMath::Max(1, InChunkSize);
	CellSize = FMath::Max(1.0f, InCellSize);
	WorldOrigin = InWorldOrigin;
	
	const float ChunkWorldSize = ChunkSize * CellSize;
	ChunkWorldPosition = WorldOrigin + FVector(
		ChunkCoord.X * ChunkWorldSize,
		ChunkCoord.Y * ChunkWorldSize,
		ChunkCoord.Z * ChunkWorldSize
	);
	
	const int32 TotalCells = ChunkSize * ChunkSize * ChunkSize;
	Cells.SetNum(TotalCells);
	NextCells.SetNum(TotalCells);
	
	for (int32 i = 0; i < TotalCells; ++i)
	{
		Cells[i] = FCAFluidCell();
		NextCells[i] = FCAFluidCell();
		// Initialize terrain height to a very low value to ensure proper terrain detection
		Cells[i].TerrainHeight = -FLT_MAX;
		NextCells[i].TerrainHeight = -FLT_MAX;
	}
	
	State = EChunkState::Unloaded;
}

void UFluidChunk::UpdateSimulation(float DeltaTime)
{
	if (State != EChunkState::Active)
		return;
	
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_UpdateSimulation);
	
	// CRITICAL: First remove any water from solid cells (terrain)
	// This prevents water from existing inside terrain
	static int32 GlobalCleanupCount = 0;
	int32 LocalCleanupCount = 0;
	for (int32 i = 0; i < Cells.Num(); ++i)
	{
		if (Cells[i].bIsSolid && Cells[i].FluidLevel > 0.0f)
		{
			LocalCleanupCount++;
			GlobalCleanupCount++;
			
			// Log first few occurrences to debug
			if (GlobalCleanupCount <= 10)
			{
				int32 LocalX = i % ChunkSize;
				int32 LocalY = (i / ChunkSize) % ChunkSize;
				int32 LocalZ = i / (ChunkSize * ChunkSize);
				FVector WorldPos = ChunkWorldPosition + FVector(LocalX * CellSize, LocalY * CellSize, LocalZ * CellSize);
				
				UE_LOG(LogTemp, Error, TEXT("WATER IN SOLID TERRAIN! Chunk %s, Cell[%d,%d,%d], WorldPos %s, FluidLevel %.2f, TerrainHeight %.1f"),
					*ChunkCoord.ToString(), LocalX, LocalY, LocalZ, *WorldPos.ToString(), 
					Cells[i].FluidLevel, Cells[i].TerrainHeight);
			}
			
			Cells[i].FluidLevel = 0.0f;
			Cells[i].bSettled = false;
			Cells[i].bSourceBlock = false;
			NextCells[i].FluidLevel = 0.0f;
			NextCells[i].bSettled = false;
			NextCells[i].bSourceBlock = false;
		}
	}
	
	if (LocalCleanupCount > 0 && GlobalCleanupCount <= 100)
	{
		UE_LOG(LogTemp, Warning, TEXT("Cleaned %d water cells from solid terrain in chunk %s"), 
			LocalCleanupCount, *ChunkCoord.ToString());
	}
	
	// Smart optimization: If chunk has been fully settled for a while, reduce update frequency
	if (bFullySettled && TotalFluidActivity < 0.001f)
	{
		static float SkipTimer = 0.0f;
		SkipTimer += DeltaTime;
		if (SkipTimer < 0.1f) // Only update every 100ms for settled chunks
		{
			return;
		}
		SkipTimer = 0.0f;
		DeltaTime *= 0.5f; // Use slower timestep for settled chunks
	}
	
	// Check if we should switch between sparse and dense representation
	UpdateSparseRepresentation();
	
	// Early exit for empty chunks
	if (bUseSparseRepresentation)
	{
		if (SparseCells.Num() == 0)
			return;
	}
	else
	{
		if (Cells.Num() == 0)
			return;
	}
	
	// Track total fluid change for mesh update decision
	float TotalFluidChange = 0.0f;
	
	if (bUseSparseRepresentation)
	{
		// Sparse path: only update cells with fluid
		for (auto& Pair : SparseCells)
		{
			Pair.Value.LastFluidLevel = Pair.Value.FluidLevel;
		}
		SparseNextCells = SparseCells;
	}
	else
	{
		// Dense path: update all cells
		for (int32 i = 0; i < Cells.Num(); ++i)
		{
			Cells[i].LastFluidLevel = Cells[i].FluidLevel;
		}
		NextCells = Cells;
	}
	
	// Simplified simulation without settling-related functions
	if (CurrentLOD == 0)
	{
		// Full quality simulation
		ApplyGravity(DeltaTime);
		ApplyFlowRules(DeltaTime);
		ApplyPressure(DeltaTime);
		ApplyEvaporation(DeltaTime);
	}
	else if (CurrentLOD == 1)
	{
		// Reduced quality simulation
		ApplyGravity(DeltaTime * 0.5f);
		ApplyFlowRules(DeltaTime * 0.5f);
		ApplyPressure(DeltaTime);
		ApplyEvaporation(DeltaTime * 0.5f);
	}
	else if (CurrentLOD == 2)
	{
		// Minimal simulation
		ApplyGravity(DeltaTime * 0.25f);
		ApplyEvaporation(DeltaTime * 0.25f);
	}
	
	ProcessBorderFlow(DeltaTime);
	
	// Don't swap buffers here - the ChunkManager will do it after border synchronization
	// Cells = NextCells;
	LastUpdateTime += DeltaTime;
	
	// Calculate total change and activity metrics
	int32 SettledCount = 0;
	int32 FluidCellCount = 0;
	TotalFluidActivity = 0.0f;
	
	if (bUseSparseRepresentation)
	{
		// Sparse mode: iterate through sparse cells
		for (const auto& CellPair : SparseNextCells)
		{
			const FCAFluidCell& NextCell = CellPair.Value;
			float LastLevel = 0.0f;
			if (const FCAFluidCell* LastCell = SparseCells.Find(CellPair.Key))
			{
				LastLevel = LastCell->LastFluidLevel;
			}
			
			const float Change = FMath::Abs(NextCell.FluidLevel - LastLevel);
			TotalFluidChange += Change;
			TotalFluidActivity += Change;
			
			if (NextCell.FluidLevel > MinFluidLevel && !NextCell.bIsSolid)
			{
				FluidCellCount++;
				if (NextCell.bSettled)
				{
					SettledCount++;
				}
			}
		}
	}
	else
	{
		// Dense mode: original implementation
		for (int32 i = 0; i < NextCells.Num(); ++i)
		{
			const float Change = FMath::Abs(NextCells[i].FluidLevel - NextCells[i].LastFluidLevel);
			TotalFluidChange += Change;
			TotalFluidActivity += Change;
			
			if (NextCells[i].FluidLevel > MinFluidLevel && !NextCells[i].bIsSolid)
			{
				FluidCellCount++;
				if (NextCells[i].bSettled)
				{
					SettledCount++;
				}
			}
		}
	}
	
	// Update activity tracking
	if (TotalFluidActivity < 0.0001f)
	{
		InactiveFrameCount++;
	}
	else
	{
		InactiveFrameCount = 0;
	}
	
	// Removed settling logic
	bFullySettled = false; // Never consider chunks as fully settled
	
	// Always update every frame
	UpdateFrequency = 1;
	
	// Old logic disabled:
	// if (TotalFluidActivity < 0.001f && bFullySettled)
	// {
	// 	UpdateFrequency = 4; // Very low activity, update every 4th frame
	// }
	// else if (TotalFluidActivity < 0.01f)
	// {
	// 	UpdateFrequency = 2; // Low activity, update every other frame
	// }
	// else
	// {
	// 	UpdateFrequency = 1; // Normal activity, update every frame
	// }
	
	// Only consider mesh update if there was significant change
	if (TotalFluidChange > 0.001f)
	{
		ConsiderMeshUpdate(TotalFluidChange / Cells.Num()); // Average change per cell
	}
	
	bDirty = true;
	LastActivityLevel = TotalFluidActivity;
}

void UFluidChunk::FinalizeSimulationStep()
{
	// Swap buffers after border synchronization
	if (bUseSparseRepresentation)
	{
		// Sparse mode: swap sparse buffers
		SparseCells = SparseNextCells;
		// Update active cell indices
		ActiveCellIndices.Empty();
		for (const auto& CellPair : SparseCells)
		{
			if (CellPair.Value.FluidLevel > MinFluidLevel || CellPair.Value.bIsSolid)
			{
				ActiveCellIndices.Add(CellPair.Key);
			}
		}
	}
	else
	{
		// Dense mode: swap dense buffers
		Cells = NextCells;
	}
}

void UFluidChunk::ActivateChunk()
{
	if (State == EChunkState::Active)
		return;
	
	State = EChunkState::Active;
	TimeSinceLastActive = 0.0f;
}

void UFluidChunk::DeactivateChunk()
{
	if (State != EChunkState::Active)
		return;
	
	State = EChunkState::Inactive;
}

void UFluidChunk::LoadChunk()
{
	if (State != EChunkState::Unloaded)
		return;
	
	State = EChunkState::Loading;
	
	const int32 TotalCells = ChunkSize * ChunkSize * ChunkSize;
	if (Cells.Num() != TotalCells)
	{
		Cells.SetNum(TotalCells);
		NextCells.SetNum(TotalCells);
		
		for (int32 i = 0; i < TotalCells; ++i)
		{
			Cells[i] = FCAFluidCell();
			NextCells[i] = FCAFluidCell();
			// Initialize terrain height to a very low value to ensure proper terrain detection
			Cells[i].TerrainHeight = -FLT_MAX;
			NextCells[i].TerrainHeight = -FLT_MAX;
		}
	}
	
	State = EChunkState::Inactive;
}

void UFluidChunk::UnloadChunk()
{
	if (State == EChunkState::Unloaded)
		return;
	
	State = EChunkState::Unloading;
	
	PendingBorderData = ExtractBorderData();
	
	// Note: Actual persistence happens in ChunkManager before calling this
	// This allows the manager to handle caching strategy
	
	Cells.Empty();
	NextCells.Empty();
	ActiveNeighbors.Empty();
	
	State = EChunkState::Unloaded;
}

void UFluidChunk::AddFluid(int32 LocalX, int32 LocalY, int32 LocalZ, float Amount)
{
	const int32 Idx = GetLocalCellIndex(LocalX, LocalY, LocalZ);
	if (Idx >= 0 && Idx < Cells.Num() && !Cells[Idx].bIsSolid)
	{
		const float OldLevel = Cells[Idx].FluidLevel;
		Cells[Idx].FluidLevel = FMath::Min(Cells[Idx].FluidLevel + Amount, MaxFluidLevel);
		const float Change = FMath::Abs(Cells[Idx].FluidLevel - OldLevel);
		bDirty = true;
		ConsiderMeshUpdate(Change); // Only mark dirty if change is significant
	}
}

void UFluidChunk::RemoveFluid(int32 LocalX, int32 LocalY, int32 LocalZ, float Amount)
{
	const int32 Idx = GetLocalCellIndex(LocalX, LocalY, LocalZ);
	if (Idx >= 0 && Idx < Cells.Num())
	{
		const float OldLevel = Cells[Idx].FluidLevel;
		Cells[Idx].FluidLevel = FMath::Max(Cells[Idx].FluidLevel - Amount, 0.0f);
		const float Change = FMath::Abs(OldLevel - Cells[Idx].FluidLevel);
		bDirty = true;
		ConsiderMeshUpdate(Change); // Only mark dirty if change is significant
	}
}

float UFluidChunk::GetFluidAt(int32 LocalX, int32 LocalY, int32 LocalZ) const
{
	const int32 Idx = GetLocalCellIndex(LocalX, LocalY, LocalZ);
	// Add explicit bounds checking to prevent array access violations
	if (Idx >= 0 && Idx < Cells.Num())
	{
		return Cells[Idx].FluidLevel;
	}
	return 0.0f;
}

void UFluidChunk::SetTerrainHeight(int32 LocalX, int32 LocalY, float Height)
{
	for (int32 z = 0; z < ChunkSize; ++z)
	{
		const int32 Idx = GetLocalCellIndex(LocalX, LocalY, z);
		if (Idx >= 0 && Idx < Cells.Num())
		{
			Cells[Idx].TerrainHeight = Height;
			
			// Calculate the center of the cell for accurate collision detection
			const float CellWorldZ = ChunkWorldPosition.Z + ((z + 0.5f) * CellSize);
			
			// Standard terrain collision: Mark as solid if cell center is below terrain
			Cells[Idx].bIsSolid = (CellWorldZ < Height);
			
			// Also update the next cells to ensure consistency
			NextCells[Idx].TerrainHeight = Height;
			NextCells[Idx].bIsSolid = Cells[Idx].bIsSolid;
		}
	}
	bDirty = true;
}

void UFluidChunk::SetCellSolid(int32 LocalX, int32 LocalY, int32 LocalZ, bool bSolid)
{
	const int32 Idx = GetLocalCellIndex(LocalX, LocalY, LocalZ);
	if (Idx >= 0 && Idx < Cells.Num())
	{
		bool bWasSolid = Cells[Idx].bIsSolid;
		Cells[Idx].bIsSolid = bSolid;
		NextCells[Idx].bIsSolid = bSolid;
		
		// If cell became solid, remove any fluid
		if (bSolid && !bWasSolid)
		{
			Cells[Idx].FluidLevel = 0.0f;
			Cells[Idx].bSettled = false;
			Cells[Idx].SettledCounter = 0;
			NextCells[Idx].FluidLevel = 0.0f;
			NextCells[Idx].bSettled = false;
			NextCells[Idx].SettledCounter = 0;
			
			// Mark as needing mesh update
			ConsiderMeshUpdate(1.0f);
		}
		// If cell became empty, wake it up for potential flow
		else if (!bSolid && bWasSolid)
		{
			Cells[Idx].bSettled = false;
			Cells[Idx].SettledCounter = 0;
			NextCells[Idx].bSettled = false;
			NextCells[Idx].SettledCounter = 0;
			
			// Mark as needing mesh update
			ConsiderMeshUpdate(1.0f);
		}
		
		bDirty = true;
		
		// If border cell changed, mark border dirty
		bool bIsBorderCell = (LocalX == 0 || LocalX == ChunkSize - 1 || 
							  LocalY == 0 || LocalY == ChunkSize - 1 || 
							  LocalZ == 0 || LocalZ == ChunkSize - 1);
		if (bIsBorderCell)
		{
			bBorderDirty = true;
		}
	}
}

bool UFluidChunk::IsCellSolid(int32 LocalX, int32 LocalY, int32 LocalZ) const
{
	const int32 Idx = GetLocalCellIndex(LocalX, LocalY, LocalZ);
	if (Idx >= 0 && Idx < Cells.Num())
	{
		return Cells[Idx].bIsSolid;
	}
	return true; // Out of bounds cells are considered solid
}

FVector UFluidChunk::GetWorldPositionFromLocal(int32 LocalX, int32 LocalY, int32 LocalZ) const
{
	return ChunkWorldPosition + FVector(LocalX * CellSize, LocalY * CellSize, LocalZ * CellSize);
}

bool UFluidChunk::GetLocalFromWorldPosition(const FVector& WorldPos, int32& OutX, int32& OutY, int32& OutZ) const
{
	const FVector LocalPos = WorldPos - ChunkWorldPosition;
	
	OutX = FMath::FloorToInt(LocalPos.X / CellSize);
	OutY = FMath::FloorToInt(LocalPos.Y / CellSize);
	OutZ = FMath::FloorToInt(LocalPos.Z / CellSize);
	
	return IsValidLocalCell(OutX, OutY, OutZ);
}

FChunkBorderData UFluidChunk::ExtractBorderData() const
{
	FChunkBorderData BorderData;
	
	const int32 YZSize = ChunkSize * ChunkSize;
	const int32 XZSize = ChunkSize * ChunkSize;
	const int32 XYSize = ChunkSize * ChunkSize;
	
	BorderData.PositiveX.SetNum(YZSize);
	BorderData.NegativeX.SetNum(YZSize);
	BorderData.PositiveY.SetNum(XZSize);
	BorderData.NegativeY.SetNum(XZSize);
	BorderData.PositiveZ.SetNum(XYSize);
	BorderData.NegativeZ.SetNum(XYSize);
	
	for (int32 y = 0; y < ChunkSize; ++y)
	{
		for (int32 z = 0; z < ChunkSize; ++z)
		{
			const int32 BorderIdx = y * ChunkSize + z;
			BorderData.NegativeX[BorderIdx] = Cells[GetLocalCellIndex(0, y, z)];
			BorderData.PositiveX[BorderIdx] = Cells[GetLocalCellIndex(ChunkSize - 1, y, z)];
		}
	}
	
	for (int32 x = 0; x < ChunkSize; ++x)
	{
		for (int32 z = 0; z < ChunkSize; ++z)
		{
			const int32 BorderIdx = x * ChunkSize + z;
			BorderData.NegativeY[BorderIdx] = Cells[GetLocalCellIndex(x, 0, z)];
			BorderData.PositiveY[BorderIdx] = Cells[GetLocalCellIndex(x, ChunkSize - 1, z)];
		}
	}
	
	for (int32 x = 0; x < ChunkSize; ++x)
	{
		for (int32 y = 0; y < ChunkSize; ++y)
		{
			const int32 BorderIdx = x * ChunkSize + y;
			BorderData.NegativeZ[BorderIdx] = Cells[GetLocalCellIndex(x, y, 0)];
			BorderData.PositiveZ[BorderIdx] = Cells[GetLocalCellIndex(x, y, ChunkSize - 1)];
		}
	}
	
	return BorderData;
}

void UFluidChunk::ApplyBorderData(const FChunkBorderData& BorderData)
{
	FScopeLock Lock(&BorderDataMutex);
	PendingBorderData = BorderData;
	bBorderDirty = true;
}

void UFluidChunk::UpdateBorderCell(int32 LocalX, int32 LocalY, int32 LocalZ, const FCAFluidCell& Cell)
{
	const int32 Idx = GetLocalCellIndex(LocalX, LocalY, LocalZ);
	if (Idx >= 0 && Idx < Cells.Num())
	{
		Cells[Idx] = Cell;
		bDirty = true;
	}
}

bool UFluidChunk::HasActiveFluid() const
{
	for (const FCAFluidCell& Cell : Cells)
	{
		if (Cell.FluidLevel > MinFluidLevel)
			return true;
	}
	return false;
}

float UFluidChunk::GetTotalFluidVolume() const
{
	float TotalVolume = 0.0f;
	for (const FCAFluidCell& Cell : Cells)
	{
		TotalVolume += Cell.FluidLevel;
	}
	return TotalVolume;
}

int32 UFluidChunk::GetActiveCellCount() const
{
	int32 Count = 0;
	for (const FCAFluidCell& Cell : Cells)
	{
		if (Cell.FluidLevel > MinFluidLevel)
			Count++;
	}
	return Count;
}

FBox UFluidChunk::GetWorldBounds() const
{
	const float ChunkWorldSize = ChunkSize * CellSize;
	return FBox(ChunkWorldPosition, ChunkWorldPosition + FVector(ChunkWorldSize));
}

bool UFluidChunk::IsInLODRange(const FVector& ViewerPosition, float LODDistance) const
{
	const FBox Bounds = GetWorldBounds();
	const float DistSq = Bounds.ComputeSquaredDistanceToPoint(ViewerPosition);
	return DistSq <= (LODDistance * LODDistance);
}

void UFluidChunk::SetLODLevel(int32 NewLODLevel)
{
	// Force LOD 0 for all chunks to ensure full speed simulation
	// CurrentLOD = 0; // Ignore requested LOD, always use full quality
	CurrentLOD = FMath::Clamp(NewLODLevel, 0, 2);
}

void UFluidChunk::ClearChunk()
{
	for (FCAFluidCell& Cell : Cells)
	{
		Cell.FluidLevel = 0.0f;
		Cell.bSettled = false;
		Cell.SettledCounter = 0;
		Cell.LastFluidLevel = 0.0f;
	}
	NextCells = Cells;
	bDirty = true;
}

bool UFluidChunk::IsValidLocalCell(int32 X, int32 Y, int32 Z) const
{
	return X >= 0 && X < ChunkSize &&
		   Y >= 0 && Y < ChunkSize &&
		   Z >= 0 && Z < ChunkSize;
}

int32 UFluidChunk::GetLocalCellIndex(int32 X, int32 Y, int32 Z) const
{
	if (!IsValidLocalCell(X, Y, Z))
		return -1;
	
	return X + Y * ChunkSize + Z * ChunkSize * ChunkSize;
}

FChunkPersistentData UFluidChunk::SerializeChunkData() const
{
	FChunkPersistentData PersistentData;
	PersistentData.ChunkCoord = ChunkCoord;
	PersistentData.CompressFrom(Cells);
	
	UE_LOG(LogTemp, Log, TEXT("Serialized chunk %s: %d non-empty cells, %.2f total fluid"),
	       *ChunkCoord.ToString(), PersistentData.NonEmptyCellCount, PersistentData.TotalFluidVolume);
	
	return PersistentData;
}

void UFluidChunk::DeserializeChunkData(const FChunkPersistentData& PersistentData)
{
	if (!PersistentData.ValidateChecksum())
	{
		UE_LOG(LogTemp, Warning, TEXT("Chunk %s failed checksum validation, loading empty"),
		       *ChunkCoord.ToString());
		return;
	}
	
	if (PersistentData.CompressedCells.Num() != Cells.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("Chunk %s size mismatch: expected %d cells, got %d"),
		       *ChunkCoord.ToString(), Cells.Num(), PersistentData.CompressedCells.Num());
		       
		if (PersistentData.CompressedCells.Num() == ChunkSize * ChunkSize * ChunkSize)
		{
			// Size matches expected chunk size, resize our arrays
			Cells.SetNum(PersistentData.CompressedCells.Num());
			NextCells.SetNum(PersistentData.CompressedCells.Num());
		}
		else
		{
			return; // Can't load mismatched data
		}
	}
	
	PersistentData.DecompressTo(Cells);
	NextCells = Cells;
	
	UE_LOG(LogTemp, Log, TEXT("Deserialized chunk %s: %d non-empty cells, %.2f total fluid"),
	       *ChunkCoord.ToString(), PersistentData.NonEmptyCellCount, PersistentData.TotalFluidVolume);
	       
	// Mark as needing mesh update if there's fluid
	if (PersistentData.bHasFluid)
	{
		bDirty = true;
		ConsiderMeshUpdate(1.0f);
	}
}

bool UFluidChunk::HasFluid() const
{
	for (const FCAFluidCell& Cell : Cells)
	{
		if (Cell.FluidLevel > MinFluidLevel && !Cell.bIsSolid)
		{
			return true;
		}
	}
	return false;
}

// GetTotalFluidVolume implementation is at line 496

void UFluidChunk::ApplyGravity(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_ApplyGravity);
	// Reduce gravity effect for better water accumulation
	const float GravityFlow = (Gravity / 1000.0f) * DeltaTime * 0.8f;
	
	// Check if we're in sparse mode
	if (bUseSparseRepresentation)
	{
		// Sparse mode: only process cells that exist
		TArray<TPair<int32, float>> GravityTransfers;
		
		for (const auto& CellPair : SparseCells)
		{
			const int32 CurrentIdx = CellPair.Key;
			const FCAFluidCell& CurrentCell = CellPair.Value;
			
			// Skip empty cells and static water source blocks
			if (CurrentCell.FluidLevel <= 0.0f || CurrentCell.bSourceBlock)
				continue;
			
			// Calculate coordinates from linear index
			const int32 x = CurrentIdx % ChunkSize;
			const int32 y = (CurrentIdx / ChunkSize) % ChunkSize;
			const int32 z = CurrentIdx / (ChunkSize * ChunkSize);
			
			if (z == 0)
				continue; // Already at bottom
			
			const int32 BelowIdx = GetLocalCellIndex(x, y, z - 1);
			
			// Get below cell if it exists
			FCAFluidCell BelowCell;
			if (const FCAFluidCell* BelowPtr = SparseCells.Find(BelowIdx))
			{
				BelowCell = *BelowPtr;
			}
			
			if (!BelowCell.bIsSolid)
			{
				const float SpaceBelow = MaxFluidLevel - BelowCell.FluidLevel;
				float GravityMultiplier = 1.0f;
				
				if (BelowCell.FluidLevel > MaxFluidLevel * 0.5f)
				{
					GravityMultiplier = 0.3f;
				}
				else if (BelowCell.FluidLevel > MaxFluidLevel * 0.2f)
				{
					GravityMultiplier = 0.6f;
				}
				
				const float AdjustedGravityFlow = GravityFlow * GravityMultiplier;
				const float FlowAmount = FMath::Min(CurrentCell.FluidLevel * AdjustedGravityFlow, SpaceBelow);
				
				if (FlowAmount > 0)
				{
					GravityTransfers.Add(TPair<int32, float>(CurrentIdx, -FlowAmount));
					GravityTransfers.Add(TPair<int32, float>(BelowIdx, FlowAmount));
				}
			}
		}
		
		// Apply gravity transfers to SparseNextCells
		for (const auto& Transfer : GravityTransfers)
		{
			if (FCAFluidCell* Cell = SparseNextCells.Find(Transfer.Key))
			{
				Cell->FluidLevel += Transfer.Value;
				if (Cell->FluidLevel <= MinFluidLevel && Transfer.Value < 0)
				{
					// Remove cell if it's now empty
					SparseNextCells.Remove(Transfer.Key);
					ActiveCellIndices.Remove(Transfer.Key);
				}
			}
			else if (Transfer.Value > 0)
			{
				// Add new cell if receiving fluid
				FCAFluidCell NewCell;
				NewCell.FluidLevel = Transfer.Value;
				SparseNextCells.Add(Transfer.Key, NewCell);
				ActiveCellIndices.Add(Transfer.Key);
			}
		}
		
		return; // Exit early for sparse mode
	}
	
	// Dense mode: original implementation
	for (int32 z = ChunkSize - 1; z >= 1; --z)
	{
		for (int32 y = 0; y < ChunkSize; ++y)
		{
			for (int32 x = 0; x < ChunkSize; ++x)
			{
				const int32 CurrentIdx = GetLocalCellIndex(x, y, z);
				const int32 BelowIdx = GetLocalCellIndex(x, y, z - 1);
				
				if (CurrentIdx == -1 || BelowIdx == -1)
					continue;
				
				FCAFluidCell& CurrentCell = Cells[CurrentIdx];
				FCAFluidCell& BelowCell = Cells[BelowIdx];
				
				// Allow even tiny amounts of fluid to fall with gravity
				// BUT: Skip source blocks (static water) - they should never flow
				if (CurrentCell.FluidLevel > 0.0f && !BelowCell.bIsSolid && !CurrentCell.bSourceBlock)
				{
					const float SpaceBelow = MaxFluidLevel - BelowCell.FluidLevel;
					float GravityMultiplier = 1.0f;
					
					if (BelowCell.FluidLevel > MaxFluidLevel * 0.5f)
					{
						GravityMultiplier = 0.3f;
					}
					else if (BelowCell.FluidLevel > MaxFluidLevel * 0.2f)
					{
						GravityMultiplier = 0.6f;
					}
					
					const float AdjustedGravityFlow = GravityFlow * GravityMultiplier;
					const float FlowAmount = FMath::Min(CurrentCell.FluidLevel * AdjustedGravityFlow, SpaceBelow);
					
					if (FlowAmount > 0)
					{
						NextCells[CurrentIdx].FluidLevel -= FlowAmount;
						NextCells[BelowIdx].FluidLevel += FlowAmount;
						// Removed velocity tracking - using simplified CA
					}
				}
			}
		}
	}
}

void UFluidChunk::ApplyFlowRules(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_ApplyFlowRules);
	
	// Smart optimization: Skip flow calculation if chunk has very low activity
	if (TotalFluidActivity < 0.01f)
	{
		return; // Skip flow for nearly settled chunks
	}
	
	// Reduce flow rate for better pooling and accumulation
	const float FlowAmount = FlowRate * DeltaTime * 0.7f;
	
	// Handle sparse mode
	if (bUseSparseRepresentation)
	{
		TArray<TPair<int32, float>> FlowTransfers;
		
		for (const auto& CellPair : SparseCells)
		{
			const int32 CurrentIdx = CellPair.Key;
			const FCAFluidCell& CurrentCell = CellPair.Value;
			
			// Skip empty, solid, or static water source blocks  
			if (CurrentCell.FluidLevel <= 0.0f || CurrentCell.bIsSolid || CurrentCell.bSourceBlock)
				continue;
			
			// Calculate coordinates from linear index
			const int32 x = CurrentIdx % ChunkSize;
			const int32 y = (CurrentIdx / ChunkSize) % ChunkSize;
			const int32 z = CurrentIdx / (ChunkSize * ChunkSize);
			
			// Check horizontal neighbors
			const TArray<FIntVector> Directions = {
				FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
				FIntVector(0, 1, 0), FIntVector(0, -1, 0)
			};
			
			for (const FIntVector& Dir : Directions)
			{
				const int32 NeighborX = x + Dir.X;
				const int32 NeighborY = y + Dir.Y;
				const int32 NeighborZ = z + Dir.Z;
				
				if (NeighborX < 0 || NeighborX >= ChunkSize || 
					NeighborY < 0 || NeighborY >= ChunkSize || 
					NeighborZ < 0 || NeighborZ >= ChunkSize)
					continue;
				
				const int32 NeighborIdx = GetLocalCellIndex(NeighborX, NeighborY, NeighborZ);
				
				FCAFluidCell NeighborCell;
				if (const FCAFluidCell* NeighborPtr = SparseCells.Find(NeighborIdx))
				{
					NeighborCell = *NeighborPtr;
				}
				
				if (!NeighborCell.bIsSolid)
				{
					const float FluidDiff = CurrentCell.FluidLevel - NeighborCell.FluidLevel;
					if (FluidDiff > 0)
					{
						const float MaxFlow = FluidDiff * FlowAmount * 0.25f; // Divide by 4 for horizontal flow
						const float SpaceAvailable = MaxFluidLevel - NeighborCell.FluidLevel;
						const float ActualFlow = FMath::Min(MaxFlow, SpaceAvailable);
						
						if (ActualFlow > 0)
						{
							FlowTransfers.Add(TPair<int32, float>(CurrentIdx, -ActualFlow));
							FlowTransfers.Add(TPair<int32, float>(NeighborIdx, ActualFlow));
						}
					}
				}
			}
		}
		
		// Apply flow transfers
		for (const auto& Transfer : FlowTransfers)
		{
			if (FCAFluidCell* Cell = SparseNextCells.Find(Transfer.Key))
			{
				Cell->FluidLevel += Transfer.Value;
				if (Cell->FluidLevel <= MinFluidLevel && Transfer.Value < 0)
				{
					SparseNextCells.Remove(Transfer.Key);
					ActiveCellIndices.Remove(Transfer.Key);
				}
			}
			else if (Transfer.Value > 0)
			{
				FCAFluidCell NewCell;
				NewCell.FluidLevel = Transfer.Value;
				SparseNextCells.Add(Transfer.Key, NewCell);
				ActiveCellIndices.Add(Transfer.Key);
			}
		}
		
		return; // Exit early for sparse mode
	}
	
	// Dense mode: optimized implementation
	int32 ProcessedCells = 0;
	const int32 MaxCellsToProcess = (TotalFluidActivity > 0.1f) ? ChunkSize * ChunkSize * ChunkSize : (ChunkSize * ChunkSize * ChunkSize / 4);
	
	for (int32 z = 0; z < ChunkSize && ProcessedCells < MaxCellsToProcess; ++z)
	{
		for (int32 y = 0; y < ChunkSize && ProcessedCells < MaxCellsToProcess; ++y)
		{
			for (int32 x = 0; x < ChunkSize && ProcessedCells < MaxCellsToProcess; ++x)
			{
				const int32 CurrentIdx = GetLocalCellIndex(x, y, z);
				if (CurrentIdx == -1)
					continue;
				
				FCAFluidCell& CurrentCell = Cells[CurrentIdx];
				
				// Don't skip cells with small amounts of fluid - let them accumulate or flow
				// Only skip completely dry, solid cells, or static water source blocks
				if (CurrentCell.FluidLevel <= 0.0f || CurrentCell.bIsSolid || CurrentCell.bSourceBlock)
					continue;
				
				ProcessedCells++;
				
				bool bHasSolidBelow = false;
				if (z > 0)
				{
					const int32 BelowIdx = GetLocalCellIndex(x, y, z - 1);
					if (BelowIdx != -1)
					{
						bHasSolidBelow = Cells[BelowIdx].bIsSolid || Cells[BelowIdx].FluidLevel >= MaxFluidLevel * 0.95f;
					}
				}
				else
				{
					bHasSolidBelow = true;
				}
				
				// Reduce horizontal flow when on solid ground to encourage pooling
				const float HorizontalFlowMultiplier = bHasSolidBelow ? 1.5f : 1.0f;
				const float AdjustedFlowAmount = FlowAmount * HorizontalFlowMultiplier;
				
				const int32 Neighbors[4][2] = {
					{x + 1, y},
					{x - 1, y},
					{x, y + 1},
					{x, y - 1}
				};
				
				float TotalOutflow = 0.0f;
				float OutflowToNeighbor[4] = {0.0f};
				
				for (int32 i = 0; i < 4; ++i)
				{
					const int32 nx = Neighbors[i][0];
					const int32 ny = Neighbors[i][1];
					
					if (IsValidLocalCell(nx, ny, z))
					{
						const int32 NeighborIdx = GetLocalCellIndex(nx, ny, z);
						FCAFluidCell& NeighborCell = Cells[NeighborIdx];
						
						if (!NeighborCell.bIsSolid)
						{
							// Use cell's world Z position for proper height comparison
							const float CurrentCellZ = ChunkWorldPosition.Z + ((z + 0.5f) * CellSize);
							const float CurrentFluidHeight = FMath::Max(CurrentCellZ, CurrentCell.TerrainHeight) + CurrentCell.FluidLevel;
							const float NeighborFluidHeight = FMath::Max(CurrentCellZ, NeighborCell.TerrainHeight) + NeighborCell.FluidLevel;
							const float HeightDiff = CurrentFluidHeight - NeighborFluidHeight;
							
							// Increase minimum height difference for flow to encourage pooling
							const float MinHeightDiffForFlow = bHasSolidBelow ? 0.02f : 0.01f;
							
							// Only flow if there's significant height difference or significant fluid volume
							if (HeightDiff > MinHeightDiffForFlow || (bHasSolidBelow && CurrentCell.FluidLevel > 0.2f && NeighborCell.FluidLevel < CurrentCell.FluidLevel * 0.8f))
							{
								// Reduce flow amount for better accumulation
								const float PossibleFlow = bHasSolidBelow ?
									FMath::Min(CurrentCell.FluidLevel * AdjustedFlowAmount, FMath::Max(HeightDiff * 0.5f, CurrentCell.FluidLevel * 0.15f)) :
									FMath::Min(CurrentCell.FluidLevel * AdjustedFlowAmount, HeightDiff * 0.3f);
								
								const float SpaceInNeighbor = MaxFluidLevel - NeighborCell.FluidLevel;
								OutflowToNeighbor[i] = FMath::Min(PossibleFlow, SpaceInNeighbor);
								TotalOutflow += OutflowToNeighbor[i];
							}
						}
					}
					else
					{
						bBorderDirty = true;
					}
				}
				
				if (TotalOutflow > CurrentCell.FluidLevel)
				{
					const float Scale = CurrentCell.FluidLevel / TotalOutflow;
					for (int32 i = 0; i < 4; ++i)
					{
						OutflowToNeighbor[i] *= Scale;
					}
					TotalOutflow = CurrentCell.FluidLevel;
				}
				
				for (int32 i = 0; i < 4; ++i)
				{
					if (OutflowToNeighbor[i] > 0)
					{
						const int32 nx = Neighbors[i][0];
						const int32 ny = Neighbors[i][1];
						
						NextCells[CurrentIdx].FluidLevel -= OutflowToNeighbor[i];
						
						if (IsValidLocalCell(nx, ny, z))
						{
							const int32 NeighborIdx = GetLocalCellIndex(nx, ny, z);
							NextCells[NeighborIdx].FluidLevel += OutflowToNeighbor[i];
						}
						
						// Removed velocity tracking - using simplified CA
					}
				}
			}
		}
	}
}

void UFluidChunk::ApplyPressure(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_ApplyPressure);
	
	// Handle sparse mode
	if (bUseSparseRepresentation)
	{
		TArray<TPair<int32, float>> PressureTransfers;
		
		for (const auto& CellPair : SparseCells)
		{
			const int32 CurrentIdx = CellPair.Key;
			const FCAFluidCell& CurrentCell = CellPair.Value;
			
			if (CurrentCell.FluidLevel <= CompressionFactor)
				continue;
			
			// Calculate coordinates from linear index
			const int32 x = CurrentIdx % ChunkSize;
			const int32 y = (CurrentIdx / ChunkSize) % ChunkSize;
			const int32 z = CurrentIdx / (ChunkSize * ChunkSize);
			
			if (z >= ChunkSize - 1)
				continue; // Can't push up from top layer
			
			const int32 AboveIdx = GetLocalCellIndex(x, y, z + 1);
			
			FCAFluidCell AboveCell;
			if (const FCAFluidCell* AbovePtr = SparseCells.Find(AboveIdx))
			{
				AboveCell = *AbovePtr;
			}
			
			if (!AboveCell.bIsSolid)
			{
				const float Compression = CurrentCell.FluidLevel - CompressionFactor;
				const float SpaceAbove = MaxFluidLevel - AboveCell.FluidLevel;
				const float PushUp = FMath::Min(Compression * 0.3f, SpaceAbove);
				
				if (PushUp > 0)
				{
					PressureTransfers.Add(TPair<int32, float>(CurrentIdx, -PushUp));
					PressureTransfers.Add(TPair<int32, float>(AboveIdx, PushUp));
				}
			}
		}
		
		// Apply pressure transfers
		for (const auto& Transfer : PressureTransfers)
		{
			if (FCAFluidCell* Cell = SparseNextCells.Find(Transfer.Key))
			{
				Cell->FluidLevel += Transfer.Value;
				if (Cell->FluidLevel <= MinFluidLevel && Transfer.Value < 0)
				{
					SparseNextCells.Remove(Transfer.Key);
					ActiveCellIndices.Remove(Transfer.Key);
				}
			}
			else if (Transfer.Value > 0)
			{
				FCAFluidCell NewCell;
				NewCell.FluidLevel = Transfer.Value;
				SparseNextCells.Add(Transfer.Key, NewCell);
				ActiveCellIndices.Add(Transfer.Key);
			}
		}
		
		return; // Exit early for sparse mode
	}
	
	// Dense mode: original implementation
	// Simple compression: when a cell is overfilled, push water upward
	for (int32 z = 0; z < ChunkSize - 1; ++z)
	{
		for (int32 y = 0; y < ChunkSize; ++y)
		{
			for (int32 x = 0; x < ChunkSize; ++x)
			{
				const int32 CurrentIdx = GetLocalCellIndex(x, y, z);
				const int32 AboveIdx = GetLocalCellIndex(x, y, z + 1);
				
				if (CurrentIdx == -1 || AboveIdx == -1)
					continue;
				
				FCAFluidCell& CurrentCell = NextCells[CurrentIdx];
				FCAFluidCell& AboveCell = NextCells[AboveIdx];
				
				// If current cell is overfilled and not solid
				if (CurrentCell.FluidLevel > MaxFluidLevel && !CurrentCell.bIsSolid)
				{
					// Push excess water upward if possible
					if (!AboveCell.bIsSolid)
					{
						const float Excess = CurrentCell.FluidLevel - MaxFluidLevel;
						const float SpaceAbove = MaxFluidLevel - AboveCell.FluidLevel;
						const float TransferAmount = FMath::Min(Excess, SpaceAbove);
						
						CurrentCell.FluidLevel -= TransferAmount;
						AboveCell.FluidLevel += TransferAmount;
						AboveCell.bSettled = false;
						AboveCell.SettledCounter = 0;
					}
				}
			}
		}
	}
}

void UFluidChunk::ApplyEvaporation(float DeltaTime)
{
	// Only apply evaporation if rate is greater than 0
	if (EvaporationRate <= 0.0f)
		return;
	
	// Apply evaporation to all cells with fluid
	const float EvaporationAmount = EvaporationRate * DeltaTime;
	
	// Handle sparse mode
	if (bUseSparseRepresentation)
	{
		TArray<int32> CellsToRemove;
		
		for (auto& CellPair : SparseNextCells)
		{
			FCAFluidCell& Cell = CellPair.Value;
			if (Cell.FluidLevel > 0.0f && !Cell.bIsSolid)
			{
				// Evaporate fluid, but don't go below 0
				Cell.FluidLevel = FMath::Max(0.0f, Cell.FluidLevel - EvaporationAmount);
				
				// Mark for removal if fluid is now below threshold
				if (Cell.FluidLevel <= MinFluidLevel)
				{
					CellsToRemove.Add(CellPair.Key);
				}
			}
		}
		
		// Remove cells that have evaporated below minimum threshold
		for (int32 CellIndex : CellsToRemove)
		{
			SparseNextCells.Remove(CellIndex);
			ActiveCellIndices.Remove(CellIndex);
		}
		
		return; // Exit early for sparse mode
	}
	
	// Dense mode: original implementation
	for (int32 i = 0; i < NextCells.Num(); ++i)
	{
		if (NextCells[i].FluidLevel > 0.0f && !NextCells[i].bIsSolid)
		{
			// Evaporate fluid, but don't go below 0
			NextCells[i].FluidLevel = FMath::Max(0.0f, NextCells[i].FluidLevel - EvaporationAmount);
			
			// If fluid level drops below MinFluidLevel after evaporation, remove it completely
			// This prevents tiny amounts from lingering
			if (NextCells[i].FluidLevel < MinFluidLevel)
			{
				NextCells[i].FluidLevel = 0.0f;
			}
		}
	}
}

// UpdateVelocities removed - was part of settling system

void UFluidChunk::ProcessBorderFlow(float DeltaTime)
{
	if (!bBorderDirty)
		return;
	
	FScopeLock Lock(&BorderDataMutex);
	
	bBorderDirty = false;
}

void UFluidChunk::StoreMeshData(const TArray<FVector>& Vertices, const TArray<int32>& Triangles, 
								const TArray<FVector>& Normals, const TArray<FVector2D>& UVs, 
								const TArray<FColor>& VertexColors, float IsoLevel, int32 LODLevel)
{
	// Store mesh data for persistence
	StoredMeshData.Vertices = Vertices;
	StoredMeshData.Triangles = Triangles;
	StoredMeshData.Normals = Normals;
	StoredMeshData.UVs = UVs;
	StoredMeshData.VertexColors = VertexColors;
	StoredMeshData.GeneratedIsoLevel = IsoLevel;
	StoredMeshData.GeneratedLOD = LODLevel;
	StoredMeshData.GenerationTimestamp = FPlatformTime::Seconds();
	StoredMeshData.FluidStateHash = CalculateFluidStateHash();
	StoredMeshData.bIsValid = true;
	
	// Mark mesh data as clean since we just generated it
	bMeshDataDirty = false;
	AccumulatedMeshChange = 0.0f; // Reset accumulated changes
	LastMeshUpdateTime = FPlatformTime::Seconds();
}

bool UFluidChunk::HasValidMeshData(int32 DesiredLOD, float DesiredIsoLevel) const
{
	// Don't regenerate if chunk is mostly settled and changes are minimal
	if (!ShouldRegenerateMesh())
	{
		// Use cached mesh even if technically "dirty" if changes are too small
		return StoredMeshData.IsValidForLOD(DesiredLOD, DesiredIsoLevel);
	}
	
	if (bMeshDataDirty)
		return false;
		
	// Only check hash if we really need to regenerate
	// This is expensive so we avoid it when possible
	uint32 CurrentFluidHash = CalculateFluidStateHash();
	if (CurrentFluidHash != StoredMeshData.FluidStateHash)
		return false;
		
	return StoredMeshData.IsValidForLOD(DesiredLOD, DesiredIsoLevel);
}

void UFluidChunk::ClearMeshData()
{
	StoredMeshData.Clear();
	bMeshDataDirty = true;
}

void UFluidChunk::MarkMeshDataDirty()
{
	bMeshDataDirty = true;
}

uint32 UFluidChunk::CalculateFluidStateHash() const
{
	// Create a hash of the current fluid state for dirty checking
	uint32 Hash = 0;
	
	// Hash fluid level data (sample every few cells for performance)
	const int32 SampleStep = FMath::Max(1, ChunkSize / 8); // Sample 8x8x8 grid
	
	for (int32 X = 0; X < ChunkSize; X += SampleStep)
	{
		for (int32 Y = 0; Y < ChunkSize; Y += SampleStep)
		{
			for (int32 Z = 0; Z < ChunkSize; Z += SampleStep)
			{
				const int32 Index = GetLocalCellIndex(X, Y, Z);
				if (Index >= 0 && Index < Cells.Num())
				{
					// Hash fluid level as fixed point to avoid floating point precision issues
					const uint32 FluidLevel = (uint32)(Cells[Index].FluidLevel * 1000.0f);
					Hash = HashCombine(Hash, FluidLevel);
				}
			}
		}
	}
	
	return Hash;
}

// Removed settling-related functions: CalculateHydrostaticPressure, DetectAndMarkPools, ApplyUpwardPressureFlow

void UFluidChunk::ApplyUpwardPressureFlow(float DeltaTime)
{
	// Simple compression: when a cell is overfilled, push water upward
	for (int32 z = 0; z < ChunkSize - 1; ++z)
	{
		for (int32 y = 0; y < ChunkSize; ++y)
		{
			for (int32 x = 0; x < ChunkSize; ++x)
			{
				const int32 CurrentIdx = GetLocalCellIndex(x, y, z);
				const int32 AboveIdx = GetLocalCellIndex(x, y, z + 1);
				
				if (CurrentIdx == -1 || AboveIdx == -1)
					continue;
				
				FCAFluidCell& CurrentCell = NextCells[CurrentIdx];
				FCAFluidCell& AboveCell = NextCells[AboveIdx];
				
				// If current cell is overfilled and not solid
				if (CurrentCell.FluidLevel > MaxFluidLevel && !CurrentCell.bIsSolid)
				{
					// Push excess water upward if possible
					if (!AboveCell.bIsSolid)
					{
						const float Excess = CurrentCell.FluidLevel - MaxFluidLevel;
						const float SpaceAbove = MaxFluidLevel - AboveCell.FluidLevel;
						const float TransferAmount = FMath::Min(Excess, SpaceAbove);
						
						if (TransferAmount > 0)
						{
							CurrentCell.FluidLevel -= TransferAmount;
							AboveCell.FluidLevel += TransferAmount;
							AboveCell.bSettled = false;
							AboveCell.SettledCounter = 0;
						}
					}
				}
			}
		}
	}
}

// Removed ApplyDiagonalFlow and ApplyPressureEqualization - part of settling system

void UFluidChunk::ApplyPressureEqualization(float DeltaTime)
{
	// Disabled - this function was causing slow updates because:
	// 1. It only processes settled cells (which take 20-30 frames to become settled)
	// 2. It uses a very slow lerp rate (0.5f * DeltaTime)
	// 3. This created sluggish fluid movement
	return;
	
	// Original implementation commented out:
	// // Equalize water levels in settled regions for stable pools
	// for (int32 z = 0; z < ChunkSize; ++z)
	// {
	// 	for (int32 y = 0; y < ChunkSize; ++y)
	// 	{
	// 		for (int32 x = 0; x < ChunkSize; ++x)
	// 		{
	// 			const int32 CurrentIdx = GetLocalCellIndex(x, y, z);
	// 			if (CurrentIdx == -1)
	// 				continue;
	// 			
	// 			FCAFluidCell& CurrentCell = NextCells[CurrentIdx];
	// 			
	// 			// Skip if no water or not settled
	// 			if (CurrentCell.FluidLevel <= MinFluidLevel || !CurrentCell.bSettled || CurrentCell.bIsSolid)
	// 				continue;
	// 			
	// 			// Find connected neighbors at same level
	// 			const int32 Neighbors[4][2] = {
	// 				{x + 1, y},
	// 				{x - 1, y},
	// 				{x, y + 1},
	// 				{x, y - 1}
	// 			};
	// 			
	// 			float TotalLevel = CurrentCell.FluidLevel;
	// 			int32 ConnectedCount = 1;
	// 			TArray<int32> ConnectedCells;
	// 			
	// 			for (int32 i = 0; i < 4; ++i)
	// 			{
	// 				const int32 nx = Neighbors[i][0];
	// 				const int32 ny = Neighbors[i][1];
	// 				
	// 				if (IsValidLocalCell(nx, ny, z))
	// 				{
	// 					const int32 NeighborIdx = GetLocalCellIndex(nx, ny, z);
	// 					FCAFluidCell& NeighborCell = NextCells[NeighborIdx];
	// 					
	// 					if (!NeighborCell.bIsSolid && NeighborCell.bSettled && NeighborCell.FluidLevel > MinFluidLevel)
	// 					{
	// 						ConnectedCells.Add(NeighborIdx);
	// 						TotalLevel += NeighborCell.FluidLevel;
	// 						ConnectedCount++;
	// 					}
	// 				}
	// 			}
	// 			
	// 			// Set all connected cells to average level
	// 			if (ConnectedCount > 1)
	// 			{
	// 				const float AverageLevel = TotalLevel / ConnectedCount;
	// 				const float AdjustmentRate = 0.5f * DeltaTime;
	// 				
	// 				CurrentCell.FluidLevel = FMath::Lerp(CurrentCell.FluidLevel, AverageLevel, AdjustmentRate);
	// 				
	// 				for (int32 ConnectedIdx : ConnectedCells)
	// 				{
	// 					NextCells[ConnectedIdx].FluidLevel = FMath::Lerp(NextCells[ConnectedIdx].FluidLevel, AverageLevel, AdjustmentRate);
	// 				}
	// 			}
	// 		}
	// 	}
	// }
}

void UFluidChunk::ConsiderMeshUpdate(float FluidChange)
{
	// Accumulate changes over time
	AccumulatedMeshChange += FluidChange;
	
	// Check if we've exceeded the threshold for mesh regeneration
	if (AccumulatedMeshChange > MeshChangeThreshold)
	{
		bMeshDataDirty = true;
		// Reset accumulation when threshold is reached
		AccumulatedMeshChange = 0.0f;
	}
	
	// For any change at chunk boundaries, mark dirty immediately for seamless rendering
	if (FluidChange > 0.001f && bBorderDirty)
	{
		bMeshDataDirty = true;
	}
	
	// Also mark dirty if it's been too long since last update (prevent stale meshes)
	const float CurrentTime = FPlatformTime::Seconds();
	if (CurrentTime - LastMeshUpdateTime > 2.0f) // Force update every 2 seconds (reduced from 5)
	{
		bMeshDataDirty = true;
		AccumulatedMeshChange = 0.0f;
	}
}

bool UFluidChunk::ShouldRegenerateMesh() const
{
	// Simplified: always regenerate if marked dirty
	// Removed all settling-based checks that were preventing mesh updates
	return bMeshDataDirty;
}

int32 UFluidChunk::GetSettledCellCount() const
{
	// Removed settling system - always return 0
	return 0;
}

// ============================================================================
// Sparse Grid Implementation
// ============================================================================

void UFluidChunk::ConvertToSparse()
{
	if (bUseSparseRepresentation)
		return; // Already sparse
	
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_ConvertToSparse);
	
	// Clear existing sparse data
	SparseCells.Empty();
	ActiveCellIndices.Empty();
	
	// Convert dense to sparse
	int32 NonEmptyCells = 0;
	const int32 TotalCells = ChunkSize * ChunkSize * ChunkSize;
	
	for (int32 i = 0; i < TotalCells; ++i)
	{
		const FCAFluidCell& Cell = Cells[i];
		
		// Only store cells with fluid or solid terrain
		if (Cell.FluidLevel > MinFluidLevel || Cell.bIsSolid)
		{
			SparseCells.Add(i, Cell);
			ActiveCellIndices.Add(i);
			NonEmptyCells++;
		}
	}
	
	// Calculate occupancy
	SparseGridOccupancy = (float)NonEmptyCells / (float)TotalCells;
	
	// Switch to sparse representation
	bUseSparseRepresentation = true;
	
	// Keep dense arrays sized but clear them (some systems may still expect them to exist)
	// Don't call Empty() to avoid crashes in other systems that may access these arrays
	for (int32 i = 0; i < TotalCells; ++i)
	{
		Cells[i] = FCAFluidCell();
		NextCells[i] = FCAFluidCell();
	}
	
	UE_LOG(LogTemp, Log, TEXT("Chunk %s converted to sparse: %d/%d cells (%.1f%% occupancy)"),
		*ChunkCoord.ToString(), NonEmptyCells, TotalCells, SparseGridOccupancy * 100.0f);
}

void UFluidChunk::ConvertToDense()
{
	if (!bUseSparseRepresentation)
		return; // Already dense
	
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_ConvertToDense);
	
	// Allocate dense arrays
	const int32 TotalCells = ChunkSize * ChunkSize * ChunkSize;
	Cells.SetNum(TotalCells);
	NextCells.SetNum(TotalCells);
	
	// Initialize all cells to empty
	for (int32 i = 0; i < TotalCells; ++i)
	{
		Cells[i] = FCAFluidCell();
		NextCells[i] = FCAFluidCell();
	}
	
	// Copy sparse cells back to dense array
	for (const auto& Pair : SparseCells)
	{
		Cells[Pair.Key] = Pair.Value;
		NextCells[Pair.Key] = Pair.Value;
	}
	
	// Switch to dense representation
	bUseSparseRepresentation = false;
	SparseGridOccupancy = 1.0f;
	
	// Clear sparse data
	SparseCells.Empty();
	SparseNextCells.Empty();
	ActiveCellIndices.Empty();
	
	UE_LOG(LogTemp, Log, TEXT("Chunk %s converted to dense"), *ChunkCoord.ToString());
}

bool UFluidChunk::ShouldUseSparse() const
{
	// Use sparse if occupancy is below threshold (30% by default)
	const float SparseThreshold = 0.3f;
	return SparseGridOccupancy < SparseThreshold;
}

void UFluidChunk::UpdateSparseRepresentation()
{
	// Check if we should switch between sparse and dense
	const float CurrentOccupancy = CalculateOccupancy();
	SparseGridOccupancy = CurrentOccupancy;
	
	const float SparseThreshold = 0.3f;
	const float DenseThreshold = 0.5f; // Hysteresis to prevent thrashing
	
	if (!bUseSparseRepresentation && CurrentOccupancy < SparseThreshold)
	{
		// Convert to sparse
		ConvertToSparse();
	}
	else if (bUseSparseRepresentation && CurrentOccupancy > DenseThreshold)
	{
		// Convert back to dense
		ConvertToDense();
	}
}

float UFluidChunk::CalculateOccupancy() const
{
	int32 NonEmptyCells = 0;
	const int32 TotalCells = ChunkSize * ChunkSize * ChunkSize;
	
	if (bUseSparseRepresentation)
	{
		NonEmptyCells = SparseCells.Num();
	}
	else
	{
		for (const FCAFluidCell& Cell : Cells)
		{
			if (Cell.FluidLevel > MinFluidLevel || Cell.bIsSolid)
			{
				NonEmptyCells++;
			}
		}
	}
	
	return (float)NonEmptyCells / (float)TotalCells;
}

bool UFluidChunk::GetSparseCell(int32 X, int32 Y, int32 Z, FCAFluidCell& OutCell) const
{
	if (!IsValidLocalCell(X, Y, Z))
		return false;
	
	const int32 Index = GetLocalCellIndex(X, Y, Z);
	
	if (bUseSparseRepresentation)
	{
		if (const FCAFluidCell* Cell = SparseCells.Find(Index))
		{
			OutCell = *Cell;
			return true;
		}
		else
		{
			OutCell = FCAFluidCell(); // Empty cell
			return true;
		}
	}
	else
	{
		OutCell = Cells[Index];
		return true;
	}
}

void UFluidChunk::SetSparseCell(int32 X, int32 Y, int32 Z, const FCAFluidCell& Cell)
{
	if (!IsValidLocalCell(X, Y, Z))
		return;
	
	const int32 Index = GetLocalCellIndex(X, Y, Z);
	
	if (bUseSparseRepresentation)
	{
		if (Cell.FluidLevel > MinFluidLevel || Cell.bIsSolid)
		{
			// Add or update cell
			SparseCells.Add(Index, Cell);
			ActiveCellIndices.Add(Index);
		}
		else
		{
			// Remove empty cell
			SparseCells.Remove(Index);
			ActiveCellIndices.Remove(Index);
		}
	}
	else
	{
		Cells[Index] = Cell;
	}
}

bool UFluidChunk::GetSparseNeighbor(int32 DX, int32 DY, int32 DZ, int32 FromX, int32 FromY, int32 FromZ, FCAFluidCell& OutCell) const
{
	const int32 NeighborX = FromX + DX;
	const int32 NeighborY = FromY + DY;
	const int32 NeighborZ = FromZ + DZ;
	
	return GetSparseCell(NeighborX, NeighborY, NeighborZ, OutCell);
}

int32 UFluidChunk::GetSparseMemoryUsage() const
{
	if (bUseSparseRepresentation)
	{
		// Each sparse cell entry: key (4 bytes) + value (sizeof(FCAFluidCell))
		const int32 PerCellMemory = sizeof(int32) + sizeof(FCAFluidCell);
		const int32 DataMemory = SparseCells.Num() * PerCellMemory;
		
		// Overhead for hash table (roughly 2x the data)
		const int32 HashOverhead = SparseCells.Num() * sizeof(int32) * 2;
		
		return DataMemory + HashOverhead;
	}
	else
	{
		return GetDenseMemoryUsage();
	}
}

int32 UFluidChunk::GetDenseMemoryUsage() const
{
	const int32 TotalCells = ChunkSize * ChunkSize * ChunkSize;
	return TotalCells * sizeof(FCAFluidCell) * 2; // Cells + NextCells
}

float UFluidChunk::GetCompressionRatio() const
{
	if (!bUseSparseRepresentation)
		return 1.0f;
	
	const float SparseMemory = (float)GetSparseMemoryUsage();
	const float DenseMemory = (float)GetDenseMemoryUsage();
	
	return DenseMemory / FMath::Max(SparseMemory, 1.0f);
}