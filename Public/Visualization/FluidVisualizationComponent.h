#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "Visualization/MarchingCubes.h"
#include "FluidVisualizationComponent.generated.h"

class UCAFluidGrid;
class UFluidChunkManager;
class UMaterialInterface;
class UInstancedStaticMeshComponent;
class UFluidChunk;
class UProceduralMeshComponent;
class FMarchingCubes;

UENUM(BlueprintType)
enum class EFluidRenderMode : uint8
{
	Instances UMETA(DisplayName = "Instanced Meshes"),
	Debug UMETA(DisplayName = "Debug Boxes"),
	MarchingCubes UMETA(DisplayName = "Marching Cubes Mesh")
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class VOXELFLUIDSYSTEM_API UFluidVisualizationComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UFluidVisualizationComponent();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:
	UFUNCTION(BlueprintCallable, Category = "Fluid Visualization")
	void SetFluidGrid(UCAFluidGrid* InFluidGrid);

	UFUNCTION(BlueprintCallable, Category = "Fluid Visualization")
	void SetChunkManager(UFluidChunkManager* InChunkManager);

	UFUNCTION(BlueprintCallable, Category = "Fluid Visualization")
	void UpdateVisualization();
	
	void OnChunkUnloaded(const struct FFluidChunkCoord& ChunkCoord);

	UFUNCTION(BlueprintCallable, Category = "Fluid Visualization")
	void GenerateInstancedVisualization();

	UFUNCTION(BlueprintCallable, Category = "Fluid Visualization")
	void GenerateChunkedVisualization();

	UFUNCTION(BlueprintCallable, Category = "Fluid Visualization")
	void SetMaxRenderDistance(float Distance) { MaxRenderDistance = Distance; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visualization")
	EFluidRenderMode RenderMode = EFluidRenderMode::Debug;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visualization")
	UMaterialInterface* FluidMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visualization")
	UStaticMesh* FluidCellMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visualization")
	float MinFluidLevelToRender = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visualization")
	float MeshUpdateInterval = 0.033f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visualization")
	FLinearColor FluidColor = FLinearColor(0.0f, 0.5f, 1.0f, 0.7f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visualization")
	bool bEnableFlowVisualization = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visualization")
	float FlowVectorScale = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk Visualization", meta = (ClampMin = "1000.0", ClampMax = "100000.0"))
	float MaxRenderDistance = 30000.0f; // Increased to 300 meters for visible rivers/lakes

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk Visualization")
	bool bShowChunkBounds = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk Visualization")
	int32 MaxCellsToRenderPerFrame = 50000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk Visualization")
	bool bUseLODForVisualization = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marching Cubes", meta = (ClampMin = "0.0001", ClampMax = "1.0"))
	float MarchingCubesIsoLevel = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marching Cubes")
	bool bSmoothNormals = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marching Cubes")
	bool bGenerateCollision = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marching Cubes")
	bool bFlipNormals = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marching Cubes", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float MeshInterpolationSpeed = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marching Cubes")
	bool bSmoothMeshUpdates = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marching Cubes", meta = (ClampMin = "0.001", ClampMax = "0.1"))
	float MeshUpdateThreshold = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marching Cubes")
	bool bEnableDensitySmoothing = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marching Cubes", meta = (ClampMin = "1", ClampMax = "5"))
	int32 SmoothingIterations = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marching Cubes", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float SmoothingStrength = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marching Cubes")
	bool bEnableGapFilling = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marching Cubes", meta = (ClampMin = "1", ClampMax = "4"))
	int32 MarchingCubesResolutionMultiplier = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marching Cubes")
	bool bUseAdaptiveResolution = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
	bool bUseAsyncMeshGeneration = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance", meta = (ClampMin = "1", ClampMax = "10"))
	int32 MaxAsyncTasksPerFrame = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance", meta = (ClampMin = "0.001", ClampMax = "0.1"))
	float MaxMeshGenerationTimePerFrame = 0.008f; // 8ms budget

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marching Cubes", meta = (ClampMin = "0.016", ClampMax = "1.0"))
	float MinTimeBetweenMeshUpdates = 0.033f; // Minimum time between mesh updates for a chunk (30 FPS default)

private:
	UPROPERTY()
	UCAFluidGrid* FluidGrid;

