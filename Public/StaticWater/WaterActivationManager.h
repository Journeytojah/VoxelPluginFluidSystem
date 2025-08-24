#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/World.h"
#include "WaterActivationManager.generated.h"

class UStaticWaterGenerator;
class UStaticWaterRenderer;
class UFluidChunkManager;
class UCAFluidGrid;

USTRUCT(BlueprintType)
struct VOXELFLUIDSYSTEM_API FWaterActivationRegion
{
	GENERATED_BODY()

	UPROPERTY()
	FBox Bounds = FBox(EForceInit::ForceInit);

	UPROPERTY()
	float ActivationRadius = 1000.0f;

	UPROPERTY()
	bool bIsActive = false;

	UPROPERTY()
	float ActivationTime = 0.0f;

	UPROPERTY()
	int32 Priority = 0;

	// Cached static water data before activation
	TArray<FVector> StaticWaterPositions;
	TArray<float> StaticWaterAmounts;

	void Clear()
	{
		StaticWaterPositions.Empty();
		StaticWaterAmounts.Empty();
		bIsActive = false;
		ActivationTime = 0.0f;
	}

	bool ContainsPoint(const FVector& Point) const
	{
		return Bounds.IsInside(Point);
	}

	bool ShouldActivate(const FVector& EditPosition) const
	{
		return FVector::Dist(Bounds.GetCenter(), EditPosition) <= ActivationRadius;
	}
};

USTRUCT(BlueprintType)
struct VOXELFLUIDSYSTEM_API FWaterActivationSettings
{
	GENERATED_BODY()

	// Activation trigger settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activation", meta = (ClampMin = "100", ClampMax = "5000"))
	float DefaultActivationRadius = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activation", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float TerrainChangeThreshold = 50.0f; // Minimum terrain change to trigger activation

	// Deactivation settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deactivation", meta = (ClampMin = "10", ClampMax = "300"))
	float DeactivationDelay = 60.0f; // Time before converting back to static

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deactivation", meta = (ClampMin = "0.001", ClampMax = "0.1"))
	float FluidSettleThreshold = 0.01f; // Fluid velocity threshold for settling

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deactivation", meta = (ClampMin = "1", ClampMax = "60"))
	float SettleCheckInterval = 10.0f; // How often to check if fluid has settled

	// Performance settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance", meta = (ClampMin = "1", ClampMax = "20"))
	int32 MaxActiveRegions = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance", meta = (ClampMin = "1", ClampMax = "10"))
	int32 MaxActivationsPerFrame = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float UpdateFrequency = 0.1f;

	// Transition settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	bool bSmoothTransitions = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float TransitionDuration = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	bool bPreserveFluidVolume = true;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWaterRegionActivated, const FVector&, Center, float, Radius);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWaterRegionDeactivated, const FVector&, Center, float, Radius);

/**
 * Manages transitions between static and simulated water systems
 * Activates simulation when terrain edits occur near static water
 * Converts settled fluid back to static water for performance
 */
UCLASS(BlueprintType, Blueprintable, ClassGroup=(VoxelFluidSystem), meta=(BlueprintSpawnableComponent))
class VOXELFLUIDSYSTEM_API UWaterActivationManager : public UActorComponent
{
	GENERATED_BODY()

public:
	UWaterActivationManager();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// System references
	UFUNCTION(BlueprintCallable, Category = "Water Activation")
	void SetStaticWaterGenerator(UStaticWaterGenerator* InGenerator);

	UFUNCTION(BlueprintCallable, Category = "Water Activation")
	void SetStaticWaterRenderer(UStaticWaterRenderer* InRenderer);

	UFUNCTION(BlueprintCallable, Category = "Water Activation")
	void SetFluidChunkManager(UFluidChunkManager* InChunkManager);

	UFUNCTION(BlueprintCallable, Category = "Water Activation")
	void SetFluidGrid(UCAFluidGrid* InFluidGrid);

	// Activation triggers
	UFUNCTION(BlueprintCallable, Category = "Water Activation")
	void OnTerrainEdited(const FVector& EditPosition, float EditRadius, float HeightChange);

	UFUNCTION(BlueprintCallable, Category = "Water Activation")
	void OnFluidAdded(const FVector& Position, float Amount);

