#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "FluidVisualizationComponent.generated.h"

class UCAFluidGrid;
class UFluidChunkManager;
class UMaterialInterface;
class UInstancedStaticMeshComponent;
class UFluidChunk;
class UProceduralMeshComponent;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk Visualization")
	float MaxRenderDistance = 10000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk Visualization")
	bool bShowChunkBounds = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk Visualization")
	int32 MaxCellsToRenderPerFrame = 50000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk Visualization")
	bool bUseLODForVisualization = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marching Cubes", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float MarchingCubesIsoLevel = 0.1f;

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
	void RenderFluidChunk(UFluidChunk* Chunk, const FVector& ViewerPosition);
	bool ShouldRenderChunk(UFluidChunk* Chunk, const FVector& ViewerPosition) const;
	FVector GetPrimaryViewerPosition() const;
	void DrawChunkBounds() const;
	
	TMap<UFluidChunk*, UInstancedStaticMeshComponent*> ChunkMeshComponents;
	TMap<UFluidChunk*, UProceduralMeshComponent*> ChunkMarchingCubesMeshes;
};