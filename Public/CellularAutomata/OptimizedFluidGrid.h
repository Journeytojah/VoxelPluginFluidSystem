#pragma once

#include "CoreMinimal.h"
#include "OptimizedFluidGrid.generated.h"

// Compressed fluid cell - 4 bytes instead of 44 bytes
struct FCompressedFluidCellSoA
{
	uint16 FluidLevel;      // Quantized fluid level (0-65535 maps to 0.0-1.0)
	uint8 Flags;            // Bit 0-2: bIsSolid, bSettled, bSourceBlock
	uint8 SettledCounter;   // 0-255 counter for settling
	
	FCompressedFluidCellSoA()
	{
		FluidLevel = 0;
		Flags = 0;
		SettledCounter = 0;
	}
	
	void SetFluidLevel(float Level)
	{
		FluidLevel = (uint16)(FMath::Clamp(Level, 0.0f, 1.0f) * 65535.0f);
	}
	
	float GetFluidLevel() const
	{
		return (float)FluidLevel / 65535.0f;
	}
	
	void SetSolid(bool bSolid)
	{
		if (bSolid)
			Flags |= 0x01;
		else
			Flags &= ~0x01;
	}
	
	bool IsSolid() const
	{
		return (Flags & 0x01) != 0;
	}
	
	void SetSettled(bool bSettled)
	{
		if (bSettled)
			Flags |= 0x02;
		else
			Flags &= ~0x02;
	}
	
	bool IsSettled() const
	{
		return (Flags & 0x02) != 0;
	}
	
	void SetSourceBlock(bool bSource)
	{
		if (bSource)
			Flags |= 0x04;
		else
			Flags &= ~0x04;
	}
	
	bool IsSourceBlock() const
	{
		return (Flags & 0x04) != 0;
	}
};

// Structure of Arrays layout for better cache performance
USTRUCT()
struct FFluidGridSoA
{
	GENERATED_BODY()
	
	// Primary data - hot path
	TArray<uint16> FluidLevels;        // 2 bytes per cell
	TArray<uint8> Flags;               // 1 byte per cell (packed flags)
	
	// Secondary data - cold path
	TArray<uint8> SettledCounters;     // 1 byte per cell
	TArray<uint16> TerrainHeights;     // 2 bytes per cell (quantized)
	
	// Change tracking for predictive settling
	TArray<uint8> ChangeHistory;       // Compressed change history
	
	int32 GridSizeX = 0;
	int32 GridSizeY = 0;
	int32 GridSizeZ = 0;
	
	void Initialize(int32 SizeX, int32 SizeY, int32 SizeZ)
	{
		GridSizeX = SizeX;
		GridSizeY = SizeY;
		GridSizeZ = SizeZ;
		
		const int32 TotalCells = SizeX * SizeY * SizeZ;
		
		FluidLevels.SetNum(TotalCells);
		Flags.SetNum(TotalCells);
		SettledCounters.SetNum(TotalCells);
		TerrainHeights.SetNum(TotalCells);
		ChangeHistory.SetNum(TotalCells * 3); // 3 frames of history
		
		// Initialize all to zero
		FMemory::Memzero(FluidLevels.GetData(), FluidLevels.Num() * sizeof(uint16));
		FMemory::Memzero(Flags.GetData(), Flags.Num());
		FMemory::Memzero(SettledCounters.GetData(), SettledCounters.Num());
		FMemory::Memzero(TerrainHeights.GetData(), TerrainHeights.Num() * sizeof(uint16));
		FMemory::Memzero(ChangeHistory.GetData(), ChangeHistory.Num());
	}
	
	FORCEINLINE float GetFluidLevel(int32 Index) const
	{
		return (float)FluidLevels[Index] / 65535.0f;
	}
	
	FORCEINLINE void SetFluidLevel(int32 Index, float Level)
	{
		FluidLevels[Index] = (uint16)(FMath::Clamp(Level, 0.0f, 1.0f) * 65535.0f);
	}
	
	FORCEINLINE bool IsSolid(int32 Index) const
	{
		return (Flags[Index] & 0x01) != 0;
	}
	
	FORCEINLINE void SetSolid(int32 Index, bool bSolid)
	{
		if (bSolid)
			Flags[Index] |= 0x01;
		else
			Flags[Index] &= ~0x01;
	}
	
	FORCEINLINE bool IsSettled(int32 Index) const
	{
		return (Flags[Index] & 0x02) != 0;
	}
	
