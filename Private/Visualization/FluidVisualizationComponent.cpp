#include "Visualization/FluidVisualizationComponent.h"
#include "CellularAutomata/CAFluidGrid.h"
#include "CellularAutomata/FluidChunkManager.h"
#include "CellularAutomata/FluidChunk.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "VoxelFluidStats.h"
#include "GameFramework/PlayerController.h"

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
	if (bUseChunkedSystem && ChunkManager)
	{
		GenerateChunkedVisualization();
	}
	else if (FluidGrid)
	{
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

void UFluidVisualizationComponent::SetChunkManager(UFluidChunkManager* InChunkManager)
{
	ChunkManager = InChunkManager;
	bUseChunkedSystem = (ChunkManager != nullptr);
	
	if (bUseChunkedSystem)
	{
		FluidGrid = nullptr;
	}
	
	UpdateVisualization();
}

void UFluidVisualizationComponent::GenerateChunkedVisualization()
{
	if (!ChunkManager)
		return;
	
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_Visualization);
	
	switch (RenderMode)
	{
		case EFluidRenderMode::Instances:
			UpdateChunkedInstancedMeshes();
			break;
		case EFluidRenderMode::Debug:
		default:
			DrawChunkedDebugFluid();
			break;
	}
	
	if (bShowChunkBounds)
	{
		DrawChunkBounds();
	}
}

