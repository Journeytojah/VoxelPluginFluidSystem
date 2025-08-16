#pragma once

#include "CoreMinimal.h"
#include "CAFluidGrid.h"
#include "FluidChunk.generated.h"

// Structure to store serialized mesh data for chunk persistence
USTRUCT()
struct FChunkMeshData
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FVector> Vertices;
	
	UPROPERTY()
	TArray<int32> Triangles;
	
	UPROPERTY()
	TArray<FVector> Normals;
	
	UPROPERTY()
	TArray<FVector2D> UVs;
	
	UPROPERTY()
	TArray<FColor> VertexColors;
	
	UPROPERTY()
	float GeneratedIsoLevel = 0.1f;
	
	UPROPERTY()
	int32 GeneratedLOD = 0;
	
	UPROPERTY()
	float GenerationTimestamp = 0.0f;
	
	UPROPERTY()
	bool bIsValid = false;
	
	// Hash of fluid state when mesh was generated (for dirty checking)
	UPROPERTY()
	uint32 FluidStateHash = 0;

	FChunkMeshData()
	{
		Vertices.Empty();
		Triangles.Empty();
		Normals.Empty();
		UVs.Empty();
		VertexColors.Empty();
	}
	
	void Clear()
	{
		Vertices.Empty();
		Triangles.Empty();
		Normals.Empty();
		UVs.Empty();
		VertexColors.Empty();
		bIsValid = false;
		FluidStateHash = 0;
	}
	
	bool IsValidForLOD(int32 DesiredLOD, float DesiredIsoLevel) const
	{
		return bIsValid && 
			   GeneratedLOD <= DesiredLOD && // Can use higher quality mesh for lower LOD
			   FMath::IsNearlyEqual(GeneratedIsoLevel, DesiredIsoLevel, 0.01f);
	}
};

USTRUCT(BlueprintType)
struct VOXELFLUIDSYSTEM_API FFluidChunkCoord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 X = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Y = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Z = 0;

	FFluidChunkCoord() {}
	FFluidChunkCoord(int32 InX, int32 InY, int32 InZ) : X(InX), Y(InY), Z(InZ) {}

	bool operator==(const FFluidChunkCoord& Other) const
	{
		return X == Other.X && Y == Other.Y && Z == Other.Z;
	}

	friend uint32 GetTypeHash(const FFluidChunkCoord& Coord)
	{
		return HashCombine(HashCombine(GetTypeHash(Coord.X), GetTypeHash(Coord.Y)), GetTypeHash(Coord.Z));
	}

	FString ToString() const
	{
		return FString::Printf(TEXT("(%d,%d,%d)"), X, Y, Z);
	}
};

UENUM(BlueprintType)
enum class EChunkState : uint8
{
	Unloaded,
	Loading,
	Active,
	Inactive,
	Unloading,
	BorderOnly
};

USTRUCT(BlueprintType)
struct VOXELFLUIDSYSTEM_API FChunkBorderData
{
	GENERATED_BODY()

	TArray<FCAFluidCell> PositiveX;
	TArray<FCAFluidCell> NegativeX;
	TArray<FCAFluidCell> PositiveY;
	TArray<FCAFluidCell> NegativeY;
	TArray<FCAFluidCell> PositiveZ;
	TArray<FCAFluidCell> NegativeZ;

	void Clear()
	{
		PositiveX.Empty();
		NegativeX.Empty();
		PositiveY.Empty();
		NegativeY.Empty();
		PositiveZ.Empty();
		NegativeZ.Empty();
	}
};

// Compressed fluid data for a single cell (2 bytes instead of full struct)
USTRUCT()
struct FCompressedFluidCell
{
	GENERATED_BODY()

	uint16 FluidLevel; // Quantized to 16-bit (0-65535 maps to 0.0-1.0)
	uint8 Flags; // Bit 0: bIsSolid, Bit 1: bSettled, Bit 2: bSourceBlock
	
	FCompressedFluidCell()
	{
		FluidLevel = 0;
		Flags = 0;
	}
	
