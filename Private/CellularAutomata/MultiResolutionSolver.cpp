#include "CellularAutomata/MultiResolutionSolver.h"
#include "CellularAutomata/CAFluidGrid.h"
#include "CellularAutomata/FluidChunk.h"
#include "VoxelFluidStats.h"
#include "Async/ParallelFor.h"

void FMultiResolutionSolver::SolvePressureMultiRes(
	UCAFluidGrid* FluidGrid,
	int32 ResolutionDivisor,
	int32 Iterations,
	float DeltaTime)
{
	if (!FluidGrid || ResolutionDivisor < 1)
		return;
	
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_UpdateSimulation);
	
	const int32 HighResX = FluidGrid->GridSizeX;
	const int32 HighResY = FluidGrid->GridSizeY;
	const int32 HighResZ = FluidGrid->GridSizeZ;
	
	// Calculate low resolution dimensions
	const int32 LowResX = FMath::Max(1, HighResX / ResolutionDivisor);
	const int32 LowResY = FMath::Max(1, HighResY / ResolutionDivisor);
	const int32 LowResZ = FMath::Max(1, HighResZ / ResolutionDivisor);
	
	// Allocate low resolution arrays
	TArray<float> LowResFluid;
	TArray<float> LowResPressure;
	LowResFluid.SetNum(LowResX * LowResY * LowResZ);
	LowResPressure.SetNum(LowResX * LowResY * LowResZ);
	
	// Downsample fluid data
	DownsampleFluidData(FluidGrid->Cells, LowResFluid, 
		HighResX, HighResY, HighResZ,
		LowResX, LowResY, LowResZ);
	
	// Solve pressure at low resolution
	SolvePressureCore(LowResFluid, LowResPressure,
		LowResX, LowResY, LowResZ, Iterations);
	
	// Upsample pressure back to high resolution
	TArray<float> HighResPressure;
	HighResPressure.SetNum(HighResX * HighResY * HighResZ);
	UpsamplePressure(LowResPressure, HighResPressure,
		LowResX, LowResY, LowResZ,
		HighResX, HighResY, HighResZ);
	
	// Apply pressure to velocity
	ApplyPressureToVelocity(FluidGrid->Cells, HighResPressure,
		HighResX, HighResY, HighResZ, DeltaTime);
}

void FMultiResolutionSolver::SolveChunkPressure(
	UFluidChunk* Chunk,
	int32 ResolutionDivisor,
	int32 Iterations)
{
	if (!Chunk || ResolutionDivisor < 1)
		return;
	
	const int32 ChunkSize = Chunk->ChunkSize;
	const int32 LowResSize = FMath::Max(1, ChunkSize / ResolutionDivisor);
	
	// Allocate low resolution arrays
	TArray<float> LowResFluid;
	TArray<float> LowResPressure;
	LowResFluid.SetNum(LowResSize * LowResSize * LowResSize);
	LowResPressure.SetNum(LowResSize * LowResSize * LowResSize);
	
	// Downsample
	DownsampleFluidData(Chunk->Cells, LowResFluid,
		ChunkSize, ChunkSize, ChunkSize,
		LowResSize, LowResSize, LowResSize);
	
	// Solve
	SolvePressureCore(LowResFluid, LowResPressure,
		LowResSize, LowResSize, LowResSize, Iterations);
	
	// Upsample
	TArray<float> HighResPressure;
	HighResPressure.SetNum(ChunkSize * ChunkSize * ChunkSize);
	UpsamplePressure(LowResPressure, HighResPressure,
		LowResSize, LowResSize, LowResSize,
		ChunkSize, ChunkSize, ChunkSize);
	
	// Apply pressure
	ApplyPressureToVelocity(Chunk->Cells, HighResPressure,
		ChunkSize, ChunkSize, ChunkSize, 0.016f);
}

