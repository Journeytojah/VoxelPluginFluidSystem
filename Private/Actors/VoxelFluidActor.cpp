#include "Actors/VoxelFluidActor.h"
#include "CellularAutomata/CAFluidGrid.h"
#include "CellularAutomata/FluidChunkManager.h"
#include "CellularAutomata/FluidChunk.h"
#include "VoxelIntegration/VoxelFluidIntegration.h"
#include "Visualization/FluidVisualizationComponent.h"
#include "Components/BoxComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "VoxelFluidStats.h"
#include "GameFramework/PlayerController.h"

AVoxelFluidActor::AVoxelFluidActor()
{
	PrimaryActorTick.bCanEverTick = true;
	
	BoundsComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("BoundsComponent"));
	RootComponent = BoundsComponent;
	BoundsComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	
	FluidGrid = CreateDefaultSubobject<UCAFluidGrid>(TEXT("FluidGrid"));
	
	ChunkManager = CreateDefaultSubobject<UFluidChunkManager>(TEXT("ChunkManager"));
	
	VoxelIntegration = CreateDefaultSubobject<UVoxelFluidIntegration>(TEXT("VoxelIntegration"));
	
	VisualizationComponent = CreateDefaultSubobject<UFluidVisualizationComponent>(TEXT("VisualizationComponent"));
	VisualizationComponent->SetupAttachment(RootComponent);
	
	GridSizeX = 128;
	GridSizeY = 128;
	GridSizeZ = 32;
	CellSize = 100.0f;
	bUseChunkedSystem = true;
	ChunkSize = 32;
	ChunkLoadDistance = 8000.0f;
	ChunkActiveDistance = 5000.0f;
	MaxActiveChunks = 64;
	MaxLoadedChunks = 128;
	bUseAsyncChunkLoading = true;
	LOD1Distance = 2000.0f;
	LOD2Distance = 4000.0f;
	FluidViscosity = 0.1f;
	FluidFlowRate = 0.5f;
	GravityStrength = 981.0f;
	bAutoStart = true;
	bIsSimulating = false;
	SimulationSpeed = 1.0f;
	bShowDebugGrid = false;
	bShowFlowVectors = false;
	DebugFluidSpawnAmount = 1.0f;
}

void AVoxelFluidActor::BeginPlay()
{
	Super::BeginPlay();
	
	InitializeFluidSystem();
	
	if (bAutoStart)
	{
		StartSimulation();
	}
	
	UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: BeginPlay completed, ChunkedSystem=%s"), 
		   bUseChunkedSystem ? TEXT("True") : TEXT("False"));
}

void AVoxelFluidActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopSimulation();
	
	if (bUseChunkedSystem && ChunkManager)
	{
		ChunkManager->ClearAllChunks();
	}
	else if (FluidGrid)
	{
		FluidGrid->ClearGrid();
	}
	
	FluidSources.Empty();
	
	UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: EndPlay completed, Reason: %d"), (int32)EndPlayReason);
	
	Super::EndPlay(EndPlayReason);
}

void AVoxelFluidActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	// Update grid origin if actor has moved
	UpdateGridOriginForMovement();
	
	if (bIsSimulating)
	{
		const double StartTime = FPlatformTime::Seconds();
		
		if (bUseFixedTimestep)
		{
			// Accumulate time for fixed timestep simulation
			SimulationAccumulator += DeltaTime * SimulationSpeed;
			
			// Run simulation steps at fixed timestep
			while (SimulationAccumulator >= SimulationTimestep)
			{
				if (bUseChunkedSystem && ChunkManager)
				{
					UpdateChunkedSystem(SimulationTimestep);
				}
				else if (FluidGrid)
				{
					UpdateFluidSources(SimulationTimestep);
					FluidGrid->UpdateSimulation(SimulationTimestep);
				}
				
				SimulationAccumulator -= SimulationTimestep;
			}
		}
		else
		{
			// Variable timestep simulation
			const float ScaledDeltaTime = DeltaTime * SimulationSpeed;
			
			if (bUseChunkedSystem && ChunkManager)
			{
				UpdateChunkedSystem(ScaledDeltaTime);
			}
			else if (FluidGrid)
			{
				UpdateFluidSources(ScaledDeltaTime);
				FluidGrid->UpdateSimulation(ScaledDeltaTime);
			}
		}
		
		LastFrameSimulationTime = (FPlatformTime::Seconds() - StartTime) * 1000.0f; // Convert to ms
	}
	
	if (bShowDebugGrid || bShowFlowVectors)
	{
		UpdateDebugVisualization();
	}

	VisualizationComponent->UpdateVisualization();
}

void AVoxelFluidActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	
	CalculateGridBounds();
	UpdateBounds();
}

void AVoxelFluidActor::InitializeFluidSystem()
{
	CalculateGridBounds();
	
	if (bUseChunkedSystem)
	{
		InitializeChunkedSystem();
	}
	else
	{
		if (!FluidGrid)
		{
			FluidGrid = NewObject<UCAFluidGrid>(this, UCAFluidGrid::StaticClass());
		}
		
		if (FluidGrid)
		{
			FluidGrid->InitializeGrid(GridSizeX, GridSizeY, GridSizeZ, CellSize, CalculatedGridOrigin);
			FluidGrid->FlowRate = FluidFlowRate;
			FluidGrid->Viscosity = FluidViscosity;
			FluidGrid->Gravity = GravityStrength;
			FluidGrid->bAllowFluidEscape = bAllowFluidEscape;
		}
	}
	
	if (VoxelIntegration)
	{
		if (bUseChunkedSystem && ChunkManager)
		{
			VoxelIntegration->SetChunkManager(ChunkManager);
			VoxelIntegration->GridResolutionX = GridSizeX;
			VoxelIntegration->GridResolutionY = GridSizeY;
			VoxelIntegration->GridResolutionZ = GridSizeZ;
			VoxelIntegration->CellWorldSize = CellSize;
			VoxelIntegration->bDebugDrawCells = bShowDebugGrid;
			VoxelIntegration->bEnableFlowVisualization = bShowFlowVectors;
			
			if (TargetVoxelWorld)
			{
				VoxelIntegration->InitializeFluidSystem(TargetVoxelWorld);
			}
			
			// Bind to chunk loaded delegate to refresh terrain data
			ChunkManager->OnChunkLoadedDelegate.AddLambda([this](const FFluidChunkCoord& ChunkCoord)
			{
				if (VoxelIntegration && VoxelIntegration->IsVoxelWorldValid())
				{
					VoxelIntegration->UpdateTerrainForChunkCoord(ChunkCoord);
				}
			});
		}
		else if (FluidGrid)
		{
			VoxelIntegration->FluidGrid = FluidGrid;
			VoxelIntegration->GridResolutionX = GridSizeX;
			VoxelIntegration->GridResolutionY = GridSizeY;
			VoxelIntegration->GridResolutionZ = GridSizeZ;
			VoxelIntegration->CellWorldSize = CellSize;
			VoxelIntegration->bDebugDrawCells = bShowDebugGrid;
			VoxelIntegration->bEnableFlowVisualization = bShowFlowVectors;
			
			if (TargetVoxelWorld)
			{
				VoxelIntegration->InitializeFluidSystem(TargetVoxelWorld);
			}
		}
	}
	
	if (VisualizationComponent)
	{
		if (bUseChunkedSystem && ChunkManager)
		{
			VisualizationComponent->SetChunkManager(ChunkManager);
		}
		else if (FluidGrid)
		{
			VisualizationComponent->SetFluidGrid(FluidGrid);
		}
	}
	
	UpdateBounds();
}

void AVoxelFluidActor::StartSimulation()
{
	bIsSimulating = true;
	
	UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Simulation started"));
}

void AVoxelFluidActor::StopSimulation()
{
	bIsSimulating = false;
	
	UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Simulation stopped"));
}

void AVoxelFluidActor::ResetSimulation()
{
	StopSimulation();
	
	if (bUseChunkedSystem && ChunkManager)
	{
		ChunkManager->ClearAllChunks();
	}
	else if (FluidGrid)
	{
		FluidGrid->ClearGrid();
	}
	
	FluidSources.Empty();
	
	if (VoxelIntegration && TargetVoxelWorld)
	{
		if (bUseChunkedSystem)
		{
			VoxelIntegration->UpdateChunkedTerrainHeights();
		}
		else
		{
			VoxelIntegration->SyncWithVoxelTerrain();
		}
	}
	
	UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Simulation reset"));
}

