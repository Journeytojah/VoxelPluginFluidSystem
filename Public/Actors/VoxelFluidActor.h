#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelFluidActor.generated.h"

class UCAFluidGrid;
class UFluidChunkManager;
class UVoxelFluidIntegration;
class UFluidVisualizationComponent;
class UBoxComponent;
class UBillboardComponent;
class UStaticWaterManager;
struct FChunkStreamingConfig;
struct FStaticWaterRegion;

UCLASS(Blueprintable, BlueprintType)
class VOXELFLUIDSYSTEM_API AVoxelFluidActor : public AActor
{
	GENERATED_BODY()
	
public:	
	AVoxelFluidActor();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }
#endif

public:
	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void InitializeFluidSystem();

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void StartSimulation();

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void StopSimulation();

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid")
	void ResetSimulation();

	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid", meta = (CallInEditor = "true"))
	void AddFluidSource(const FVector& WorldPosition, float FlowRate = -1.0f);

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
	void UpdateSimulationBounds();


	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UBoxComponent* BoundsComponent;

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UBillboardComponent* SpriteComponent;
#endif

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UCAFluidGrid* FluidGrid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UFluidChunkManager* ChunkManager;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UVoxelFluidIntegration* VoxelIntegration;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UFluidVisualizationComponent* VisualizationComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticWaterManager* StaticWaterManager;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
	AActor* TargetVoxelWorld;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	bool bAutoStart = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation")
	bool bIsSimulating = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	float SimulationSpeed = 1.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation", meta = (ClampMin = "0.001", ClampMax = "0.1"))
	float SimulationTimestep = 0.016f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	bool bUseFixedTimestep = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk Settings")
	int32 ChunkSize = 32;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk Settings")
	float CellSize = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk Settings")
	float ChunkLoadDistance = 20000.0f; // Increased from 8000 for more distance

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk Settings")
	float ChunkActiveDistance = 15000.0f; // Increased from 5000 for more active chunks

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk Settings")
	int32 MaxActiveChunks = 200; // Increased from 64 for more chunks

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk Settings")
	int32 MaxLoadedChunks = 400; // Increased from 128 for more chunks


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk Settings")
	float LOD1Distance = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk Settings")
	float LOD2Distance = 4000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation Bounds", meta = (CallInEditor = "true"))
	FVector SimulationBoundsExtent = FVector(25600.0f, 25600.0f, 3200.0f); // Much larger for more chunks

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation Bounds")
	FVector SimulationBoundsOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Properties")
	float FluidViscosity = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Properties", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float DefaultSourceFlowRate = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Properties")
	float GravityStrength = 981.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Properties")
	bool bAllowFluidEscape = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Properties", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float FluidAccumulation = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Properties", meta = (ClampMin = "0.001", ClampMax = "0.1"))
	float MinFluidThreshold = 0.001f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Properties", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FluidEvaporationRate = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid Properties", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float FluidDensityMultiplier = 1.0f;

	// Static Water Properties
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Water")
	bool bEnableStaticWater = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Water")
	bool bShowStaticWaterBounds = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Water", meta = (EditCondition = "bEnableStaticWater"))
	bool bAutoCreateOcean = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Water", meta = (EditCondition = "bAutoCreateOcean"))
	float OceanWaterLevel = 0.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Water", meta = (EditCondition = "bAutoCreateOcean"))
	float OceanSize = 100000.0f;

	// Optimization settings removed
	
	// Octree optimization removed

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bShowFlowVectors = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	float DebugFluidSpawnAmount = 1.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug - Chunks")
	bool bShowChunkBorders = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug - Chunks")
	bool bShowChunkStates = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug - Chunks", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float ChunkDebugUpdateInterval = 0.5f;

	// Performance Monitoring
	UFUNCTION(BlueprintCallable, Category = "Performance", meta = (CallInEditor = "true"))
	FString GetPerformanceStats() const;

	UFUNCTION(BlueprintCallable, Category = "Performance")
	void EnableProfiling(bool bEnable);

	UFUNCTION(BlueprintCallable, Category = "Performance")
	float GetLastFrameSimulationTime() const { return LastFrameSimulationTime; }

	UFUNCTION(BlueprintCallable, Category = "Performance")
	int32 GetActiveCellCount() const;

	UFUNCTION(BlueprintCallable, Category = "Performance")
	float GetTotalFluidVolume() const;
	

	UFUNCTION(BlueprintCallable, Category = "Chunk System")
	int32 GetLoadedChunkCount() const;

	UFUNCTION(BlueprintCallable, Category = "Chunk System")
	int32 GetActiveChunkCount() const;

	UFUNCTION(BlueprintCallable, Category = "Chunk System")
	void ForceUpdateChunkStreaming();

	UFUNCTION(BlueprintCallable, Category = "Chunk System")
	FString GetChunkSystemStats() const;

	// Debug functions for testing persistence
	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid Debug", meta = (CallInEditor = "true"))
	void TestPersistenceAtLocation(const FVector& WorldPosition);
	
	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid Debug", meta = (CallInEditor = "true"))
	void ForceUnloadAllChunks();
	
	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid Debug", meta = (CallInEditor = "true"))
	void ShowCacheStatus();
	
	UFUNCTION(BlueprintCallable, Category = "Voxel Fluid Debug", meta = (CallInEditor = "true"))
	void TestPersistenceWithSourcePause();
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bPauseFluidSources = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bEnableDebugLogging = false;

	// Static Water Body Functions
	UFUNCTION(BlueprintCallable, Category = "Static Water", meta = (CallInEditor = "true"))
	void CreateOcean(float WaterLevel = 0.0f, float Size = 100000.0f);
	
	UFUNCTION(BlueprintCallable, Category = "Static Water", meta = (CallInEditor = "true"))
	void CreateLake(const FVector& Center, float Radius, float WaterLevel, float Depth = 1000.0f);
	
	UFUNCTION(BlueprintCallable, Category = "Static Water", meta = (CallInEditor = "true"))
	void CreateRectangularLake(const FVector& Min, const FVector& Max, float WaterLevel);
	
	UFUNCTION(BlueprintCallable, Category = "Static Water", meta = (CallInEditor = "true"))
	void ClearStaticWater();
	
	UFUNCTION(BlueprintCallable, Category = "Static Water")
	void ApplyStaticWaterToAllChunks();
	
	UFUNCTION(BlueprintCallable, Category = "Static Water")
	bool IsPointInStaticWater(const FVector& WorldPosition) const;
	
	UFUNCTION(BlueprintCallable, Category = "Static Water", meta = (CallInEditor = "true"))
	void RetryStaticWaterApplication();
	
	UFUNCTION(BlueprintCallable, Category = "Static Water")
	void RefillStaticWaterInRadius(const FVector& Center, float Radius);
	
	UFUNCTION(BlueprintCallable, Category = "Static Water Debug", meta = (CallInEditor = "true"))
	void TestTerrainRefreshAtLocation(const FVector& Location, float Radius = 200.0f);

private:
	TMap<FVector, float> FluidSources;
	
	void UpdateFluidSources(float DeltaTime);
	void UpdateDebugVisualization();
	void DrawDebugChunks();
	void InitializeChunkSystem();
	void UpdateChunkSystem(float DeltaTime);
	TArray<FVector> GetViewerPositions() const;

	FVector SimulationOrigin;
	FVector ActiveBoundsExtent;
	
	// Performance tracking
	float LastFrameSimulationTime = 0.0f;
	bool bProfilingEnabled = false;
	FDateTime LastProfilingTime;
	
	// Simulation timing
	float SimulationAccumulator = 0.0f;
	
	// Static water dynamic refill system
	void StartDynamicToStaticConversion(const FVector& Center, float Radius);
	void ConvertSettledFluidToStatic(const FVector& Center, float Radius);
	
	UPROPERTY(EditAnywhere, Category = "Static Water")
	float DynamicToStaticSettleTime = 5.0f;
};