#include "Actors/VoxelFluidActor.h"
#include "CellularAutomata/FluidChunkManager.h"
#include "CellularAutomata/FluidChunk.h"
#include "CellularAutomata/StaticWaterBody.h"
#include "VoxelIntegration/VoxelFluidIntegration.h"
#include "VoxelFluidStats.h"
#include "VoxelFluidDebug.h"
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

	StaticWaterManager = CreateDefaultSubobject<UStaticWaterManager>(TEXT("StaticWaterManager"));

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

	// Initialize static water properties
	bEnableStaticWater = false;
	bShowStaticWaterBounds = false;
	bAutoCreateOcean = false;
	OceanWaterLevel = 0.0f;
	OceanSize = 100000.0f;
}

void AVoxelFluidActor::BeginPlay()
{
	Super::BeginPlay();

	// Ensure StaticWaterManager exists (needed for Blueprint-derived actors)
	if (!StaticWaterManager)
	{
		StaticWaterManager = NewObject<UStaticWaterManager>(this, UStaticWaterManager::StaticClass(), TEXT("StaticWaterManager"));
	}

	// Ensure ocean water level is at a reasonable height (below typical terrain)
	if (OceanWaterLevel >= 0.0f)
	{
		OceanWaterLevel = -500.0f; // Set below terrain to avoid floating water
	}

	InitializeFluidSystem();

	if (bAutoStart)
	{
		StartSimulation();
	}

	// Create ocean after terrain system is initialized
	if (bEnableStaticWater && bAutoCreateOcean)
	{
		FTimerHandle TimerHandle;
		GetWorld()->GetTimerManager().SetTimer(TimerHandle, [this]()
		{
			// Force load chunks around the player first
			if (ChunkManager)
			{
				TArray<FVector> ViewerPositions = GetViewerPositions();
				
				// Force an update to load chunks
				ChunkManager->UpdateChunks(0.1f, ViewerPositions);
			}
			
			// Clear any existing static water first
			if (StaticWaterManager)
			{
				StaticWaterManager->ClearAllStaticWaterRegions();
			}

			// Now create ocean with proper level
			CreateOcean(OceanWaterLevel, OceanSize);
		}, 1.0f, false); // Wait 1 second for terrain to be ready
	}

}

void AVoxelFluidActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopSimulation();

	if (ChunkManager)
	{
		ChunkManager->ClearAllChunks();
	}

	FluidSources.Empty();


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

	if (bShowFlowVectors || bShowChunkBorders || bShowChunkStates)
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

		// Set static water manager if enabled
		if (StaticWaterManager && bEnableStaticWater)
		{
			ChunkManager->SetStaticWaterManager(StaticWaterManager);
		}

		if (TargetVoxelWorld)
		{
			VoxelIntegration->InitializeFluidSystem(TargetVoxelWorld);
		}

		// Bind to chunk loaded delegate to refresh terrain data
		ChunkManager->OnChunkLoadedDelegate.AddLambda([this](const FFluidChunkCoord& ChunkCoord)
		{
			if (VoxelIntegration && VoxelIntegration->IsVoxelWorldValid())
			{
				// Update terrain heights
				VoxelIntegration->UpdateTerrainForChunkCoord(ChunkCoord);

				// CRITICAL: Clean up any water that might be in solid cells
				// This handles any race conditions or timing issues
				if (UFluidChunk* Chunk = ChunkManager->GetChunk(ChunkCoord))
				{
					int32 CleanedCells = 0;
					for (int32 i = 0; i < Chunk->Cells.Num(); ++i)
					{
						// If cell is solid terrain, remove ANY water from it
						if (Chunk->Cells[i].bIsSolid && Chunk->Cells[i].FluidLevel > 0.0f)
						{
							Chunk->Cells[i].FluidLevel = 0.0f;
							Chunk->Cells[i].bSettled = false;
							Chunk->Cells[i].bSourceBlock = false;
							Chunk->NextCells[i].FluidLevel = 0.0f;
							Chunk->NextCells[i].bSettled = false;
							Chunk->NextCells[i].bSourceBlock = false;
							CleanedCells++;
						}
					}

					if (CleanedCells > 0)
					{
						Chunk->bDirty = true;
					}
				}

				// Apply static water ONLY after verifying terrain is loaded
				if (StaticWaterManager && bEnableStaticWater)
				{
					// Longer delay and verify terrain before applying water
					FTimerHandle WaterPlacementHandle;
					GetWorld()->GetTimerManager().SetTimer(WaterPlacementHandle, [this, ChunkCoord]()
					{
						if (UFluidChunk* Chunk = ChunkManager->GetChunk(ChunkCoord))
						{
							// Count terrain cells to verify terrain is loaded
							int32 TerrainCells = 0;
							for (int32 i = 0; i < FMath::Min(100, Chunk->Cells.Num()); ++i)
							{
								if (Chunk->Cells[i].TerrainHeight > -10000.0f && Chunk->Cells[i].TerrainHeight < 10000.0f)
									TerrainCells++;
							}

							// Only apply water if terrain is actually loaded
							if (TerrainCells > 50) // At least 50% of sampled cells have terrain
							{
								// Clean ANY existing water from solid cells first
								for (int32 i = 0; i < Chunk->Cells.Num(); ++i)
								{
									if (Chunk->Cells[i].bIsSolid)
									{
										Chunk->Cells[i].FluidLevel = 0.0f;
										Chunk->Cells[i].bSourceBlock = false;
									}
								}

								// Now apply static water (it will also check bIsSolid)
								FBox ChunkBounds = Chunk->GetWorldBounds();
								if (StaticWaterManager->ChunkIntersectsStaticWater(ChunkBounds))
								{
									StaticWaterManager->ApplyStaticWaterToChunkWithTerrain(Chunk, ChunkManager);
								}
							}
							else
							{
								// Terrain not ready, try again later
								FTimerHandle RetryHandle;
								GetWorld()->GetTimerManager().SetTimer(RetryHandle, [this, ChunkCoord]()
								{
									if (VoxelIntegration && VoxelIntegration->IsVoxelWorldValid())
									{
										VoxelIntegration->UpdateTerrainForChunkCoord(ChunkCoord);
									}

									if (UFluidChunk* RetryChunk = ChunkManager->GetChunk(ChunkCoord))
									{
										// Clean solid cells
										for (int32 i = 0; i < RetryChunk->Cells.Num(); ++i)
										{
											if (RetryChunk->Cells[i].bIsSolid)
											{
												RetryChunk->Cells[i].FluidLevel = 0.0f;
												RetryChunk->Cells[i].bSourceBlock = false;
											}
										}

										StaticWaterManager->ApplyStaticWaterToChunkWithTerrain(RetryChunk, ChunkManager);
									}
								}, 0.5f, false);
							}
						}
					}, 0.5f, false); // Wait longer initially
				}
			}
			else
			{
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

}

void AVoxelFluidActor::StopSimulation()
{
	bIsSimulating = false;

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

}

void AVoxelFluidActor::AddFluidSource(const FVector& WorldPosition, float FlowRate)
{
	// Use DefaultSourceFlowRate if no flow rate is specified
	const float ActualFlowRate = (FlowRate < 0.0f) ? DefaultSourceFlowRate : FlowRate;
	// Apply density multiplier to the flow rate
	const float FinalFlowRate = ActualFlowRate * FluidDensityMultiplier;

	if (FluidSources.Contains(WorldPosition))
	{
		FluidSources[WorldPosition] = FinalFlowRate;
	}
	else
	{
		FluidSources.Add(WorldPosition, FinalFlowRate);
	}
}

void AVoxelFluidActor::RemoveFluidSource(const FVector& WorldPosition)
{
	FluidSources.Remove(WorldPosition);

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
		for (UFluidChunk* Chunk : ActiveChunks)
		{
			if (Chunk && Chunk->HasActiveFluid())
			{
			}
		}
	}
}

void AVoxelFluidActor::UpdateFluidSources(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_FluidSourceUpdate);

	if (ChunkManager)
	{
		SET_DWORD_STAT(STAT_VoxelFluid_ActiveSources, FluidSources.Num());

		float TotalFlowRate = 0.0f;
		for (const auto& Source : FluidSources)
		{
			const FVector& SourcePos = Source.Key;
			const float SourceFlowRate = Source.Value;
			TotalFlowRate += SourceFlowRate;

			// Use the source's specific flow rate, not the global one
			ChunkManager->AddFluidAtWorldPosition(SourcePos, SourceFlowRate * DeltaTime);
		}

		SET_FLOAT_STAT(STAT_VoxelFluid_TotalSourceFlow, TotalFlowRate);
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

		// Octree debug settings removed

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

	// Draw static water bounds if enabled
	if (bShowStaticWaterBounds && StaticWaterManager && bEnableStaticWater)
	{
		if (UWorld* World = GetWorld())
		{
			// Draw bounds for each static water region
			for (const FStaticWaterRegion& Region : StaticWaterManager->GetStaticWaterRegions())
			{
				FColor DebugColor = FColor::Cyan;
				switch (Region.WaterType)
				{
					case EStaticWaterType::Ocean:
						DebugColor = FColor::Blue;
						break;
					case EStaticWaterType::Lake:
						DebugColor = FColor::Cyan;
						break;
					case EStaticWaterType::River:
						DebugColor = FColor::Green;
						break;
				}

				// Draw the water surface plane
				DrawDebugBox(World, Region.Bounds.GetCenter(), Region.Bounds.GetExtent(), DebugColor, false, -1.0f, 0, 2.0f);

				// Draw water level plane
				FVector PlaneCenter = Region.Bounds.GetCenter();
				PlaneCenter.Z = Region.WaterLevel;
				FVector PlaneExtent = Region.Bounds.GetExtent();
				PlaneExtent.Z = 1.0f;

				DrawDebugBox(World, PlaneCenter, PlaneExtent, DebugColor.WithAlpha(128), false, -1.0f, 0, 1.0f);
			}
		}
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
	}
	else
	{
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

	// Optimization settings removed - using default behavior


	// Sync debug settings
	ChunkManager->bShowChunkBorders = bShowChunkBorders;
	ChunkManager->bShowChunkStates = bShowChunkStates;
	ChunkManager->DebugUpdateInterval = ChunkDebugUpdateInterval;

}

void AVoxelFluidActor::UpdateChunkSystem(float DeltaTime)
{
	if (!ChunkManager)
		return;

	TArray<FVector> ViewerPositions = GetViewerPositions();

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
		return;
	}

	ChunkManager->TestPersistence(WorldPosition);
}

void AVoxelFluidActor::ForceUnloadAllChunks()
{
	if (!ChunkManager)
	{
		return;
	}

	ChunkManager->ForceUnloadAllChunks();
}

void AVoxelFluidActor::ShowCacheStatus()
{
	if (!ChunkManager)
	{
		return;
	}

	int32 CacheSize = ChunkManager->GetCacheSize();
	int32 CacheMemoryKB = ChunkManager->GetCacheMemoryUsage();

}


void AVoxelFluidActor::TestPersistenceWithSourcePause()
{
	// Prevent test from running multiple times simultaneously
	static bool bTestInProgress = false;
	if (bTestInProgress)
	{
		return;
	}

	if (!ChunkManager)
	{
		return;
	}

	bTestInProgress = true;


	// Step 1: Pause fluid sources AND simulation to prevent interference
	bool bWasPaused = bPauseFluidSources;
	bool bWasSimulating = bIsSimulating;
	bPauseFluidSources = true;
	bIsSimulating = false;

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
		}
	}

	// Step 3: Force unload all chunks
	ChunkManager->ForceUnloadAllChunks();

	// Step 4: Force reload ONLY chunks that had fluid

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
			}
			else
			{
			}
		}
		else
		{
		}
	}

	// Wait a moment for chunks to settle
	FPlatformProcess::Sleep(0.05f);

	// Force update chunk states to ensure they're recognized
	ChunkManager->ForceUpdateChunkStates();

	// Step 5: Check fluid state after reload - use the volumes from persistent data
	float TotalFluidAfter = 0.0f;
	for (const auto& Entry : RestoredFluidVolumes)
	{
		TotalFluidAfter += Entry.Value;
		if (Entry.Value > 0.0f)
		{
		}
	}

	// Also check the actual chunk fluid volumes to verify deserialization worked
	for (UFluidChunk* Chunk : RestoredChunks)
	{
		if (Chunk)
		{
			float ActualVolume = Chunk->GetTotalFluidVolume();
		}
	}

	// Step 6: Compare results
	float Difference = FMath::Abs(TotalFluidBefore - TotalFluidAfter);
	float PercentageDifference = (TotalFluidBefore > 0.0f) ? (Difference / TotalFluidBefore * 100.0f) : 0.0f;

	// Allow up to 3% difference due to 16-bit quantization in compression
	// This is acceptable for the memory savings achieved
	if (PercentageDifference < 3.0f)
	{
	}
	else
	{
	}

	// Restore pause and simulation state
	bPauseFluidSources = bWasPaused;
	bIsSimulating = bWasSimulating;

	// Clear test in progress flag
	bTestInProgress = false;
}

