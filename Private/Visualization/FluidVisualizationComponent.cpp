#include "Visualization/FluidVisualizationComponent.h"
#include "Visualization/MarchingCubes.h"
#include "CellularAutomata/CAFluidGrid.h"
#include "CellularAutomata/FluidChunkManager.h"
#include "CellularAutomata/FluidChunk.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
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
	else if (RenderMode == EFluidRenderMode::MarchingCubes && !MarchingCubesMesh)
	{
		MarchingCubesMesh = NewObject<UProceduralMeshComponent>(GetOwner(), UProceduralMeshComponent::StaticClass());
		MarchingCubesMesh->RegisterComponent();
		MarchingCubesMesh->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
		
		if (FluidMaterial)
		{
			MarchingCubesMesh->SetMaterial(0, FluidMaterial);
		}
	}
}

void UFluidVisualizationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	if (!FluidGrid)
		return;
	
	// Handle smooth interpolation for marching cubes
	if (RenderMode == EFluidRenderMode::MarchingCubes && bSmoothMeshUpdates)
	{
		UpdateDensityInterpolation(DeltaTime);
	}
	
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
							// Velocity tracking removed in simplified CA
							const FVector Velocity = FVector::ZeroVector;
							if (false) // Disabled velocity visualization
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
			case EFluidRenderMode::MarchingCubes:
				GenerateMarchingCubesVisualization();
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
		case EFluidRenderMode::MarchingCubes:
			GenerateChunkedMarchingCubes();
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
								// Velocity tracking removed in simplified CA
								const FVector Velocity = FVector::ZeroVector;
								if (false) // Disabled velocity visualization
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
	
	// Use the more detailed chunk manager debug visualization instead
	ChunkManager->DrawDebugChunks(GetWorld());
}

void UFluidVisualizationComponent::GenerateMarchingCubesVisualization()
{
	if (!FluidGrid || !MarchingCubesMesh)
		return;

	// Create density grid from fluid grid
	TArray<float> NewDensityGrid;
	const int32 GridSizeX = FluidGrid->GridSizeX;
	const int32 GridSizeY = FluidGrid->GridSizeY;
	const int32 GridSizeZ = FluidGrid->GridSizeZ;
	
	NewDensityGrid.Reserve(GridSizeX * GridSizeY * GridSizeZ);
	
	// Convert fluid levels to density values
	for (int32 Z = 0; Z < GridSizeZ; ++Z)
	{
		for (int32 Y = 0; Y < GridSizeY; ++Y)
		{
			for (int32 X = 0; X < GridSizeX; ++X)
			{
				const float FluidLevel = FluidGrid->GetFluidAt(X, Y, Z);
				NewDensityGrid.Add(FluidLevel);
			}
		}
	}
	
	// Apply density smoothing to reduce gaps
	if (bEnableDensitySmoothing)
	{
		SmoothDensityGrid(NewDensityGrid, FIntVector(GridSizeX, GridSizeY, GridSizeZ));
	}
	
	// Use smooth interpolation if enabled
	TArray<float> DensityGrid;
	if (bSmoothMeshUpdates)
	{
		// Check if we need to update the mesh based on threshold
		if (!ShouldUpdateMesh(NewDensityGrid))
		{
			// Use interpolated values
			DensityGrid = InterpolatedDensityGrid;
		}
		else
		{
			// Store new target and reset interpolation
			PreviousDensityGrid = CurrentDensityGrid;
			CurrentDensityGrid = NewDensityGrid;
			InterpolationAlpha = 0.0f;
			
			// Initialize interpolated grid
			if (InterpolatedDensityGrid.Num() != NewDensityGrid.Num())
			{
				InterpolatedDensityGrid = PreviousDensityGrid;
			}
			
			DensityGrid = InterpolatedDensityGrid;
		}
	}
	else
	{
		// Direct update without interpolation
		DensityGrid = NewDensityGrid;
	}
	
	// Generate marching cubes mesh
	TArray<FMarchingCubes::FMarchingCubesVertex> MarchingVertices;
	TArray<FMarchingCubes::FMarchingCubesTriangle> MarchingTriangles;
	
	FMarchingCubes::GenerateGridMesh(
		DensityGrid,
		FIntVector(GridSizeX, GridSizeY, GridSizeZ),
		FluidGrid->CellSize,
		FluidGrid->GridOrigin,
		MarchingCubesIsoLevel,
		MarchingVertices,
		MarchingTriangles
	);
	
	// Convert to UE4 procedural mesh format
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> VertexColors;
	
	Vertices.Reserve(MarchingVertices.Num());
	Normals.Reserve(MarchingVertices.Num());
	UVs.Reserve(MarchingVertices.Num());
	VertexColors.Reserve(MarchingVertices.Num());
	Triangles.Reserve(MarchingTriangles.Num() * 3);
	
	// Convert vertices
	for (const auto& MarchingVertex : MarchingVertices)
	{
		Vertices.Add(MarchingVertex.Position);
		Normals.Add(bFlipNormals ? -MarchingVertex.Normal : MarchingVertex.Normal);
		UVs.Add(MarchingVertex.UV);
		VertexColors.Add(FColor::Blue); // Default fluid color
	}
	
	// Convert triangles (correct winding order for upward-facing normals)
	for (const auto& MarchingTriangle : MarchingTriangles)
	{
		Triangles.Add(MarchingTriangle.VertexIndices[0]);
		Triangles.Add(MarchingTriangle.VertexIndices[1]);
		Triangles.Add(MarchingTriangle.VertexIndices[2]);
	}
	
	// Update procedural mesh
	MarchingCubesMesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, VertexColors, TArray<FProcMeshTangent>(), bGenerateCollision);
}

