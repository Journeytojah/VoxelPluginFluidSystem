#include "Actors/VoxelFluidActor.h"
#include "CellularAutomata/CAFluidGrid.h"
#include "VoxelIntegration/VoxelFluidIntegration.h"
#include "Visualization/FluidVisualizationComponent.h"
#include "Components/BoxComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "VoxelFluidStats.h"

AVoxelFluidActor::AVoxelFluidActor()
{
	PrimaryActorTick.bCanEverTick = true;
	
	BoundsComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("BoundsComponent"));
	RootComponent = BoundsComponent;
	BoundsComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	
	FluidGrid = CreateDefaultSubobject<UCAFluidGrid>(TEXT("FluidGrid"));
	
	VoxelIntegration = CreateDefaultSubobject<UVoxelFluidIntegration>(TEXT("VoxelIntegration"));
	
	VisualizationComponent = CreateDefaultSubobject<UFluidVisualizationComponent>(TEXT("VisualizationComponent"));
	VisualizationComponent->SetupAttachment(RootComponent);
	
	GridSizeX = 128;
	GridSizeY = 128;
	GridSizeZ = 32;
	CellSize = 100.0f;
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
}

void AVoxelFluidActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	// Update grid origin if actor has moved
	UpdateGridOriginForMovement();
	
	if (bIsSimulating && FluidGrid)
	{
		const double StartTime = FPlatformTime::Seconds();
		
		const float ScaledDeltaTime = DeltaTime * SimulationSpeed;
		
		UpdateFluidSources(ScaledDeltaTime);
		
		FluidGrid->UpdateSimulation(ScaledDeltaTime);
		
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
	if (!FluidGrid)
	{
		FluidGrid = NewObject<UCAFluidGrid>(this, UCAFluidGrid::StaticClass());
	}
	
	CalculateGridBounds();
	
	if (FluidGrid)
	{
		FluidGrid->InitializeGrid(GridSizeX, GridSizeY, GridSizeZ, CellSize, CalculatedGridOrigin);
		FluidGrid->FlowRate = FluidFlowRate;
		FluidGrid->Viscosity = FluidViscosity;
		FluidGrid->Gravity = GravityStrength;
		FluidGrid->bAllowFluidEscape = bAllowFluidEscape;
	}
	
	if (VoxelIntegration && FluidGrid)
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
	
	if (VisualizationComponent && FluidGrid)
	{
		VisualizationComponent->SetFluidGrid(FluidGrid);
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
	
	if (FluidGrid)
	{
		FluidGrid->ClearGrid();
	}
	
	FluidSources.Empty();
	
	if (VoxelIntegration && TargetVoxelWorld)
	{
		VoxelIntegration->SyncWithVoxelTerrain();
	}
	
	UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Simulation reset"));
}

void AVoxelFluidActor::AddFluidSource(const FVector& WorldPosition, float FlowRate)
{
	if (FluidSources.Contains(WorldPosition))
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Fluid source already exists at %s, updating flow rate from %f to %f"), 
			   *WorldPosition.ToString(), FluidSources[WorldPosition], FlowRate);
		FluidSources[WorldPosition] = FlowRate;
	}
	else
	{
		FluidSources.Add(WorldPosition, FlowRate);
		UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Added new fluid source at %s with flow rate %f"), 
			   *WorldPosition.ToString(), FlowRate);
	}
}

void AVoxelFluidActor::RemoveFluidSource(const FVector& WorldPosition)
{
	FluidSources.Remove(WorldPosition);
	
	UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Removed fluid source at %s"), *WorldPosition.ToString());
}

void AVoxelFluidActor::AddFluidAtLocation(const FVector& WorldPosition, float Amount)
{
	if (VoxelIntegration)
	{
		VoxelIntegration->AddFluidAtWorldPosition(WorldPosition, Amount);
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
		VoxelIntegration->UpdateTerrainHeights();
		
		UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Terrain data refreshed"));
	}
}

void AVoxelFluidActor::TestFluidSpawn()
{
	if (!FluidGrid)
	{
		InitializeFluidSystem();
	}
	
	const int32 TestX = GridSizeX / 2;
	const int32 TestY = GridSizeY / 2;
	const int32 TestZ = GridSizeZ * 3 / 4;
	
	if (FluidGrid)
	{
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
		
		UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Test fluid spawned at grid position (%d, %d, %d), world position %s"), 
			   TestX, TestY, TestZ, *SpawnWorldPos.ToString());
	}
}

void AVoxelFluidActor::UpdateFluidSources(float DeltaTime)
{
	if (!FluidGrid)
		return;
	
	for (const auto& Source : FluidSources)
	{
		const FVector& SourcePos = Source.Key;
		const float FlowRate = Source.Value;
		
		const FVector LocalPos = SourcePos - GetActorLocation();
		int32 CellX, CellY, CellZ;
		
		if (FluidGrid->GetCellFromWorldPosition(LocalPos, CellX, CellY, CellZ))
		{
			FluidGrid->AddFluid(CellX, CellY, CellZ, FlowRate * DeltaTime);
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
		
		// Update terrain data when grid moves
		if (VoxelIntegration && VoxelIntegration->bAutoUpdateTerrain)
		{
			VoxelIntegration->UpdateTerrainHeights();
		}
	}
}

FString AVoxelFluidActor::GetPerformanceStats() const
{
	if (!FluidGrid)
		return TEXT("Fluid Grid not initialized");
	
	const int32 TotalCells = GridSizeX * GridSizeY * GridSizeZ;
	const int32 ActiveCells = GetActiveCellCount();
	const float TotalVolume = GetTotalFluidVolume();
	const float CellsPerMs = TotalCells / FMath::Max(0.001f, LastFrameSimulationTime);
	
	return FString::Printf(
		TEXT("=== VoxelFluid Performance Stats ===\n")
		TEXT("Grid Size: %dx%dx%d (%d cells)\n")
		TEXT("Active Cells: %d (%.1f%%)\n")
		TEXT("Total Fluid Volume: %.2f\n")
		TEXT("Last Frame Time: %.3f ms\n")
		TEXT("Cells/ms: %.0f\n")
		TEXT("Est. Max FPS: %.1f"),
		GridSizeX, GridSizeY, GridSizeZ, TotalCells,
		ActiveCells, (float)ActiveCells / TotalCells * 100.0f,
		TotalVolume,
		LastFrameSimulationTime,
		CellsPerMs,
		1000.0f / FMath::Max(0.001f, LastFrameSimulationTime)
	);
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
	if (!FluidGrid)
		return 0;
	
	int32 Count = 0;
	for (const FCAFluidCell& Cell : FluidGrid->Cells)
	{
		if (Cell.FluidLevel > FluidGrid->MinFluidLevel)
			Count++;
	}
	return Count;
}

float AVoxelFluidActor::GetTotalFluidVolume() const
{
	if (!FluidGrid)
		return 0.0f;
	
	float TotalVolume = 0.0f;
	for (const FCAFluidCell& Cell : FluidGrid->Cells)
	{
		TotalVolume += Cell.FluidLevel;
	}
	return TotalVolume;
}