void FMultiResolutionSolver::DownsampleFluidData(
	const TArray<FCAFluidCell>& HighResCells,
	TArray<float>& LowResFluid,
	int32 HighResX, int32 HighResY, int32 HighResZ,
	int32 LowResX, int32 LowResY, int32 LowResZ)
{
	const float ScaleX = (float)HighResX / LowResX;
	const float ScaleY = (float)HighResY / LowResY;
	const float ScaleZ = (float)HighResZ / LowResZ;
	
	// Parallel downsample
	ParallelFor(LowResZ, [&](int32 lz)
	{
		for (int32 ly = 0; ly < LowResY; ++ly)
		{
			for (int32 lx = 0; lx < LowResX; ++lx)
			{
				// Calculate corresponding high-res region
				const int32 StartX = FMath::FloorToInt(lx * ScaleX);
				const int32 StartY = FMath::FloorToInt(ly * ScaleY);
				const int32 StartZ = FMath::FloorToInt(lz * ScaleZ);
				const int32 EndX = FMath::Min(HighResX, FMath::CeilToInt((lx + 1) * ScaleX));
				const int32 EndY = FMath::Min(HighResY, FMath::CeilToInt((ly + 1) * ScaleY));
				const int32 EndZ = FMath::Min(HighResZ, FMath::CeilToInt((lz + 1) * ScaleZ));
				
				// Average fluid levels in the region
				float TotalFluid = 0.0f;
				int32 Count = 0;
				
				for (int32 hz = StartZ; hz < EndZ; ++hz)
				{
					for (int32 hy = StartY; hy < EndY; ++hy)
					{
						for (int32 hx = StartX; hx < EndX; ++hx)
						{
							const int32 HighResIdx = hx + hy * HighResX + hz * HighResX * HighResY;
							if (HighResIdx < HighResCells.Num())
							{
								TotalFluid += HighResCells[HighResIdx].FluidLevel;
								Count++;
							}
						}
					}
				}
				
				const int32 LowResIdx = lx + ly * LowResX + lz * LowResX * LowResY;
				LowResFluid[LowResIdx] = Count > 0 ? TotalFluid / Count : 0.0f;
			}
		}
	});
}

void FMultiResolutionSolver::UpsamplePressure(
	const TArray<float>& LowResPressure,
	TArray<float>& HighResPressure,
	int32 LowResX, int32 LowResY, int32 LowResZ,
	int32 HighResX, int32 HighResY, int32 HighResZ)
{
	const float ScaleX = (float)LowResX / HighResX;
	const float ScaleY = (float)LowResY / HighResY;
	const float ScaleZ = (float)LowResZ / HighResZ;
	
	// Parallel upsample using trilinear interpolation
	ParallelFor(HighResZ, [&](int32 hz)
	{
		for (int32 hy = 0; hy < HighResY; ++hy)
		{
			for (int32 hx = 0; hx < HighResX; ++hx)
			{
				// Find position in low-res grid
				const float LowX = hx * ScaleX;
				const float LowY = hy * ScaleY;
				const float LowZ = hz * ScaleZ;
				
				// Trilinear interpolation
				const int32 HighResIdx = hx + hy * HighResX + hz * HighResX * HighResY;
				HighResPressure[HighResIdx] = TrilinearInterpolate(
					LowResPressure, LowX, LowY, LowZ,
					LowResX, LowResY, LowResZ);
			}
		}
	});
}

void FMultiResolutionSolver::SolvePressureCore(
	TArray<float>& FluidLevels,
	TArray<float>& Pressure,
	int32 SizeX, int32 SizeY, int32 SizeZ,
	int32 Iterations)
{
	// Simple Jacobi iteration for pressure solve
	// In production, would use more sophisticated method
	
	TArray<float> PressureNew;
	PressureNew.SetNum(Pressure.Num());
	
	for (int32 Iter = 0; Iter < Iterations; ++Iter)
	{
		ParallelFor(SizeZ, [&](int32 z)
		{
			for (int32 y = 0; y < SizeY; ++y)
			{
				for (int32 x = 0; x < SizeX; ++x)
				{
					const int32 Idx = x + y * SizeX + z * SizeX * SizeY;
					
					// Skip cells with no fluid
					if (FluidLevels[Idx] < 0.001f)
					{
						PressureNew[Idx] = 0.0f;
						continue;
					}
					
					// Average pressure from neighbors
					float PressureSum = 0.0f;
					int32 NeighborCount = 0;
					
					// Check all 6 neighbors
					if (x > 0)
					{
						PressureSum += Pressure[Idx - 1];
						NeighborCount++;
					}
					if (x < SizeX - 1)
					{
						PressureSum += Pressure[Idx + 1];
						NeighborCount++;
					}
					if (y > 0)
					{
						PressureSum += Pressure[Idx - SizeX];
						NeighborCount++;
					}
					if (y < SizeY - 1)
					{
						PressureSum += Pressure[Idx + SizeX];
						NeighborCount++;
					}
					if (z > 0)
					{
						PressureSum += Pressure[Idx - SizeX * SizeY];
						NeighborCount++;
					}
					if (z < SizeZ - 1)
					{
						PressureSum += Pressure[Idx + SizeX * SizeY];
						NeighborCount++;
					}
					
					// Update pressure
					if (NeighborCount > 0)
					{
						// Add divergence correction based on fluid level
						const float Divergence = FluidLevels[Idx] - 1.0f;
						PressureNew[Idx] = (PressureSum / NeighborCount) + Divergence * 0.1f;
					}
					else
					{
						PressureNew[Idx] = 0.0f;
					}
				}
			}
		});
		
		// Swap buffers
		Swap(Pressure, PressureNew);
	}
}

