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
					
					// First half of fill level (0 to 0.5) grows the bottom face
					// Second half (0.5 to 1.0) grows the height
					float HorizontalScale = 1.0f;
					float VerticalScale = 1.0f;
					
					if (FluidLevel <= 0.5f)
					{
						// Grow bottom face from 0 to full size
						HorizontalScale = FluidLevel * 2.0f; // Maps 0-0.5 to 0-1
						VerticalScale = 0.05f; // Keep a minimal height for visibility
					}
					else
					{
						// Bottom face is full size, now grow height
						HorizontalScale = 1.0f;
						VerticalScale = (FluidLevel - 0.5f) * 2.0f; // Maps 0.5-1 to 0-1
					}
					
					const float ScaledWidth = BaseSize * HorizontalScale;
					const float ScaledHeight = BaseSize * VerticalScale;
					
					// Adjust position so box grows upward from bottom
					const FVector AdjustedPos = CellWorldPos + FVector(0, 0, (ScaledHeight - BaseSize) * 0.5f);
					
					const FColor DebugColor = FColor::MakeRedToGreenColorFromScalar(1.0f - FluidLevel);
					
					DrawDebugBox(GetWorld(), AdjustedPos, FVector(ScaledWidth * 0.5f, ScaledWidth * 0.5f, ScaledHeight * 0.5f), DebugColor, false, -1.0f, 0, 2.0f);
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