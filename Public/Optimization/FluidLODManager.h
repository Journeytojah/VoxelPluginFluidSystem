#pragma once

#include "CoreMinimal.h"
#include "CellularAutomata/FluidChunk.h"
#include "FluidLODManager.generated.h"

UENUM(BlueprintType)
enum class EFluidLODLevel : uint8
{
	LOD0 = 0 UMETA(DisplayName = "Full Detail"),
	LOD1 = 1 UMETA(DisplayName = "Medium Detail"),
	LOD2 = 2 UMETA(DisplayName = "Low Detail"),
	LOD3 = 3 UMETA(DisplayName = "Very Low Detail"),
	LOD_Culled = 4 UMETA(DisplayName = "Culled")
};

USTRUCT(BlueprintType)
struct VOXELFLUIDSYSTEM_API FFluidLODSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD", meta = (ClampMin = "100.0"))
	float LOD0Distance = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD", meta = (ClampMin = "100.0"))
	float LOD1Distance = 2500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD", meta = (ClampMin = "100.0"))
	float LOD2Distance = 5000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD", meta = (ClampMin = "100.0"))
	float LOD3Distance = 10000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD", meta = (ClampMin = "100.0"))
	float CullDistance = 20000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD")
	bool bUseFrustumCulling = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD")
	bool bUseOcclusionCulling = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float LODTransitionSpeed = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD")
	bool bSmoothLODTransitions = true;

	FFluidLODSettings()
	{
		LOD0Distance = 1000.0f;
		LOD1Distance = 2500.0f;
		LOD2Distance = 5000.0f;
		LOD3Distance = 10000.0f;
		CullDistance = 20000.0f;
		bUseFrustumCulling = true;
		bUseOcclusionCulling = false;
		LODTransitionSpeed = 1.0f;
		bSmoothLODTransitions = true;
	}

	EFluidLODLevel GetLODForDistance(float Distance) const
	{
		if (Distance > CullDistance)
			return EFluidLODLevel::LOD_Culled;
		if (Distance > LOD3Distance)
			return EFluidLODLevel::LOD3;
		if (Distance > LOD2Distance)
			return EFluidLODLevel::LOD2;
		if (Distance > LOD1Distance)
			return EFluidLODLevel::LOD1;
		return EFluidLODLevel::LOD0;
	}

	int32 GetSimulationSkipFrames(EFluidLODLevel LOD) const
	{
		switch (LOD)
		{
		case EFluidLODLevel::LOD0:
			return 1; // Update every frame
		case EFluidLODLevel::LOD1:
			return 2; // Update every 2nd frame
		case EFluidLODLevel::LOD2:
			return 4; // Update every 4th frame
		case EFluidLODLevel::LOD3:
			return 8; // Update every 8th frame
		default:
			return 0; // No updates for culled
		}
	}

	float GetMeshSimplificationRatio(EFluidLODLevel LOD) const
	{
		switch (LOD)
		{
		case EFluidLODLevel::LOD0:
			return 1.0f; // Full quality
		case EFluidLODLevel::LOD1:
			return 0.5f; // 50% vertices
		case EFluidLODLevel::LOD2:
			return 0.25f; // 25% vertices
		case EFluidLODLevel::LOD3:
			return 0.125f; // 12.5% vertices
		default:
			return 0.0f; // No mesh
		}
	}

	int32 GetCellSkipSize(EFluidLODLevel LOD) const
	{
		switch (LOD)
		{
		case EFluidLODLevel::LOD0:
			return 1; // Process every cell
		case EFluidLODLevel::LOD1:
			return 2; // Process every 2nd cell
		case EFluidLODLevel::LOD2:
			return 4; // Process every 4th cell
		case EFluidLODLevel::LOD3:
			return 8; // Process every 8th cell
		default:
			return 0; // No processing
		}
	}
};

USTRUCT(BlueprintType)
struct VOXELFLUIDSYSTEM_API FChunkLODState
{
	GENERATED_BODY()

	UPROPERTY()
	FFluidChunkCoord ChunkCoord;

	UPROPERTY()
	EFluidLODLevel CurrentLOD = EFluidLODLevel::LOD0;

	UPROPERTY()
	EFluidLODLevel TargetLOD = EFluidLODLevel::LOD0;

	UPROPERTY()
	float TransitionAlpha = 0.0f;

	UPROPERTY()
	float LastUpdateTime = 0.0f;