	UFUNCTION(BlueprintCallable, Category = "Water Activation")
	void OnExplosion(const FVector& Position, float Radius);

	// Manual activation/deactivation
	UFUNCTION(BlueprintCallable, Category = "Water Activation")
	bool ActivateWaterInRegion(const FVector& Center, float Radius);

	UFUNCTION(BlueprintCallable, Category = "Water Activation")
	bool DeactivateWaterInRegion(const FVector& Center, float Radius);

	UFUNCTION(BlueprintCallable, Category = "Water Activation", meta = (CallInEditor = "true"))
	void ForceDeactivateAllRegions();

	// Query methods
	UFUNCTION(BlueprintCallable, Category = "Water Activation")
	bool IsRegionActive(const FVector& Position) const;

	UFUNCTION(BlueprintCallable, Category = "Water Activation")
	int32 GetActiveRegionCount() const;

	UFUNCTION(BlueprintCallable, Category = "Water Activation")
	TArray<FVector> GetActiveRegionCenters() const;

	UFUNCTION(BlueprintCallable, Category = "Water Activation")
	float GetRegionActivationTime(const FVector& Position) const;

	// Settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activation Settings")
	FWaterActivationSettings ActivationSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Systems")
	UStaticWaterGenerator* StaticWaterGenerator = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Systems")
	UStaticWaterRenderer* StaticWaterRenderer = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Systems")
	UFluidChunkManager* FluidChunkManager = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Systems")
	UCAFluidGrid* FluidGrid = nullptr;

	// Events
	UPROPERTY(BlueprintAssignable, Category = "Water Activation Events")
	FOnWaterRegionActivated OnWaterRegionActivated;

	UPROPERTY(BlueprintAssignable, Category = "Water Activation Events")
	FOnWaterRegionDeactivated OnWaterRegionDeactivated;

	// Debug settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bShowActiveRegions = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bShowActivationRadius = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bEnableLogging = false;

protected:
	// Core activation logic
	void UpdateActiveRegions(float DeltaTime);
	void CheckForDeactivation(float DeltaTime);
	void ProcessActivationQueue();

	// Region management
	FWaterActivationRegion* FindRegionAtPosition(const FVector& Position);
	const FWaterActivationRegion* FindRegionAtPosition(const FVector& Position) const;
	FWaterActivationRegion* CreateActivationRegion(const FVector& Center, float Radius);
	void RemoveActivationRegion(int32 RegionIndex);
	bool ShouldMergeRegions(const FWaterActivationRegion& RegionA, const FWaterActivationRegion& RegionB) const;
	void MergeRegions(FWaterActivationRegion& TargetRegion, const FWaterActivationRegion& SourceRegion);

	// Water system integration
	void ActivateSimulation(FWaterActivationRegion& Region);
	void DeactivateSimulation(FWaterActivationRegion& Region);
	void TransferStaticToSimulation(const FWaterActivationRegion& Region);
	void TransferSimulationToStatic(FWaterActivationRegion& Region);
	
	// Fluid state analysis
	bool IsFluidSettled(const FWaterActivationRegion& Region) const;
	float GetAverageFluidVelocity(const FWaterActivationRegion& Region) const;
	bool HasFluidInRegion(const FWaterActivationRegion& Region) const;

	// Optimization
	void OptimizeRegions();
	void RemoveEmptyRegions();

	// Debug visualization
	void DrawDebugInfo() const;

private:
	// Active regions
	TArray<FWaterActivationRegion> ActiveRegions;

	// Activation queue for performance spreading
	struct FPendingActivation
	{
		FVector Center;
		float Radius;
		int32 Priority;
		float QueueTime;
	};
	TArray<FPendingActivation> ActivationQueue;

	// Update timers
	float RegionUpdateTimer = 0.0f;
	float DeactivationCheckTimer = 0.0f;
	float OptimizationTimer = 0.0f;

	// Performance tracking
	int32 ActivationsThisFrame = 0;
	int32 DeactivationsThisFrame = 0;
	float LastActivationTime = 0.0f;

	// Thread safety
	mutable FCriticalSection RegionsMutex;

	bool bIsInitialized = false;
};