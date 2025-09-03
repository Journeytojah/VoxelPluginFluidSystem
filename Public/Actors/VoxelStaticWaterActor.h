#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelStackLayer.h"
#include "VoxelIntegration/VoxelTerrainSampler.h"
#include "VoxelStaticWaterActor.generated.h"

class UStaticWaterGenerator;
class UStaticWaterRenderer;
class UWaterActivationManager;
class UBoxComponent;
class UBillboardComponent;
class AVoxelFluidActor;
struct FStaticWaterRegion;

/**
 * Actor dedicated to managing static water bodies (oceans, lakes, rivers)
 * Handles non-simulated water with minimal performance overhead
 * Communicates with VoxelFluidActor for dynamic water activation when needed
 */
UCLASS(Blueprintable, BlueprintType)
class VOXELFLUIDSYSTEM_API AVoxelStaticWaterActor : public AActor
{
	GENERATED_BODY()
	
public:	
	AVoxelStaticWaterActor();

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
	// ========== Initialization ==========
	UFUNCTION(BlueprintCallable, Category = "Static Water")
	void InitializeStaticWaterSystem();

	UFUNCTION(BlueprintCallable, Category = "Static Water")
	void SetVoxelWorld(AActor* InVoxelWorld);

	UFUNCTION(BlueprintCallable, Category = "Static Water")
	void SetFluidActor(AVoxelFluidActor* InFluidActor);

	// ========== Ocean Creation ==========
	UFUNCTION(BlueprintCallable, Category = "Static Water|Ocean", meta = (CallInEditor = "true"))
	void CreateOcean(float WaterLevel = 0.0f, float Size = 100000.0f);
	
	UFUNCTION(BlueprintCallable, Category = "Static Water|Ocean", meta = (CallInEditor = "true"))
	void CreateTestOcean();
	
	UFUNCTION(BlueprintCallable, Category = "Static Water|Ocean", meta = (CallInEditor = "true"))
	void RecenterOceanOnPlayer();
	
	UFUNCTION(BlueprintCallable, Category = "Static Water|Ocean", meta = (CallInEditor = "true"))
	void ClearOcean();

	// ========== Lake Creation ==========
	UFUNCTION(BlueprintCallable, Category = "Static Water|Lakes", meta = (CallInEditor = "true"))
	void CreateLake(const FVector& Center, float Radius, float WaterLevel, float Depth = 1000.0f);
	
	UFUNCTION(BlueprintCallable, Category = "Static Water|Lakes", meta = (CallInEditor = "true"))
	void CreateRectangularLake(const FVector& Min, const FVector& Max, float WaterLevel);
	
	UFUNCTION(BlueprintCallable, Category = "Static Water|Lakes", meta = (CallInEditor = "true"))
	void AddStaticWaterRegion(const FVector& Center, float Radius, float WaterLevel);

	UFUNCTION(BlueprintCallable, Category = "Static Water|Lakes", meta = (CallInEditor = "true"))
	void RemoveStaticWaterRegion(const FVector& Center, float Radius);

	// ========== Water Queries ==========
	UFUNCTION(BlueprintCallable, Category = "Static Water|Queries")
	bool IsPointInStaticWater(const FVector& WorldPosition) const;
	
	UFUNCTION(BlueprintCallable, Category = "Static Water|Queries")
	float GetWaterLevelAtPosition(const FVector& WorldPosition) const;
	
	UFUNCTION(BlueprintCallable, Category = "Static Water|Queries")
	int32 GetStaticWaterRegionCount() const;

	// ========== Terrain Interaction ==========
	UFUNCTION(BlueprintCallable, Category = "Static Water|Terrain")
	void OnTerrainEdited(const FVector& EditPosition, float EditRadius, float HeightChange);

	UFUNCTION(BlueprintCallable, Category = "Static Water|Terrain", meta = (CallInEditor = "true"))
	void OnVoxelTerrainModified(const FVector& ModifiedPosition, float ModifiedRadius);
	
	UFUNCTION(BlueprintCallable, Category = "Static Water|Terrain", meta = (CallInEditor = "true"))
	void RefreshTerrainDataInRadius(const FVector& Center, float Radius);
	
	UFUNCTION(BlueprintCallable, Category = "Static Water|Terrain")
	void ApplyStaticWaterToChunkWithTerrain(class UFluidChunk* Chunk, class UFluidChunkManager* ChunkManager);

	// ========== Dynamic Water Activation ==========
	UFUNCTION(BlueprintCallable, Category = "Static Water|Activation")
	bool IsRegionActiveForSimulation(const FVector& Position) const;

	UFUNCTION(BlueprintCallable, Category = "Static Water|Activation", meta = (CallInEditor = "true"))
	void ForceActivateWaterAtLocation(const FVector& Position, float Radius);

	UFUNCTION(BlueprintCallable, Category = "Static Water|Activation", meta = (CallInEditor = "true"))
	void ForceDeactivateAllWaterRegions();