// Static Water Implementation
void AVoxelFluidActor::CreateOcean(float WaterLevel, float Size)
{
	if (!StaticWaterManager)
	{
		return;
	}

	FBox OceanBounds(
		FVector(-Size, -Size, WaterLevel - 10000.0f),
		FVector(Size, Size, WaterLevel)
	);
	

	StaticWaterManager->CreateOcean(WaterLevel, OceanBounds);

	// Apply to all loaded chunks
	if (bEnableStaticWater)
	{
		ApplyStaticWaterToAllChunks();

		// Schedule a retry after a short delay to catch any chunks that had uninitialized terrain
		FTimerHandle RetryTimerHandle;
		GetWorld()->GetTimerManager().SetTimer(RetryTimerHandle, [this]()
		{
			RetryStaticWaterApplication();
		}, 1.0f, false);
	}

}

void AVoxelFluidActor::CreateLake(const FVector& Center, float Radius, float WaterLevel, float Depth)
{
	if (!StaticWaterManager)
	{
		return;
	}

	StaticWaterManager->CreateLake(Center, Radius, WaterLevel, Depth);

	// Apply to all loaded chunks
	if (bEnableStaticWater)
	{
		ApplyStaticWaterToAllChunks();
	}

}

void AVoxelFluidActor::CreateRectangularLake(const FVector& Min, const FVector& Max, float WaterLevel)
{
	if (!StaticWaterManager)
	{
		return;
	}

	FBox LakeBounds(Min, FVector(Max.X, Max.Y, WaterLevel));
	StaticWaterManager->CreateRectangularLake(LakeBounds, WaterLevel);

	// Apply to all loaded chunks
	if (bEnableStaticWater)
	{
		ApplyStaticWaterToAllChunks();
	}

}

