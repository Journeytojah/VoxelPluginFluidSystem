#include "Visualization/FluidVisualizationComponent.h"
#include "CellularAutomata/CAFluidGrid.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"

UFluidVisualizationComponent::UFluidVisualizationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UFluidVisualizationComponent::BeginPlay()
{
	Super::BeginPlay();
	
	if (RenderMode == EFluidRenderMode::Instances && !InstancedMeshComponent)
	{
		InstancedMeshComponent = NewObject<UInstancedStaticMeshComponent>(GetOwner(), UInstancedStaticMeshComponent::StaticClass());
		InstancedMeshComponent->RegisterComponent();
		InstancedMeshComponent->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
		
		if (FluidCellMesh)
		{
			InstancedMeshComponent->SetStaticMesh(FluidCellMesh);
		}
		
		if (FluidMaterial)
		{
			InstancedMeshComponent->SetMaterial(0, FluidMaterial);
		}
	}
}

void UFluidVisualizationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	if (!FluidGrid)
		return;
	
	MeshUpdateTimer += DeltaTime;
	if (MeshUpdateTimer >= MeshUpdateInterval)
	{
		MeshUpdateTimer = 0.0f;
		UpdateVisualization();
	}
	
	if (bEnableFlowVisualization && GetWorld())
	{
		for (int32 x = 0; x < FluidGrid->GridSizeX; x += 4)
		{
			for (int32 y = 0; y < FluidGrid->GridSizeY; y += 4)
			{
				for (int32 z = 0; z < FluidGrid->GridSizeZ; z += 2)
				{
					const float FluidLevel = FluidGrid->GetFluidAt(x, y, z);
					if (FluidLevel > MinFluidLevelToRender)
					{
						const FVector CellPos = FluidGrid->GetWorldPositionFromCell(x, y, z);
						const int32 CellIdx = x + y * FluidGrid->GridSizeX + z * FluidGrid->GridSizeX * FluidGrid->GridSizeY;
						
						if (CellIdx >= 0 && CellIdx < FluidGrid->Cells.Num())
						{
							const FVector Velocity = FluidGrid->Cells[CellIdx].FlowVelocity;
							if (Velocity.Size() > 0.1f)
							{
								DrawDebugDirectionalArrow(GetWorld(), CellPos, CellPos + Velocity * FlowVectorScale, 
														  20.0f, FColor::Cyan, false, -1.0f, 0, 2.0f);
							}
						}
					}
				}
			}
		}
	}
}

void UFluidVisualizationComponent::SetFluidGrid(UCAFluidGrid* InFluidGrid)
{
	FluidGrid = InFluidGrid;
	UpdateVisualization();
}

void UFluidVisualizationComponent::UpdateVisualization()
{
	if (!FluidGrid)
		return;
	
	switch (RenderMode)
	{
		case EFluidRenderMode::Instances:
			GenerateInstancedVisualization();
			break;
		case EFluidRenderMode::Debug:
		default:
			DrawDebugFluid();
			break;
	}
}

void UFluidVisualizationComponent::GenerateInstancedVisualization()
{
	if (!FluidGrid || !InstancedMeshComponent)
		return;
	
	UpdateInstancedMeshes();
}

void UFluidVisualizationComponent::DrawDebugFluid()
{
	if (!FluidGrid || !GetWorld())
		return;
	
	for (int32 x = 0; x < FluidGrid->GridSizeX; ++x)
	{
		for (int32 y = 0; y < FluidGrid->GridSizeY; ++y)
		{
			for (int32 z = 0; z < FluidGrid->GridSizeZ; ++z)
			{
				const float FluidLevel = FluidGrid->GetFluidAt(x, y, z);
				
				if (FluidLevel > MinFluidLevelToRender)
				{
					const FVector CellWorldPos = FluidGrid->GetWorldPositionFromCell(x, y, z);
					const float BaseSize = FluidGrid->CellSize * 0.9f;
					const float VerticalScale = FluidLevel;
					const float ScaledHeight = BaseSize * VerticalScale;
					
					// Adjust position so box grows upward from bottom
					const FVector AdjustedPos = CellWorldPos + FVector(0, 0, (ScaledHeight - BaseSize) * 0.5f);
					
					const FColor DebugColor = FColor::MakeRedToGreenColorFromScalar(1.0f - FluidLevel);
					
					DrawDebugBox(GetWorld(), AdjustedPos, FVector(BaseSize * 0.5f, BaseSize * 0.5f, ScaledHeight * 0.5f), DebugColor, false, -1.0f, 0, 2.0f);
				}
			}
		}
	}
}

void UFluidVisualizationComponent::UpdateInstancedMeshes()
{
	if (!InstancedMeshComponent || !FluidGrid)
		return;
	
	InstancedMeshComponent->ClearInstances();
	
	TArray<FTransform> Transforms;
	Transforms.Reserve(FluidGrid->GridSizeX * FluidGrid->GridSizeY * FluidGrid->GridSizeZ / 10);
	
	for (int32 x = 0; x < FluidGrid->GridSizeX; ++x)
	{
		for (int32 y = 0; y < FluidGrid->GridSizeY; ++y)
		{
			for (int32 z = 0; z < FluidGrid->GridSizeZ; ++z)
			{
				const float FluidLevel = FluidGrid->GetFluidAt(x, y, z);
				
				if (FluidLevel > MinFluidLevelToRender)
				{
					const FVector CellWorldPos = FluidGrid->GetWorldPositionFromCell(x, y, z);
					const float Scale = FluidLevel;
					
					FTransform InstanceTransform;
					InstanceTransform.SetLocation(CellWorldPos);
					InstanceTransform.SetScale3D(FVector(Scale, Scale, Scale));
					
					Transforms.Add(InstanceTransform);
				}
			}
		}
	}
	
	if (Transforms.Num() > 0)
	{
		InstancedMeshComponent->AddInstances(Transforms, false);
	}
}