	UFUNCTION(BlueprintCallable, Category = "Static Water|Activation")
	int32 GetActiveWaterRegionCount() const;
	
	UFUNCTION(BlueprintCallable, Category = "Static Water|Activation")
	void ConvertToDynamicWater(const FVector& Center, float Radius);

	// ========== Visualization ==========
	UFUNCTION(BlueprintCallable, Category = "Static Water|Debug", meta = (CallInEditor = "true"))
	void ToggleDebugVisualization();
	
	UFUNCTION(BlueprintCallable, Category = "Static Water|Debug", meta = (CallInEditor = "true"))
	void ShowStaticWaterBounds();
	
	UFUNCTION(BlueprintCallable, Category = "Static Water|Debug")
	FString GetStaticWaterStats() const;

	// ========== Quick Setup ==========
	UFUNCTION(BlueprintCallable, Category = "Static Water|Setup", meta = (CallInEditor = "true"))
	void SetupTestWaterSystem();

	// ========== Components ==========
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UBoxComponent* BoundsComponent;

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UBillboardComponent* SpriteComponent;
#endif

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticWaterGenerator* StaticWaterGenerator;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticWaterRenderer* StaticWaterRenderer;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UWaterActivationManager* WaterActivationManager;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	class UVoxelFluidIntegration* VoxelIntegration;

	// ========== Properties ==========
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Water Settings")
	AActor* TargetVoxelWorld;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Water Settings")
	AVoxelFluidActor* LinkedFluidActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Water Settings")
	bool bAutoInitialize = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Water Settings")
	bool bEnableDebugVisualization = false;
	
	// Ocean Settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean Settings")
	bool bAutoCreateOcean = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean Settings", meta = (EditCondition = "bAutoCreateOcean"))
	float OceanWaterLevel = -500.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean Settings", meta = (EditCondition = "bAutoCreateOcean"))
	float OceanSize = 100000.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean Settings", meta = (EditCondition = "bAutoCreateOcean"))
	bool bFollowPlayer = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean Settings", meta = (EditCondition = "bAutoCreateOcean && bFollowPlayer"))
	float PlayerFollowDistance = 50000.0f;

	// Rendering Settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering")
	float RenderDistance = 20000.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering")
	float LODDistance1 = 5000.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering")
	float LODDistance2 = 10000.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering")
	bool bUseMeshOptimization = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering")
	int32 MeshResolution = 64;

	// Activation Settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Activation")
	bool bEnableDynamicActivation = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Activation", meta = (EditCondition = "bEnableDynamicActivation"))
	float ActivationRadius = 500.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Activation", meta = (EditCondition = "bEnableDynamicActivation"))
	float DeactivationDelay = 5.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Activation", meta = (EditCondition = "bEnableDynamicActivation"))
	float MinDisturbanceForActivation = 100.0f;

	// Performance Settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
	int32 MaxConcurrentRegions = 100;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
	float UpdateFrequency = 0.1f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
	bool bUseAsyncGeneration = true;

	// Terrain Sampling Settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain")
	bool bUseTerrainAdaptiveMesh = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain", meta = (EditCondition = "bUseTerrainAdaptiveMesh"))
	FVoxelStackLayer TerrainLayer;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain", meta = (EditCondition = "bUseTerrainAdaptiveMesh"))
	EVoxelSamplingMethod SamplingMethod = EVoxelSamplingMethod::VoxelQuery;
	
	// Runtime Volume Layer for terrain modifications
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain|Runtime Modifications")
	bool bUseRuntimeVolumeLayer = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain|Runtime Modifications", meta = (EditCondition = "bUseRuntimeVolumeLayer"))
	FVoxelStackLayer RuntimeVolumeLayer;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain|Runtime Modifications", meta = (EditCondition = "bUseRuntimeVolumeLayer", ClampMin = "0.0", ClampMax = "100.0"))
	float TerrainUpdateRadius = 50.0f;
	
	// Material Settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials")
	UMaterialInterface* WaterMaterial = nullptr;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials", meta = (DisplayName = "Water Material LOD1"))
	UMaterialInterface* WaterMaterialLOD1 = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain", meta = (EditCondition = "bUseTerrainAdaptiveMesh"))
	bool bUseVoxelLayerSampling = true;

	// Bounds Settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bounds", meta = (CallInEditor = "true"))
	FVector StaticWaterBoundsExtent = FVector(50000.0f, 50000.0f, 5000.0f);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bounds")
	FVector StaticWaterBoundsOffset = FVector::ZeroVector;

protected:
	void UpdateBoundsVisualization();
	void UpdateOceanPosition();
	TArray<FVector> GetViewerPositions() const;
	void NotifyFluidActorOfActivation(const FVector& Position, float Radius);
	
private:
	bool bIsInitialized = false;
	float UpdateAccumulator = 0.0f;
	FVector LastPlayerPosition = FVector::ZeroVector;
	
	// Ocean tracking
	bool bHasOcean = false;
	FVector OceanCenter = FVector::ZeroVector;
};