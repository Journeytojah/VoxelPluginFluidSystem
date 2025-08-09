#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CellularAutomata/CAFluidGrid.h"
#include "VoxelFluidIntegration.generated.h"

class AActor;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class VOXELFLUIDSYSTEM_API UVoxelFluidIntegration : public UActorComponent
{
	GENERATED_BODY()

public:
	UVoxelFluidIntegration();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:
	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void InitializeFluidSystem(AActor* InVoxelWorld);

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void SyncWithVoxelTerrain();

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void UpdateTerrainHeights();

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	float SampleVoxelHeight(float WorldX, float WorldY);

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void AddFluidAtWorldPosition(const FVector& WorldPosition, float Amount);

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void RemoveFluidAtWorldPosition(const FVector& WorldPosition, float Amount);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Fluid")
	AActor* VoxelWorld;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Fluid")
	UCAFluidGrid* FluidGrid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	int32 GridResolutionX = 128;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	int32 GridResolutionY = 128;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	int32 GridResolutionZ = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	float CellWorldSize = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	bool bAutoUpdateTerrain = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	float TerrainUpdateInterval = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	bool bDebugDrawCells = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Settings")
	float MinFluidToRender = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bEnableFlowVisualization = false;

private:
	float TerrainUpdateTimer = 0.0f;
	FVector GridWorldOrigin;

	void DrawDebugFluid();
	bool IsVoxelWorldValid() const;
};