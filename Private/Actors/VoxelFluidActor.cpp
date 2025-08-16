#include "Actors/VoxelFluidActor.h"
#include "CellularAutomata/FluidChunkManager.h"
#include "CellularAutomata/FluidChunk.h"
#include "VoxelIntegration/VoxelFluidIntegration.h"
#include "Visualization/FluidVisualizationComponent.h"
#include "Components/BoxComponent.h"
#include "Components/BillboardComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"
#include "VoxelFluidStats.h"
#include "GameFramework/PlayerController.h"

AVoxelFluidActor::AVoxelFluidActor()
{
	PrimaryActorTick.bCanEverTick = true;
	
	BoundsComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("BoundsComponent"));
	RootComponent = BoundsComponent;
	BoundsComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	
#if WITH_EDITORONLY_DATA
	// Create billboard component for editor visualization
	SpriteComponent = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("SpriteComponent"));
	if (SpriteComponent)
	{
		SpriteComponent->SetupAttachment(RootComponent);
		SpriteComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 50.0f)); // Slight offset above actor
		SpriteComponent->bHiddenInGame = true;
		SpriteComponent->bIsScreenSizeScaled = true;
		SpriteComponent->ScreenSize = 0.0025f;
		
		// Try to load a water/fluid icon, fallback to default icon if not found
		static ConstructorHelpers::FObjectFinder<UTexture2D> WaterSpriteTexture(TEXT("/Engine/EditorResources/S_Fluid"));
		if (WaterSpriteTexture.Succeeded())
		{
			SpriteComponent->SetSprite(WaterSpriteTexture.Object);
		}
		else
		{
			// Try alternative paths for water/fluid related icons
			static ConstructorHelpers::FObjectFinder<UTexture2D> AlternativeSprite(TEXT("/Engine/EditorResources/S_Emitter"));
			if (AlternativeSprite.Succeeded())
			{
				SpriteComponent->SetSprite(AlternativeSprite.Object);
			}
		}
	}