	UPROPERTY()
	UFluidChunkManager* ChunkManager;

	UPROPERTY()
	UInstancedStaticMeshComponent* InstancedMeshComponent;

	UPROPERTY()
	UProceduralMeshComponent* MarchingCubesMesh;

	float MeshUpdateTimer = 0.0f;
	bool bUseChunkedSystem = false;

	// Smooth interpolation state
	TArray<float> PreviousDensityGrid;
	TArray<float> CurrentDensityGrid;
	TArray<float> InterpolatedDensityGrid;
	float InterpolationAlpha = 0.0f;

	void DrawDebugFluid();
	void DrawChunkedDebugFluid();
	void UpdateInstancedMeshes();
	void UpdateChunkedInstancedMeshes();
	void GenerateMarchingCubesVisualization();
	void GenerateChunkedMarchingCubes();
	void UpdateDensityInterpolation(float DeltaTime);
	void InterpolateDensityGrids();
	bool ShouldUpdateMesh(const TArray<float>& NewDensityGrid) const;
	void SmoothDensityGrid(TArray<float>& DensityGrid, const FIntVector& GridSize) const;
	void ApplyGaussianSmoothing(TArray<float>& DensityGrid, const FIntVector& GridSize, float Strength) const;
	float GetSmoothedDensity(const TArray<float>& DensityGrid, const FIntVector& GridSize, int32 X, int32 Y, int32 Z) const;
	void RenderFluidChunk(UFluidChunk* Chunk, const FVector& ViewerPosition);
	bool ShouldRenderChunk(UFluidChunk* Chunk, const FVector& ViewerPosition) const;
	FVector GetPrimaryViewerPosition() const;
	void DrawChunkBounds() const;
	int32 CalculateLODLevel(float Distance) const;
	void GenerateChunkMeshWithLOD(UFluidChunk* Chunk, int32 LODLevel, TArray<FMarchingCubes::FMarchingCubesVertex>& OutVertices, TArray<FMarchingCubes::FMarchingCubesTriangle>& OutTriangles);
	
	TMap<UFluidChunk*, UInstancedStaticMeshComponent*> ChunkMeshComponents;
	TMap<UFluidChunk*, UProceduralMeshComponent*> ChunkMarchingCubesMeshes;
	
	// Optimization: Track chunks that need mesh updates
	TSet<UFluidChunk*> ChunksNeedingMeshUpdate;
	TMap<UFluidChunk*, float> ChunkLastMeshUpdateTime;
	UPROPERTY(EditAnywhere)
	float ChunkMeshCheckInterval = 0.1f; // Check chunks less frequently
	float ChunkMeshCheckTimer = 0.0f;
	int32 MaxChunksToUpdatePerFrame = 100; // Increased limit for real-time updates
	
	// Async mesh generation
	struct FAsyncMeshGenerationTask
	{
		UFluidChunk* Chunk;
		int32 LODLevel;
		float IsoLevel;
		int32 ResolutionMultiplier;
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FColor> VertexColors;
		bool bCompleted;
		bool bStarted;
		
		FAsyncMeshGenerationTask() : Chunk(nullptr), LODLevel(0), IsoLevel(0.01f), 
		                            ResolutionMultiplier(1), bCompleted(false), bStarted(false) {}
	};
	
	TArray<TSharedPtr<FAsyncMeshGenerationTask>> AsyncMeshTasks;
	FCriticalSection AsyncTaskMutex;
	
	// Performance tracking
	float CurrentFrameMeshGenTime = 0.0f;
	int32 AsyncTasksRunningCount = 0;
	
	void ProcessAsyncMeshTasks();
	void StartAsyncMeshGeneration(UFluidChunk* Chunk, int32 LODLevel);
	void ApplyGeneratedMesh(TSharedPtr<FAsyncMeshGenerationTask> Task);
};