	UPROPERTY()
	int32 FramesSinceLastUpdate = 0;

	UPROPERTY()
	bool bIsVisible = true;

	UPROPERTY()
	bool bInFrustum = true;

	UPROPERTY()
	bool bOccluded = false;

	UPROPERTY()
	float DistanceToViewer = 0.0f;

	UPROPERTY()
	float ImportanceFactor = 1.0f;

	void UpdateTransition(float DeltaTime, float TransitionSpeed)
	{
		if (CurrentLOD != TargetLOD)
		{
			TransitionAlpha += DeltaTime * TransitionSpeed;
			if (TransitionAlpha >= 1.0f)
			{
				CurrentLOD = TargetLOD;
				TransitionAlpha = 0.0f;
			}
		}
	}

	bool ShouldUpdateThisFrame(int32 CurrentFrame) const
	{
		if (!bIsVisible || CurrentLOD == EFluidLODLevel::LOD_Culled)
			return false;

		int32 SkipFrames = 1;
		switch (CurrentLOD)
		{
		case EFluidLODLevel::LOD1:
			SkipFrames = 2;
			break;
		case EFluidLODLevel::LOD2:
			SkipFrames = 4;
			break;
		case EFluidLODLevel::LOD3:
			SkipFrames = 8;
			break;
		default:
			break;
		}

		return (CurrentFrame % SkipFrames) == 0;
	}
};

UCLASS(BlueprintType)
class VOXELFLUIDSYSTEM_API UFluidLODManager : public UObject
{
	GENERATED_BODY()

public:
	UFluidLODManager();

	void Initialize(const FFluidLODSettings& InSettings);

	UFUNCTION(BlueprintCallable, Category = "Fluid LOD")
	void UpdateLODStates(const TArray<UFluidChunk*>& Chunks, const TArray<FVector>& ViewerPositions, float DeltaTime);

	UFUNCTION(BlueprintCallable, Category = "Fluid LOD")
	EFluidLODLevel GetChunkLOD(const FFluidChunkCoord& ChunkCoord) const;

	UFUNCTION(BlueprintCallable, Category = "Fluid LOD")
	bool ShouldUpdateChunk(UFluidChunk* Chunk, int32 CurrentFrame) const;

	UFUNCTION(BlueprintCallable, Category = "Fluid LOD")
	void SetLODSettings(const FFluidLODSettings& NewSettings);

	UFUNCTION(BlueprintCallable, Category = "Fluid LOD")
	FString GetLODStats() const;

	UFUNCTION(BlueprintCallable, Category = "Fluid LOD")
	int32 GetChunksAtLOD(EFluidLODLevel LOD) const;

	UFUNCTION(BlueprintCallable, Category = "Fluid LOD")
	void ForceUpdateLOD(UFluidChunk* Chunk, EFluidLODLevel NewLOD);

	UFUNCTION(BlueprintCallable, Category = "Fluid LOD")
	void UpdateFrustumCulling(const FMatrix& ViewProjectionMatrix);

	UFUNCTION(BlueprintCallable, Category = "Fluid LOD")
	void PerformOcclusionQueries(UWorld* World, const FVector& ViewPosition);

	UFUNCTION(BlueprintCallable, Category = "Fluid LOD")
	float CalculateChunkImportance(UFluidChunk* Chunk, const FVector& ViewPosition) const;

	UFUNCTION(BlueprintCallable, Category = "Fluid LOD")
	TArray<UFluidChunk*> GetVisibleChunks() const;

	UFUNCTION(BlueprintCallable, Category = "Fluid LOD")
	void OptimizeLODDistribution(int32 MaxActiveChunks);

protected:
	UPROPERTY()
	FFluidLODSettings Settings;

	UPROPERTY()
	TMap<FFluidChunkCoord, FChunkLODState> ChunkLODStates;

	UPROPERTY()
	TArray<FPlane> FrustumPlanes;

	UPROPERTY()
	int32 CurrentFrame = 0;

	UPROPERTY()
	TArray<int32> LODCounts;

	FCriticalSection LODMutex;

	bool IsChunkInFrustum(UFluidChunk* Chunk) const;
	float GetMinDistanceToViewers(const FVector& ChunkPosition, const TArray<FVector>& ViewerPositions) const;
	void UpdateLODCounts();
	void AdaptiveLODAdjustment(int32 MaxActiveChunks);
};