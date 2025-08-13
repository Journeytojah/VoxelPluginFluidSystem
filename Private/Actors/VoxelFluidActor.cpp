#include "Actors/VoxelFluidActor.h"
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
	
	
	ChunkManager = CreateDefaultSubobject<UFluidChunkManager>(TEXT("ChunkManager"));
	
	VoxelIntegration = CreateDefaultSubobject<UVoxelFluidIntegration>(TEXT("VoxelIntegration"));
	
	VisualizationComponent = CreateDefaultSubobject<UFluidVisualizationComponent>(TEXT("VisualizationComponent"));
	VisualizationComponent->SetupAttachment(RootComponent);
	
	ChunkSize = 32;
	CellSize = 100.0f;
	ChunkLoadDistance = 8000.0f;
	ChunkActiveDistance = 5000.0f;
	MaxActiveChunks = 64;
	MaxLoadedChunks = 128;
	LOD1Distance = 2000.0f;
	LOD2Distance = 4000.0f;
	FluidViscosity = 0.1f;
	GravityStrength = 981.0f;
	bAutoStart = true;
	bIsSimulating = false;
	SimulationSpeed = 1.0f;
	bShowFlowVectors = false;
	DebugFluidSpawnAmount = 1.0f;
	
	// Initialize new fluid properties
	FluidAccumulation = 0.1f;
	MinFluidThreshold = 0.001f;
	FluidEvaporationRate = 0.0f;
	FluidDensityMultiplier = 1.0f;
}

void AVoxelFluidActor::BeginPlay()
{
	Super::BeginPlay();
	
	InitializeFluidSystem();
	
	if (bAutoStart)
	{
		StartSimulation();
	}
	
	UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: BeginPlay completed with chunked system"));
}

void AVoxelFluidActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopSimulation();
	
	if (ChunkManager)
	{
		ChunkManager->ClearAllChunks();
	}
	
	FluidSources.Empty();
	
	UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: EndPlay completed, Reason: %d"), (int32)EndPlayReason);
	
	Super::EndPlay(EndPlayReason);
}

void AVoxelFluidActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	
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
				if (ChunkManager)
				{
					UpdateChunkSystem(SimulationTimestep);
				}
				
				SimulationAccumulator -= SimulationTimestep;
			}
		}
		else
		{
			// Variable timestep simulation
			const float ScaledDeltaTime = DeltaTime * SimulationSpeed;
			
			if (ChunkManager)
			{
				UpdateChunkSystem(ScaledDeltaTime);
			}
		}
		
		LastFrameSimulationTime = (FPlatformTime::Seconds() - StartTime) * 1000.0f; // Convert to ms
	}
	
	if (bShowFlowVectors)
	{
		UpdateDebugVisualization();
	}

	VisualizationComponent->UpdateVisualization();
}

void AVoxelFluidActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	
	UpdateSimulationBounds();
}

void AVoxelFluidActor::InitializeFluidSystem()
{
	InitializeChunkSystem();
	
	if (VoxelIntegration && ChunkManager)
	{
		VoxelIntegration->SetChunkManager(ChunkManager);
		VoxelIntegration->CellWorldSize = CellSize;
		
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
	
	if (VisualizationComponent && ChunkManager)
	{
		VisualizationComponent->SetChunkManager(ChunkManager);
	}
	
	UpdateSimulationBounds();
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
	
	if (ChunkManager)
	{
		ChunkManager->ClearAllChunks();
	}
	
	FluidSources.Empty();
	
	if (VoxelIntegration && TargetVoxelWorld)
	{
		VoxelIntegration->UpdateChunkedTerrainHeights();
	}
	
	UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Simulation reset"));
}

void AVoxelFluidActor::AddFluidSource(const FVector& WorldPosition, float FlowRate)
{
	// Use DefaultSourceFlowRate if no flow rate is specified
	const float ActualFlowRate = (FlowRate < 0.0f) ? DefaultSourceFlowRate : FlowRate;
	// Apply density multiplier to the flow rate
	const float FinalFlowRate = ActualFlowRate * FluidDensityMultiplier;
	
	if (FluidSources.Contains(WorldPosition))
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Fluid source already exists at %s, updating flow rate from %f to %f"), 
			   *WorldPosition.ToString(), FluidSources[WorldPosition], FinalFlowRate);
		FluidSources[WorldPosition] = FinalFlowRate;
	}
	else
	{
		FluidSources.Add(WorldPosition, FinalFlowRate);
		UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Added new fluid source at %s with flow rate %f"), 
			   *WorldPosition.ToString(), FinalFlowRate);
	}
}