	FORCEINLINE void SetSettled(int32 Index, bool bSettled)
	{
		if (bSettled)
			Flags[Index] |= 0x02;
		else
			Flags[Index] &= ~0x02;
	}
	
	FORCEINLINE bool IsSourceBlock(int32 Index) const
	{
		return (Flags[Index] & 0x04) != 0;
	}
	
	FORCEINLINE void SetSourceBlock(int32 Index, bool bSource)
	{
		if (bSource)
			Flags[Index] |= 0x04;
		else
			Flags[Index] &= ~0x04;
	}
	
	// Morton encoding for Z-order curve (improves cache locality)
	FORCEINLINE int32 GetMortonIndex(int32 X, int32 Y, int32 Z) const
	{
		// For simplicity, using linear index here, but can be replaced with Morton encoding
		return X + Y * GridSizeX + Z * GridSizeX * GridSizeY;
	}
	
	int32 GetMemorySize() const
	{
		return FluidLevels.Num() * sizeof(uint16) +
			   Flags.Num() +
			   SettledCounters.Num() +
			   TerrainHeights.Num() * sizeof(uint16) +
			   ChangeHistory.Num();
	}
};

// Optimized chunk with memory pooling
USTRUCT()
struct FOptimizedFluidChunk
{
	GENERATED_BODY()
	
	FFluidGridSoA GridData;
	FIntVector ChunkCoord;
	
	// Bit-packed active cell mask for quick skipping
	TArray<uint32> ActiveCellMask;  // 1 bit per cell, 32 cells per uint32
	int32 ActiveCellCount = 0;
	
	// Cached neighbor indices for fast access
	TArray<int32> NeighborOffsets;
	
	bool bFullySettled = false;
	float LastActivityTime = 0.0f;
	
	void Initialize(int32 ChunkSize)
	{
		GridData.Initialize(ChunkSize, ChunkSize, ChunkSize);
		
		// Initialize active cell mask
		const int32 TotalCells = ChunkSize * ChunkSize * ChunkSize;
		const int32 MaskSize = (TotalCells + 31) / 32;  // Round up
		ActiveCellMask.SetNum(MaskSize);
		FMemory::Memzero(ActiveCellMask.GetData(), ActiveCellMask.Num() * sizeof(uint32));
		
		// Pre-calculate neighbor offsets
		NeighborOffsets.SetNum(6);
		NeighborOffsets[0] = 1;                                          // +X
		NeighborOffsets[1] = -1;                                         // -X
		NeighborOffsets[2] = ChunkSize;                                  // +Y
		NeighborOffsets[3] = -ChunkSize;                                 // -Y
		NeighborOffsets[4] = ChunkSize * ChunkSize;                      // +Z
		NeighborOffsets[5] = -ChunkSize * ChunkSize;                     // -Z
	}
	
	FORCEINLINE void SetCellActive(int32 Index, bool bActive)
	{
		const int32 MaskIndex = Index / 32;
		const int32 BitIndex = Index % 32;
		
		if (bActive)
		{
			ActiveCellMask[MaskIndex] |= (1u << BitIndex);
			ActiveCellCount++;
		}
		else
		{
			ActiveCellMask[MaskIndex] &= ~(1u << BitIndex);
			ActiveCellCount--;
		}
	}
	
	FORCEINLINE bool IsCellActive(int32 Index) const
	{
		const int32 MaskIndex = Index / 32;
		const int32 BitIndex = Index % 32;
		return (ActiveCellMask[MaskIndex] & (1u << BitIndex)) != 0;
	}
	
	// Fast batch operations using SIMD
	void ProcessGravityBatch(int32 StartIndex, int32 Count);
	void ProcessFlowBatch(int32 StartIndex, int32 Count);
};

// Memory pool for chunk allocation
UCLASS()
class VOXELFLUIDSYSTEM_API UFluidChunkMemoryPool : public UObject
{
	GENERATED_BODY()
	
public:
	void Initialize(int32 MaxChunks, int32 ChunkSize);
	FOptimizedFluidChunk* AllocateChunk();
	void ReturnChunk(FOptimizedFluidChunk* Chunk);
	
private:
	TArray<FOptimizedFluidChunk> ChunkPool;
	TQueue<FOptimizedFluidChunk*> AvailableChunks;
	int32 ChunkSize = 32;
};