void FMultiResolutionSolver::ApplyPressureToVelocity(
	TArray<FCAFluidCell>& Cells,
	const TArray<float>& Pressure,
	int32 SizeX, int32 SizeY, int32 SizeZ,
	float DeltaTime)
{
	// Apply pressure gradient to fluid flow
	ParallelFor(Cells.Num(), [&](int32 Idx)
	{
		if (Cells[Idx].FluidLevel < 0.001f)
			return;
		
		const int32 x = Idx % SizeX;
		const int32 y = (Idx / SizeX) % SizeY;
		const int32 z = Idx / (SizeX * SizeY);
		
		// Calculate pressure gradient
		float GradX = 0.0f, GradY = 0.0f, GradZ = 0.0f;
		
		if (x > 0 && x < SizeX - 1)
			GradX = (Pressure[Idx + 1] - Pressure[Idx - 1]) * 0.5f;
		if (y > 0 && y < SizeY - 1)
			GradY = (Pressure[Idx + SizeX] - Pressure[Idx - SizeX]) * 0.5f;
		if (z > 0 && z < SizeZ - 1)
			GradZ = (Pressure[Idx + SizeX * SizeY] - Pressure[Idx - SizeX * SizeY]) * 0.5f;
		
		// Apply to velocity (simplified - would normally update velocity field)
		// Pressure pushes fluid from high to low pressure
		const float PressureForce = 0.1f; // Tunable parameter
		
		// Modify fluid level based on pressure gradient
		// This is simplified - in reality would update velocity field
		Cells[Idx].FluidLevel -= (GradX + GradY + GradZ) * PressureForce * DeltaTime;
		Cells[Idx].FluidLevel = FMath::Clamp(Cells[Idx].FluidLevel, 0.0f, 1.0f);
	});
}

float FMultiResolutionSolver::TrilinearInterpolate(
	const TArray<float>& Data,
	float x, float y, float z,
	int32 SizeX, int32 SizeY, int32 SizeZ)
{
	// Clamp coordinates
	x = FMath::Clamp(x, 0.0f, SizeX - 1.001f);
	y = FMath::Clamp(y, 0.0f, SizeY - 1.001f);
	z = FMath::Clamp(z, 0.0f, SizeZ - 1.001f);
	
	// Get integer coordinates
	const int32 x0 = FMath::FloorToInt(x);
	const int32 y0 = FMath::FloorToInt(y);
	const int32 z0 = FMath::FloorToInt(z);
	const int32 x1 = FMath::Min(x0 + 1, SizeX - 1);
	const int32 y1 = FMath::Min(y0 + 1, SizeY - 1);
	const int32 z1 = FMath::Min(z0 + 1, SizeZ - 1);
	
	// Get fractional parts
	const float fx = x - x0;
	const float fy = y - y0;
	const float fz = z - z0;
	
	// Get values at corners
	const float V000 = Data[x0 + y0 * SizeX + z0 * SizeX * SizeY];
	const float V100 = Data[x1 + y0 * SizeX + z0 * SizeX * SizeY];
	const float V010 = Data[x0 + y1 * SizeX + z0 * SizeX * SizeY];
	const float V110 = Data[x1 + y1 * SizeX + z0 * SizeX * SizeY];
	const float V001 = Data[x0 + y0 * SizeX + z1 * SizeX * SizeY];
	const float V101 = Data[x1 + y0 * SizeX + z1 * SizeX * SizeY];
	const float V011 = Data[x0 + y1 * SizeX + z1 * SizeX * SizeY];
	const float V111 = Data[x1 + y1 * SizeX + z1 * SizeX * SizeY];
	
	// Trilinear interpolation
	const float V00 = FMath::Lerp(V000, V100, fx);
	const float V10 = FMath::Lerp(V010, V110, fx);
	const float V01 = FMath::Lerp(V001, V101, fx);
	const float V11 = FMath::Lerp(V011, V111, fx);
	const float V0 = FMath::Lerp(V00, V10, fy);
	const float V1 = FMath::Lerp(V01, V11, fy);
	
	return FMath::Lerp(V0, V1, fz);
}

