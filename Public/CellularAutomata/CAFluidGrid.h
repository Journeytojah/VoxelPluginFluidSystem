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
	FVector FlowVelocity = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	float TerrainHeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	bool bIsSolid = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	float Pressure = 0.0f;

	FCAFluidCell()
	{
		FluidLevel = 0.0f;
		FlowVelocity = FVector::ZeroVector;
		TerrainHeight = 0.0f;
		bIsSolid = false;
		Pressure = 0.0f;
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fluid Settings")
	float CellSize = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fluid Settings")
	int32 GridSizeX = 128;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fluid Settings")
	int32 GridSizeY = 128;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fluid Settings")
	int32 GridSizeZ = 32;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	bool bAllowFluidEscape = true;

	TArray<FCAFluidCell> Cells;
	TArray<FCAFluidCell> NextCells;

	FVector GridOrigin;

protected:

	bool IsValidCell(int32 X, int32 Y, int32 Z) const;
	int32 GetCellIndex(int32 X, int32 Y, int32 Z) const;
	
	void ApplyGravity(float DeltaTime);
	void ApplyFlowRules(float DeltaTime);
	void ApplyPressure(float DeltaTime);
	void UpdateVelocities(float DeltaTime);
	
	float CalculateFlowTo(int32 FromX, int32 FromY, int32 FromZ, 
						  int32 ToX, int32 ToY, int32 ToZ, float DeltaTime);
};