void AVoxelFluidActor::AddFluidSource(const FVector& WorldPosition, float FlowRate)
{
	// Use DefaultSourceFlowRate if no flow rate is specified
	const float ActualFlowRate = (FlowRate < 0.0f) ? DefaultSourceFlowRate : FlowRate;
	
	if (FluidSources.Contains(WorldPosition))
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Fluid source already exists at %s, updating flow rate from %f to %f"), 
			   *WorldPosition.ToString(), FluidSources[WorldPosition], ActualFlowRate);
		FluidSources[WorldPosition] = ActualFlowRate;
	}
	else
	{
		FluidSources.Add(WorldPosition, ActualFlowRate);
		UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Added new fluid source at %s with flow rate %f"), 
			   *WorldPosition.ToString(), ActualFlowRate);
	}
}

void AVoxelFluidActor::RemoveFluidSource(const FVector& WorldPosition)
{
	FluidSources.Remove(WorldPosition);
	
	UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Removed fluid source at %s"), *WorldPosition.ToString());
}

void AVoxelFluidActor::AddFluidAtLocation(const FVector& WorldPosition, float Amount)
{
	if (bUseChunkedSystem && ChunkManager)
	{
		ChunkManager->AddFluidAtWorldPosition(WorldPosition, Amount);
	}
	else if (VoxelIntegration)
	{
		VoxelIntegration->AddFluidAtWorldPosition(WorldPosition, Amount);
	}
	else if (!bUseChunkedSystem && FluidGrid)
	{
		// Convert world position to grid coordinates
		int32 CellX, CellY, CellZ;
		if (FluidGrid->GetCellFromWorldPosition(WorldPosition, CellX, CellY, CellZ))
		{
			FluidGrid->AddFluid(CellX, CellY, CellZ, Amount);
		}
	}
}

void AVoxelFluidActor::SetVoxelWorld(AActor* InVoxelWorld)
{
	TargetVoxelWorld = InVoxelWorld;
	
	if (VoxelIntegration && TargetVoxelWorld)
	{
		VoxelIntegration->InitializeFluidSystem(TargetVoxelWorld);
	}
}

void AVoxelFluidActor::RefreshTerrainData()
{
	if (VoxelIntegration)
	{
		if (bUseChunkedSystem)
		{
			VoxelIntegration->UpdateChunkedTerrainHeights();
		}
		else
		{
			VoxelIntegration->UpdateTerrainHeights();
		}
		
		UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Terrain data refreshed (%s)"), 
			   bUseChunkedSystem ? TEXT("Chunked") : TEXT("Grid"));
	}
}

void AVoxelFluidActor::TestFluidSpawn()
{
	if (!bUseChunkedSystem && !FluidGrid)
	{
		InitializeFluidSystem();
	}
	else if (bUseChunkedSystem && !ChunkManager)
	{
		InitializeFluidSystem();
	}
	
	if (bUseChunkedSystem && ChunkManager)
	{
		// Spawn fluid at the center of the world bounds
		const FVector WorldCenter = GetActorLocation();
		const FVector SpawnPos = WorldCenter + FVector(0, 0, 500.0f); // Spawn above center
		
		// Create a 5x5x3 area of fluid
		for (int32 dx = -2; dx <= 2; ++dx)
		{
			for (int32 dy = -2; dy <= 2; ++dy)
			{
				for (int32 dz = 0; dz <= 2; ++dz)
				{
					const FVector FluidSpawnPos = SpawnPos + FVector(dx * CellSize, dy * CellSize, dz * CellSize);
					ChunkManager->AddFluidAtWorldPosition(FluidSpawnPos, DebugFluidSpawnAmount);
				}
			}
		}
		
		UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Test fluid spawned in chunked system at world position %s"), 
			   *SpawnPos.ToString());
	}
	else if (FluidGrid)
	{
		const int32 TestX = GridSizeX / 2;
		const int32 TestY = GridSizeY / 2;
		const int32 TestZ = GridSizeZ * 3 / 4;
		
		for (int32 dx = -2; dx <= 2; ++dx)
		{
			for (int32 dy = -2; dy <= 2; ++dy)
			{
				for (int32 dz = 0; dz <= 2; ++dz)
				{
					FluidGrid->AddFluid(TestX + dx, TestY + dy, TestZ + dz, DebugFluidSpawnAmount);
				}
			}
		}
		
		// Calculate the correct world position using the grid's origin
		const FVector SpawnWorldPos = FluidGrid->GetWorldPositionFromCell(TestX, TestY, TestZ);
		
		UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Test fluid spawned in grid system at position (%d, %d, %d), world position %s"), 
			   TestX, TestY, TestZ, *SpawnWorldPos.ToString());
	}
}

