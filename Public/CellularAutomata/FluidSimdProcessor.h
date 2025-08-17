#pragma once

#include "CoreMinimal.h"
#include "CellularAutomata/CAFluidGrid.h"

/**
 * Parameters for fluid simulation
 */
struct FFluidSimulationParams
{
	float MinFluidLevel = 0.001f;
	float MaxFluidLevel = 1.0f;
	float FlowRate = 0.25f;
	float EqualizationRate = 0.5f;
	float SettlingThreshold = 0.0001f;
	int32 SettlingFrames = 5;
	float DeltaTime = 0.016f;
	bool bEnableSettling = true;
	bool bUseSleepChains = true;
	bool bUsePredictiveSettling = true;
};

/**
 * SIMD-optimized fluid processing utilities
 * Processes multiple cells simultaneously using SSE/AVX instructions
 */
class VOXELFLUIDSYSTEM_API FFluidSimdProcessor
{
public:
	// Process 4 cells at once using SSE
	static void ProcessGravitySimd4(
		float* FluidLevels,
		const uint8* Flags,
		int32 StartIdx,
		int32 GridSizeX,
		int32 GridSizeY,
		int32 GridSizeZ,
		float MinFluidLevel,
		float MaxFluidLevel
	);
	
	// Process 8 cells at once using AVX (if available)
	static void ProcessGravitySimd8(
		float* FluidLevels,
		const uint8* Flags,
		int32 StartIdx,
		int32 GridSizeX,
		int32 GridSizeY,
		int32 GridSizeZ,
		float MinFluidLevel,
		float MaxFluidLevel
	);
	
	// Process horizontal flow for 4 cells simultaneously
	static void ProcessFlowSimd4(
		float* FluidLevels,
		const uint8* Flags,
		int32 StartIdx,
		int32 GridSizeX,
		int32 GridSizeY,
		int32 GridSizeZ,
		float FlowRate,
		float MinFluidLevel
	);
	
	// Batch process settling detection
	static void UpdateSettlingSimd(
		const float* FluidLevels,
		const float* LastFluidLevels,
		uint8* SettledFlags,
		int32* SettledCounters,
		int32 StartIdx,
		int32 Count,
		float SettlingThreshold,
		int32 RequiredFrames
	);
	
	// Check if SIMD is available on this platform
	static bool IsSimdAvailable();
	static bool IsAvxAvailable();
	
	// Process cells in batches optimized for cache
	static void ProcessCellBatch(
		FCAFluidCell* Cells,
		FCAFluidCell* NextCells,
		int32 StartIdx,
		int32 BatchSize,
		const FFluidSimulationParams& Params
	);
	
	// Optimized memory copy for cell data
	static void CopyCellsSimd(
		FCAFluidCell* Dest,
		const FCAFluidCell* Source,
		int32 Count
	);
	
	// Fast clear for cell arrays
	static void ClearCellsSimd(
		FCAFluidCell* Cells,
		int32 Count
	);
};

/**
 * Parallel chunk processor with SIMD optimization
 */
class VOXELFLUIDSYSTEM_API FParallelFluidProcessor
{
public:
	// Process multiple chunks in parallel with optimal work distribution
	static void ProcessChunksParallel(
		TArray<class UFluidChunk*>& Chunks,
		float DeltaTime,
		int32 NumThreads = 0  // 0 = auto-detect
	);
	
	// Process a single grid with parallel+SIMD optimization
	static void ProcessGridParallel(
		class UCAFluidGrid* Grid,
		float DeltaTime,
		int32 NumThreads = 0
	);
	
	// Optimized border synchronization between chunks
	static void SynchronizeBordersParallel(
		TArray<class UFluidChunk*>& Chunks,
		const TMap<FFluidChunkCoord, class UFluidChunk*>& ChunkMap
	);
	
	// Get optimal batch size for current CPU
	static int32 GetOptimalBatchSize();
	
	// Get optimal thread count for fluid processing
	static int32 GetOptimalThreadCount();
	
private:
	// Worker function for parallel chunk processing
	static void ProcessChunkBatch(
		TArray<class UFluidChunk*>& Chunks,
		int32 StartIdx,
		int32 EndIdx,
		float DeltaTime
	);
	
	// Worker function for parallel grid processing
	static void ProcessGridSection(
		class UCAFluidGrid* Grid,
		int32 StartZ,
		int32 EndZ,
		float DeltaTime
	);
};