void AVoxelFluidActor::ClearStaticWater()
{
	if (!StaticWaterManager)
	{
		return;
	}

	StaticWaterManager->ClearAllStaticWaterRegions();

	// Clear static water from all chunks
	if (ChunkManager)
	{
		TArray<UFluidChunk*> AllChunks = ChunkManager->GetActiveChunks();
		for (UFluidChunk* Chunk : AllChunks)
		{
			if (Chunk)
			{
				// Clear cells that were marked as static water sources
				for (int32 i = 0; i < Chunk->Cells.Num(); i++)
				{
					if (Chunk->Cells[i].bSourceBlock)
					{
						Chunk->Cells[i].FluidLevel = 0.0f;
						Chunk->Cells[i].bSourceBlock = false;
						Chunk->Cells[i].bSettled = false;
					}
				}
			}
		}
	}

}

void AVoxelFluidActor::ApplyStaticWaterToAllChunks()
{
	if (!StaticWaterManager || !ChunkManager || !bEnableStaticWater)
	{
		return;
	}

	TArray<UFluidChunk*> AllChunks = ChunkManager->GetActiveChunks();
	int32 AppliedCount = 0;
	int32 SkippedCount = 0;

	for (UFluidChunk* Chunk : AllChunks)
	{
		if (Chunk)
		{
			FBox ChunkBounds = Chunk->GetWorldBounds();
			if (StaticWaterManager->ChunkIntersectsStaticWater(ChunkBounds))
			{
				// Force terrain update first if VoxelIntegration is available
				if (VoxelIntegration && VoxelIntegration->IsVoxelWorldValid())
				{
					VoxelIntegration->UpdateTerrainForChunkCoord(Chunk->ChunkCoord);
				}

				// Apply static water with terrain awareness
				StaticWaterManager->ApplyStaticWaterToChunkWithTerrain(Chunk, ChunkManager);
				AppliedCount++;
			}
			else
			{
				SkippedCount++;
			}
		}
	}
}

bool AVoxelFluidActor::IsPointInStaticWater(const FVector& WorldPosition) const
{
	if (!StaticWaterManager || !bEnableStaticWater)
	{
		return false;
	}

	return StaticWaterManager->IsPointInStaticWater(WorldPosition);
}