void AVoxelFluidActor::RemoveFluidSource(const FVector& WorldPosition)
{
	FluidSources.Remove(WorldPosition);
	
	UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Removed fluid source at %s"), *WorldPosition.ToString());
}

void AVoxelFluidActor::AddFluidAtLocation(const FVector& WorldPosition, float Amount)
{
	// Apply accumulation and density multiplier
	const float AdjustedAmount = Amount * FluidDensityMultiplier * (1.0f + FluidAccumulation);
	
	if (ChunkManager)
	{
		ChunkManager->AddFluidAtWorldPosition(WorldPosition, AdjustedAmount);
	}
	else if (VoxelIntegration)
	{
		VoxelIntegration->AddFluidAtWorldPosition(WorldPosition, AdjustedAmount);
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
		VoxelIntegration->UpdateChunkedTerrainHeights();
		
		UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Terrain data refreshed with chunked system"));
	}
}

void AVoxelFluidActor::TestFluidSpawn()
{
	if (!ChunkManager)
	{
		InitializeFluidSystem();
	}
	
	if (ChunkManager)
	{
		// Test cross-chunk flow by spawning fluid at chunk boundary
		const FVector WorldCenter = GetActorLocation();
		
		// Get chunk size in world units
		const float ChunkWorldSize = ChunkSize * CellSize;
		
		// Spawn fluid at the boundary between two chunks
		// This will test if fluid flows properly between chunks
		const FVector SpawnPos = WorldCenter + FVector(ChunkWorldSize - CellSize, 0, 500.0f);
		
		UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Testing cross-chunk flow at chunk boundary"));
		UE_LOG(LogTemp, Log, TEXT("  Chunk size: %d cells, Cell size: %.1f units, Chunk world size: %.1f units"), 
			ChunkSize, CellSize, ChunkWorldSize);
		UE_LOG(LogTemp, Log, TEXT("  Spawning fluid at: %s"), *SpawnPos.ToString());
		
		// Create a 6x6x5 area of fluid that spans chunk boundary
		for (int32 dx = -3; dx <= 3; ++dx)
		{
			for (int32 dy = -3; dy <= 3; ++dy)
			{
				for (int32 dz = 0; dz <= 4; ++dz)
				{
					const FVector FluidSpawnPos = SpawnPos + FVector(dx * CellSize, dy * CellSize, dz * CellSize);
					ChunkManager->AddFluidAtWorldPosition(FluidSpawnPos, DebugFluidSpawnAmount);
				}
			}
		}
		
		// Check which chunks received fluid
		TArray<UFluidChunk*> ActiveChunks = ChunkManager->GetActiveChunks();
		UE_LOG(LogTemp, Log, TEXT("  Active chunks after spawn: %d"), ActiveChunks.Num());
		for (UFluidChunk* Chunk : ActiveChunks)
		{
			if (Chunk && Chunk->HasActiveFluid())
			{
				UE_LOG(LogTemp, Log, TEXT("    Chunk [%d,%d,%d] has %.2f fluid volume"), 
					Chunk->ChunkCoord.X, Chunk->ChunkCoord.Y, Chunk->ChunkCoord.Z, 
					Chunk->GetTotalFluidVolume());
			}
		}
	}
}

void AVoxelFluidActor::UpdateFluidSources(float DeltaTime)
{
	if (ChunkManager)
	{
		for (const auto& Source : FluidSources)
		{
			const FVector& SourcePos = Source.Key;
			const float SourceFlowRate = Source.Value;
			
			// Use the source's specific flow rate, not the global one
			ChunkManager->AddFluidAtWorldPosition(SourcePos, SourceFlowRate * DeltaTime);
		}
	}
}

void AVoxelFluidActor::UpdateDebugVisualization()
{
	
	// Update chunk debug visualization at specified intervals
	if (ChunkManager)
	{
		// Sync debug settings in case they changed
		ChunkManager->bShowChunkBorders = bShowChunkBorders;
		ChunkManager->bShowChunkStates = bShowChunkStates;
		ChunkManager->DebugUpdateInterval = ChunkDebugUpdateInterval;
		
		if (ChunkManager->ShouldUpdateDebugVisualization())
		{
			if (UWorld* World = GetWorld())
			{
				ChunkManager->DrawDebugChunks(World);
			}
		}
	}
	
	if (VoxelIntegration)
	{
		// Legacy VoxelIntegration debug properties removed - handled by VisualizationComponent now
	}
	
	if (VisualizationComponent)
	{
		VisualizationComponent->bEnableFlowVisualization = bShowFlowVectors;
	}
}

void AVoxelFluidActor::DrawDebugChunks()
{
	if (!GetWorld() || !ChunkManager)
		return;
	
	// Delegate to ChunkManager for debug drawing
	ChunkManager->DrawDebugChunks(GetWorld());
}

