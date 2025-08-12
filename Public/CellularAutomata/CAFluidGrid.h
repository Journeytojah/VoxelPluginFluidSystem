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

	TArray<FCAFluidCell> Cells;
	TArray<FCAFluidCell> NextCells;
	
	// Settling optimization
	TArray<bool> CellNeedsUpdate;
	int32 ActiveCellCount = 0;
	int32 TotalSettledCells = 0;

	FVector GridOrigin;

protected:

	bool IsValidCell(int32 X, int32 Y, int32 Z) const;
	int32 GetCellIndex(int32 X, int32 Y, int32 Z) const;
	
	// Simplified CA methods
	void ProcessGravity(float DeltaTime);
	void ProcessHorizontalFlow(float DeltaTime);
	void ProcessEqualization(float DeltaTime);
	void ProcessCompression(float DeltaTime);
	void UpdateSettledStates();
	
	// Settling optimization methods
	void InitializeUpdateFlags();
	void MarkCellForUpdate(int32 X, int32 Y, int32 Z);
	void WakeUpNeighbors(int32 X, int32 Y, int32 Z);
	bool ShouldUpdateCell(int32 X, int32 Y, int32 Z) const;
	bool CanCellSettle(int32 X, int32 Y, int32 Z) const;
	void PropagateWakeUp(int32 X, int32 Y, int32 Z, int32 Distance = 2);
	
	float GetStableFluidLevel(int32 X, int32 Y, int32 Z) const;
	bool CanFlowInto(int32 X, int32 Y, int32 Z) const;
	void DistributeWater(int32 X, int32 Y, int32 Z, float Amount);
};