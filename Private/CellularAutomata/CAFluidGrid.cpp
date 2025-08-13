#include "CellularAutomata/CAFluidGrid.h"
#include "Math/UnrealMathUtility.h"
#include "VoxelFluidStats.h"

UCAFluidGrid::UCAFluidGrid()
{
	GridSizeX = 128;
	GridSizeY = 128;
	GridSizeZ = 32;
	CellSize = 100.0f;
	MaxFluidLevel = 1.0f;
	MinFluidLevel = 0.001f;
	FlowRate = 0.25f;
	SettledThreshold = 5;
	EqualizationRate = 0.5f;
	bUseMinecraftRules = true;
	CompressionThreshold = 0.95f;
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
	CellNeedsUpdate.SetNum(TotalCells);

	for (int32 i = 0; i < TotalCells; ++i)
	{
		Cells[i] = FCAFluidCell();
		NextCells[i] = FCAFluidCell();
		CellNeedsUpdate[i] = true; // Initially all cells need updating
	}

	GridOrigin = InGridOrigin;
	ActiveCellCount = TotalCells;
	TotalSettledCells = 0;
}

void UCAFluidGrid::UpdateSimulation(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_UpdateSimulation);
	
	if (Cells.Num() == 0)
		return;

	// Early exit if everything is settled
	if (bEnableSettling && ActiveCellCount == 0)
	{
		return;
	}
	
	// Quick scan for any fluid activity
	bool bHasActiveFluid = false;
	for (int32 i = 0; i < Cells.Num(); i += 16) // Sample every 16th cell for quick check
	{
		if (i < Cells.Num() && Cells[i].FluidLevel > MinFluidLevel && !Cells[i].bSettled)
		{
			bHasActiveFluid = true;
			break;
		}
	}
	
	// If no active fluid found in sampling, do full check
	if (!bHasActiveFluid)
	{
		for (int32 i = 0; i < Cells.Num(); ++i)
		{
			if (Cells[i].FluidLevel > MinFluidLevel && !Cells[i].bSettled)
			{
				bHasActiveFluid = true;
				break;
			}
		}
		
		if (!bHasActiveFluid)
		{
			// Everything is settled, skip simulation
			return;
		}
	}

	// Store previous state for settling detection
	for (int32 i = 0; i < Cells.Num(); ++i)
	{
		Cells[i].LastFluidLevel = Cells[i].FluidLevel;
	}

	NextCells = Cells;

	// Count active cells and total volume for stats
	int32 FluidCellCount = 0;
	int32 SettledCellCount = 0;
	float TotalVolume = 0.0f;
	for (int32 i = 0; i < Cells.Num(); ++i)
	{
		const FCAFluidCell& Cell = Cells[i];
		if (Cell.FluidLevel > MinFluidLevel)
		{
			FluidCellCount++;
			TotalVolume += Cell.FluidLevel;
			if (Cell.bSettled)
			{
				SettledCellCount++;
			}
		}
	}
	SET_DWORD_STAT(STAT_VoxelFluid_ActiveCells, ActiveCellCount);
	SET_DWORD_STAT(STAT_VoxelFluid_TotalCells, Cells.Num());
	SET_FLOAT_STAT(STAT_VoxelFluid_TotalVolume, TotalVolume);
	TotalSettledCells = SettledCellCount;

	// Initialize update flags for this frame
	if (bEnableSettling)
	{
		InitializeUpdateFlags();
	}

	// Combined physics pass for better cache efficiency
	ProcessCombinedPhysics(DeltaTime);
	ProcessHorizontalFlow(DeltaTime);
	ProcessEqualization(DeltaTime);
	UpdateSettledStates();

	Cells = NextCells;
	
	// Log settling efficiency
	if (FluidCellCount > 0)
	{
		const float SettledPercentage = (SettledCellCount * 100.0f) / FluidCellCount;
		UE_LOG(LogTemp, VeryVerbose, TEXT("Fluid Settling: %d/%d cells settled (%.1f%%), %d cells active"), 
			SettledCellCount, FluidCellCount, SettledPercentage, ActiveCellCount);
	}
}