void UFluidVisualizationComponent::GenerateChunkedMarchingCubes()
{
	if (!ChunkManager)
		return;
		
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_MarchingCubes);
	
	const FVector ViewerPos = GetPrimaryViewerPosition();
	const TArray<UFluidChunk*> ActiveChunks = ChunkManager->GetActiveChunks();
	const float CurrentTime = FPlatformTime::Seconds();
	
	// Reset rendering stats counters
	int32 RenderedChunks = 0;
	int32 CachedMeshesUsed = 0;
	int32 MeshesGenerated = 0;
	int32 LOD0Meshes = 0;
	int32 LOD1Meshes = 0;
	int32 LOD2Meshes = 0;
	
	// Step 0: Periodically check which chunks need updates
	ChunkMeshCheckTimer += GetWorld()->GetDeltaSeconds();
	if (ChunkMeshCheckTimer >= ChunkMeshCheckInterval)
	{
		ChunkMeshCheckTimer = 0.0f;
		ChunksNeedingMeshUpdate.Empty();
		
		// Check each active chunk to see if it needs an update
		for (UFluidChunk* Chunk : ActiveChunks)
		{
			if (!Chunk || !ShouldRenderChunk(Chunk, ViewerPos))
				continue;
				
			// Check if chunk has been updated recently
			float* LastUpdateTime = ChunkLastMeshUpdateTime.Find(Chunk);
			float TimeSinceUpdate = LastUpdateTime ? (CurrentTime - *LastUpdateTime) : 999.0f;
			
			// Only consider update if enough time has passed and chunk is dirty
			if (TimeSinceUpdate > 0.5f && Chunk->ShouldRegenerateMesh())
			{
				ChunksNeedingMeshUpdate.Add(Chunk);
			}
		}
	}
	
	// Step 1: Clean up meshes for chunks that are no longer active or out of range
	TArray<UFluidChunk*> ChunksToRemove;
	for (auto& ChunkMeshPair : ChunkMarchingCubesMeshes)
	{
		UFluidChunk* Chunk = ChunkMeshPair.Key;
		UProceduralMeshComponent* ChunkMesh = ChunkMeshPair.Value;
		
		// Check if chunk is still active and within render range
		bool bShouldKeep = false;
		if (Chunk && ChunkMesh)
		{
			// Check if chunk is still in active chunks list
			if (ActiveChunks.Contains(Chunk))
			{
				// Check if chunk should still be rendered based on distance and state
				if (ShouldRenderChunk(Chunk, ViewerPos))
				{
					bShouldKeep = true;
				}
			}
		}
		
		if (!bShouldKeep)
		{
			// Clean up the mesh component
			if (ChunkMesh && IsValid(ChunkMesh))
			{
				ChunkMesh->ClearAllMeshSections();
				ChunkMesh->DestroyComponent();
			}
			ChunksToRemove.Add(Chunk);
		}
	}
	
	// Remove cleaned up chunks from our tracking map
	for (UFluidChunk* ChunkToRemove : ChunksToRemove)
	{
		ChunkMarchingCubesMeshes.Remove(ChunkToRemove);
	}
	
	// Step 2: Process chunks that need mesh updates (limited per frame)
	int32 ChunksUpdatedThisFrame = 0;
	
	// First process chunks that explicitly need updates
	for (UFluidChunk* Chunk : ChunksNeedingMeshUpdate)
	{
		if (ChunksUpdatedThisFrame >= MaxChunksToUpdatePerFrame)
			break;
			
		if (!Chunk || !ShouldRenderChunk(Chunk, ViewerPos))
			continue;
			
		// Calculate LOD level based on distance
		const FBox ChunkBounds = Chunk->GetWorldBounds();
		const float Distance = FVector::Dist(ChunkBounds.GetCenter(), ViewerPos);
		int32 LODLevel = CalculateLODLevel(Distance);
		
		// Get or create procedural mesh component for this chunk
		UProceduralMeshComponent* ChunkMesh = nullptr;
		
		if (UProceduralMeshComponent** ExistingMesh = ChunkMarchingCubesMeshes.Find(Chunk))
		{
			ChunkMesh = *ExistingMesh;
		}
		else
		{
			ChunkMesh = NewObject<UProceduralMeshComponent>(GetOwner());
			ChunkMesh->RegisterComponent();
			ChunkMesh->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
			
			if (FluidMaterial)
			{
				ChunkMesh->SetMaterial(0, FluidMaterial);
			}
			
			ChunkMarchingCubesMeshes.Add(Chunk, ChunkMesh);
		}
		
		// Check if we can use cached mesh data first
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FColor> VertexColors;
		
		bool bUsedCachedMesh = false;
		
		// Try to use cached mesh data if available and valid
		if (Chunk->HasValidMeshData(LODLevel, MarchingCubesIsoLevel))
		{
			// Use stored mesh data
			const FChunkMeshData& StoredData = Chunk->StoredMeshData;
			Vertices = StoredData.Vertices;
			Triangles = StoredData.Triangles;
			Normals = StoredData.Normals;
			UVs = StoredData.UVs;
			VertexColors = StoredData.VertexColors;
			bUsedCachedMesh = true;
			CachedMeshesUsed++;
		}
		else
		{
			// Generate new mesh data
			TArray<FMarchingCubes::FMarchingCubesVertex> MarchingVertices;
			TArray<FMarchingCubes::FMarchingCubesTriangle> MarchingTriangles;
			
			GenerateChunkMeshWithLOD(Chunk, LODLevel, MarchingVertices, MarchingTriangles);
			
			// Convert to UE4 procedural mesh format
			if (MarchingVertices.Num() > 0)
			{
				Vertices.Reserve(MarchingVertices.Num());
				Normals.Reserve(MarchingVertices.Num());
				UVs.Reserve(MarchingVertices.Num());
				VertexColors.Reserve(MarchingVertices.Num());
				Triangles.Reserve(MarchingTriangles.Num() * 3);
				
				// Convert vertices
				for (const auto& MarchingVertex : MarchingVertices)
				{
					Vertices.Add(MarchingVertex.Position);
					Normals.Add(bFlipNormals ? -MarchingVertex.Normal : MarchingVertex.Normal);
					UVs.Add(MarchingVertex.UV);
					
					// Color based on height for visual interest (fade with LOD)
					const float HeightFactor = FMath::Clamp((MarchingVertex.Position.Z - Chunk->ChunkWorldPosition.Z) / (Chunk->ChunkSize * Chunk->CellSize), 0.0f, 1.0f);
					FColor VertexColor = FColor::MakeRedToGreenColorFromScalar(HeightFactor);
					
					// Fade color with distance/LOD for performance indication
					if (LODLevel > 0)
					{
						const float LODFade = FMath::Clamp(1.0f - (LODLevel * 0.2f), 0.5f, 1.0f);
						VertexColor.R = (uint8)(VertexColor.R * LODFade);
						VertexColor.G = (uint8)(VertexColor.G * LODFade);
						VertexColor.B = (uint8)(VertexColor.B * LODFade);
					}
					
					VertexColors.Add(VertexColor);
				}
				
				// Convert triangles (correct winding order for upward-facing normals)
				for (const auto& MarchingTriangle : MarchingTriangles)
				{
					Triangles.Add(MarchingTriangle.VertexIndices[0]);
					Triangles.Add(MarchingTriangle.VertexIndices[1]);
					Triangles.Add(MarchingTriangle.VertexIndices[2]);
				}
				
				// Store the generated mesh data for persistence
				Chunk->StoreMeshData(Vertices, Triangles, Normals, UVs, VertexColors, MarchingCubesIsoLevel, LODLevel);
				MeshesGenerated++;
			}
		}
		
		// Count LOD statistics
		switch (LODLevel)
		{
			case 0: LOD0Meshes++; break;
			case 1: LOD1Meshes++; break;
			case 2: LOD2Meshes++; break;
		}
		
		if (Vertices.Num() > 0)
		{
			// Update procedural mesh with either cached or newly generated data
			ChunkMesh->ClearAllMeshSections();
			ChunkMesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, VertexColors, TArray<FProcMeshTangent>(), bGenerateCollision);
			RenderedChunks++;
			
			// Track that we updated this chunk
			ChunkLastMeshUpdateTime.Add(Chunk, CurrentTime);
			ChunksUpdatedThisFrame++;
			
			// Remove from pending updates
			ChunksNeedingMeshUpdate.Remove(Chunk);
		}
		else
		{
			// No fluid in chunk, clear mesh
			ChunkMesh->ClearAllMeshSections();
			ChunkLastMeshUpdateTime.Add(Chunk, CurrentTime);
		}
	}
	
	// Step 3: Also check for chunks that have never been rendered (new chunks)
	for (UFluidChunk* Chunk : ActiveChunks)
	{
		if (ChunksUpdatedThisFrame >= MaxChunksToUpdatePerFrame)
			break;
			
		// Skip if already processed or doesn't need rendering
		if (!Chunk || !ShouldRenderChunk(Chunk, ViewerPos))
			continue;
			
		// Skip if already has a mesh
		if (ChunkMarchingCubesMeshes.Contains(Chunk))
			continue;
			
		// This is a new chunk that needs initial mesh generation
		ChunksNeedingMeshUpdate.Add(Chunk);
		
		// Process it immediately if we have capacity
		if (ChunksUpdatedThisFrame < MaxChunksToUpdatePerFrame)
		{
			// [The same mesh generation code would go here, but it's already in the loop above]
			// For now, we'll let it be processed in the next frame
		}
	}
	
	// === Update Rendering Statistics ===
	SET_DWORD_STAT(STAT_VoxelFluid_RenderedChunks, RenderedChunks);
	SET_DWORD_STAT(STAT_VoxelFluid_CachedMeshes, CachedMeshesUsed);
	SET_DWORD_STAT(STAT_VoxelFluid_GeneratedMeshes, MeshesGenerated);
	SET_DWORD_STAT(STAT_VoxelFluid_LOD0Meshes, LOD0Meshes);
	SET_DWORD_STAT(STAT_VoxelFluid_LOD1Meshes, LOD1Meshes);
	SET_DWORD_STAT(STAT_VoxelFluid_LOD2Meshes, LOD2Meshes);
	
	// Log performance info periodically
	static float LastLogTime = 0.0f;
	if (CurrentTime - LastLogTime > 2.0f)
	{
		UE_LOG(LogTemp, Log, TEXT("Marching Cubes: %d chunks rendered, %d cached, %d generated this frame, %d pending updates"),
			RenderedChunks, CachedMeshesUsed, MeshesGenerated, ChunksNeedingMeshUpdate.Num());
		LastLogTime = CurrentTime;
	}
}