void AVoxelFluidActor::UpdateFluidSources(float DeltaTime)
{
	if (bUseChunkedSystem && ChunkManager)
	{
		for (const auto& Source : FluidSources)
		{
			const FVector& SourcePos = Source.Key;
			const float SourceFlowRate = Source.Value;
			
			// Use the source's specific flow rate, not the global one
			ChunkManager->AddFluidAtWorldPosition(SourcePos, SourceFlowRate * DeltaTime);
		}
	}
	else if (FluidGrid)
	{
		for (const auto& Source : FluidSources)
		{
			const FVector& SourcePos = Source.Key;
			const float SourceFlowRate = Source.Value;
			
			const FVector LocalPos = SourcePos - GetActorLocation();
			int32 CellX, CellY, CellZ;
			
			if (FluidGrid->GetCellFromWorldPosition(LocalPos, CellX, CellY, CellZ))
			{
				// Use the source's specific flow rate, not the global one
				FluidGrid->AddFluid(CellX, CellY, CellZ, SourceFlowRate * DeltaTime);
			}
		}
	}
}

void AVoxelFluidActor::UpdateDebugVisualization()
{
	if (bShowDebugGrid)
	{
		DrawDebugGrid();
	}
	
	if (VoxelIntegration)
	{
		VoxelIntegration->bDebugDrawCells = bShowDebugGrid;
		VoxelIntegration->bEnableFlowVisualization = bShowFlowVectors;
	}
	
	if (VisualizationComponent)
	{
		VisualizationComponent->bEnableFlowVisualization = bShowFlowVectors;
	}
}

void AVoxelFluidActor::DrawDebugGrid()
{
	if (!GetWorld() || !FluidGrid)
		return;
	
	const FVector GridOrigin = FluidGrid->GridOrigin;
	const float GridWidth = GridSizeX * CellSize;
	const float GridHeight = GridSizeY * CellSize;
	const float GridDepth = GridSizeZ * CellSize;
	
	// Draw grid lines in X direction
	for (int32 x = 0; x <= GridSizeX; ++x)
	{
		const float XPos = x * CellSize;
		const FVector LineStart = GridOrigin + FVector(XPos, 0, 0);
		const FVector LineEnd = GridOrigin + FVector(XPos, GridHeight, 0);
		
		DrawDebugLine(GetWorld(), LineStart, LineEnd, FColor::White, false, -1.0f, 0, 1.0f);
		DrawDebugLine(GetWorld(), LineStart + FVector(0, 0, GridDepth), 
					  LineEnd + FVector(0, 0, GridDepth), FColor::White, false, -1.0f, 0, 1.0f);
	}
	
	// Draw grid lines in Y direction
	for (int32 y = 0; y <= GridSizeY; ++y)
	{
		const float YPos = y * CellSize;
		const FVector LineStart = GridOrigin + FVector(0, YPos, 0);
		const FVector LineEnd = GridOrigin + FVector(GridWidth, YPos, 0);
		
		DrawDebugLine(GetWorld(), LineStart, LineEnd, FColor::White, false, -1.0f, 0, 1.0f);
		DrawDebugLine(GetWorld(), LineStart + FVector(0, 0, GridDepth), 
					  LineEnd + FVector(0, 0, GridDepth), FColor::White, false, -1.0f, 0, 1.0f);
	}
	
	// Draw vertical lines at corners
	for (int32 x = 0; x <= GridSizeX; x += GridSizeX)
	{
		for (int32 y = 0; y <= GridSizeY; y += GridSizeY)
		{
			const FVector VerticalStart = GridOrigin + FVector(x * CellSize, y * CellSize, 0);
			const FVector VerticalEnd = VerticalStart + FVector(0, 0, GridDepth);
			
			DrawDebugLine(GetWorld(), VerticalStart, VerticalEnd, FColor::White, false, -1.0f, 0, 1.0f);
		}
	}
}