void AVoxelFluidActor::RetryStaticWaterApplication()
{
	if (!StaticWaterManager || !ChunkManager || !bEnableStaticWater)
	{
		return;
	}

	TArray<UFluidChunk*> AllChunks = ChunkManager->GetActiveChunks();
	int32 RetriedCount = 0;


	for (UFluidChunk* Chunk : AllChunks)
	{
		if (Chunk)
		{
			FBox ChunkBounds = Chunk->GetWorldBounds();
			if (StaticWaterManager->ChunkIntersectsStaticWater(ChunkBounds))
			{
				// Force terrain update first
				if (VoxelIntegration && VoxelIntegration->IsVoxelWorldValid())
				{
					VoxelIntegration->UpdateTerrainForChunkCoord(Chunk->ChunkCoord);
				}

				// Clear any existing static water first
				for (int32 i = 0; i < Chunk->Cells.Num(); i++)
				{
					if (Chunk->Cells[i].bSourceBlock)
					{
						Chunk->Cells[i].FluidLevel = 0.0f;
						Chunk->Cells[i].bSourceBlock = false;
						Chunk->Cells[i].bSettled = false;
					}
				}

				// Apply static water with terrain awareness
				StaticWaterManager->ApplyStaticWaterToChunkWithTerrain(Chunk, ChunkManager);
				RetriedCount++;
			}
		}
	}

}

void AVoxelFluidActor::RefillStaticWaterInRadius(const FVector& Center, float Radius)
{
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_DynamicRefill);

	if (!StaticWaterManager || !ChunkManager || !bEnableStaticWater)
	{
		return;
	}

	// Find all chunks within the radius
	FBox RefreshBounds(Center - FVector(Radius), Center + FVector(Radius));
	TArray<FFluidChunkCoord> AffectedChunks = ChunkManager->GetChunksInBounds(RefreshBounds);

	int32 ActivatedChunks = 0;
	int32 TotalActivatedSources = 0;


	for (const FFluidChunkCoord& ChunkCoord : AffectedChunks)
	{
		UFluidChunk* Chunk = ChunkManager->GetChunk(ChunkCoord);
		if (!Chunk)
			continue;

		// Check if this chunk intersects any static water regions
		FBox ChunkBounds = Chunk->GetWorldBounds();
		if (!StaticWaterManager->ChunkIntersectsStaticWater(ChunkBounds))
			continue;

		// Force terrain update for this specific chunk
		if (VoxelIntegration && VoxelIntegration->IsVoxelWorldValid())
		{
			VoxelIntegration->UpdateTerrainForChunkCoord(Chunk->ChunkCoord);
		}

		// Count active sources before
		int32 SourcesBefore = 0;
		for (int32 i = 0; i < Chunk->Cells.Num(); i++)
		{
			if (!Chunk->Cells[i].bSourceBlock && Chunk->Cells[i].FluidLevel > 0.5f)
			{
				SourcesBefore++;
			}
		}

		// Create dynamic fluid sources around the excavated area
		StaticWaterManager->CreateDynamicFluidSourcesInRadius(Chunk, Center, Radius);

		// Count active sources after
		int32 SourcesAfter = 0;
		for (int32 i = 0; i < Chunk->Cells.Num(); i++)
		{
			if (!Chunk->Cells[i].bSourceBlock && Chunk->Cells[i].FluidLevel > 0.5f)
			{
				SourcesAfter++;
			}
		}

		int32 NewSources = SourcesAfter - SourcesBefore;
		if (NewSources > 0)
		{
			TotalActivatedSources += NewSources;
			ActivatedChunks++;

			// Activate the chunk for simulation
			Chunk->bFullySettled = false;
			Chunk->bDirty = true;
		}
	}

	// Start fluid settling monitoring
	if (TotalActivatedSources > 0)
	{
		StartDynamicToStaticConversion(Center, Radius);
	}

}

void AVoxelFluidActor::StartDynamicToStaticConversion(const FVector& Center, float Radius)
{
	// Schedule conversion after settling time
	FTimerHandle ConversionTimerHandle;
	GetWorld()->GetTimerManager().SetTimer(ConversionTimerHandle, [this, Center, Radius]()
	{
		ConvertSettledFluidToStatic(Center, Radius);
	}, DynamicToStaticSettleTime, false);

}