void UFluidVisualizationComponent::UpdateDensityInterpolation(float DeltaTime)
{
	if (InterpolationAlpha >= 1.0f || CurrentDensityGrid.Num() == 0 || PreviousDensityGrid.Num() == 0)
		return;
	
	// Update interpolation alpha
	InterpolationAlpha = FMath::Min(1.0f, InterpolationAlpha + DeltaTime * MeshInterpolationSpeed);
	
	// Interpolate density values
	InterpolateDensityGrids();
}

void UFluidVisualizationComponent::InterpolateDensityGrids()
{
	if (CurrentDensityGrid.Num() != PreviousDensityGrid.Num())
		return;
		
	InterpolatedDensityGrid.SetNum(CurrentDensityGrid.Num());
	
	for (int32 i = 0; i < CurrentDensityGrid.Num(); ++i)
	{
		InterpolatedDensityGrid[i] = FMath::Lerp(PreviousDensityGrid[i], CurrentDensityGrid[i], InterpolationAlpha);
	}
}

bool UFluidVisualizationComponent::ShouldUpdateMesh(const TArray<float>& NewDensityGrid) const
{
	if (CurrentDensityGrid.Num() != NewDensityGrid.Num())
		return true;
		
	// Check if changes are significant enough to warrant an update
	float MaxDifference = 0.0f;
	for (int32 i = 0; i < NewDensityGrid.Num(); ++i)
	{
		const float Difference = FMath::Abs(NewDensityGrid[i] - CurrentDensityGrid[i]);
		MaxDifference = FMath::Max(MaxDifference, Difference);
	}
	
	return MaxDifference > MeshUpdateThreshold;
}

