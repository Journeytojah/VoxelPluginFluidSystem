#include "CellularAutomata/FluidChunk.h"
#include "VoxelFluidStats.h"
#include "Math/UnrealMathUtility.h"

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
	
	NextCells = Cells;
	
	if (CurrentLOD == 0)
	{
		ApplyGravity(DeltaTime);
		ApplyFlowRules(DeltaTime);
		ApplyPressure(DeltaTime);
		UpdateVelocities(DeltaTime);
	}
	else if (CurrentLOD == 1)
	{
		ApplyGravity(DeltaTime * 0.5f);
		ApplyFlowRules(DeltaTime * 0.5f);
	}
	else if (CurrentLOD == 2)
	{
		ApplyGravity(DeltaTime * 0.25f);
	}
	
	ProcessBorderFlow(DeltaTime);
	
	// Don't swap buffers here - the ChunkManager will do it after border synchronization
	// Cells = NextCells;
	LastUpdateTime += DeltaTime;
	
	bDirty = true;
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
		Cells[Idx].FluidLevel = FMath::Min(Cells[Idx].FluidLevel + Amount, MaxFluidLevel);
		bDirty = true;
	}
}

void UFluidChunk::RemoveFluid(int32 LocalX, int32 LocalY, int32 LocalZ, float Amount)
{
	const int32 Idx = GetLocalCellIndex(LocalX, LocalY, LocalZ);
	if (Idx != -1)
	{
		Cells[Idx].FluidLevel = FMath::Max(Cells[Idx].FluidLevel - Amount, 0.0f);
		bDirty = true;
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
		Cell.FlowVelocity = FVector::ZeroVector;
		Cell.Pressure = 0.0f;
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
						NextCells[CurrentIdx].FlowVelocity.Z = -FlowAmount / DeltaTime;
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
						
						const float VelocityMagnitude = OutflowToNeighbor[i] / DeltaTime;
						if (i == 0) NextCells[CurrentIdx].FlowVelocity.X = VelocityMagnitude;
						else if (i == 1) NextCells[CurrentIdx].FlowVelocity.X = -VelocityMagnitude;
						else if (i == 2) NextCells[CurrentIdx].FlowVelocity.Y = VelocityMagnitude;
						else if (i == 3) NextCells[CurrentIdx].FlowVelocity.Y = -VelocityMagnitude;
					}
				}
			}
		}
	}
}

void UFluidChunk::ApplyPressure(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_ApplyPressure);
	
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
				
				if (CurrentCell.FluidLevel > MinFluidLevel)
				{
					float FluidAbove = 0.0f;
					for (int32 zAbove = z + 1; zAbove < ChunkSize; ++zAbove)
					{
						const int32 AboveIdx = GetLocalCellIndex(x, y, zAbove);
						if (AboveIdx != -1)
						{
							FluidAbove += Cells[AboveIdx].FluidLevel;
						}
					}
					
					CurrentCell.Pressure = CurrentCell.FluidLevel + FluidAbove * CompressionFactor;
				}
				else
				{
					CurrentCell.Pressure = 0.0f;
				}
			}
		}
	}
}

void UFluidChunk::UpdateVelocities(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_UpdateVelocities);
	const float ViscosityDamping = 1.0f - (Viscosity * DeltaTime);
	
	for (int32 i = 0; i < NextCells.Num(); ++i)
	{
		NextCells[i].FlowVelocity *= ViscosityDamping;
		
		const float MaxVelocity = CellSize / DeltaTime;
		NextCells[i].FlowVelocity.X = FMath::Clamp(NextCells[i].FlowVelocity.X, -MaxVelocity, MaxVelocity);
		NextCells[i].FlowVelocity.Y = FMath::Clamp(NextCells[i].FlowVelocity.Y, -MaxVelocity, MaxVelocity);
		NextCells[i].FlowVelocity.Z = FMath::Clamp(NextCells[i].FlowVelocity.Z, -MaxVelocity, MaxVelocity);
	}
}

void UFluidChunk::ProcessBorderFlow(float DeltaTime)
{
	if (!bBorderDirty)
		return;
	
	FScopeLock Lock(&BorderDataMutex);
	
	bBorderDirty = false;
}