	FCompressedFluidCell(const FCAFluidCell& Cell)
	{
		// Quantize fluid level to 16-bit
		FluidLevel = (uint16)(FMath::Clamp(Cell.FluidLevel, 0.0f, 1.0f) * 65535.0f);
		
		// Pack flags
		Flags = 0;
		if (Cell.bIsSolid) Flags |= 0x01;
		if (Cell.bSettled) Flags |= 0x02;
		if (Cell.bSourceBlock) Flags |= 0x04;
	}
	
	void Decompress(FCAFluidCell& OutCell) const
	{
		OutCell.FluidLevel = (float)FluidLevel / 65535.0f;
		OutCell.bIsSolid = (Flags & 0x01) != 0;
		OutCell.bSettled = (Flags & 0x02) != 0;
		OutCell.bSourceBlock = (Flags & 0x04) != 0;
		OutCell.LastFluidLevel = OutCell.FluidLevel;
		OutCell.SettledCounter = OutCell.bSettled ? 10 : 0;
	}
};

// Persistent chunk data that can be saved/loaded
USTRUCT(BlueprintType)
struct VOXELFLUIDSYSTEM_API FChunkPersistentData
{
	GENERATED_BODY()

	UPROPERTY()
	FFluidChunkCoord ChunkCoord;
	
	UPROPERTY()
	TArray<FCompressedFluidCell> CompressedCells;
	
	UPROPERTY()
	float Timestamp;
	
	UPROPERTY()
	int32 Version = 1;
	
	UPROPERTY()
	uint32 Checksum = 0;
	
	// Statistics for optimization
	UPROPERTY()
	int32 NonEmptyCellCount = 0;
	
	UPROPERTY()
	float TotalFluidVolume = 0.0f;
	
	UPROPERTY()
	bool bHasFluid = false;
	
	FChunkPersistentData()
	{
		Timestamp = 0.0f;
		Version = 1;
		Checksum = 0;
		NonEmptyCellCount = 0;
		TotalFluidVolume = 0.0f;
		bHasFluid = false;
	}
	
	void CompressFrom(const TArray<FCAFluidCell>& Cells);
	void DecompressTo(TArray<FCAFluidCell>& OutCells) const;
	uint32 CalculateChecksum() const;
	bool ValidateChecksum() const;
	int32 GetMemorySize() const;
};

UCLASS(BlueprintType)
class VOXELFLUIDSYSTEM_API UFluidChunk : public UObject
{
	GENERATED_BODY()

public:
	UFluidChunk();

	void Initialize(const FFluidChunkCoord& InCoord, int32 InChunkSize, float InCellSize, const FVector& InWorldOrigin);
	
	void UpdateSimulation(float DeltaTime);
	
	void ActivateChunk();
	void DeactivateChunk();
	void LoadChunk();
	void UnloadChunk();
	
	// Persistence methods
	FChunkPersistentData SerializeChunkData() const;
	void DeserializeChunkData(const FChunkPersistentData& PersistentData);
	bool HasFluid() const;
	float GetTotalFluidVolume() const;

	void AddFluid(int32 LocalX, int32 LocalY, int32 LocalZ, float Amount);
	void RemoveFluid(int32 LocalX, int32 LocalY, int32 LocalZ, float Amount);
	float GetFluidAt(int32 LocalX, int32 LocalY, int32 LocalZ) const;
	
	void SetTerrainHeight(int32 LocalX, int32 LocalY, float Height);
	void SetCellSolid(int32 LocalX, int32 LocalY, int32 LocalZ, bool bSolid);
	bool IsCellSolid(int32 LocalX, int32 LocalY, int32 LocalZ) const;
	
	FVector GetWorldPositionFromLocal(int32 LocalX, int32 LocalY, int32 LocalZ) const;
	bool GetLocalFromWorldPosition(const FVector& WorldPos, int32& OutX, int32& OutY, int32& OutZ) const;
	
	FChunkBorderData ExtractBorderData() const;
	void ApplyBorderData(const FChunkBorderData& BorderData);
	
	void UpdateBorderCell(int32 LocalX, int32 LocalY, int32 LocalZ, const FCAFluidCell& Cell);
	
	bool HasActiveFluid() const;
	int32 GetActiveCellCount() const;
	
