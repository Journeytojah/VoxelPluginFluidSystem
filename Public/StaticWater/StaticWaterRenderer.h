#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "StaticWaterRenderer.generated.h"

class UStaticWaterGenerator;
class UProceduralMeshComponent;

USTRUCT(BlueprintType)
struct VOXELFLUIDSYSTEM_API FStaticWaterRenderChunk
{
	GENERATED_BODY()

	UPROPERTY()
	FIntVector ChunkCoord = FIntVector::ZeroValue;

	UPROPERTY()
	FBox WorldBounds = FBox(EForceInit::ForceInit);

	UPROPERTY()
	float WaterLevel = 0.0f;

	UPROPERTY()
	bool bHasWater = false;

	UPROPERTY()
	bool bNeedsRebuild = true;

	UPROPERTY()
	int32 LODLevel = 0;

	// Mesh data
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;

	// Component reference
	UPROPERTY()
	UProceduralMeshComponent* MeshComponent = nullptr;

	void Clear()
	{
		Vertices.Empty();
		Triangles.Empty();
		Normals.Empty();
		UVs.Empty();
		bHasWater = false;
		bNeedsRebuild = true;
	}

	bool IsValid() const;
};

USTRUCT(BlueprintType)
struct VOXELFLUIDSYSTEM_API FStaticWaterRenderSettings
{
	GENERATED_BODY()

	// Chunk settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering", meta = (ClampMin = "1000", ClampMax = "10000"))
	float RenderChunkSize = 12800.0f; // Larger 128x128m render chunks for fewer chunks and better startup performance

	// Ring rendering settings - static water only renders between MinRenderDistance and MaxRenderDistance
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Rendering", meta = (ClampMin = "1000", ClampMax = "10000"))
	float MinRenderDistance = 3000.0f; // Don't render static water closer than this (let simulation handle it)
	
	// LOD settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD", meta = (ClampMin = "500", ClampMax = "25000"))
	float LOD0Distance = 15000.0f; // Increased to cover at least one full chunk around player with 12800 chunk size

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD", meta = (ClampMin = "1000", ClampMax = "30000"))
	float LOD1Distance = 25000.0f; // Adjusted proportionally

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD", meta = (ClampMin = "5000", ClampMax = "50000"))
	float MaxRenderDistance = 32000.0f; // Increased to cover ocean size (25000) with chunk size (12800) = ~3 chunk radius

	// Culling settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Culling")
	bool bEnableFrustumCulling = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Culling")
	bool bEnableOcclusionCulling = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Culling", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float CullDistanceScale = 1.0f;

	// Mesh generation settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Generation", meta = (ClampMin = "10", ClampMax = "500"))
	float MeshResolution = 50.0f; // Vertex spacing in cm - reduced from 200 for 4x better resolution

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Generation")
	bool bGenerateNormals = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Generation")
	bool bGenerateUVs = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Generation")
	bool bSmoothNormals = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Generation", meta = (ClampMin = "0", ClampMax = "50"))
	float EdgePenetrationDepth = 15.0f; // How far water penetrates into terrain at edges (cm)

	// Performance settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance", meta = (ClampMin = "1", ClampMax = "32"))
	int32 MaxChunksToUpdatePerFrame = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxChunksToCreatePerFrame = 2; // Limit initial chunk creation for faster startup

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance", meta = (ClampMin = "10", ClampMax = "1000"))
	int32 MaxRenderChunks = 100; // Reduced for faster startup

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float UpdateFrequency = 0.1f; // Reduced frequency for better startup performance

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
	bool bUseProgressiveLoading = true; // Enable progressive chunk loading to prevent startup lag
};

/**
 * Renders static water surfaces independent of simulation system
 * Uses player-centric streaming and LOD for optimal performance
 */
UCLASS(BlueprintType, Blueprintable, ClassGroup=(VoxelFluidSystem), meta=(BlueprintSpawnableComponent))
class VOXELFLUIDSYSTEM_API UStaticWaterRenderer : public UActorComponent
{
	GENERATED_BODY()

public:
	UStaticWaterRenderer();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Configuration
	UFUNCTION(BlueprintCallable, Category = "Static Water Rendering")
	void SetWaterGenerator(UStaticWaterGenerator* InGenerator);

	UFUNCTION(BlueprintCallable, Category = "Static Water Rendering")
	void SetWaterMaterial(UMaterialInterface* InMaterial);

	UFUNCTION(BlueprintCallable, Category = "Static Water Rendering")
	void SetVoxelIntegration(class UVoxelFluidIntegration* InVoxelIntegration);

	// Viewer management
	UFUNCTION(BlueprintCallable, Category = "Static Water Rendering")
	void SetViewerPosition(const FVector& Position);

	UFUNCTION(BlueprintCallable, Category = "Static Water Rendering")
	void AddViewer(const FVector& Position);

	UFUNCTION(BlueprintCallable, Category = "Static Water Rendering")
	void RemoveViewer(int32 ViewerIndex);

	UFUNCTION(BlueprintCallable, Category = "Static Water Rendering")
	void ClearViewers();