void UCAFluidGrid::ProcessCombinedPhysics(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_ApplyGravity);
	// Combined gravity and compression in a single pass for better cache efficiency
	// Process from bottom to top for gravity, handle compression simultaneously

	// Track cells that need compression processing
	TArray<int32> CompressionCells;
	CompressionCells.Reserve(1024);

	// Process gravity from bottom up
	for (int32 z = 1; z < GridSizeZ; ++z)
	{
		for (int32 y = 0; y < GridSizeY; ++y)
		{
			for (int32 x = 0; x < GridSizeX; ++x)
			{
				// Skip settled cells if settling is enabled
				if (bEnableSettling && !ShouldUpdateCell(x, y, z))
					continue;

				const int32 CurrentIdx = GetCellIndex(x, y, z);
				if (CurrentIdx == -1)
					continue;

				FCAFluidCell& CurrentCell = Cells[CurrentIdx];

				// Skip if current cell is empty
				if (CurrentCell.FluidLevel <= MinFluidLevel || CurrentCell.bIsSolid)
					continue;

				// === GRAVITY PROCESSING ===
				const int32 BelowIdx = GetCellIndex(x, y, z - 1);
				if (BelowIdx != -1)
				{
					FCAFluidCell& BelowCell = NextCells[BelowIdx];

					// Skip if below is solid
					if (!BelowCell.bIsSolid)
					{
						// Calculate how much can flow down
						const float SpaceBelow = MaxFluidLevel - BelowCell.FluidLevel;
						if (SpaceBelow > MinFluidLevel)
						{
							// Transfer as much as possible
							const float TransferAmount = FMath::Min(CurrentCell.FluidLevel, SpaceBelow);
							
							NextCells[CurrentIdx].FluidLevel -= TransferAmount;
							NextCells[BelowIdx].FluidLevel += TransferAmount;
							
							// Wake up the cells involved and their neighbors
							if (bEnableSettling)
							{
								WakeUpNeighbors(x, y, z);
								WakeUpNeighbors(x, y, z - 1);
							}
						}
					}
				}

				// === COMPRESSION CHECK ===
				// Check if this cell needs upward compression (overfilled)
				if (NextCells[CurrentIdx].FluidLevel > MaxFluidLevel && z < GridSizeZ - 1)
				{
					CompressionCells.Add(CurrentIdx);
				}
			}
		}
	}

	// === COMPRESSION PROCESSING ===
	// Process compression for overfilled cells
	for (int32 CompressIdx : CompressionCells)
	{
		FCAFluidCell& CurrentCell = NextCells[CompressIdx];
		
		// If still overfilled after gravity
		if (CurrentCell.FluidLevel > MaxFluidLevel && !CurrentCell.bIsSolid)
		{
			// Get cell coordinates
			int32 x = CompressIdx % GridSizeX;
			int32 y = (CompressIdx / GridSizeX) % GridSizeY;
			int32 z = CompressIdx / (GridSizeX * GridSizeY);
			
			if (z < GridSizeZ - 1)
			{
				const int32 AboveIdx = GetCellIndex(x, y, z + 1);
				if (AboveIdx != -1)
				{
					FCAFluidCell& AboveCell = NextCells[AboveIdx];
					
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
						
						// Wake up the cells involved and their neighbors
						if (bEnableSettling)
						{
							WakeUpNeighbors(x, y, z);
							WakeUpNeighbors(x, y, z + 1);
						}
					}
				}
			}
		}
	}
}