void AVoxelFluidActor::UpdateBounds()
{
	if (BoundsComponent)
	{
		BoundsComponent->SetBoxExtent(CalculatedBoundsExtent);
		BoundsComponent->SetRelativeLocation(BoundsOffset);
	}
}

void AVoxelFluidActor::CalculateGridBounds()
{
	const FVector ActorLocation = GetActorLocation();
	
	if (bUseWorldBounds)
	{
		CalculatedGridOrigin = WorldBoundsMin;
		CalculatedBoundsExtent = (WorldBoundsMax - WorldBoundsMin) * 0.5f;
		
		GridSizeX = FMath::CeilToInt((WorldBoundsMax.X - WorldBoundsMin.X) / CellSize);
		GridSizeY = FMath::CeilToInt((WorldBoundsMax.Y - WorldBoundsMin.Y) / CellSize);
		GridSizeZ = FMath::CeilToInt((WorldBoundsMax.Z - WorldBoundsMin.Z) / CellSize);
	}
	else
	{
		// Grid origin should be at actor location minus half extents (so actor is at center)
		CalculatedGridOrigin = ActorLocation - BoundsExtent + BoundsOffset;
		CalculatedBoundsExtent = BoundsExtent;
		
		GridSizeX = FMath::CeilToInt((BoundsExtent.X * 2.0f) / CellSize);
		GridSizeY = FMath::CeilToInt((BoundsExtent.Y * 2.0f) / CellSize);
		GridSizeZ = FMath::CeilToInt((BoundsExtent.Z * 2.0f) / CellSize);
	}
}

void AVoxelFluidActor::UpdateGridOriginForMovement()
{
	if (!FluidGrid || bUseWorldBounds)
		return;
	
	const FVector ActorLocation = GetActorLocation();
	const FVector NewGridOrigin = ActorLocation - BoundsExtent + BoundsOffset;
	
	// Only update if the actor has actually moved
	if (!NewGridOrigin.Equals(FluidGrid->GridOrigin, 1.0f))
	{
		FluidGrid->GridOrigin = NewGridOrigin;
		
		// Don't auto-update terrain when grid moves to avoid hitches
		// User can manually call RefreshTerrainData if needed
	}
}

FString AVoxelFluidActor::GetPerformanceStats() const
{
	if (bUseChunkedSystem && ChunkManager)
	{
		return GetChunkSystemStats();
	}
	else if (FluidGrid)
	{
		const int32 TotalCells = GridSizeX * GridSizeY * GridSizeZ;
		const int32 ActiveCells = GetActiveCellCount();
		const float TotalVolume = GetTotalFluidVolume();
		const float CellsPerMs = TotalCells / FMath::Max(0.001f, LastFrameSimulationTime);
		
		return FString::Printf(
			TEXT("=== VoxelFluid Performance Stats (Grid) ===\n")
			TEXT("Grid Size: %dx%dx%d (%d cells)\n")
			TEXT("Active Cells: %d (%.1f%%)\n")
			TEXT("Total Fluid Volume: %.2f\n")
			TEXT("Last Frame Time: %.3f ms\n")
			TEXT("Cells/ms: %.0f\n")
			TEXT("Est. Max FPS: %.1f"),
			GridSizeX, GridSizeY, GridSizeZ, TotalCells,
			ActiveCells, TotalCells > 0 ? (float)ActiveCells / TotalCells * 100.0f : 0.0f,
			TotalVolume,
			LastFrameSimulationTime,
			CellsPerMs,
			1000.0f / FMath::Max(0.001f, LastFrameSimulationTime)
		);
	}
	return TEXT("Fluid system not initialized");
}

