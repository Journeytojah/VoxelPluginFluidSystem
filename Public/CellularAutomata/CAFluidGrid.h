#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "CAFluidGrid.generated.h"

USTRUCT(BlueprintType)
struct VOXELFLUIDSYSTEM_API FCAFluidCell
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	float FluidLevel = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	float TerrainHeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	bool bIsSolid = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	bool bSettled = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	int32 SettledCounter = 0;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	float LastFluidLevel = 0.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	bool bSourceBlock = false;

	FCAFluidCell()
	{
		FluidLevel = 0.0f;
		TerrainHeight = 0.0f;
		bIsSolid = false;
		bSettled = false;
		SettledCounter = 0;
		LastFluidLevel = 0.0f;
		bSourceBlock = false;
	}
};

// Sleep chain for grouped settling optimization
USTRUCT()
struct FSleepChain
{
	GENERATED_BODY()

	TArray<int32> CellIndices;
	FIntVector MinBounds;
	FIntVector MaxBounds;
	float LastActivityTime;
	bool bFullySleeping;
	int32 ChainId;

	FSleepChain()
	{
		LastActivityTime = 0.0f;
		bFullySleeping = false;
		ChainId = -1;
	}

	bool ContainsCell(int32 X, int32 Y, int32 Z) const
	{
		return X >= MinBounds.X && X <= MaxBounds.X &&
		       Y >= MinBounds.Y && Y <= MaxBounds.Y &&
		       Z >= MinBounds.Z && Z <= MaxBounds.Z;
	}
};

UCLASS(BlueprintType, Blueprintable)
class VOXELFLUIDSYSTEM_API UCAFluidGrid : public UObject
{
	GENERATED_BODY()

public:
	UCAFluidGrid();

	UFUNCTION(BlueprintCallable, Category = "Fluid CA")
	void InitializeGrid(int32 InSizeX, int32 InSizeY, int32 InSizeZ, float InCellSize, const FVector& InGridOrigin = FVector::ZeroVector);

	UFUNCTION(BlueprintCallable, Category = "Fluid CA")
	void UpdateSimulation(float DeltaTime);

	UFUNCTION(BlueprintCallable, Category = "Fluid CA")
	void AddFluid(int32 X, int32 Y, int32 Z, float Amount);

	UFUNCTION(BlueprintCallable, Category = "Fluid CA")
	void RemoveFluid(int32 X, int32 Y, int32 Z, float Amount);

	UFUNCTION(BlueprintCallable, Category = "Fluid CA")
	float GetFluidAt(int32 X, int32 Y, int32 Z) const;

	UFUNCTION(BlueprintCallable, Category = "Fluid CA")
	void SetTerrainHeight(int32 X, int32 Y, float Height);
	
	UFUNCTION(BlueprintCallable, Category = "Fluid CA")
	void SetCellSolid(int32 X, int32 Y, int32 Z, bool bSolid);
	
	UFUNCTION(BlueprintCallable, Category = "Fluid CA")
	bool IsCellSolid(int32 X, int32 Y, int32 Z) const;

	UFUNCTION(BlueprintCallable, Category = "Fluid CA")
	FVector GetWorldPositionFromCell(int32 X, int32 Y, int32 Z) const;

	UFUNCTION(BlueprintCallable, Category = "Fluid CA")
	bool GetCellFromWorldPosition(const FVector& WorldPos, int32& OutX, int32& OutY, int32& OutZ) const;

	UFUNCTION(BlueprintCallable, Category = "Fluid CA")
	void ClearGrid();
	
	// Debug and stats methods
	UFUNCTION(BlueprintCallable, Category = "Fluid CA Debug")
	int32 GetSettledCellCount() const { return TotalSettledCells; }
	
	UFUNCTION(BlueprintCallable, Category = "Fluid CA Debug")
	int32 GetActiveCellCount() const { return ActiveCellCount; }
	
	UFUNCTION(BlueprintCallable, Category = "Fluid CA Debug")
	bool IsCellSettled(int32 X, int32 Y, int32 Z) const;
	
	UFUNCTION(BlueprintCallable, Category = "Fluid CA Debug")
	float GetSettlingPercentage() const;
	
