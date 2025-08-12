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
	float GetTotalFluidVolume() const;
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
	float MeshChangeThreshold = 0.05f; // Only regenerate if > 5% change
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

	bool bDirty = false;
	bool bBorderDirty = false;
	
	TSet<FFluidChunkCoord> ActiveNeighbors;

protected:
	bool IsValidLocalCell(int32 X, int32 Y, int32 Z) const;
	
	void ApplyGravity(float DeltaTime);
	void ApplyFlowRules(float DeltaTime);
	void ApplyPressure(float DeltaTime);
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