void AVoxelFluidActor::UpdateSimulationBounds()
{
	if (BoundsComponent)
	{
		BoundsComponent->SetBoxExtent(SimulationBoundsExtent);
		BoundsComponent->SetRelativeLocation(SimulationBoundsOffset);
	}
}


FString AVoxelFluidActor::GetPerformanceStats() const
{
	if (ChunkManager)
	{
		return GetChunkSystemStats();
	}
	return TEXT("Chunk system not initialized");
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
	if (ChunkManager)
	{
		return ChunkManager->GetStats().TotalActiveCells;
	}
	return 0;
}

float AVoxelFluidActor::GetTotalFluidVolume() const
{
	if (ChunkManager)
	{
		return ChunkManager->GetStats().TotalFluidVolume;
	}
	return 0.0f;
}

void AVoxelFluidActor::InitializeChunkSystem()
{
	if (!ChunkManager)
	{
		ChunkManager = NewObject<UFluidChunkManager>(this, UFluidChunkManager::StaticClass());
	}
	
	const FVector ActorLocation = GetActorLocation();
	SimulationOrigin = ActorLocation - SimulationBoundsExtent + SimulationBoundsOffset;
	ActiveBoundsExtent = SimulationBoundsExtent;
	
	const FVector WorldSize = ActiveBoundsExtent * 2.0f;
	
	ChunkManager->Initialize(ChunkSize, CellSize, SimulationOrigin, WorldSize);
	
	FChunkStreamingConfig Config;
	Config.ActiveDistance = ChunkActiveDistance;
	Config.LoadDistance = ChunkLoadDistance;
	Config.MaxActiveChunks = MaxActiveChunks;
	Config.MaxLoadedChunks = MaxLoadedChunks;
	Config.LOD1Distance = LOD1Distance;
	Config.LOD2Distance = LOD2Distance;
	
	ChunkManager->SetStreamingConfig(Config);
	ChunkManager->Viscosity = FluidViscosity;
	ChunkManager->Gravity = GravityStrength;
	
	// Sync debug settings
	ChunkManager->bShowChunkBorders = bShowChunkBorders;
	ChunkManager->bShowChunkStates = bShowChunkStates;
	ChunkManager->DebugUpdateInterval = ChunkDebugUpdateInterval;
	
	UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Chunked system initialized with %d chunk size"), ChunkSize);
}

void AVoxelFluidActor::UpdateChunkSystem(float DeltaTime)
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
	if (ChunkManager)
	{
		return ChunkManager->GetStats().TotalChunks;
	}
	return 0;
}

int32 AVoxelFluidActor::GetActiveChunkCount() const
{
	if (ChunkManager)
	{
		return ChunkManager->GetStats().ActiveChunks;
	}
	return 0;
}

void AVoxelFluidActor::ForceUpdateChunkStreaming()
{
	if (ChunkManager)
	{
		ChunkManager->ForceUpdateChunkStates();
		UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Forced chunk streaming update"));
	}
}

FString AVoxelFluidActor::GetChunkSystemStats() const
{
	if (!ChunkManager)
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
	
	// Handle chunk settings changes
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AVoxelFluidActor, ChunkSize) ||
			 PropertyName == GET_MEMBER_NAME_CHECKED(AVoxelFluidActor, ChunkLoadDistance) ||
			 PropertyName == GET_MEMBER_NAME_CHECKED(AVoxelFluidActor, ChunkActiveDistance) ||
			 PropertyName == GET_MEMBER_NAME_CHECKED(AVoxelFluidActor, MaxActiveChunks) ||
			 PropertyName == GET_MEMBER_NAME_CHECKED(AVoxelFluidActor, MaxLoadedChunks))
	{
		if (ChunkManager && IsInGameThread())
		{
			FChunkStreamingConfig Config;
			Config.ActiveDistance = ChunkActiveDistance;
			Config.LoadDistance = ChunkLoadDistance;
			Config.MaxActiveChunks = MaxActiveChunks;
			Config.MaxLoadedChunks = MaxLoadedChunks;
			Config.LOD1Distance = LOD1Distance;
			Config.LOD2Distance = LOD2Distance;
			
			ChunkManager->SetStreamingConfig(Config);
		}
	}
	// Handle simulation bounds changes
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AVoxelFluidActor, SimulationBoundsExtent) ||
			 PropertyName == GET_MEMBER_NAME_CHECKED(AVoxelFluidActor, SimulationBoundsOffset) ||
			 PropertyName == GET_MEMBER_NAME_CHECKED(AVoxelFluidActor, CellSize))
	{
		if (IsInGameThread())
		{
			UpdateSimulationBounds();
			InitializeFluidSystem();
		}
	}
}
#endif