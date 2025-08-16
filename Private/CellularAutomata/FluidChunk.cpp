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
	
	// Early exit if chunk is fully settled and has no activity
	if (bFullySettled && InactiveFrameCount > 60) // Skip after 1 second of inactivity
	{
		InactiveFrameCount++;
		return;
	}
	
	// Check if we should skip this frame based on hierarchical update frequency
	static int32 FrameCounter = 0;
	FrameCounter++;
	if (UpdateFrequency > 1 && (FrameCounter % UpdateFrequency) != 0)
	{
		return;
	}
	
	// Track total fluid change for mesh update decision
	float TotalFluidChange = 0.0f;
	for (int32 i = 0; i < Cells.Num(); ++i)
	{
		Cells[i].LastFluidLevel = Cells[i].FluidLevel;
	}
	
	NextCells = Cells;
	
	if (CurrentLOD == 0)
	{
		// Full quality simulation with enhanced features
		CalculateHydrostaticPressure();
		DetectAndMarkPools(DeltaTime);
		
		ApplyGravity(DeltaTime);
		ApplyUpwardPressureFlow(DeltaTime);
		ApplyFlowRules(DeltaTime);
		ApplyDiagonalFlow(DeltaTime);
		ApplyPressureEqualization(DeltaTime);
		ApplyPressure(DeltaTime);
		UpdateVelocities(DeltaTime);
	}
	else if (CurrentLOD == 1)
	{
		// Reduced quality simulation
		CalculateHydrostaticPressure();
		ApplyGravity(DeltaTime * 0.5f);
		ApplyFlowRules(DeltaTime * 0.5f);
		ApplyPressure(DeltaTime);
	}
	else if (CurrentLOD == 2)
	{
		// Minimal simulation
		ApplyGravity(DeltaTime * 0.25f);
	}
	
	ProcessBorderFlow(DeltaTime);
	
	// Don't swap buffers here - the ChunkManager will do it after border synchronization
	// Cells = NextCells;
	LastUpdateTime += DeltaTime;
	
	// Calculate total change and activity metrics
	int32 SettledCount = 0;
	int32 FluidCellCount = 0;
	TotalFluidActivity = 0.0f;
	
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
	
	// Update activity tracking
	if (TotalFluidActivity < 0.0001f)
	{
		InactiveFrameCount++;
	}
	else
	{
		InactiveFrameCount = 0;
	}
	
	// Check if chunk is fully settled
	bFullySettled = (FluidCellCount > 0) && (SettledCount == FluidCellCount);
	
	// Adjust update frequency based on activity level
	if (TotalFluidActivity < 0.001f && bFullySettled)
	{
		UpdateFrequency = 4; // Very low activity, update every 4th frame
	}
	else if (TotalFluidActivity < 0.01f)
	{
		UpdateFrequency = 2; // Low activity, update every other frame
	}
	else
	{
		UpdateFrequency = 1; // Normal activity, update every frame
	}
	
	// Only consider mesh update if there was significant change
	if (TotalFluidChange > 0.001f)
	{
		ConsiderMeshUpdate(TotalFluidChange / Cells.Num()); // Average change per cell
	}
	
	bDirty = true;
	LastActivityLevel = TotalFluidActivity;
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
	if (Idx != -1 && !Cells[Idx].bIsSolid)
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
	if (Idx != -1)
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
	return (Idx != -1) ? Cells[Idx].FluidLevel : 0.0f;
}