void UCAFluidGrid::ProcessHorizontalFlow(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_ApplyFlowRules);
	
	// Simple horizontal spreading - only spread when resting on solid ground
	for (int32 z = 0; z < GridSizeZ; ++z)
	{
		for (int32 y = 0; y < GridSizeY; ++y)
		{
			for (int32 x = 0; x < GridSizeX; ++x)
			{
				// Skip settled cells if settling is enabled
				if (bEnableSettling && !ShouldUpdateCell(x, y, z))
					continue;

				const int32 CurrentIdx = GetCellIndex(x, y, z);
				if (CurrentIdx == -1)
					continue;

				FCAFluidCell& CurrentCell = Cells[CurrentIdx];
				
				// Skip if no fluid or is solid
				if (CurrentCell.FluidLevel <= MinFluidLevel || CurrentCell.bIsSolid)
					continue;
				
				// Check if water has solid support below
				bool bCanSpread = false;
				if (z > 0)
				{
					const int32 BelowIdx = GetCellIndex(x, y, z - 1);
					if (BelowIdx != -1)
					{
						const FCAFluidCell& BelowCell = Cells[BelowIdx];
						// Can spread if below is solid or nearly full of water
						bCanSpread = BelowCell.bIsSolid || BelowCell.FluidLevel >= CompressionThreshold;
					}
				}
				else
				{
					bCanSpread = true; // At bottom
				}
				
				if (!bCanSpread)
					continue;
				
				// Find all valid neighbors at same height
				const int32 Neighbors[4][2] = {
					{x + 1, y},
					{x - 1, y},
					{x, y + 1},
					{x, y - 1}
				};
				
				TArray<int32> ValidNeighbors;
				float TotalFluid = CurrentCell.FluidLevel;
				int32 CellCount = 1;
				
				for (int32 i = 0; i < 4; ++i)
				{
					const int32 nx = Neighbors[i][0];
					const int32 ny = Neighbors[i][1];
					
					if (IsValidCell(nx, ny, z))
					{
						const int32 NeighborIdx = GetCellIndex(nx, ny, z);
						const FCAFluidCell& NeighborCell = Cells[NeighborIdx];
						
						// Can flow into neighbor if it's not solid and has less water
						if (!NeighborCell.bIsSolid && NeighborCell.FluidLevel < CurrentCell.FluidLevel)
						{
							// Check if neighbor also has solid support
							bool bNeighborSupported = false;
							if (z > 0)
							{
								const int32 NeighborBelowIdx = GetCellIndex(nx, ny, z - 1);
								if (NeighborBelowIdx != -1)
								{
									const FCAFluidCell& NeighborBelow = Cells[NeighborBelowIdx];
									bNeighborSupported = NeighborBelow.bIsSolid || NeighborBelow.FluidLevel >= CompressionThreshold;
								}
							}
							else
							{
								bNeighborSupported = true;
							}
							
							if (bNeighborSupported)
							{
								ValidNeighbors.Add(NeighborIdx);
								TotalFluid += NeighborCell.FluidLevel;
								CellCount++;
							}
						}
					}
				}
				
				// Distribute water evenly among current cell and valid neighbors
				if (ValidNeighbors.Num() > 0)
				{
					const float AverageLevel = TotalFluid / CellCount;
					const float FlowSpeed = FlowRate * DeltaTime;
					
					// Move towards average level
					float NewLevel = FMath::Lerp(CurrentCell.FluidLevel, AverageLevel, FlowSpeed);
					NewLevel = FMath::Clamp(NewLevel, 0.0f, MaxFluidLevel);
					NextCells[CurrentIdx].FluidLevel = NewLevel;
					
					for (int32 NeighborIdx : ValidNeighbors)
					{
						float NeighborNewLevel = FMath::Lerp(Cells[NeighborIdx].FluidLevel, AverageLevel, FlowSpeed);
						NeighborNewLevel = FMath::Clamp(NeighborNewLevel, 0.0f, MaxFluidLevel);
						NextCells[NeighborIdx].FluidLevel = NeighborNewLevel;
						NextCells[NeighborIdx].bSettled = false;
						NextCells[NeighborIdx].SettledCounter = 0;
					}
					
					// Wake up cells involved in the flow
					if (bEnableSettling)
					{
						WakeUpNeighbors(x, y, z);
						// Also wake up the neighbor cells we flowed into
						for (int32 i = 0; i < ValidNeighbors.Num(); ++i)
						{
							// Convert back to grid coordinates
							int32 nx, ny, nz;
							nx = (ValidNeighbors[i] % GridSizeX);
							ny = ((ValidNeighbors[i] / GridSizeX) % GridSizeY);
							nz = (ValidNeighbors[i] / (GridSizeX * GridSizeY));
							WakeUpNeighbors(nx, ny, nz);
						}
					}
				}
			}
		}
	}
}

// ProcessCompression is now integrated into ProcessCombinedPhysics for better performance