	UFUNCTION(BlueprintCallable, Category = "Fluid CA")
	void ForceWakeAllFluid();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fluid Settings")
	float CellSize = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fluid Settings")
	int32 GridSizeX = 128;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fluid Settings")
	int32 GridSizeY = 128;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fluid Settings")
	int32 GridSizeZ = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	float MaxFluidLevel = 1.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	float MinFluidLevel = 0.001f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	float FlowRate = 0.25f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	int32 SettledThreshold = 5;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	float EqualizationRate = 0.5f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	bool bUseMinecraftRules = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	float CompressionThreshold = 0.95f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	bool bEnableSettling = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	float SettlingChangeThreshold = 0.0001f;

	// Advanced settling parameters
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings|Advanced Settling")
	bool bUseSleepChains = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings|Advanced Settling")
	float SleepChainMergeDistance = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings|Advanced Settling")
	bool bUsePredictiveSettling = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings|Advanced Settling")
	float PredictiveSettlingConfidenceThreshold = 0.95f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings|Advanced Settling")
	float WakeThresholdMultiplier = 2.0f;  // Wake if change > threshold * multiplier

	TArray<FCAFluidCell> Cells;
	TArray<FCAFluidCell> NextCells;
	
	// Settling optimization
	TArray<bool> CellNeedsUpdate;
	int32 ActiveCellCount = 0;
	int32 TotalSettledCells = 0;

	// Advanced settling - Sleep chains
	TArray<FSleepChain> SleepChains;
	TMap<int32, int32> CellToChainMap;  // Maps cell index to chain index
	int32 NextChainId = 0;

	// Predictive settling
	TArray<float> FluidChangeHistory;  // Track last 3 frames of changes per cell
	TArray<float> SettlingConfidence;  // Confidence that cell will settle soon
	int32 HistoryFrameCount = 3;

	// Settling hysteresis to prevent rapid settle/unsettle
	TArray<int32> UnsettleCountdown;  // Frames before cell can settle again after waking
	int32 HysteresisFrames = 10;

	FVector GridOrigin;

protected:

	bool IsValidCell(int32 X, int32 Y, int32 Z) const;
	int32 GetCellIndex(int32 X, int32 Y, int32 Z) const;
	
	// Simplified CA methods
	void ProcessCombinedPhysics(float DeltaTime);  // Combined gravity + compression
	void ProcessHorizontalFlow(float DeltaTime);
	void ProcessEqualization(float DeltaTime);
	void UpdateSettledStates();
	
	// Settling optimization methods
	void InitializeUpdateFlags();
	void InitializeUpdateFlagsOptimized();
	void MarkCellForUpdate(int32 X, int32 Y, int32 Z);
	void WakeUpNeighbors(int32 X, int32 Y, int32 Z);
	bool ShouldUpdateCell(int32 X, int32 Y, int32 Z) const;
	bool CanCellSettle(int32 X, int32 Y, int32 Z) const;
	void PropagateWakeUp(int32 X, int32 Y, int32 Z, int32 Distance = 2);

	// Advanced settling methods
	void UpdateSleepChains();
	void UpdateSleepChainsOptimized();
	void CreateSleepChain(int32 StartX, int32 StartY, int32 StartZ);
	void MergeSleepChains(int32 Chain1, int32 Chain2);
	void WakeUpSleepChain(int32 ChainIndex);
	bool IsCellInSleepChain(int32 CellIndex) const;

	// Predictive settling
	void UpdateSettlingPrediction();
	float PredictSettlingTime(int32 CellIndex) const;
	void UpdateFluidChangeHistory();
	bool ShouldPredictiveSettle(int32 X, int32 Y, int32 Z) const;
	
	// Memory optimization methods
	void EnableCompressedMode(bool bEnable);
	void CompressCells();
	void DecompressCells();
	int32 GetCompressedMemorySize() const;
	void OptimizeMemoryLayout();
	
	float GetStableFluidLevel(int32 X, int32 Y, int32 Z) const;
	bool CanFlowInto(int32 X, int32 Y, int32 Z) const;
	void DistributeWater(int32 X, int32 Y, int32 Z, float Amount);
	
private:
	// Compressed storage for memory optimization
	bool bUseCompressedStorage = false;
	TArray<uint16> CompressedFluidLevels;
	TArray<uint8> CompressedFlags;
	TArray<uint8> CompressedSettledCounters;
};