void UFluidChunk::SetTerrainHeight(int32 LocalX, int32 LocalY, float Height)
{
	for (int32 z = 0; z < ChunkSize; ++z)
	{
		const int32 Idx = GetLocalCellIndex(LocalX, LocalY, z);
		if (Idx != -1)
		{
			Cells[Idx].TerrainHeight = Height;
			
			// Calculate the center of the cell for accurate collision detection
			const float CellWorldZ = ChunkWorldPosition.Z + ((z + 0.5f) * CellSize);
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
	if (Idx != -1)
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
	if (Idx != -1)
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
	if (Idx != -1)
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
	const float GravityFlow = (Gravity / 1000.0f) * DeltaTime;
	
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
				
				if (CurrentCell.FluidLevel > MinFluidLevel && !BelowCell.bIsSolid)
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
	const float FlowAmount = FlowRate * DeltaTime;
	
	for (int32 z = 0; z < ChunkSize; ++z)
	{
		for (int32 y = 0; y < ChunkSize; ++y)
		{
			for (int32 x = 0; x < ChunkSize; ++x)
			{
				const int32 CurrentIdx = GetLocalCellIndex(x, y, z);
				if (CurrentIdx == -1)
					continue;
				
				FCAFluidCell& CurrentCell = Cells[CurrentIdx];
				
				if (CurrentCell.FluidLevel <= MinFluidLevel || CurrentCell.bIsSolid)
					continue;
				
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
				
				const float HorizontalFlowMultiplier = bHasSolidBelow ? 2.5f : 1.0f;
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
							
							const float MinHeightDiffForFlow = bHasSolidBelow ? 0.01f : 0.0f;
							
							if (HeightDiff > MinHeightDiffForFlow || (bHasSolidBelow && CurrentCell.FluidLevel > 0.1f && NeighborCell.FluidLevel < CurrentCell.FluidLevel))
							{
								const float PossibleFlow = bHasSolidBelow ?
									FMath::Min(CurrentCell.FluidLevel * AdjustedFlowAmount, FMath::Max(HeightDiff * 0.8f, CurrentCell.FluidLevel * 0.25f)) :
									FMath::Min(CurrentCell.FluidLevel * AdjustedFlowAmount, HeightDiff * 0.5f);
								
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

void UFluidChunk::UpdateVelocities(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_UpdateVelocities);
	
	// Update settled states based on whether fluid level changed
	for (int32 z = 0; z < ChunkSize; ++z)
	{
		for (int32 y = 0; y < ChunkSize; ++y)
		{
			for (int32 x = 0; x < ChunkSize; ++x)
			{
				const int32 i = GetLocalCellIndex(x, y, z);
				if (i == -1)
					continue;
					
				FCAFluidCell& Cell = NextCells[i];
				
				if (Cell.FluidLevel <= MinFluidLevel || Cell.bIsSolid)
				{
					Cell.bSettled = false;
					Cell.SettledCounter = 0;
					continue;
				}
				
				// Check if fluid level is stable
				const float Change = FMath::Abs(Cell.FluidLevel - Cell.LastFluidLevel);
				
				// Border cells need more careful settling detection
				bool bIsBorderCell = (x == 0 || x == ChunkSize - 1 || 
									  y == 0 || y == ChunkSize - 1 || 
									  z == 0 || z == ChunkSize - 1);
				
				if (Change < MinFluidLevel)
				{
					Cell.SettledCounter++;
					
					// Border cells need more time to settle
					int32 RequiredSettleCount = bIsBorderCell ? 10 : 5;
					
					if (Cell.SettledCounter >= RequiredSettleCount)
					{
						Cell.bSettled = true;
					}
				}
				else
				{
					Cell.bSettled = false;
					Cell.SettledCounter = 0;
					
					// If a border cell becomes unsettled, mark chunk border as dirty
					if (bIsBorderCell)
					{
						bBorderDirty = true;
					}
				}
			}
		}
	}
}

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

void UFluidChunk::CalculateHydrostaticPressure()
{
	// Simplified - no pressure calculation needed for basic CA
	// Method kept for interface compatibility
}

void UFluidChunk::DetectAndMarkPools(float DeltaTime)
{
	// Store previous state for settling detection
	for (FCAFluidCell& Cell : Cells)
	{
		Cell.LastFluidLevel = Cell.FluidLevel;
	}
}

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

void UFluidChunk::ApplyDiagonalFlow(float DeltaTime)
{
	// Simplified - diagonal flow handled in main horizontal flow for simplicity
	// Method kept for interface compatibility
}

void UFluidChunk::ApplyPressureEqualization(float DeltaTime)
{
	// Equalize water levels in settled regions for stable pools
	for (int32 z = 0; z < ChunkSize; ++z)
	{
		for (int32 y = 0; y < ChunkSize; ++y)
		{
			for (int32 x = 0; x < ChunkSize; ++x)
			{
				const int32 CurrentIdx = GetLocalCellIndex(x, y, z);
				if (CurrentIdx == -1)
					continue;
				
				FCAFluidCell& CurrentCell = NextCells[CurrentIdx];
				
				// Skip if no water or not settled
				if (CurrentCell.FluidLevel <= MinFluidLevel || !CurrentCell.bSettled || CurrentCell.bIsSolid)
					continue;
				
				// Find connected neighbors at same level
				const int32 Neighbors[4][2] = {
					{x + 1, y},
					{x - 1, y},
					{x, y + 1},
					{x, y - 1}
				};
				
				float TotalLevel = CurrentCell.FluidLevel;
				int32 ConnectedCount = 1;
				TArray<int32> ConnectedCells;
				
				for (int32 i = 0; i < 4; ++i)
				{
					const int32 nx = Neighbors[i][0];
					const int32 ny = Neighbors[i][1];
					
					if (IsValidLocalCell(nx, ny, z))
					{
						const int32 NeighborIdx = GetLocalCellIndex(nx, ny, z);
						FCAFluidCell& NeighborCell = NextCells[NeighborIdx];
						
						if (!NeighborCell.bIsSolid && NeighborCell.bSettled && NeighborCell.FluidLevel > MinFluidLevel)
						{
							ConnectedCells.Add(NeighborIdx);
							TotalLevel += NeighborCell.FluidLevel;
							ConnectedCount++;
						}
					}
				}
				
				// Set all connected cells to average level
				if (ConnectedCount > 1)
				{
					const float AverageLevel = TotalLevel / ConnectedCount;
					const float AdjustmentRate = 0.5f * DeltaTime;
					
					CurrentCell.FluidLevel = FMath::Lerp(CurrentCell.FluidLevel, AverageLevel, AdjustmentRate);
					
					for (int32 ConnectedIdx : ConnectedCells)
					{
						NextCells[ConnectedIdx].FluidLevel = FMath::Lerp(NextCells[ConnectedIdx].FluidLevel, AverageLevel, AdjustmentRate);
					}
				}
			}
		}
	}
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
	// Always regenerate if border is dirty (for seamless cross-chunk rendering)
	if (bMeshDataDirty && bBorderDirty)
	{
		return true;
	}
	
	// Don't regenerate if chunk is mostly settled
	const float SettledRatio = GetSettledCellCount() / (float)FMath::Max(1, GetActiveCellCount());
	if (SettledRatio > 0.9f && AccumulatedMeshChange < MeshChangeThreshold * 0.5f)
	{
		return false; // Chunk is mostly settled, don't regenerate unless changes are significant
	}
	
	// Regenerate if marked dirty (removed the accumulated change requirement as it was too restrictive)
	return bMeshDataDirty;
}

int32 UFluidChunk::GetSettledCellCount() const
{
	int32 Count = 0;
	for (const FCAFluidCell& Cell : Cells)
	{
		if (Cell.bSettled && Cell.FluidLevel > MinFluidLevel)
			Count++;
	}
	return Count;
}