void AVoxelFluidActor::ConvertSettledFluidToStatic(const FVector& Center, float Radius)
{
	if (!StaticWaterManager || !ChunkManager || !bEnableStaticWater)
	{
		return;
	}

	// Find all chunks within the radius
	FBox ConversionBounds(Center - FVector(Radius), Center + FVector(Radius));
	TArray<FFluidChunkCoord> AffectedChunks = ChunkManager->GetChunksInBounds(ConversionBounds);

	int32 ConvertedChunks = 0;
	int32 TotalConvertedCells = 0;
	const float RadiusSquared = Radius * Radius;
	const float SettledThreshold = 0.01f; // Fluid change threshold for "settled"


	for (const FFluidChunkCoord& ChunkCoord : AffectedChunks)
	{
		UFluidChunk* Chunk = ChunkManager->GetChunk(ChunkCoord);
		if (!Chunk)
			continue;

		// Check if this chunk intersects any static water regions
		FBox ChunkBounds = Chunk->GetWorldBounds();
		if (!StaticWaterManager->ChunkIntersectsStaticWater(ChunkBounds))
			continue;

		int32 ConvertedInChunk = 0;

		for (int32 LocalZ = 0; LocalZ < Chunk->ChunkSize; LocalZ++)
		{
			for (int32 LocalY = 0; LocalY < Chunk->ChunkSize; LocalY++)
			{
				for (int32 LocalX = 0; LocalX < Chunk->ChunkSize; LocalX++)
				{
					FVector CellWorldPos = Chunk->GetWorldPositionFromLocal(LocalX, LocalY, LocalZ);
					float DistanceSquared = FVector::DistSquared(CellWorldPos, Center);

					// Only process cells within the radius
					if (DistanceSquared > RadiusSquared)
						continue;

					int32 LinearIndex = Chunk->GetLocalCellIndex(LocalX, LocalY, LocalZ);
					if (!Chunk->Cells.IsValidIndex(LinearIndex))
						continue;

					FCAFluidCell& Cell = Chunk->Cells[LinearIndex];

					// Check if this is a dynamic fluid cell that should be static
					if (!Cell.bSourceBlock && Cell.FluidLevel > 0.5f)
					{
						// Check if this position should have static water
						float ExpectedWaterLevel = 0.0f;
						if (StaticWaterManager->ShouldHaveStaticWaterAt(CellWorldPos, ExpectedWaterLevel))
						{
							// Check if fluid has settled (small change from last frame)
							float FluidChange = FMath::Abs(Cell.FluidLevel - Cell.LastFluidLevel);
							bool bIsSettled = FluidChange < SettledThreshold || Cell.bSettled;

							if (bIsSettled && Cell.FluidLevel > 0.8f)
							{
								// Convert to static water
								Cell.bSourceBlock = true;
								Cell.bSettled = true;
								Cell.FluidLevel = 1.0f;
								Cell.LastFluidLevel = 1.0f;
								ConvertedInChunk++;
							}
						}
					}
				}
			}
		}

		if (ConvertedInChunk > 0)
		{
			TotalConvertedCells += ConvertedInChunk;
			ConvertedChunks++;
		}
	}

}

void AVoxelFluidActor::TestTerrainRefreshAtLocation(const FVector& Location, float Radius)
{

	if (!ChunkManager)
	{
		return;
	}

	// Find chunks in the area
	FBox TestBounds(Location - FVector(Radius), Location + FVector(Radius));
	TArray<FFluidChunkCoord> AffectedChunks = ChunkManager->GetChunksInBounds(TestBounds);


	// Log info about each chunk before refresh
	for (const FFluidChunkCoord& ChunkCoord : AffectedChunks)
	{
		UFluidChunk* Chunk = ChunkManager->GetChunk(ChunkCoord);
		if (Chunk)
		{
			int32 FluidCellCount = 0;
			for (int32 i = 0; i < Chunk->Cells.Num(); i++)
			{
				if (Chunk->Cells[i].FluidLevel > 0.1f)
					FluidCellCount++;
			}

		}
	}

	// Test the terrain refresh
	if (VoxelIntegration && VoxelIntegration->IsVoxelWorldValid())
	{
		VoxelIntegration->RefreshTerrainInRadius(Location, Radius);
	}
	else
	{
	}


	// Now test the static water refill
	RefillStaticWaterInRadius(Location, Radius);
}