	FBox GetWorldBounds() const;
	bool IsInLODRange(const FVector& ViewerPosition, float LODDistance) const;
	
	void SetLODLevel(int32 NewLODLevel);
	
	void ClearChunk();
	
	int32 GetLocalCellIndex(int32 X, int32 Y, int32 Z) const;
	
	// Mesh persistence methods
	void StoreMeshData(const TArray<FVector>& Vertices, const TArray<int32>& Triangles, 
					   const TArray<FVector>& Normals, const TArray<FVector2D>& UVs, 
					   const TArray<FColor>& VertexColors, float IsoLevel, int32 LODLevel);
	bool HasValidMeshData(int32 DesiredLOD, float DesiredIsoLevel) const;
	void ClearMeshData();
	void MarkMeshDataDirty();
	void ConsiderMeshUpdate(float FluidChange);
	bool ShouldRegenerateMesh() const;
	int32 GetSettledCellCount() const;
	uint32 CalculateFluidStateHash() const;

public:
	UPROPERTY(BlueprintReadOnly)
	FFluidChunkCoord ChunkCoord;
	
	UPROPERTY(BlueprintReadOnly)
	EChunkState State = EChunkState::Unloaded;
	
	UPROPERTY(BlueprintReadOnly)
	int32 ChunkSize = 32;
	
	UPROPERTY(BlueprintReadOnly)
	float CellSize = 100.0f;
	
	UPROPERTY(BlueprintReadOnly)
	FVector WorldOrigin;
	
	UPROPERTY(BlueprintReadOnly)
	FVector ChunkWorldPosition;
	
	TArray<FCAFluidCell> Cells;
	TArray<FCAFluidCell> NextCells;
	
	UPROPERTY(BlueprintReadOnly)
	float LastUpdateTime = 0.0f;
	
	UPROPERTY(BlueprintReadOnly)
	float TimeSinceLastActive = 0.0f;
	
	UPROPERTY(BlueprintReadOnly)
	int32 CurrentLOD = 0;
	
	// Mesh persistence data
	UPROPERTY()
	FChunkMeshData StoredMeshData;
	
	UPROPERTY()
	bool bMeshDataDirty = true;
	
	// Mesh optimization
	float LastMeshChangeAmount = 0.0f;
	float AccumulatedMeshChange = 0.0f;
	
	UPROPERTY(EditAnywhere, Category = "Mesh Generation", meta = (ClampMin = "0.001", ClampMax = "0.5"))
	float MeshChangeThreshold = 0.01f; // Only regenerate if > 1% change (reduced from 5% for better responsiveness)
	
	int32 SettledCellCount = 0;
	float LastMeshUpdateTime = 0.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	float FlowRate = 0.5f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	float Viscosity = 0.1f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	float Gravity = 981.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	float MinFluidLevel = 0.001f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	float MaxFluidLevel = 1.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	float CompressionFactor = 0.05f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EvaporationRate = 0.0f; // Amount of fluid to evaporate per second (0 = no evaporation)

	bool bDirty = false;
	bool bBorderDirty = false;
	
	// Activity tracking for optimization
	bool bFullySettled = false;
	float TotalFluidActivity = 0.0f;
	float LastActivityLevel = 0.0f;
	int32 InactiveFrameCount = 0;
	int32 UpdateFrequency = 1; // 1 = every frame, 2 = every other frame, etc.
	
	TSet<FFluidChunkCoord> ActiveNeighbors;

protected:
	bool IsValidLocalCell(int32 X, int32 Y, int32 Z) const;
	
	void ApplyGravity(float DeltaTime);
	void ApplyFlowRules(float DeltaTime);
	void ApplyPressure(float DeltaTime);
	void ApplyEvaporation(float DeltaTime);
	void UpdateVelocities(float DeltaTime);
	
	void CalculateHydrostaticPressure();
	void DetectAndMarkPools(float DeltaTime);
	void ApplyUpwardPressureFlow(float DeltaTime);
	void ApplyDiagonalFlow(float DeltaTime);
	void ApplyPressureEqualization(float DeltaTime);
	
	void ProcessBorderFlow(float DeltaTime);
	
	FChunkBorderData PendingBorderData;
	FCriticalSection BorderDataMutex;
};