#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "FluidVisualizationComponent.generated.h"

class UCAFluidGrid;
class UMaterialInterface;
class UInstancedStaticMeshComponent;

UENUM(BlueprintType)
enum class EFluidRenderMode : uint8
{
	Instances UMETA(DisplayName = "Instanced Meshes"),
	Debug UMETA(DisplayName = "Debug Boxes")
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
	void UpdateVisualization();

	UFUNCTION(BlueprintCallable, Category = "Fluid Visualization")
	void GenerateInstancedVisualization();

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

private:
	UPROPERTY()
	UCAFluidGrid* FluidGrid;

	UPROPERTY()
	UInstancedStaticMeshComponent* InstancedMeshComponent;

	float MeshUpdateTimer = 0.0f;

	void DrawDebugFluid();
	void UpdateInstancedMeshes();
};