	// Rendering control
	UFUNCTION(BlueprintCallable, Category = "Static Water Rendering")
	void SetRenderingEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Static Water Rendering", meta = (CallInEditor = "true"))
	void ForceRebuildAllChunks();

	UFUNCTION(BlueprintCallable, Category = "Static Water Rendering")
	void RebuildChunksInRadius(const FVector& Center, float Radius);

	UFUNCTION(BlueprintCallable, Category = "Static Water Rendering")
	void RegenerateAroundViewer();

	UFUNCTION(BlueprintCallable, Category = "Static Water Rendering")
	void ResetRenderer();

	UFUNCTION(BlueprintCallable, Category = "Static Water Rendering")
	void EnableAutoTracking(bool bEnable = true);

	// Query methods
	UFUNCTION(BlueprintCallable, Category = "Static Water Rendering")
	int32 GetActiveRenderChunkCount() const;

	UFUNCTION(BlueprintCallable, Category = "Static Water Rendering")
	int32 GetVisibleRenderChunkCount() const;

	UFUNCTION(BlueprintCallable, Category = "Static Water Rendering")
	TArray<FIntVector> GetActiveRenderChunkCoords() const;
	
	// LOD statistics
	UFUNCTION(BlueprintCallable, Category = "Static Water Rendering")
	void GetLODStatistics(int32& OutLOD0Count, int32& OutLOD1Count, int32& OutLOD2Count) const;

	// Settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render Settings")
	FStaticWaterRenderSettings RenderSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials")
	UMaterialInterface* WaterMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials")
	UMaterialInterface* WaterMaterialLOD1 = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering")
	bool bRenderingEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering")
	bool bAutoTrackPlayer = true;

	// Debug settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bShowRenderChunkBounds = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bShowLODColors = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bEnableLogging = false;

protected:
	// Core rendering
	void UpdateRenderChunks(float DeltaTime);
	void UpdateChunkLODs();
	void UpdateChunkVisibility();

	// Chunk management
	FIntVector WorldPositionToRenderChunkCoord(const FVector& WorldPosition) const;
	FVector RenderChunkCoordToWorldPosition(const FIntVector& ChunkCoord) const;
	void UpdateActiveRenderChunks();
	void LoadRenderChunk(const FIntVector& ChunkCoord);
	void UnloadRenderChunk(const FIntVector& ChunkCoord);
	bool ShouldLoadRenderChunk(const FIntVector& ChunkCoord) const;
	bool ShouldUnloadRenderChunk(const FIntVector& ChunkCoord) const;

	// Mesh generation
	void BuildChunkMesh(FStaticWaterRenderChunk& Chunk);
	void GenerateWaterSurface(FStaticWaterRenderChunk& Chunk);
	void GeneratePlanarWaterMesh(FStaticWaterRenderChunk& Chunk, float WaterLevel);
	void GenerateAdaptiveWaterMesh(FStaticWaterRenderChunk& Chunk);
	void UpdateChunkMesh(FStaticWaterRenderChunk& Chunk);

	// Component management
	UProceduralMeshComponent* CreateMeshComponent(const FIntVector& ChunkCoord);
	void DestroyMeshComponent(UProceduralMeshComponent* MeshComp);
	void UpdateComponentMaterial(UProceduralMeshComponent* MeshComp, int32 LODLevel);

	// Utility
	float GetDistanceToChunk(const FIntVector& ChunkCoord) const;
	float GetClosestViewerDistance(const FVector& Position) const;
	bool IsChunkVisible(const FStaticWaterRenderChunk& Chunk) const;
	int32 CalculateLODLevel(float Distance) const;

	// Debug visualization
	void DrawDebugInfo() const;

private:
	// Render chunk cache
	TMap<FIntVector, FStaticWaterRenderChunk> LoadedRenderChunks;
	TSet<FIntVector> ActiveRenderChunkCoords;
	TQueue<FIntVector> ChunkLoadQueue;
	TQueue<FIntVector> ChunkUnloadQueue;

	// Viewer tracking
	TArray<FVector> ViewerPositions;

	// Update timers
	float RenderUpdateTimer = 0.0f;
	float LODUpdateTimer = 0.0f;
	float VisibilityUpdateTimer = 0.0f;

	// Water generator reference
	UPROPERTY()
	UStaticWaterGenerator* WaterGenerator = nullptr;

	// Voxel integration for terrain sampling
	UPROPERTY()
	class UVoxelFluidIntegration* VoxelIntegration = nullptr;

	// Component pool for reuse
	TArray<UProceduralMeshComponent*> AvailableMeshComponents;
	TSet<UProceduralMeshComponent*> UsedMeshComponents;

	// Performance tracking
	int32 ChunksUpdatedThisFrame = 0;
	int32 ChunksBuiltThisFrame = 0;
	float LastRenderTime = 0.0f;

	// Thread safety
	mutable FCriticalSection RenderChunkMutex;

	bool bIsInitialized = false;
	
	// Startup optimization
	float StartupTime = 0.0f;
	float OriginalMaxRenderDistance = 0.0f;
	static constexpr float StartupProgressionTime = 3.0f; // Gradually increase render distance over 3 seconds
};