#include "CellularAutomata/CAFluidGrid.h"
#include "Math/UnrealMathUtility.h"
#include "VoxelFluidStats.h"

UCAFluidGrid::UCAFluidGrid()
{
	GridSizeX = 128;
	GridSizeY = 128;
	GridSizeZ = 32;
	CellSize = 100.0f;
	FlowRate = 0.5f;
	Viscosity = 0.1f;
	Gravity = 981.0f;
	MinFluidLevel = 0.001f;
	MaxFluidLevel = 1.0f;
	CompressionFactor = 0.05f;
	bAllowFluidEscape = true;
}

void UCAFluidGrid::InitializeGrid(int32 InSizeX, int32 InSizeY, int32 InSizeZ, float InCellSize, const FVector& InGridOrigin)
{
	GridSizeX = FMath::Max(1, InSizeX);
	GridSizeY = FMath::Max(1, InSizeY);
	GridSizeZ = FMath::Max(1, InSizeZ);
	CellSize = FMath::Max(1.0f, InCellSize);

	const int32 TotalCells = GridSizeX * GridSizeY * GridSizeZ;
	Cells.SetNum(TotalCells);
	NextCells.SetNum(TotalCells);

	for (int32 i = 0; i < TotalCells; ++i)
	{
		Cells[i] = FCAFluidCell();
		NextCells[i] = FCAFluidCell();
	}

	GridOrigin = InGridOrigin;
}

void UCAFluidGrid::UpdateSimulation(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_UpdateSimulation);
	
	if (Cells.Num() == 0)
		return;

	NextCells = Cells;

	// Count active cells and total volume for stats
	int32 ActiveCellCount = 0;
	float TotalVolume = 0.0f;
	for (const FCAFluidCell& Cell : Cells)
	{
		if (Cell.FluidLevel > MinFluidLevel)
		{
			ActiveCellCount++;
			TotalVolume += Cell.FluidLevel;
		}
	}
	SET_DWORD_STAT(STAT_VoxelFluid_ActiveCells, ActiveCellCount);
	SET_DWORD_STAT(STAT_VoxelFluid_TotalCells, Cells.Num());
	SET_FLOAT_STAT(STAT_VoxelFluid_TotalVolume, TotalVolume);

	ApplyGravity(DeltaTime);
	ApplyFlowRules(DeltaTime);
	ApplyPressure(DeltaTime);
	UpdateVelocities(DeltaTime);

	Cells = NextCells;
}

