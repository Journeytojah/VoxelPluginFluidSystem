#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelFluidActor.generated.h"

class UCAFluidGrid;
class UVoxelFluidIntegration;
class UFluidVisualizationComponent;
class UBoxComponent;

UCLASS(Blueprintable, BlueprintType)
class VOXELFLUIDSYSTEM_API AVoxelFluidActor : public AActor
{
	GENERATED_BODY()
	
public:	
	AVoxelFluidActor();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void OnConstruction(const FTransform& Transform) override;

public:
	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void InitializeFluidSystem();

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void StartSimulation();

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void StopSimulation();

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void ResetSimulation();

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void AddFluidSource(const FVector& WorldPosition, float FlowRate);

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void RemoveFluidSource(const FVector& WorldPosition);

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void AddFluidAtLocation(const FVector& WorldPosition, float Amount);

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void SetVoxelWorld(AActor* InVoxelWorld);

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid", meta = (CallInEditor = "true"))
	void RefreshTerrainData();

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid", meta = (CallInEditor = "true"))
	void TestFluidSpawn();

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid", meta = (CallInEditor = "true"))
	void UpdateBounds();

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void UpdateGridOriginForMovement();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UBoxComponent* BoundsComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UCAFluidGrid* FluidGrid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UVoxelFluidIntegration* VoxelIntegration;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UFluidVisualizationComponent* VisualizationComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
	AActor* TargetVoxelWorld;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation Settings")
	bool bAutoStart = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation Settings")
	bool bIsSimulating = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation Settings")
	float SimulationSpeed = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Settings")
	int32 GridSizeX = 128;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Settings")
	int32 GridSizeY = 128;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Settings")
	int32 GridSizeZ = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Settings")
	float CellSize = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bounds Settings", meta = (CallInEditor = "true"))
	FVector BoundsExtent = FVector(6400.0f, 6400.0f, 1600.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bounds Settings")
	FVector BoundsOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bounds Settings")
	bool bUseWorldBounds = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bounds Settings", meta = (EditCondition = "bUseWorldBounds"))
	FVector WorldBoundsMin = FVector(-5000.0f, -5000.0f, -1000.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bounds Settings", meta = (EditCondition = "bUseWorldBounds"))
	FVector WorldBoundsMax = FVector(5000.0f, 5000.0f, 2000.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Properties")
	float FluidViscosity = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Properties")
	float FluidFlowRate = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Properties")
	float GravityStrength = 981.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Properties")
	bool bAllowFluidEscape = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bShowDebugGrid = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bShowFlowVectors = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	float DebugFluidSpawnAmount = 1.0f;

private:
	TMap<FVector, float> FluidSources;
	
	void UpdateFluidSources(float DeltaTime);
	void UpdateDebugVisualization();
	void DrawDebugGrid();
	void CalculateGridBounds();

	FVector CalculatedGridOrigin;
	FVector CalculatedBoundsExtent;
};