void AVoxelFluidActor::EnableProfiling(bool bEnable)
{
	bProfilingEnabled = bEnable;
	
	if (bEnable)
	{
		LastProfilingTime = FDateTime::Now();
		UE_LOG(LogTemp, Warning, TEXT("VoxelFluid Profiling Enabled - Use 'stat VoxelFluid' to view stats"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelFluid Profiling Disabled"));
	}
}

int32 AVoxelFluidActor::GetActiveCellCount() const
{
	if (bUseChunkedSystem && ChunkManager)
	{
		return ChunkManager->GetStats().TotalActiveCells;
	}
	else if (FluidGrid)
	{
		int32 Count = 0;
		for (const FCAFluidCell& Cell : FluidGrid->Cells)
		{
			if (Cell.FluidLevel > FluidGrid->MinFluidLevel)
				Count++;
		}
		return Count;
	}
	return 0;
}

float AVoxelFluidActor::GetTotalFluidVolume() const
{
	if (bUseChunkedSystem && ChunkManager)
	{
		return ChunkManager->GetStats().TotalFluidVolume;
	}
	else if (FluidGrid)
	{
		float TotalVolume = 0.0f;
		for (const FCAFluidCell& Cell : FluidGrid->Cells)
		{
			TotalVolume += Cell.FluidLevel;
		}
		return TotalVolume;
	}
	return 0.0f;
}

void AVoxelFluidActor::InitializeChunkedSystem()
{
	if (!ChunkManager)
	{
		ChunkManager = NewObject<UFluidChunkManager>(this, UFluidChunkManager::StaticClass());
	}
	
	const FVector WorldSize = bUseWorldBounds ? 
		(WorldBoundsMax - WorldBoundsMin) : 
		(CalculatedBoundsExtent * 2.0f);
	
	ChunkManager->Initialize(ChunkSize, CellSize, CalculatedGridOrigin, WorldSize);
	
	FChunkStreamingConfig Config;
	Config.ActiveDistance = ChunkActiveDistance;
	Config.LoadDistance = ChunkLoadDistance;
	Config.MaxActiveChunks = MaxActiveChunks;
	Config.MaxLoadedChunks = MaxLoadedChunks;
	Config.LOD1Distance = LOD1Distance;
	Config.LOD2Distance = LOD2Distance;
	Config.bUseAsyncLoading = bUseAsyncChunkLoading;
	
	ChunkManager->SetStreamingConfig(Config);
	ChunkManager->FlowRate = FluidFlowRate;
	ChunkManager->Viscosity = FluidViscosity;
	ChunkManager->Gravity = GravityStrength;
	
	UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Chunked system initialized with %d chunk size"), ChunkSize);
}

void AVoxelFluidActor::UpdateChunkedSystem(float DeltaTime)
{
	if (!ChunkManager)
		return;
	
	TArray<FVector> ViewerPositions = GetViewerPositions();
	
	ChunkManager->UpdateChunks(DeltaTime, ViewerPositions);
	
	// Add fluid from all active sources using their individual flow rates
	for (const auto& Source : FluidSources)
	{
		const FVector& SourcePos = Source.Key;
		const float SourceFlowRate = Source.Value;
		ChunkManager->AddFluidAtWorldPosition(SourcePos, SourceFlowRate * DeltaTime);
	}
	
	ChunkManager->UpdateSimulation(DeltaTime);
}

TArray<FVector> AVoxelFluidActor::GetViewerPositions() const
{
	TArray<FVector> ViewerPositions;
	
	UWorld* World = GetWorld();
	if (!World)
		return ViewerPositions;
	
	if (World->GetNetMode() == NM_Standalone)
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				ViewerPositions.Add(Pawn->GetActorLocation());
			}
		}
	}
	else
	{
		for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
		{
			if (APlayerController* PC = Iterator->Get())
			{
				if (APawn* Pawn = PC->GetPawn())
				{
					ViewerPositions.Add(Pawn->GetActorLocation());
				}
			}
		}
	}
	
	if (ViewerPositions.Num() == 0)
	{
		ViewerPositions.Add(GetActorLocation());
	}
	
	return ViewerPositions;
}

int32 AVoxelFluidActor::GetLoadedChunkCount() const
{
	if (bUseChunkedSystem && ChunkManager)
	{
		return ChunkManager->GetStats().TotalChunks;
	}
	return 0;
}

int32 AVoxelFluidActor::GetActiveChunkCount() const
{
	if (bUseChunkedSystem && ChunkManager)
	{
		return ChunkManager->GetStats().ActiveChunks;
	}
	return 0;
}

void AVoxelFluidActor::ForceUpdateChunkStreaming()
{
	if (bUseChunkedSystem && ChunkManager)
	{
		ChunkManager->ForceUpdateChunkStates();
		UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Forced chunk streaming update"));
	}
}