void UFluidVisualizationComponent::SmoothDensityGrid(TArray<float>& DensityGrid, const FIntVector& GridSize) const
{
	if (!bEnableDensitySmoothing || SmoothingIterations <= 0)
		return;
	
	// Apply multiple passes of smoothing
	for (int32 Iteration = 0; Iteration < SmoothingIterations; ++Iteration)
	{
		ApplyGaussianSmoothing(DensityGrid, GridSize, SmoothingStrength);
	}
}

void UFluidVisualizationComponent::ApplyGaussianSmoothing(TArray<float>& DensityGrid, const FIntVector& GridSize, float Strength) const
{
	TArray<float> SmoothedGrid = DensityGrid;
	
	// Apply 3x3x3 gaussian-like kernel smoothing
	for (int32 X = 1; X < GridSize.X - 1; ++X)
	{
		for (int32 Y = 1; Y < GridSize.Y - 1; ++Y)
		{
			for (int32 Z = 1; Z < GridSize.Z - 1; ++Z)
			{
				const int32 CenterIndex = X + Y * GridSize.X + Z * GridSize.X * GridSize.Y;
				const float CenterValue = DensityGrid[CenterIndex];
				
				// Calculate weighted average of neighbors
				float WeightedSum = 0.0f;
				float TotalWeight = 0.0f;
				
				// Center weight (highest)
				const float CenterWeight = 4.0f;
				WeightedSum += CenterValue * CenterWeight;
				TotalWeight += CenterWeight;
				
				// Face neighbors (medium weight)
				const float FaceWeight = 2.0f;
				const int32 FaceOffsets[6][3] = {
					{-1, 0, 0}, {1, 0, 0},   // X neighbors
					{0, -1, 0}, {0, 1, 0},   // Y neighbors
					{0, 0, -1}, {0, 0, 1}    // Z neighbors
				};
				
				for (int32 i = 0; i < 6; ++i)
				{
					const int32 NeighborX = X + FaceOffsets[i][0];
					const int32 NeighborY = Y + FaceOffsets[i][1];
					const int32 NeighborZ = Z + FaceOffsets[i][2];
					const int32 NeighborIndex = NeighborX + NeighborY * GridSize.X + NeighborZ * GridSize.X * GridSize.Y;
					
					if (NeighborIndex >= 0 && NeighborIndex < DensityGrid.Num())
					{
						WeightedSum += DensityGrid[NeighborIndex] * FaceWeight;
						TotalWeight += FaceWeight;
					}
				}
				
				// Edge neighbors (lower weight)
				const float EdgeWeight = 1.0f;
				const int32 EdgeOffsets[12][3] = {
					// Edges along X axis
					{-1, -1, 0}, {-1, 1, 0}, {1, -1, 0}, {1, 1, 0},
					// Edges along Y axis
					{-1, 0, -1}, {-1, 0, 1}, {1, 0, -1}, {1, 0, 1},
					// Edges along Z axis
					{0, -1, -1}, {0, -1, 1}, {0, 1, -1}, {0, 1, 1}
				};
				
				for (int32 i = 0; i < 12; ++i)
				{
					const int32 NeighborX = X + EdgeOffsets[i][0];
					const int32 NeighborY = Y + EdgeOffsets[i][1];
					const int32 NeighborZ = Z + EdgeOffsets[i][2];
					
					if (NeighborX >= 0 && NeighborX < GridSize.X &&
						NeighborY >= 0 && NeighborY < GridSize.Y &&
						NeighborZ >= 0 && NeighborZ < GridSize.Z)
					{
						const int32 NeighborIndex = NeighborX + NeighborY * GridSize.X + NeighborZ * GridSize.X * GridSize.Y;
						WeightedSum += DensityGrid[NeighborIndex] * EdgeWeight;
						TotalWeight += EdgeWeight;
					}
				}
				
				// Calculate smoothed value
				const float SmoothedValue = WeightedSum / TotalWeight;
				
				// Lerp between original and smoothed value based on strength
				SmoothedGrid[CenterIndex] = FMath::Lerp(CenterValue, SmoothedValue, Strength);
			}
		}
	}
	
	DensityGrid = SmoothedGrid;
}