void UFluidVisualizationComponent::DrawChunkedDebugFluid()
{
	if (!ChunkManager || !GetWorld())
		return;
	
	const FVector ViewerPos = GetPrimaryViewerPosition();
	const TArray<UFluidChunk*> ActiveChunks = ChunkManager->GetActiveChunks();
	
	int32 CellsRendered = 0;
	
	for (UFluidChunk* Chunk : ActiveChunks)
	{
		if (!ShouldRenderChunk(Chunk, ViewerPos))
			continue;
		
		const int32 ChunkSize = Chunk->ChunkSize;
		const float CellSize = Chunk->CellSize;
		const FVector ChunkOrigin = Chunk->ChunkWorldPosition;
		
		int32 StepSize = 1;
		if (bUseLODForVisualization)
		{
			const FBox ChunkBounds = Chunk->GetWorldBounds();
			const float Distance = ChunkBounds.ComputeSquaredDistanceToPoint(ViewerPos);
			
			if (Distance > 8000000.0f) // > 2828 units
				StepSize = 4;
			else if (Distance > 2000000.0f) // > 1414 units
				StepSize = 2;
		}
		
		for (int32 x = 0; x < ChunkSize; x += StepSize)
		{
			for (int32 y = 0; y < ChunkSize; y += StepSize)
			{
				for (int32 z = 0; z < ChunkSize; z += StepSize)
				{
					if (CellsRendered >= MaxCellsToRenderPerFrame)
						return;
					
					const float FluidLevel = Chunk->GetFluidAt(x, y, z);
					
					if (FluidLevel > MinFluidLevelToRender)
					{
						const FVector CellWorldPos = ChunkOrigin + FVector(x * CellSize, y * CellSize, z * CellSize);
						const float BaseSize = CellSize * 0.9f;
						
						float HorizontalScale = 1.0f;
						float VerticalScale = 1.0f;
						
						if (FluidLevel <= 0.5f)
						{
							HorizontalScale = FluidLevel * 2.0f;
							VerticalScale = 0.05f;
						}
						else
						{
							HorizontalScale = 1.0f;
							VerticalScale = (FluidLevel - 0.5f) * 2.0f;
						}
						
						const float ScaledWidth = BaseSize * HorizontalScale * StepSize;
						const float ScaledHeight = BaseSize * VerticalScale;
						
						const FVector AdjustedPos = CellWorldPos + FVector(0, 0, (ScaledHeight - BaseSize) * 0.5f);
						
						const FColor DebugColor = FColor::MakeRedToGreenColorFromScalar(1.0f - FluidLevel);
						
						DrawDebugBox(GetWorld(), AdjustedPos, FVector(ScaledWidth * 0.5f, ScaledWidth * 0.5f, ScaledHeight * 0.5f), DebugColor, false, -1.0f, 0, 2.0f);
						
						CellsRendered++;
					}
				}
			}
		}
		
		if (bEnableFlowVisualization)
		{
			for (int32 x = 0; x < ChunkSize; x += 4)
			{
				for (int32 y = 0; y < ChunkSize; y += 4)
				{
					for (int32 z = 0; z < ChunkSize; z += 2)
					{
						const float FluidLevel = Chunk->GetFluidAt(x, y, z);
						if (FluidLevel > MinFluidLevelToRender)
						{
							const FVector CellPos = ChunkOrigin + FVector(x * CellSize, y * CellSize, z * CellSize);
							const int32 CellIdx = x + y * ChunkSize + z * ChunkSize * ChunkSize;
							
							if (CellIdx >= 0 && CellIdx < Chunk->Cells.Num())
							{
								const FVector Velocity = Chunk->Cells[CellIdx].FlowVelocity;
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
}

void UFluidVisualizationComponent::UpdateChunkedInstancedMeshes()
{
	if (!ChunkManager || !InstancedMeshComponent)
		return;
	
	InstancedMeshComponent->ClearInstances();
	
	const FVector ViewerPos = GetPrimaryViewerPosition();
	const TArray<UFluidChunk*> ActiveChunks = ChunkManager->GetActiveChunks();
	
	TArray<FTransform> Transforms;
	Transforms.Reserve(MaxCellsToRenderPerFrame);
	
	int32 CellsProcessed = 0;
	
	for (UFluidChunk* Chunk : ActiveChunks)
	{
		if (!ShouldRenderChunk(Chunk, ViewerPos))
			continue;
		
		const int32 ChunkSize = Chunk->ChunkSize;
		const float CellSize = Chunk->CellSize;
		const FVector ChunkOrigin = Chunk->ChunkWorldPosition;
		
		int32 StepSize = 1;
		if (bUseLODForVisualization)
		{
			const FBox ChunkBounds = Chunk->GetWorldBounds();
			const float Distance = ChunkBounds.ComputeSquaredDistanceToPoint(ViewerPos);
			
			if (Distance > 8000000.0f)
				StepSize = 4;
			else if (Distance > 2000000.0f)
				StepSize = 2;
		}
		
		for (int32 x = 0; x < ChunkSize; x += StepSize)
		{
			for (int32 y = 0; y < ChunkSize; y += StepSize)
			{
				for (int32 z = 0; z < ChunkSize; z += StepSize)
				{
					if (CellsProcessed >= MaxCellsToRenderPerFrame)
						break;
					
					const float FluidLevel = Chunk->GetFluidAt(x, y, z);
					
					if (FluidLevel > MinFluidLevelToRender)
					{
						const FVector CellWorldPos = ChunkOrigin + FVector(x * CellSize, y * CellSize, z * CellSize);
						const float Scale = FluidLevel * StepSize;
						
						FTransform InstanceTransform;
						InstanceTransform.SetLocation(CellWorldPos);
						InstanceTransform.SetScale3D(FVector(Scale, Scale, Scale));
						
						Transforms.Add(InstanceTransform);
						CellsProcessed++;
					}
				}
			}
		}
	}
	
	if (Transforms.Num() > 0)
	{
		InstancedMeshComponent->AddInstances(Transforms, false);
	}
}

void UFluidVisualizationComponent::RenderFluidChunk(UFluidChunk* Chunk, const FVector& ViewerPosition)
{
	if (!Chunk || Chunk->State != EChunkState::Active)
		return;
	
	UInstancedStaticMeshComponent* ChunkMeshComponent = nullptr;
	
	if (UInstancedStaticMeshComponent** ExistingComponent = ChunkMeshComponents.Find(Chunk))
	{
		ChunkMeshComponent = *ExistingComponent;
	}
	else
	{
		ChunkMeshComponent = NewObject<UInstancedStaticMeshComponent>(GetOwner());
		ChunkMeshComponent->RegisterComponent();
		ChunkMeshComponent->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
		
		if (FluidCellMesh)
		{
			ChunkMeshComponent->SetStaticMesh(FluidCellMesh);
		}
		
		if (FluidMaterial)
		{
			ChunkMeshComponent->SetMaterial(0, FluidMaterial);
		}
		
		ChunkMeshComponents.Add(Chunk, ChunkMeshComponent);
	}
	
	ChunkMeshComponent->ClearInstances();
	
	TArray<FTransform> Transforms;
	const int32 ChunkSize = Chunk->ChunkSize;
	const float CellSize = Chunk->CellSize;
	const FVector ChunkOrigin = Chunk->ChunkWorldPosition;
	
	for (int32 x = 0; x < ChunkSize; ++x)
	{
		for (int32 y = 0; y < ChunkSize; ++y)
		{
			for (int32 z = 0; z < ChunkSize; ++z)
			{
				const float FluidLevel = Chunk->GetFluidAt(x, y, z);
				
				if (FluidLevel > MinFluidLevelToRender)
				{
					const FVector CellWorldPos = ChunkOrigin + FVector(x * CellSize, y * CellSize, z * CellSize);
					
					FTransform InstanceTransform;
					InstanceTransform.SetLocation(CellWorldPos);
					InstanceTransform.SetScale3D(FVector(FluidLevel));
					
					Transforms.Add(InstanceTransform);
				}
			}
		}
	}
	
	if (Transforms.Num() > 0)
	{
		ChunkMeshComponent->AddInstances(Transforms, false);
	}
}

bool UFluidVisualizationComponent::ShouldRenderChunk(UFluidChunk* Chunk, const FVector& ViewerPosition) const
{
	if (!Chunk)
		return false;
	
	if (Chunk->State != EChunkState::Active)
		return false;
	
	if (!Chunk->HasActiveFluid())
		return false;
	
	const FBox ChunkBounds = Chunk->GetWorldBounds();
	const float Distance = FVector::Dist(ChunkBounds.GetCenter(), ViewerPosition);
	
	return Distance <= MaxRenderDistance;
}

FVector UFluidVisualizationComponent::GetPrimaryViewerPosition() const
{
	UWorld* World = GetWorld();
	if (!World)
		return GetComponentLocation();
	
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			return Pawn->GetActorLocation();
		}
	}
	
	return GetComponentLocation();
}

void UFluidVisualizationComponent::DrawChunkBounds() const
{
	if (!ChunkManager || !GetWorld())
		return;
	
	const TArray<UFluidChunk*> ActiveChunks = ChunkManager->GetActiveChunks();
	
	for (UFluidChunk* Chunk : ActiveChunks)
	{
		if (!Chunk)
			continue;
		
		const FBox ChunkBounds = Chunk->GetWorldBounds();
		FColor BoundColor = FColor::White;
		
		switch (Chunk->State)
		{
			case EChunkState::Active:
				BoundColor = FColor::Green;
				break;
			case EChunkState::Inactive:
				BoundColor = FColor::Yellow;
				break;
			case EChunkState::BorderOnly:
				BoundColor = FColor::Orange;
				break;
			default:
				BoundColor = FColor::Red;
				break;
		}
		
		DrawDebugBox(GetWorld(), ChunkBounds.GetCenter(), ChunkBounds.GetExtent(), BoundColor, false, -1.0f, 0, 1.0f);
		
		const FString ChunkInfo = FString::Printf(TEXT("Chunk %s\nLOD: %d\nCells: %d"),
			*Chunk->ChunkCoord.ToString(), Chunk->CurrentLOD, Chunk->GetActiveCellCount());
		
		DrawDebugString(GetWorld(), ChunkBounds.GetCenter(), ChunkInfo, nullptr, BoundColor, -1.0f);
	}
}