FString AVoxelFluidActor::GetChunkSystemStats() const
{
	if (!bUseChunkedSystem || !ChunkManager)
		return TEXT("Chunk system not active");
	
	const FChunkManagerStats Stats = ChunkManager->GetStats();
	
	return FString::Printf(
		TEXT("=== VoxelFluid Chunk System Stats ===\n")
		TEXT("Loaded Chunks: %d (Max: %d)\n")
		TEXT("Active Chunks: %d (Max: %d)\n")
		TEXT("Inactive Chunks: %d\n")
		TEXT("Border Only Chunks: %d\n")
		TEXT("Total Fluid Volume: %.2f\n")
		TEXT("Total Active Cells: %d\n")
		TEXT("Avg Chunk Update Time: %.3f ms\n")
		TEXT("Last Frame Time: %.3f ms"),
		Stats.TotalChunks, MaxLoadedChunks,
		Stats.ActiveChunks, MaxActiveChunks,
		Stats.InactiveChunks,
		Stats.BorderOnlyChunks,
		Stats.TotalFluidVolume,
		Stats.TotalActiveCells,
		Stats.AverageChunkUpdateTime,
		Stats.LastFrameUpdateTime
	);
}

#if WITH_EDITOR
void AVoxelFluidActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	if (!PropertyChangedEvent.Property)
		return;
	
	const FName PropertyName = PropertyChangedEvent.Property->GetFName();
	
	// Handle chunked system toggle
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AVoxelFluidActor, bUseChunkedSystem))
	{
		if (IsInGameThread())
		{
			InitializeFluidSystem();
			UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Switched to %s system in editor"), 
				   bUseChunkedSystem ? TEXT("Chunked") : TEXT("Grid"));
		}
	}
	// Handle chunk settings changes
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AVoxelFluidActor, ChunkSize) ||
			 PropertyName == GET_MEMBER_NAME_CHECKED(AVoxelFluidActor, ChunkLoadDistance) ||
			 PropertyName == GET_MEMBER_NAME_CHECKED(AVoxelFluidActor, ChunkActiveDistance) ||
			 PropertyName == GET_MEMBER_NAME_CHECKED(AVoxelFluidActor, MaxActiveChunks) ||
			 PropertyName == GET_MEMBER_NAME_CHECKED(AVoxelFluidActor, MaxLoadedChunks))
	{
		if (bUseChunkedSystem && ChunkManager && IsInGameThread())
		{
			FChunkStreamingConfig Config;
			Config.ActiveDistance = ChunkActiveDistance;
			Config.LoadDistance = ChunkLoadDistance;
			Config.MaxActiveChunks = MaxActiveChunks;
			Config.MaxLoadedChunks = MaxLoadedChunks;
			Config.LOD1Distance = LOD1Distance;
			Config.LOD2Distance = LOD2Distance;
			Config.bUseAsyncLoading = bUseAsyncChunkLoading;
			
			ChunkManager->SetStreamingConfig(Config);
		}
	}
	// Handle grid size changes
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AVoxelFluidActor, GridSizeX) ||
			 PropertyName == GET_MEMBER_NAME_CHECKED(AVoxelFluidActor, GridSizeY) ||
			 PropertyName == GET_MEMBER_NAME_CHECKED(AVoxelFluidActor, GridSizeZ) ||
			 PropertyName == GET_MEMBER_NAME_CHECKED(AVoxelFluidActor, CellSize))
	{
		if (IsInGameThread())
		{
			CalculateGridBounds();
			if (!bUseChunkedSystem)
			{
				InitializeFluidSystem();
			}
		}
	}
	// Handle bounds changes
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AVoxelFluidActor, BoundsExtent) ||
			 PropertyName == GET_MEMBER_NAME_CHECKED(AVoxelFluidActor, BoundsOffset) ||
			 PropertyName == GET_MEMBER_NAME_CHECKED(AVoxelFluidActor, bUseWorldBounds) ||
			 PropertyName == GET_MEMBER_NAME_CHECKED(AVoxelFluidActor, WorldBoundsMin) ||
			 PropertyName == GET_MEMBER_NAME_CHECKED(AVoxelFluidActor, WorldBoundsMax))
	{
		if (IsInGameThread())
		{
			CalculateGridBounds();
			UpdateBounds();
		}
	}
}
#endif