// Sparse Grid Implementation
void FSparseFluidGrid::ConvertToSparse(
	const TArray<FCAFluidCell>& DenseCells,
	TArray<FSparseCell>& SparseCells,
	TMap<int32, int32>& IndexMap,
	float MinFluidLevel)
{
	SparseCells.Empty();
	IndexMap.Empty();
	
	// Find all cells with fluid
	for (int32 i = 0; i < DenseCells.Num(); ++i)
	{
		if (DenseCells[i].FluidLevel > MinFluidLevel)
		{
			FSparseCell SparseCell;
			SparseCell.Index = i;
			SparseCell.FluidLevel = DenseCells[i].FluidLevel;
			SparseCell.Pressure = 0.0f;  // CA fluid doesn't use pressure
			SparseCell.bSettled = DenseCells[i].bSettled;
			
			const int32 SparseIdx = SparseCells.Add(SparseCell);
			IndexMap.Add(i, SparseIdx);
		}
	}
	
}

void FSparseFluidGrid::ConvertToDense(
	const TArray<FSparseCell>& SparseCells,
	TArray<FCAFluidCell>& DenseCells,
	int32 TotalCells)
{
	// Clear dense array
	for (int32 i = 0; i < TotalCells; ++i)
	{
		DenseCells[i].FluidLevel = 0.0f;
		// CA fluid doesn't have pressure field
	}
	
	// Copy sparse cells back
	for (const FSparseCell& SparseCell : SparseCells)
	{
		if (SparseCell.Index >= 0 && SparseCell.Index < TotalCells)
		{
			DenseCells[SparseCell.Index].FluidLevel = SparseCell.FluidLevel;
			// Pressure is not used in CA fluid simulation
			DenseCells[SparseCell.Index].bSettled = SparseCell.bSettled;
		}
	}
}

void FSparseFluidGrid::UpdateSparse(
	TArray<FSparseCell>& SparseCells,
	const TMap<int32, int32>& IndexMap,
	int32 GridSizeX, int32 GridSizeY, int32 GridSizeZ,
	float DeltaTime)
{
	// Process gravity for sparse cells
	ProcessSparseGravity(SparseCells, IndexMap, GridSizeX, GridSizeY, GridSizeZ, 1.0f);
	
	// Process flow between sparse cells
	ProcessSparseFlow(SparseCells, IndexMap, GridSizeX, GridSizeY, GridSizeZ, 0.25f);
}

float FSparseFluidGrid::GetCompressionRatio(
	int32 SparseCellCount,
	int32 TotalCellCount)
{
	if (TotalCellCount == 0)
		return 0.0f;
	
	const float Ratio = 1.0f - ((float)SparseCellCount / TotalCellCount);
	return Ratio * 100.0f; // Return as percentage
}