void UCAFluidGrid::ApplyGravity(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_ApplyGravity);
	const float GravityFlow = (Gravity / 1000.0f) * DeltaTime;

	// Handle fluid at bottom boundary first (z=0)
	if (bAllowFluidEscape)
	{
		for (int32 y = 0; y < GridSizeY; ++y)
		{
			for (int32 x = 0; x < GridSizeX; ++x)
			{
				const int32 BottomIdx = GetCellIndex(x, y, 0);
				if (BottomIdx != -1)
				{
					FCAFluidCell& BottomCell = Cells[BottomIdx];
					if (BottomCell.FluidLevel > MinFluidLevel && !BottomCell.bIsSolid)
					{
						// Allow fluid to escape through bottom boundary
						const float EscapeAmount = BottomCell.FluidLevel * GravityFlow * 0.1f; // Slower escape rate
						NextCells[BottomIdx].FluidLevel -= EscapeAmount;
						NextCells[BottomIdx].FlowVelocity.Z = -EscapeAmount / DeltaTime;
					}
				}
			}
		}
	}

	// Apply gravity between all cells
	for (int32 z = GridSizeZ - 1; z >= 1; --z)
	{
		for (int32 y = 0; y < GridSizeY; ++y)
		{
			for (int32 x = 0; x < GridSizeX; ++x)
			{
				const int32 CurrentIdx = GetCellIndex(x, y, z);
				const int32 BelowIdx = GetCellIndex(x, y, z - 1);

				if (CurrentIdx == -1 || BelowIdx == -1)
					continue;

				FCAFluidCell& CurrentCell = Cells[CurrentIdx];
				FCAFluidCell& BelowCell = Cells[BelowIdx];

				if (CurrentCell.FluidLevel > MinFluidLevel && !BelowCell.bIsSolid)
				{
					const float SpaceBelow = MaxFluidLevel - BelowCell.FluidLevel;
					const float FlowAmount = FMath::Min(CurrentCell.FluidLevel * GravityFlow, SpaceBelow);

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

void UCAFluidGrid::ApplyFlowRules(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_ApplyFlowRules);
	const float FlowAmount = FlowRate * DeltaTime;

	for (int32 z = 0; z < GridSizeZ; ++z)
	{
		for (int32 y = 0; y < GridSizeY; ++y)
		{
			for (int32 x = 0; x < GridSizeX; ++x)
			{
				const int32 CurrentIdx = GetCellIndex(x, y, z);
				if (CurrentIdx == -1)
					continue;

				FCAFluidCell& CurrentCell = Cells[CurrentIdx];
				
				if (CurrentCell.FluidLevel <= MinFluidLevel || CurrentCell.bIsSolid)
					continue;

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
					
					if (IsValidCell(nx, ny, z))
					{
						const int32 NeighborIdx = GetCellIndex(nx, ny, z);
						FCAFluidCell& NeighborCell = Cells[NeighborIdx];

						if (!NeighborCell.bIsSolid)
						{
							const float HeightDiff = (CurrentCell.TerrainHeight + CurrentCell.FluidLevel) - 
													 (NeighborCell.TerrainHeight + NeighborCell.FluidLevel);
							
							if (HeightDiff > 0)
							{
								const float PossibleFlow = FMath::Min(
									CurrentCell.FluidLevel * FlowAmount,
									HeightDiff * 0.5f
								);
								
								const float SpaceInNeighbor = MaxFluidLevel - NeighborCell.FluidLevel;
								OutflowToNeighbor[i] = FMath::Min(PossibleFlow, SpaceInNeighbor);
								TotalOutflow += OutflowToNeighbor[i];
							}
						}
					}
					else if (bAllowFluidEscape && (nx < 0 || nx >= GridSizeX || ny < 0 || ny >= GridSizeY))
					{
						// Allow fluid to escape through side boundaries
						const float EscapeFlow = CurrentCell.FluidLevel * FlowAmount * 0.05f; // Very slow escape
						OutflowToNeighbor[i] = EscapeFlow;
						TotalOutflow += OutflowToNeighbor[i];
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
						
						// Only add to neighbor if it's a valid cell (not escaping)
						if (IsValidCell(nx, ny, z))
						{
							const int32 NeighborIdx = GetCellIndex(nx, ny, z);
							NextCells[NeighborIdx].FluidLevel += OutflowToNeighbor[i];
						}
						// If not valid, fluid escapes and is removed

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

void UCAFluidGrid::ApplyPressure(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_ApplyPressure);
	for (int32 z = 0; z < GridSizeZ; ++z)
	{
		for (int32 y = 0; y < GridSizeY; ++y)
		{
			for (int32 x = 0; x < GridSizeX; ++x)
			{
				const int32 CurrentIdx = GetCellIndex(x, y, z);
				if (CurrentIdx == -1)
					continue;

				FCAFluidCell& CurrentCell = NextCells[CurrentIdx];
				
				if (CurrentCell.FluidLevel > MinFluidLevel)
				{
					float FluidAbove = 0.0f;
					for (int32 zAbove = z + 1; zAbove < GridSizeZ; ++zAbove)
					{
						const int32 AboveIdx = GetCellIndex(x, y, zAbove);
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

void UCAFluidGrid::UpdateVelocities(float DeltaTime)
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

void UCAFluidGrid::AddFluid(int32 X, int32 Y, int32 Z, float Amount)
{
	const int32 Idx = GetCellIndex(X, Y, Z);
	if (Idx != -1 && !Cells[Idx].bIsSolid)
	{
		Cells[Idx].FluidLevel = FMath::Min(Cells[Idx].FluidLevel + Amount, MaxFluidLevel);
	}
}

void UCAFluidGrid::RemoveFluid(int32 X, int32 Y, int32 Z, float Amount)
{
	const int32 Idx = GetCellIndex(X, Y, Z);
	if (Idx != -1)
	{
		Cells[Idx].FluidLevel = FMath::Max(Cells[Idx].FluidLevel - Amount, 0.0f);
	}
}

float UCAFluidGrid::GetFluidAt(int32 X, int32 Y, int32 Z) const
{
	const int32 Idx = GetCellIndex(X, Y, Z);
	return (Idx != -1) ? Cells[Idx].FluidLevel : 0.0f;
}

void UCAFluidGrid::SetTerrainHeight(int32 X, int32 Y, float Height)
{
	for (int32 z = 0; z < GridSizeZ; ++z)
	{
		const int32 Idx = GetCellIndex(X, Y, z);
		if (Idx != -1)
		{
			Cells[Idx].TerrainHeight = Height;
			
			const float CellWorldZ = GridOrigin.Z + (z * CellSize);
			Cells[Idx].bIsSolid = (CellWorldZ < Height);
		}
	}
}

FVector UCAFluidGrid::GetWorldPositionFromCell(int32 X, int32 Y, int32 Z) const
{
	return GridOrigin + FVector(X * CellSize, Y * CellSize, Z * CellSize);
}

bool UCAFluidGrid::GetCellFromWorldPosition(const FVector& WorldPos, int32& OutX, int32& OutY, int32& OutZ) const
{
	const FVector LocalPos = WorldPos - GridOrigin;
	
	OutX = FMath::FloorToInt(LocalPos.X / CellSize);
	OutY = FMath::FloorToInt(LocalPos.Y / CellSize);
	OutZ = FMath::FloorToInt(LocalPos.Z / CellSize);
	
	return IsValidCell(OutX, OutY, OutZ);
}

void UCAFluidGrid::ClearGrid()
{
	for (FCAFluidCell& Cell : Cells)
	{
		Cell.FluidLevel = 0.0f;
		Cell.FlowVelocity = FVector::ZeroVector;
		Cell.Pressure = 0.0f;
	}
	NextCells = Cells;
}

bool UCAFluidGrid::IsValidCell(int32 X, int32 Y, int32 Z) const
{
	return X >= 0 && X < GridSizeX && 
		   Y >= 0 && Y < GridSizeY && 
		   Z >= 0 && Z < GridSizeZ;
}

int32 UCAFluidGrid::GetCellIndex(int32 X, int32 Y, int32 Z) const
{
	if (!IsValidCell(X, Y, Z))
		return -1;
	
	return X + Y * GridSizeX + Z * GridSizeX * GridSizeY;
}

float UCAFluidGrid::CalculateFlowTo(int32 FromX, int32 FromY, int32 FromZ, 
									 int32 ToX, int32 ToY, int32 ToZ, float DeltaTime)
{
	const int32 FromIdx = GetCellIndex(FromX, FromY, FromZ);
	const int32 ToIdx = GetCellIndex(ToX, ToY, ToZ);
	
	if (FromIdx == -1 || ToIdx == -1)
		return 0.0f;
	
	const FCAFluidCell& FromCell = Cells[FromIdx];
	const FCAFluidCell& ToCell = Cells[ToIdx];
	
	if (FromCell.bIsSolid || ToCell.bIsSolid)
		return 0.0f;
	
	const float HeightDiff = (FromCell.TerrainHeight + FromCell.FluidLevel) - 
							 (ToCell.TerrainHeight + ToCell.FluidLevel);
	
	if (HeightDiff <= 0)
		return 0.0f;
	
	const float MaxFlow = FromCell.FluidLevel * FlowRate * DeltaTime;
	const float SpaceInTarget = MaxFluidLevel - ToCell.FluidLevel;
	
	return FMath::Min(MaxFlow, FMath::Min(HeightDiff * 0.5f, SpaceInTarget));
}