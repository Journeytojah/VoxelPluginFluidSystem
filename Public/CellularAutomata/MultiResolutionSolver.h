#pragma once

#include "CoreMinimal.h"
#include "CellularAutomata/CAFluidGrid.h"

/**
 * Multi-resolution pressure solver for fluid simulation
 * Solves pressure at lower resolution for performance, then interpolates
 */
class VOXELFLUIDSYSTEM_API FMultiResolutionSolver
{
public:
	/**
	 * Solve pressure using multi-resolution approach
	 * @param FluidGrid - The fluid grid to solve
	 * @param ResolutionDivisor - Resolution reduction factor (2 = half resolution, 4 = quarter)
	 * @param Iterations - Number of pressure solver iterations
	 * @param DeltaTime - Simulation time step
	 */
	static void SolvePressureMultiRes(
		class UCAFluidGrid* FluidGrid,
		int32 ResolutionDivisor = 2,
		int32 Iterations = 4,
		float DeltaTime = 0.016f
	);
	
	/**
	 * Solve pressure for a chunk using multi-resolution
	 */
	static void SolveChunkPressure(
		class UFluidChunk* Chunk,
		int32 ResolutionDivisor = 2,
		int32 Iterations = 4
	);
	
private:
	// Downsample fluid data to lower resolution
	static void DownsampleFluidData(
		const TArray<FCAFluidCell>& HighResCells,
		TArray<float>& LowResFluid,
		int32 HighResX, int32 HighResY, int32 HighResZ,
		int32 LowResX, int32 LowResY, int32 LowResZ
	);
	
	// Upsample pressure solution back to high resolution
	static void UpsamplePressure(
		const TArray<float>& LowResPressure,
		TArray<float>& HighResPressure,
		int32 LowResX, int32 LowResY, int32 LowResZ,
		int32 HighResX, int32 HighResY, int32 HighResZ
	);
	
	// Core pressure solver at given resolution
	static void SolvePressureCore(
		TArray<float>& FluidLevels,
		TArray<float>& Pressure,
		int32 SizeX, int32 SizeY, int32 SizeZ,
		int32 Iterations
	);
	
	// Apply pressure to velocity field
	static void ApplyPressureToVelocity(
		TArray<FCAFluidCell>& Cells,
		const TArray<float>& Pressure,
		int32 SizeX, int32 SizeY, int32 SizeZ,
		float DeltaTime
	);
	
	// Trilinear interpolation for upsampling
	static float TrilinearInterpolate(
		const TArray<float>& Data,
		float x, float y, float z,
		int32 SizeX, int32 SizeY, int32 SizeZ
	);
};

/**
 * Multigrid solver for even better performance
 * Uses multiple resolution levels for faster convergence
 */
class VOXELFLUIDSYSTEM_API FMultigridSolver
{
public:
	struct FMultigridLevel
	{
		int32 SizeX, SizeY, SizeZ;
		TArray<float> FluidLevels;
		TArray<float> Pressure;
		TArray<float> Residual;
		TArray<float> RightHandSide;
	};
	
	/**
	 * Solve using V-cycle multigrid method
	 * O(n) complexity vs O(n²) for standard iterative methods
	 */
	static void SolveMultigrid(
		class UCAFluidGrid* FluidGrid,
		int32 NumLevels = 3,
		int32 PreSmoothIterations = 2,
		int32 PostSmoothIterations = 2,
		float DeltaTime = 0.016f
	);
	
private:
	// Restriction operator (fine to coarse)
	static void Restrict(
		const FMultigridLevel& FineLevel,
		FMultigridLevel& CoarseLevel
	);
	
	// Prolongation operator (coarse to fine)
	static void Prolongate(
		const FMultigridLevel& CoarseLevel,
		FMultigridLevel& FineLevel
	);
	
	// Smooth using Gauss-Seidel iteration
	static void GaussSeidel(
		FMultigridLevel& Level,
		int32 Iterations
	);
	
	// Compute residual
	static float ComputeResidual(
		const FMultigridLevel& Level
	);
	
	// V-cycle recursive function
	static void VCycle(
		TArray<FMultigridLevel>& Levels,
		int32 CurrentLevel,
		int32 PreSmooth,
		int32 PostSmooth
	);
};

/**
 * Sparse grid representation for memory efficiency
 * Only stores and processes cells with fluid
 */
class VOXELFLUIDSYSTEM_API FSparseFluidGrid
{
public:
	struct FSparseCell
	{
		int32 Index;  // Linear index in full grid
		float FluidLevel;
		float Pressure;
		FVector Velocity;
		bool bSettled;
		
		FSparseCell() : Index(-1), FluidLevel(0.0f), Pressure(0.0f), Velocity(FVector::ZeroVector), bSettled(false) {}
	};
	
	/**
	 * Convert regular grid to sparse representation
	 */
	static void ConvertToSparse(
		const TArray<FCAFluidCell>& DenseCells,
		TArray<FSparseCell>& SparseCells,
		TMap<int32, int32>& IndexMap,  // Dense index -> Sparse index
		float MinFluidLevel = 0.001f
	);
	
	/**
	 * Convert sparse grid back to dense
	 */
	static void ConvertToDense(
		const TArray<FSparseCell>& SparseCells,
		TArray<FCAFluidCell>& DenseCells,
		int32 TotalCells
	);
	
	/**
	 * Update only active cells in sparse format
	 */
	static void UpdateSparse(
		TArray<FSparseCell>& SparseCells,
		const TMap<int32, int32>& IndexMap,
		int32 GridSizeX, int32 GridSizeY, int32 GridSizeZ,
		float DeltaTime
	);
	
	/**
	 * Get memory savings percentage
	 */
	static float GetCompressionRatio(
		int32 SparseCellCount,
		int32 TotalCellCount
	);
	
private:
	// Get neighbor indices in sparse format
	static void GetSparseNeighbors(
		int32 CellIndex,
		const TMap<int32, int32>& IndexMap,
		int32 GridSizeX, int32 GridSizeY, int32 GridSizeZ,
		TArray<int32>& OutNeighborIndices
	);
	
	// Process gravity for sparse cells
	static void ProcessSparseGravity(
		TArray<FSparseCell>& SparseCells,
		const TMap<int32, int32>& IndexMap,
		int32 GridSizeX, int32 GridSizeY, int32 GridSizeZ,
		float MaxFluidLevel
	);
	
	// Process flow between sparse cells
	static void ProcessSparseFlow(
		TArray<FSparseCell>& SparseCells,
		const TMap<int32, int32>& IndexMap,
		int32 GridSizeX, int32 GridSizeY, int32 GridSizeZ,
		float FlowRate
	);
};