void FSparseFluidGrid::ProcessSparseGravity(
	TArray<FSparseCell>& SparseCells,
	const TMap<int32, int32>& IndexMap,
	int32 GridSizeX, int32 GridSizeY, int32 GridSizeZ,
	float MaxFluidLevel)
{
	// Process gravity only for cells with fluid
	TArray<FSparseCell> NextCells = SparseCells;
	
	for (int32 i = 0; i < SparseCells.Num(); ++i)
	{
		const FSparseCell& Cell = SparseCells[i];
		if (Cell.FluidLevel < 0.001f)
			continue;
		
		// Get cell coordinates
		const int32 x = Cell.Index % GridSizeX;
		const int32 y = (Cell.Index / GridSizeX) % GridSizeY;
		const int32 z = Cell.Index / (GridSizeX * GridSizeY);
		
		// Check cell below
		if (z > 0)
		{
			const int32 BelowIdx = Cell.Index - GridSizeX * GridSizeY;
			
			// Check if below cell exists in sparse set
			if (const int32* SparseBelowIdx = IndexMap.Find(BelowIdx))
			{
				FSparseCell& BelowCell = NextCells[*SparseBelowIdx];
				const float SpaceBelow = MaxFluidLevel - BelowCell.FluidLevel;
				
				if (SpaceBelow > 0.001f)
				{
					const float Transfer = FMath::Min(Cell.FluidLevel, SpaceBelow);
					NextCells[i].FluidLevel -= Transfer;
					BelowCell.FluidLevel += Transfer;
				}
			}
			else if (Cell.FluidLevel > 0.001f)
			{
				// Create new sparse cell below if fluid flows there
				FSparseCell NewCell;
				NewCell.Index = BelowIdx;
				NewCell.FluidLevel = FMath::Min(Cell.FluidLevel, MaxFluidLevel);
				NextCells[i].FluidLevel -= NewCell.FluidLevel;
				NextCells.Add(NewCell);
			}
		}
	}
	
	SparseCells = NextCells;
}

void FSparseFluidGrid::ProcessSparseFlow(
	TArray<FSparseCell>& SparseCells,
	const TMap<int32, int32>& IndexMap,
	int32 GridSizeX, int32 GridSizeY, int32 GridSizeZ,
	float FlowRate)
{
	// Simplified horizontal flow for sparse cells
	TArray<FSparseCell> NextCells = SparseCells;
	
	for (int32 i = 0; i < SparseCells.Num(); ++i)
	{
		const FSparseCell& Cell = SparseCells[i];
		if (Cell.FluidLevel < 0.01f)
			continue;
		
		// Get neighbors
		TArray<int32> NeighborIndices;
		GetSparseNeighbors(Cell.Index, IndexMap, GridSizeX, GridSizeY, GridSizeZ, NeighborIndices);
		
		if (NeighborIndices.Num() > 0)
		{
			const float FlowAmount = Cell.FluidLevel * FlowRate / (NeighborIndices.Num() + 1);
			
			for (int32 NeighborSparseIdx : NeighborIndices)
			{
				if (NeighborSparseIdx >= 0 && NeighborSparseIdx < NextCells.Num())
				{
					NextCells[NeighborSparseIdx].FluidLevel += FlowAmount;
					NextCells[i].FluidLevel -= FlowAmount;
				}
			}
		}
	}
	
	SparseCells = NextCells;
}

void FSparseFluidGrid::GetSparseNeighbors(
	int32 CellIndex,
	const TMap<int32, int32>& IndexMap,
	int32 GridSizeX, int32 GridSizeY, int32 GridSizeZ,
	TArray<int32>& OutNeighborIndices)
{
	OutNeighborIndices.Empty();
	
	const int32 x = CellIndex % GridSizeX;
	const int32 y = (CellIndex / GridSizeX) % GridSizeY;
	const int32 z = CellIndex / (GridSizeX * GridSizeY);
	
	// Check 4 horizontal neighbors
	if (x > 0)
	{
		if (const int32* SparseIdx = IndexMap.Find(CellIndex - 1))
			OutNeighborIndices.Add(*SparseIdx);
	}
	if (x < GridSizeX - 1)
	{
		if (const int32* SparseIdx = IndexMap.Find(CellIndex + 1))
			OutNeighborIndices.Add(*SparseIdx);
	}
	if (y > 0)
	{
		if (const int32* SparseIdx = IndexMap.Find(CellIndex - GridSizeX))
			OutNeighborIndices.Add(*SparseIdx);
	}
	if (y < GridSizeY - 1)
	{
		if (const int32* SparseIdx = IndexMap.Find(CellIndex + GridSizeX))
			OutNeighborIndices.Add(*SparseIdx);
	}
}