void UCAFluidGrid::ProcessEqualization(float DeltaTime)
{
	// Equalize water levels in connected regions for stable pools
	for (int32 z = 0; z < GridSizeZ; ++z)
	{
		for (int32 y = 0; y < GridSizeY; ++y)
		{
			for (int32 x = 0; x < GridSizeX; ++x)
			{
				// Only process settled cells for equalization
				if (bEnableSettling && !ShouldUpdateCell(x, y, z))
					continue;
				
				const int32 CurrentIdx = GetCellIndex(x, y, z);
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
					
					if (IsValidCell(nx, ny, z))
					{
						const int32 NeighborIdx = GetCellIndex(nx, ny, z);
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
					const float AdjustmentRate = EqualizationRate * DeltaTime;
					
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

void UCAFluidGrid::UpdateSettledStates()
{
	if (!bEnableSettling)
		return;
	
	// Update settled state based on whether fluid level changed and neighbor states
	int32 NewSettledCount = 0;
	
	for (int32 z = 0; z < GridSizeZ; ++z)
	{
		for (int32 y = 0; y < GridSizeY; ++y)
		{
			for (int32 x = 0; x < GridSizeX; ++x)
			{
				const int32 Idx = GetCellIndex(x, y, z);
				if (Idx == -1)
					continue;
				
				FCAFluidCell& Cell = NextCells[Idx];
				
				if (Cell.FluidLevel <= MinFluidLevel || Cell.bIsSolid)
				{
					Cell.bSettled = false;
					Cell.SettledCounter = 0;
					continue;
				}
				
				// Check if fluid level is stable
				const float Change = FMath::Abs(Cell.FluidLevel - Cell.LastFluidLevel);
				
				if (Change < SettlingChangeThreshold)
				{
					// Increment counter if stable
					Cell.SettledCounter++;
					
					// Check if cell can actually settle based on neighbors
					if (Cell.SettledCounter >= SettledThreshold && CanCellSettle(x, y, z))
					{
						Cell.bSettled = true;
						NewSettledCount++;
					}
					else if (Cell.bSettled)
					{
						// Was settled but conditions changed
						if (!CanCellSettle(x, y, z))
						{
							Cell.bSettled = false;
							Cell.SettledCounter = 0;
							// Wake up neighbors since this cell is now active
							WakeUpNeighbors(x, y, z);
						}
						else
						{
							NewSettledCount++;
						}
					}
				}
				else
				{
					// Fluid level changed, reset settling
					if (Cell.bSettled)
					{
						// Wake up neighbors since this cell became active
						WakeUpNeighbors(x, y, z);
					}
					Cell.bSettled = false;
					Cell.SettledCounter = 0;
				}
			}
		}
	}
	
	TotalSettledCells = NewSettledCount;
	
	// Log settling progress periodically
	static float LastSettlingLogTime = 0.0f;
	static float CurrentTime = 0.0f;
	CurrentTime += 0.016f; // Approximate frame time
	
	if (CurrentTime - LastSettlingLogTime > 2.0f) // Log every 2 seconds
	{
		int32 FluidCellCount = 0;
		for (const FCAFluidCell& Cell : NextCells)
		{
			if (Cell.FluidLevel > MinFluidLevel && !Cell.bIsSolid)
				FluidCellCount++;
		}
		
		if (FluidCellCount > 0)
		{
			const float SettledPercentage = (NewSettledCount * 100.0f) / FluidCellCount;
			UE_LOG(LogTemp, Log, TEXT("Settling Status: %d/%d fluid cells settled (%.1f%%), %d cells need update"),
				NewSettledCount, FluidCellCount, SettledPercentage, ActiveCellCount);
		}
		
		LastSettlingLogTime = CurrentTime;
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

void UCAFluidGrid::SetCellSolid(int32 X, int32 Y, int32 Z, bool bSolid)
{
	const int32 Idx = GetCellIndex(X, Y, Z);
	if (Idx != -1)
	{
		FCAFluidCell& Cell = Cells[Idx];
		bool bWasSolid = Cell.bIsSolid;
		Cell.bIsSolid = bSolid;
		
		// If cell became solid, remove any fluid
		if (bSolid && !bWasSolid)
		{
			Cell.FluidLevel = 0.0f;
			Cell.bSettled = false;
			Cell.SettledCounter = 0;
			
			// Wake up neighbors since terrain changed
			if (bEnableSettling)
			{
				WakeUpNeighbors(X, Y, Z);
				// Also wake up cells above in case they need to flow down
				if (Z < GridSizeZ - 1)
				{
					WakeUpNeighbors(X, Y, Z + 1);
				}
			}
		}
		// If cell became empty, wake up neighbors so fluid can flow in
		else if (!bSolid && bWasSolid)
		{
			// Wake up all surrounding cells including above
			if (bEnableSettling)
			{
				WakeUpNeighbors(X, Y, Z);
				
				// Wake up cells above so they can fall into this newly empty space
				if (Z < GridSizeZ - 1)
				{
					WakeUpNeighbors(X, Y, Z + 1);
					// Wake up multiple cells above to ensure water flows down
					if (Z < GridSizeZ - 2)
					{
						WakeUpNeighbors(X, Y, Z + 2);
					}
				}
				
				// Also wake up cells to the sides at the level above
				// This helps water flow into holes from the sides
				if (Z < GridSizeZ - 1)
				{
					if (X > 0) MarkCellForUpdate(X - 1, Y, Z + 1);
					if (X < GridSizeX - 1) MarkCellForUpdate(X + 1, Y, Z + 1);
					if (Y > 0) MarkCellForUpdate(X, Y - 1, Z + 1);
					if (Y < GridSizeY - 1) MarkCellForUpdate(X, Y + 1, Z + 1);
				}
			}
			
			// Mark this cell as needing update
			MarkCellForUpdate(X, Y, Z);
		}
	}
}

bool UCAFluidGrid::IsCellSolid(int32 X, int32 Y, int32 Z) const
{
	const int32 Idx = GetCellIndex(X, Y, Z);
	if (Idx != -1)
	{
		return Cells[Idx].bIsSolid;
	}
	return true; // Out of bounds cells are considered solid
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
		Cell.bSettled = false;
		Cell.SettledCounter = 0;
		Cell.LastFluidLevel = 0.0f;
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

// Helper methods for simple CA
float UCAFluidGrid::GetStableFluidLevel(int32 X, int32 Y, int32 Z) const
{
	const int32 Idx = GetCellIndex(X, Y, Z);
	if (Idx == -1)
		return 0.0f;
	
	const FCAFluidCell& Cell = Cells[Idx];
	return Cell.bSettled ? Cell.FluidLevel : 0.0f;
}

bool UCAFluidGrid::CanFlowInto(int32 X, int32 Y, int32 Z) const
{
	const int32 Idx = GetCellIndex(X, Y, Z);
	if (Idx == -1)
		return false;
	
	const FCAFluidCell& Cell = Cells[Idx];
	return !Cell.bIsSolid && Cell.FluidLevel < MaxFluidLevel;
}

void UCAFluidGrid::DistributeWater(int32 X, int32 Y, int32 Z, float Amount)
{
	// Distribute water to this cell and spread if needed
	const int32 Idx = GetCellIndex(X, Y, Z);
	if (Idx == -1 || Cells[Idx].bIsSolid)
		return;
	
	NextCells[Idx].FluidLevel = FMath::Min(NextCells[Idx].FluidLevel + Amount, MaxFluidLevel);
	NextCells[Idx].bSettled = false;
	NextCells[Idx].SettledCounter = 0;
}

bool UCAFluidGrid::IsCellSettled(int32 X, int32 Y, int32 Z) const
{
	const int32 Idx = GetCellIndex(X, Y, Z);
	if (Idx == -1 || Idx >= Cells.Num())
		return false;
	
	return Cells[Idx].bSettled;
}

float UCAFluidGrid::GetSettlingPercentage() const
{
	if (!bEnableSettling)
		return 0.0f;
		
	int32 FluidCellCount = 0;
	for (const FCAFluidCell& Cell : Cells)
	{
		if (Cell.FluidLevel > MinFluidLevel && !Cell.bIsSolid)
			FluidCellCount++;
	}
	
	if (FluidCellCount == 0)
		return 100.0f; // No fluid cells, consider it 100% settled
		
	return (TotalSettledCells * 100.0f) / FluidCellCount;
}

void UCAFluidGrid::ForceWakeAllFluid()
{
	// Wake up all cells with fluid
	for (int32 z = 0; z < GridSizeZ; ++z)
	{
		for (int32 y = 0; y < GridSizeY; ++y)
		{
			for (int32 x = 0; x < GridSizeX; ++x)
			{
				const int32 Idx = GetCellIndex(x, y, z);
				if (Idx != -1)
				{
					FCAFluidCell& Cell = Cells[Idx];
					if (Cell.FluidLevel > MinFluidLevel && !Cell.bIsSolid)
					{
						Cell.bSettled = false;
						Cell.SettledCounter = 0;
						MarkCellForUpdate(x, y, z);
					}
				}
			}
		}
	}
	
	UE_LOG(LogTemp, Warning, TEXT("ForceWakeAllFluid: Woke up all fluid cells"));
}

// Settling optimization helper methods
void UCAFluidGrid::InitializeUpdateFlags()
{
	// Reset all update flags at the start of each frame
	ActiveCellCount = 0;
	
	for (int32 z = 0; z < GridSizeZ; ++z)
	{
		for (int32 y = 0; y < GridSizeY; ++y)
		{
			for (int32 x = 0; x < GridSizeX; ++x)
			{
				const int32 Idx = GetCellIndex(x, y, z);
				if (Idx == -1)
					continue;
				
				const FCAFluidCell& Cell = Cells[Idx];
				
				// Cell needs update if:
				// 1. It has fluid and is not settled
				// 2. It has fluid above the threshold
				// 3. It was marked for update by a neighbor
				bool bNeedsUpdate = false;
				
				if (Cell.FluidLevel > MinFluidLevel)
				{
					if (!Cell.bSettled)
					{
						bNeedsUpdate = true;
					}
					else
					{
						// Check if any neighbor is unsettled and could affect this cell
						bNeedsUpdate = !CanCellSettle(x, y, z);
					}
				}
				
				CellNeedsUpdate[Idx] = bNeedsUpdate;
				if (bNeedsUpdate)
				{
					ActiveCellCount++;
				}
			}
		}
	}
}

void UCAFluidGrid::MarkCellForUpdate(int32 X, int32 Y, int32 Z)
{
	const int32 Idx = GetCellIndex(X, Y, Z);
	if (Idx != -1 && Idx < CellNeedsUpdate.Num())
	{
		if (!CellNeedsUpdate[Idx])
		{
			CellNeedsUpdate[Idx] = true;
			ActiveCellCount++;
		}
	}
}

void UCAFluidGrid::WakeUpNeighbors(int32 X, int32 Y, int32 Z)
{
	// Wake up all neighbors when a cell changes
	for (int32 dx = -1; dx <= 1; ++dx)
	{
		for (int32 dy = -1; dy <= 1; ++dy)
		{
			for (int32 dz = -1; dz <= 1; ++dz)
			{
				if (dx == 0 && dy == 0 && dz == 0)
					continue;
				
				const int32 nx = X + dx;
				const int32 ny = Y + dy;
				const int32 nz = Z + dz;
				
				if (IsValidCell(nx, ny, nz))
				{
					const int32 NeighborIdx = GetCellIndex(nx, ny, nz);
					if (NeighborIdx != -1)
					{
						// Wake up the neighbor
						NextCells[NeighborIdx].bSettled = false;
						NextCells[NeighborIdx].SettledCounter = 0;
						MarkCellForUpdate(nx, ny, nz);
					}
				}
			}
		}
	}
}

bool UCAFluidGrid::ShouldUpdateCell(int32 X, int32 Y, int32 Z) const
{
	const int32 Idx = GetCellIndex(X, Y, Z);
	if (Idx == -1 || Idx >= CellNeedsUpdate.Num())
		return false;
	
	return CellNeedsUpdate[Idx];
}

bool UCAFluidGrid::CanCellSettle(int32 X, int32 Y, int32 Z) const
{
	const int32 Idx = GetCellIndex(X, Y, Z);
	if (Idx == -1)
		return false;
	
	const FCAFluidCell& Cell = Cells[Idx];
	
	// Can't settle if no fluid or is solid
	if (Cell.FluidLevel <= MinFluidLevel || Cell.bIsSolid)
		return false;
	
	// Check if cell is stable (fluid level hasn't changed)
	if (FMath::Abs(Cell.FluidLevel - Cell.LastFluidLevel) > SettlingChangeThreshold)
		return false;
	
	// Check neighbors for potential flow
	// A cell can only settle if all neighbors are also stable
	
	// Check cell above - if it has fluid, we might receive flow
	if (Z < GridSizeZ - 1)
	{
		const int32 AboveIdx = GetCellIndex(X, Y, Z + 1);
		if (AboveIdx != -1)
		{
			const FCAFluidCell& AboveCell = Cells[AboveIdx];
			if (AboveCell.FluidLevel > MinFluidLevel && !AboveCell.bIsSolid)
			{
				// If above cell is not settled, we can't settle
				if (!AboveCell.bSettled)
					return false;
			}
		}
	}
	
	// Check cell below - if we can flow down, we're not settled
	if (Z > 0)
	{
		const int32 BelowIdx = GetCellIndex(X, Y, Z - 1);
		if (BelowIdx != -1)
		{
			const FCAFluidCell& BelowCell = Cells[BelowIdx];
			if (!BelowCell.bIsSolid && BelowCell.FluidLevel < MaxFluidLevel)
			{
				// We can still flow down, not settled
				return false;
			}
		}
	}
	
	// Check horizontal neighbors for flow potential
	const int32 Neighbors[4][2] = {
		{X + 1, Y},
		{X - 1, Y},
		{X, Y + 1},
		{X, Y - 1}
	};
	
	for (int32 i = 0; i < 4; ++i)
	{
		const int32 nx = Neighbors[i][0];
		const int32 ny = Neighbors[i][1];
		
		if (IsValidCell(nx, ny, Z))
		{
			const int32 NeighborIdx = GetCellIndex(nx, ny, Z);
			if (NeighborIdx != -1)
			{
				const FCAFluidCell& NeighborCell = Cells[NeighborIdx];
				
				// Check if we have significantly different fluid levels
				if (!NeighborCell.bIsSolid)
				{
					const float LevelDiff = FMath::Abs(Cell.FluidLevel - NeighborCell.FluidLevel);
					if (LevelDiff > SettlingChangeThreshold * 10.0f)
					{
						// Significant difference means flow is likely
						return false;
					}
					
					// If neighbor is not settled and has fluid, we might exchange
					if (NeighborCell.FluidLevel > MinFluidLevel && !NeighborCell.bSettled)
					{
						return false;
					}
				}
			}
		}
	}
	
	return true;
}

void UCAFluidGrid::PropagateWakeUp(int32 X, int32 Y, int32 Z, int32 Distance)
{
	// Wake up cells within a certain distance when a cell becomes active
	for (int32 dx = -Distance; dx <= Distance; ++dx)
	{
		for (int32 dy = -Distance; dy <= Distance; ++dy)
		{
			for (int32 dz = -Distance; dz <= Distance; ++dz)
			{
				const int32 nx = X + dx;
				const int32 ny = Y + dy;
				const int32 nz = Z + dz;
				
				if (IsValidCell(nx, ny, nz))
				{
					const int32 NeighborIdx = GetCellIndex(nx, ny, nz);
					if (NeighborIdx != -1)
					{
						if (Cells[NeighborIdx].bSettled)
						{
							NextCells[NeighborIdx].bSettled = false;
							NextCells[NeighborIdx].SettledCounter = 0;
							MarkCellForUpdate(nx, ny, nz);
						}
					}
				}
			}
		}
	}
}

// Removed old complex methods - using simple CA rules now
/*
void UCAFluidGrid::CalculateHydrostaticPressure()
{
	// Calculate hydrostatic pressure for each cell based on fluid column above
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
				
				if (CurrentCell.FluidLevel > MinFluidLevel)
				{
					// Calculate total fluid weight above this cell
					float FluidAbove = 0.0f;
					for (int32 zAbove = z + 1; zAbove < GridSizeZ; ++zAbove)
					{
						const int32 AboveIdx = GetCellIndex(x, y, zAbove);
						if (AboveIdx != -1)
						{
							FluidAbove += Cells[AboveIdx].FluidLevel;
						}
					}
					
					// Hydrostatic pressure increases with depth
					CurrentCell.HydrostaticPressure = CurrentCell.FluidLevel + (FluidAbove * PressureMultiplier);
					
					// Add lateral pressure component for confined spaces
					int32 SolidNeighbors = 0;
					for (int32 dx = -1; dx <= 1; ++dx)
					{
						for (int32 dy = -1; dy <= 1; ++dy)
						{
							if (dx == 0 && dy == 0) continue;
							
							const int32 NeighborIdx = GetCellIndex(x + dx, y + dy, z);
							if (NeighborIdx != -1 && Cells[NeighborIdx].bIsSolid)
							{
								SolidNeighbors++;
							}
						}
					}
					
					// Increase pressure in confined spaces
					if (SolidNeighbors > 4)
					{
						CurrentCell.HydrostaticPressure *= 1.0f + (SolidNeighbors * 0.1f);
					}
				}
				else
				{
					CurrentCell.HydrostaticPressure = 0.0f;
				}
			}
		}
	}
}

void UCAFluidGrid::ApplyUpwardPressureFlow(float DeltaTime)
{
	// Apply upward flow when pressure exceeds threshold
	for (int32 z = 0; z < GridSizeZ - 1; ++z)
	{
		for (int32 y = 0; y < GridSizeY; ++y)
		{
			for (int32 x = 0; x < GridSizeX; ++x)
			{
				const int32 CurrentIdx = GetCellIndex(x, y, z);
				const int32 AboveIdx = GetCellIndex(x, y, z + 1);
				
				if (CurrentIdx == -1 || AboveIdx == -1)
					continue;
				
				FCAFluidCell& CurrentCell = Cells[CurrentIdx];
				FCAFluidCell& AboveCell = Cells[AboveIdx];
				
				// Check for high pressure and available space above
				if (CurrentCell.HydrostaticPressure > MaxFluidLevel * 1.5f && 
					!AboveCell.bIsSolid &&
					CurrentCell.FluidLevel > MaxFluidLevel * 0.9f)
				{
					// Calculate upward flow based on pressure difference
					const float PressureDiff = CurrentCell.HydrostaticPressure - AboveCell.HydrostaticPressure;
					
					if (PressureDiff > 0)
					{
						const float SpaceAbove = MaxFluidLevel - AboveCell.FluidLevel;
						const float UpwardFlow = FMath::Min(
							PressureDiff * UpwardFlowMultiplier * DeltaTime,
							FMath::Min(CurrentCell.FluidLevel * 0.3f, SpaceAbove)
						);
						
						if (UpwardFlow > 0)
						{
							NextCells[CurrentIdx].FluidLevel -= UpwardFlow;
							NextCells[AboveIdx].FluidLevel += UpwardFlow;
							NextCells[AboveIdx].FlowVelocity.Z = UpwardFlow / DeltaTime;
						}
					}
				}
			}
		}
	}
}

void UCAFluidGrid::DetectAndMarkPools(float DeltaTime)
{
	// Reset pool markers
	for (FCAFluidCell& Cell : Cells)
	{
		Cell.bInStablePool = false;
		Cell.PoolID = -1;
	}
	
	int32 CurrentPoolID = 0;
	
	// Detect stable pool regions
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
				
				// Check if cell has settled fluid
				if (CurrentCell.FluidLevel > MinFluidLevel && 
					FMath::Abs(CurrentCell.FlowVelocity.Size()) < PoolStabilizationThreshold)
				{
					// Check neighbors for similar fluid levels (indicating a pool)
					float NeighborLevelSum = 0.0f;
					int32 FluidNeighborCount = 0;
					bool bHasSolidBelow = false;
					
					// Check cell below
					if (z > 0)
					{
						const int32 BelowIdx = GetCellIndex(x, y, z - 1);
						if (BelowIdx != -1)
						{
							const FCAFluidCell& BelowCell = Cells[BelowIdx];
							bHasSolidBelow = BelowCell.bIsSolid || BelowCell.FluidLevel >= MaxFluidLevel * 0.95f;
						}
					}
					else
					{
						bHasSolidBelow = true;
					}
					
					// Check horizontal neighbors
					for (int32 dx = -1; dx <= 1; ++dx)
					{
						for (int32 dy = -1; dy <= 1; ++dy)
						{
							if (dx == 0 && dy == 0) continue;
							
							const int32 NeighborIdx = GetCellIndex(x + dx, y + dy, z);
							if (NeighborIdx != -1)
							{
								const FCAFluidCell& NeighborCell = Cells[NeighborIdx];
								if (NeighborCell.FluidLevel > MinFluidLevel && !NeighborCell.bIsSolid)
								{
									NeighborLevelSum += NeighborCell.FluidLevel;
									FluidNeighborCount++;
								}
							}
						}
					}
					
					// Mark as stable pool if conditions are met
					if (bHasSolidBelow && FluidNeighborCount > 2)
					{
						const float AvgNeighborLevel = NeighborLevelSum / FluidNeighborCount;
						if (FMath::Abs(CurrentCell.FluidLevel - AvgNeighborLevel) < PoolStabilizationThreshold * 10.0f)
						{
							CurrentCell.bInStablePool = true;
							CurrentCell.PoolID = CurrentPoolID;
							CurrentCell.TargetLevel = (CurrentCell.FluidLevel + AvgNeighborLevel) * 0.5f;
							CurrentCell.SettledTime += DeltaTime;
						}
					}
				}
				else
				{
					CurrentCell.SettledTime = 0.0f;
				}
			}
		}
		
		CurrentPoolID++;
	}
}

void UCAFluidGrid::ApplyPressureEqualization(float DeltaTime)
{
	// Equalize pressure in detected pools for stable surfaces
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
				
				if (CurrentCell.bInStablePool && CurrentCell.SettledTime > 0.5f)
				{
					// Gradually equalize to target level
					const float LevelDiff = CurrentCell.TargetLevel - CurrentCell.FluidLevel;
					const float Adjustment = LevelDiff * 0.1f * DeltaTime;
					
					NextCells[CurrentIdx].FluidLevel += Adjustment;
					
					// Apply surface tension to smooth the surface
					if (CurrentCell.FluidLevel > MaxFluidLevel * 0.7f)
					{
						float SurfaceSmoothing = 0.0f;
						int32 SurfaceNeighbors = 0;
						
						for (int32 dx = -1; dx <= 1; ++dx)
						{
							for (int32 dy = -1; dy <= 1; ++dy)
							{
								if (dx == 0 && dy == 0) continue;
								
								const int32 NeighborIdx = GetCellIndex(x + dx, y + dy, z);
								if (NeighborIdx != -1)
								{
									const FCAFluidCell& NeighborCell = Cells[NeighborIdx];
									if (NeighborCell.FluidLevel > MinFluidLevel)
									{
										SurfaceSmoothing += NeighborCell.FluidLevel;
										SurfaceNeighbors++;
									}
								}
							}
						}
						
						if (SurfaceNeighbors > 0)
						{
							const float TargetSurface = SurfaceSmoothing / SurfaceNeighbors;
							const float SurfaceAdjustment = (TargetSurface - CurrentCell.FluidLevel) * SurfaceTension * DeltaTime;
							NextCells[CurrentIdx].FluidLevel += SurfaceAdjustment;
						}
					}
				}
			}
		}
	}
}

void UCAFluidGrid::ApplyDiagonalFlow(float DeltaTime)
{
	// Apply diagonal flow for more natural spreading
	const float DiagonalFlowRate = FlowRate * 0.7f * DeltaTime; // Slightly reduced for diagonals
	
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
				
				// Check diagonal neighbors
				const int32 DiagonalNeighbors[4][2] = {
					{x + 1, y + 1},
					{x + 1, y - 1},
					{x - 1, y + 1},
					{x - 1, y - 1}
				};
				
				for (int32 i = 0; i < 4; ++i)
				{
					const int32 nx = DiagonalNeighbors[i][0];
					const int32 ny = DiagonalNeighbors[i][1];
					
					if (IsValidCell(nx, ny, z))
					{
						const int32 NeighborIdx = GetCellIndex(nx, ny, z);
						FCAFluidCell& NeighborCell = Cells[NeighborIdx];
						
						if (!NeighborCell.bIsSolid)
						{
							const float HeightDiff = (CurrentCell.TerrainHeight + CurrentCell.FluidLevel) - 
													 (NeighborCell.TerrainHeight + NeighborCell.FluidLevel);
							
							if (HeightDiff > 0.01f)
							{
								const float PossibleFlow = FMath::Min(
									CurrentCell.FluidLevel * DiagonalFlowRate,
									HeightDiff * 0.3f
								);
								
								const float SpaceInNeighbor = MaxFluidLevel - NeighborCell.FluidLevel;
								const float ActualFlow = FMath::Min(PossibleFlow, SpaceInNeighbor);
								
								if (ActualFlow > 0)
								{
									NextCells[CurrentIdx].FluidLevel -= ActualFlow;
									NextCells[NeighborIdx].FluidLevel += ActualFlow;
								}
							}
						}
					}
				}
			}
		}
	}
}*/