float UFluidVisualizationComponent::GetSmoothedDensity(const TArray<float>& DensityGrid, const FIntVector& GridSize, int32 X, int32 Y, int32 Z) const
{
	// Bounds checking
	if (X < 0 || X >= GridSize.X || Y < 0 || Y >= GridSize.Y || Z < 0 || Z >= GridSize.Z)
		return 0.0f;
		
	const int32 Index = X + Y * GridSize.X + Z * GridSize.X * GridSize.Y;
	if (Index < 0 || Index >= DensityGrid.Num())
		return 0.0f;
		
	return DensityGrid[Index];
}

int32 UFluidVisualizationComponent::CalculateLODLevel(float Distance) const
{
	// Use the chunk system's LOD distances from VoxelFluidActor
	// LOD 0: Close detail (0 to LOD1Distance)
	// LOD 1: Medium detail (LOD1Distance to LOD2Distance) 
	// LOD 2: Low detail (LOD2Distance to MaxRenderDistance)
	
	if (Distance <= 2000.0f) // LOD1Distance equivalent
	{
		return 0; // Full detail
	}
	else if (Distance <= 4000.0f) // LOD2Distance equivalent  
	{
		return 1; // Reduced detail
	}
	else
	{
		return 2; // Minimal detail
	}
}

void UFluidVisualizationComponent::GenerateChunkMeshWithLOD(UFluidChunk* Chunk, int32 LODLevel, TArray<FMarchingCubes::FMarchingCubesVertex>& OutVertices, TArray<FMarchingCubes::FMarchingCubesTriangle>& OutTriangles)
{
	if (!Chunk || !ChunkManager)
		return;
		
	// Adjust marching cubes generation based on LOD level
	switch (LODLevel)
	{
		case 0: // Full detail - use seamless generation
			FMarchingCubes::GenerateSeamlessChunkMesh(Chunk, ChunkManager, MarchingCubesIsoLevel, OutVertices, OutTriangles);
			break;
			
		case 1: // Medium detail - use regular generation with slightly higher iso level for performance
			FMarchingCubes::GenerateSeamlessChunkMesh(Chunk, ChunkManager, MarchingCubesIsoLevel * 1.2f, OutVertices, OutTriangles);
			break;
			
		case 2: // Low detail - use basic generation without seamless boundaries for best performance
		default:
			FMarchingCubes::GenerateChunkMesh(Chunk, MarchingCubesIsoLevel * 1.5f, OutVertices, OutTriangles);
			break;
	}
}