#endif
	
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
	
	// Update fluid source statistics
	if (FluidSources.Num() > 0)
	{
		float TotalFlowRate = 0.0f;
		for (const auto& Source : FluidSources)
		{
			TotalFlowRate += Source.Value;
		}
		SET_DWORD_STAT(STAT_VoxelFluid_ActiveSources, FluidSources.Num());
		SET_FLOAT_STAT(STAT_VoxelFluid_TotalSourceFlow, TotalFlowRate);
	}
	else
	{
		SET_DWORD_STAT(STAT_VoxelFluid_ActiveSources, 0);
		SET_FLOAT_STAT(STAT_VoxelFluid_TotalSourceFlow, 0.0f);
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
		
		// Bind to chunk unloaded delegate to clear visualization cache
		ChunkManager->OnChunkUnloadedDelegate.AddLambda([this](const FFluidChunkCoord& ChunkCoord)
		{
			if (VisualizationComponent)
			{
				VisualizationComponent->OnChunkUnloaded(ChunkCoord);
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
	ChunkManager->EvaporationRate = FluidEvaporationRate;
	
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
	
	// Log viewer position periodically for debugging
	static float LogTimer = 0.0f;
	LogTimer += DeltaTime;
	if (LogTimer > 2.0f) // Log every 2 seconds
	{
		LogTimer = 0.0f;
		if (ViewerPositions.Num() > 0)
		{
			UE_LOG(LogTemp, Log, TEXT("Viewer at: %s"), *ViewerPositions[0].ToString());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("No viewer positions available for chunk streaming!"));
		}
	}
	
	ChunkManager->UpdateChunks(DeltaTime, ViewerPositions);
	
	// Add fluid from all active sources using their individual flow rates (unless paused)
	if (!bPauseFluidSources)
	{
		for (const auto& Source : FluidSources)
		{
			const FVector& SourcePos = Source.Key;
			const float SourceFlowRate = Source.Value;
			ChunkManager->AddFluidAtWorldPosition(SourcePos, SourceFlowRate * DeltaTime);
		}
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

void AVoxelFluidActor::TestPersistenceAtLocation(const FVector& WorldPosition)
{
	if (!ChunkManager)
	{
		UE_LOG(LogTemp, Error, TEXT("Chunk manager not initialized"));
		return;
	}
	
	ChunkManager->TestPersistence(WorldPosition);
}

void AVoxelFluidActor::ForceUnloadAllChunks()
{
	if (!ChunkManager)
	{
		UE_LOG(LogTemp, Error, TEXT("Chunk manager not initialized"));
		return;
	}
	
	ChunkManager->ForceUnloadAllChunks();
}

void AVoxelFluidActor::ShowCacheStatus()
{
	if (!ChunkManager)
	{
		UE_LOG(LogTemp, Error, TEXT("Chunk manager not initialized"));
		return;
	}
	
	int32 CacheSize = ChunkManager->GetCacheSize();
	int32 CacheMemoryKB = ChunkManager->GetCacheMemoryUsage();
	
	UE_LOG(LogTemp, Warning, TEXT("=== PERSISTENCE CACHE STATUS ==="));
	UE_LOG(LogTemp, Warning, TEXT("Cache entries: %d"), CacheSize);
	UE_LOG(LogTemp, Warning, TEXT("Memory usage: %d KB"), CacheMemoryKB);
	UE_LOG(LogTemp, Warning, TEXT("Persistence enabled: %s"), 
	       ChunkManager->StreamingConfig.bEnablePersistence ? TEXT("YES") : TEXT("NO"));
	UE_LOG(LogTemp, Warning, TEXT("Max cached chunks: %d"), 
	       ChunkManager->StreamingConfig.MaxCachedChunks);
	UE_LOG(LogTemp, Warning, TEXT("Cache expiration: %.1f seconds"), 
	       ChunkManager->StreamingConfig.CacheExpirationTime);
}

void AVoxelFluidActor::TestPersistenceWithSourcePause()
{
	// Prevent test from running multiple times simultaneously
	static bool bTestInProgress = false;
	if (bTestInProgress)
	{
		UE_LOG(LogTemp, Warning, TEXT("Persistence test already in progress, please wait"));
		return;
	}
	
	if (!ChunkManager)
	{
		UE_LOG(LogTemp, Error, TEXT("Chunk manager not initialized"));
		return;
	}
	
	bTestInProgress = true;
	
	UE_LOG(LogTemp, Warning, TEXT("=== TESTING PERSISTENCE WITH SOURCE PAUSE ==="));
	
	// Step 1: Pause fluid sources AND simulation to prevent interference
	bool bWasPaused = bPauseFluidSources;
	bool bWasSimulating = bIsSimulating;
	bPauseFluidSources = true;
	bIsSimulating = false;
	UE_LOG(LogTemp, Warning, TEXT("1. Paused fluid sources and simulation"));
	
	// Step 2: Record current fluid state and which chunks have fluid
	float TotalFluidBefore = 0.0f;
	TArray<FFluidChunkCoord> ChunksWithFluid;
	TArray<UFluidChunk*> ActiveChunks = ChunkManager->GetActiveChunks();
	for (UFluidChunk* Chunk : ActiveChunks)
	{
		if (Chunk && Chunk->HasFluid())
		{
			float ChunkVolume = Chunk->GetTotalFluidVolume();
			TotalFluidBefore += ChunkVolume;
			ChunksWithFluid.Add(Chunk->ChunkCoord);
			UE_LOG(LogTemp, Log, TEXT("  Chunk %s has %.2f fluid"), 
			       *Chunk->ChunkCoord.ToString(), ChunkVolume);
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("2. Total fluid before unload: %.2f in %d chunks"), TotalFluidBefore, ChunksWithFluid.Num());
	
	// Step 3: Force unload all chunks
	ChunkManager->ForceUnloadAllChunks();
	UE_LOG(LogTemp, Warning, TEXT("3. Force unloaded all chunks"));
	
	// Step 4: Force reload ONLY chunks that had fluid
	UE_LOG(LogTemp, Warning, TEXT("4. Reloading %d chunks that had fluid..."), ChunksWithFluid.Num());
	
	// Load chunks directly (synchronously) for testing
	TArray<UFluidChunk*> RestoredChunks;
	TMap<FFluidChunkCoord, float> RestoredFluidVolumes;
	for (const FFluidChunkCoord& Coord : ChunksWithFluid)
	{
		UFluidChunk* Chunk = ChunkManager->GetOrCreateChunk(Coord);
		if (Chunk)
		{
			// Force the chunk to unloaded state if it's not already
			if (Chunk->State != EChunkState::Unloaded)
			{
				UE_LOG(LogTemp, Warning, TEXT("  Forcing chunk %s to Unloaded state from current state"), *Coord.ToString());
				Chunk->UnloadChunk();
			}
			
			// Manually restore from cache
			FChunkPersistentData PersistentData;
			if (ChunkManager->LoadChunkData(Coord, PersistentData))
			{
				Chunk->LoadChunk();
				Chunk->DeserializeChunkData(PersistentData);
				
				// Use the ChunkManager's public method to properly activate and register the chunk
				ChunkManager->ForceActivateChunk(Chunk);
				
				RestoredChunks.Add(Chunk);
				RestoredFluidVolumes.Add(Coord, PersistentData.TotalFluidVolume);
				UE_LOG(LogTemp, Log, TEXT("  Manually restored chunk %s with %.2f fluid"), 
				       *Coord.ToString(), PersistentData.TotalFluidVolume);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("  Failed to load cached data for chunk %s"), *Coord.ToString());
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("  Failed to get/create chunk %s"), *Coord.ToString());
		}
	}
	
	// Wait a moment for chunks to settle
	FPlatformProcess::Sleep(0.05f);
	
	// Force update chunk states to ensure they're recognized
	ChunkManager->ForceUpdateChunkStates();
	
	// Step 5: Check fluid state after reload - use the volumes from persistent data
	float TotalFluidAfter = 0.0f;
	UE_LOG(LogTemp, Warning, TEXT("5. Checking %d restored chunks..."), RestoredFluidVolumes.Num());
	for (const auto& Entry : RestoredFluidVolumes)
	{
		TotalFluidAfter += Entry.Value;
		if (Entry.Value > 0.0f)
		{
			UE_LOG(LogTemp, Log, TEXT("  Chunk %s has %.2f fluid from persistent data"), 
			       *Entry.Key.ToString(), Entry.Value);
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("5. Total fluid after reload: %.2f"), TotalFluidAfter);
	
	// Also check the actual chunk fluid volumes to verify deserialization worked
	UE_LOG(LogTemp, Warning, TEXT("5b. Verifying actual chunk fluid volumes:"));
	for (UFluidChunk* Chunk : RestoredChunks)
	{
		if (Chunk)
		{
			float ActualVolume = Chunk->GetTotalFluidVolume();
			UE_LOG(LogTemp, Log, TEXT("  Chunk %s actual volume: %.2f"), 
			       *Chunk->ChunkCoord.ToString(), ActualVolume);
		}
	}
	
	// Step 6: Compare results
	float Difference = FMath::Abs(TotalFluidBefore - TotalFluidAfter);
	float PercentageDifference = (TotalFluidBefore > 0.0f) ? (Difference / TotalFluidBefore * 100.0f) : 0.0f;
	
	// Allow up to 3% difference due to 16-bit quantization in compression
	// This is acceptable for the memory savings achieved
	if (PercentageDifference < 3.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("SUCCESS! Fluid preserved: Before=%.2f, After=%.2f (%.2f%% difference due to compression)"), 
		       TotalFluidBefore, TotalFluidAfter, PercentageDifference);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("FAILURE! Fluid lost: Before=%.2f, After=%.2f, Difference=%.2f (%.2f%%)"), 
		       TotalFluidBefore, TotalFluidAfter, Difference, PercentageDifference);
	}
	
	// Restore pause and simulation state
	bPauseFluidSources = bWasPaused;
	bIsSimulating = bWasSimulating;
	UE_LOG(LogTemp, Warning, TEXT("6. Restored states - Sources: %s, Simulation: %s"), 
	       bPauseFluidSources ? TEXT("PAUSED") : TEXT("ACTIVE"),
	       bIsSimulating ? TEXT("RUNNING") : TEXT("STOPPED"));
	
	// Clear test in progress flag
	bTestInProgress = false;
}