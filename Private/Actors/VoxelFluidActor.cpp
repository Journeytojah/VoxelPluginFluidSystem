#include "Actors/VoxelFluidActor.h"
#include "CellularAutomata/FluidChunkManager.h"
#include "CellularAutomata/FluidChunk.h"
#include "CellularAutomata/CAFluidGrid.h"
#include "CellularAutomata/StaticWaterBody.h"
#include "VoxelIntegration/VoxelFluidIntegration.h"
#include "VoxelFluidStats.h"
#include "VoxelFluidDebug.h"
#include "Visualization/FluidVisualizationComponent.h"
#include "StaticWater/StaticWaterGenerator.h"
#include "StaticWater/StaticWaterRenderer.h"
#include "StaticWater/WaterActivationManager.h"
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

	// Create new static water system components
	StaticWaterGenerator = CreateDefaultSubobject<UStaticWaterGenerator>(TEXT("StaticWaterGenerator"));
	StaticWaterRenderer = CreateDefaultSubobject<UStaticWaterRenderer>(TEXT("StaticWaterRenderer"));
	WaterActivationManager = CreateDefaultSubobject<UWaterActivationManager>(TEXT("WaterActivationManager"));

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

	// Initialize new static water system components if they don't exist
	if (!StaticWaterGenerator)
	{
		StaticWaterGenerator = NewObject<UStaticWaterGenerator>(this, UStaticWaterGenerator::StaticClass(), TEXT("StaticWaterGenerator"));
	}
	if (!StaticWaterRenderer)
	{
		StaticWaterRenderer = NewObject<UStaticWaterRenderer>(this, UStaticWaterRenderer::StaticClass(), TEXT("StaticWaterRenderer"));
	}
	if (!WaterActivationManager)
	{
		WaterActivationManager = NewObject<UWaterActivationManager>(this, UWaterActivationManager::StaticClass(), TEXT("WaterActivationManager"));
	}

	// Ensure ocean water level is at a reasonable height (below typical terrain)
	if (OceanWaterLevel >= 0.0f)
	{
		OceanWaterLevel = -500.0f; // Set below terrain to avoid floating water
	}

	InitializeFluidSystem();

	// Connect new static water systems
	if (StaticWaterGenerator && TargetVoxelWorld)
	{
		StaticWaterGenerator->SetVoxelWorld(TargetVoxelWorld);
	}
	if (StaticWaterRenderer && StaticWaterGenerator)
	{
		StaticWaterRenderer->SetWaterGenerator(StaticWaterGenerator);
	}
	if (StaticWaterRenderer && VoxelIntegration)
	{
		StaticWaterRenderer->SetVoxelIntegration(VoxelIntegration);
	}
	if (WaterActivationManager)
	{
		WaterActivationManager->SetStaticWaterGenerator(StaticWaterGenerator);
		WaterActivationManager->SetStaticWaterRenderer(StaticWaterRenderer);
		WaterActivationManager->SetFluidChunkManager(ChunkManager);
		// FluidGrid is not used in this system currently
		// WaterActivationManager->SetFluidGrid(FluidGrid);
	}

	if (bAutoStart)
	{
		StartSimulation();
	}

	// Set up the new static water system after terrain system is initialized
	if (bEnableStaticWater)
	{
		FTimerHandle TimerHandle;
		GetWorld()->GetTimerManager().SetTimer(TimerHandle, [this]()
		{
			// Setup the test water system first (enables debug vis and logging)
			SetupTestWaterSystem();
			
			// Only force load chunks in distance-based mode
			if (ChunkManager && ChunkActivationMode == EChunkActivationMode::DistanceBased)
			{
				TArray<FVector> ViewerPositions = GetViewerPositions();
				
				// Force an update to load chunks
				ChunkManager->UpdateChunks(0.1f, ViewerPositions);
			}
			
			// Create ocean using the new static water system
			if (bAutoCreateOcean)
			{
				CreateTestOcean();
			}
			
			// Enable auto-tracking and start simulation after a slight delay  
			FTimerHandle TrackingTimer;
			GetWorld()->GetTimerManager().SetTimer(TrackingTimer, [this]()
			{
				// Make sure simulation is running
				if (!bIsSimulating)
				{
					StartSimulation();
					UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Started fluid simulation"));
				}
				
				if (StaticWaterRenderer)
				{
					// Set the minimum render distance for the ring
					StaticWaterRenderer->RenderSettings.MinRenderDistance = 3000.0f;
					StaticWaterRenderer->EnableAutoTracking(true);
					UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Auto-tracking enabled, min distance: %.1f"), 
						StaticWaterRenderer->RenderSettings.MinRenderDistance);
				}
				
				// Only spawn initial water in distance-based mode
				if (ChunkActivationMode == EChunkActivationMode::DistanceBased)
				{
					SpawnSimulationWaterAroundPlayer();
				}
			}, 1.0f, false);
			
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

	// Update water systems to follow player
	static float WaterUpdateTimer = 0.0f;
	WaterUpdateTimer += DeltaTime;
	
	if (WaterUpdateTimer > 0.5f) // Update every 0.5 seconds
	{
		WaterUpdateTimer = 0.0f;
		
		// Get current player position
		FVector PlayerPos = FVector::ZeroVector;
		if (UWorld* World = GetWorld())
		{
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				if (APawn* PlayerPawn = PC->GetPawn())
				{
					PlayerPos = PlayerPawn->GetActorLocation();
					
					// Update static water renderer to follow player
					if (StaticWaterRenderer && StaticWaterRenderer->bAutoTrackPlayer)
					{
						// This already happens in StaticWaterRenderer's tick
					}
					
					// Manage simulation water around player
					if (bEnableStaticWater && ChunkManager)
					{
						ManageSimulationWaterAroundPlayer(PlayerPos);
					}
					
					// Update hybrid system stats
					if (StaticWaterRenderer)
					{
						// Static water rendering stats
						SET_DWORD_STAT(STAT_VoxelFluid_StaticRenderChunks, StaticWaterRenderer->GetActiveRenderChunkCount());
						
						// Get LOD distribution
						int32 LOD0Count, LOD1Count, LOD2Count;
						StaticWaterRenderer->GetLODStatistics(LOD0Count, LOD1Count, LOD2Count);
						SET_DWORD_STAT(STAT_VoxelFluid_StaticLOD0Chunks, LOD0Count);
						SET_DWORD_STAT(STAT_VoxelFluid_StaticLOD1PlusChunks, LOD1Count + LOD2Count);
						
						// Ring rendering stats  
						SET_FLOAT_STAT(STAT_VoxelFluid_StaticRingInnerRadius, StaticWaterRenderer->RenderSettings.MinRenderDistance);
						SET_FLOAT_STAT(STAT_VoxelFluid_StaticRingOuterRadius, StaticWaterRenderer->RenderSettings.MaxRenderDistance);
					}
					
					// Hybrid system distribution stats
					if (ChunkManager && StaticWaterRenderer)
					{
						int32 SimChunks = ChunkManager->GetActiveChunkCount();
						int32 StaticChunks = StaticWaterRenderer->GetActiveRenderChunkCount();
						
						SET_DWORD_STAT(STAT_VoxelFluid_SimulationChunks, SimChunks);
						SET_DWORD_STAT(STAT_VoxelFluid_HybridStaticChunks, StaticChunks);
						SET_DWORD_STAT(STAT_VoxelFluid_TransitionChunks, 0); // Would need to track transition chunks
						
						// Calculate coverage ratio
						float SimToStaticRatio = StaticChunks > 0 ? (float)SimChunks / (float)StaticChunks : 0.0f;
						SET_FLOAT_STAT(STAT_VoxelFluid_SimStaticRatio, SimToStaticRatio);
					}
					
					// Water activation stats
					if (WaterActivationManager)
					{
						SET_DWORD_STAT(STAT_VoxelFluid_ActiveRegions, WaterActivationManager->GetActiveRegionCount());
						// SET_FLOAT_STAT(STAT_VoxelFluid_WaterSpawnRate, WaterActivationManager->GetSpawnRate());
					}
				}
			}
		}
	}

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
		
		// Update performance comparison stats
		if (ChunkManager)
		{
			int32 ActiveChunks = ChunkManager->GetActiveChunkCount();
			if (ActiveChunks > 0)
			{
				float MSPerChunk = LastFrameSimulationTime / ActiveChunks;
				SET_FLOAT_STAT(STAT_VoxelFluid_SimMSPerChunk, MSPerChunk);
			}
		}
	}

	// Update static water rendering performance stats
	if (StaticWaterRenderer)
	{
		// Static water rendering is much cheaper - estimate based on chunk count
		int32 StaticChunks = StaticWaterRenderer->GetActiveRenderChunkCount();
		if (StaticChunks > 0)
		{
			// Static water typically takes ~0.01-0.05ms per chunk for simple mesh rendering
			float EstimatedStaticMS = StaticChunks * 0.02f; // 0.02ms per chunk estimate
			SET_FLOAT_STAT(STAT_VoxelFluid_StaticMSPerChunk, 0.02f);
			
			// Total frame MS
			float TotalFrameMS = LastFrameSimulationTime + EstimatedStaticMS;
			SET_FLOAT_STAT(STAT_VoxelFluid_TotalFrameMS, TotalFrameMS);
			
			// FPS impact
			float FPSImpact = TotalFrameMS > 0 ? 1000.0f / TotalFrameMS : 0.0f;
			SET_FLOAT_STAT(STAT_VoxelFluid_FPSImpact, FPSImpact);
		}
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

	// Notify water activation manager about fluid being added
	if (WaterActivationManager && AdjustedAmount > 0.0f)
	{
		WaterActivationManager->OnFluidAdded(WorldPosition, AdjustedAmount);
	}

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
	FString Stats;
	
	// Hybrid System Overview
	Stats += TEXT("=== HYBRID WATER SYSTEM ===\n");
	
	// Simulation Stats
	if (ChunkManager)
	{
		int32 SimChunks = ChunkManager->GetActiveChunkCount();
		int32 LoadedChunks = ChunkManager->GetLoadedChunkCount();
		Stats += FString::Printf(TEXT("Simulation: %d active / %d loaded chunks\n"), SimChunks, LoadedChunks);
		Stats += FString::Printf(TEXT("Sim Time: %.2f ms\n"), LastFrameSimulationTime);
	}
	
	// Static Water Stats
	if (StaticWaterRenderer)
	{
		int32 StaticChunks = StaticWaterRenderer->GetActiveRenderChunkCount();
		int32 VisibleChunks = StaticWaterRenderer->GetVisibleRenderChunkCount();
		
		int32 LOD0, LOD1, LOD2;
		StaticWaterRenderer->GetLODStatistics(LOD0, LOD1, LOD2);
		
		Stats += FString::Printf(TEXT("Static Water: %d active / %d visible chunks\n"), StaticChunks, VisibleChunks);
		Stats += FString::Printf(TEXT("LODs: %d (High) / %d (Med) / %d (Low)\n"), LOD0, LOD1, LOD2);
		Stats += FString::Printf(TEXT("Ring: %.1fm - %.1fm\n"), 
			StaticWaterRenderer->RenderSettings.MinRenderDistance / 100.0f,
			StaticWaterRenderer->RenderSettings.MaxRenderDistance / 100.0f);
	}
	
	// Water Activation Stats
	if (WaterActivationManager)
	{
		int32 ActiveRegions = WaterActivationManager->GetActiveRegionCount();
		Stats += FString::Printf(TEXT("Active Water Regions: %d\n"), ActiveRegions);
	}
	
	// Performance Comparison
	if (ChunkManager && StaticWaterRenderer)
	{
		int32 SimChunks = ChunkManager->GetActiveChunkCount();
		int32 StaticChunks = StaticWaterRenderer->GetActiveRenderChunkCount();
		float Ratio = StaticChunks > 0 ? (float)SimChunks / (float)StaticChunks : 0.0f;
		
		Stats += TEXT("\n=== PERFORMANCE ===\n");
		Stats += FString::Printf(TEXT("Sim/Static Ratio: %.2f\n"), Ratio);
		
		if (SimChunks > 0)
		{
			float MSPerSimChunk = LastFrameSimulationTime / SimChunks;
			Stats += FString::Printf(TEXT("MS per Sim Chunk: %.3f\n"), MSPerSimChunk);
		}
		
		// Static water is much cheaper
		Stats += FString::Printf(TEXT("MS per Static Chunk: ~0.02 (estimated)\n"));
	}
	
	// Add original chunk system stats if available
	if (ChunkManager)
	{
		Stats += TEXT("\n");
		Stats += GetChunkSystemStats();
	}
	
	return Stats;
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
	Config.ActivationMode = ChunkActivationMode;
	Config.EditActivationRadius = EditActivationRadius;
	Config.SettledDeactivationDelay = SettledDeactivationDelay;
	Config.MinActivityForDeactivation = MinActivityForDeactivation;
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

// New Static Water System Function Implementations

void AVoxelFluidActor::AddStaticWaterRegion(const FVector& Center, float Radius, float WaterLevel)
{
	if (!StaticWaterGenerator)
		return;

	FStaticWaterRegionDef NewRegion;
	const FVector RegionSize = FVector(Radius * 2.0f, Radius * 2.0f, 2000.0f);
	NewRegion.Bounds = FBox::BuildAABB(Center, RegionSize * 0.5f);
	NewRegion.WaterLevel = WaterLevel;
	NewRegion.bInfiniteDepth = false;
	NewRegion.MinDepth = 100.0f;
	NewRegion.Priority = 0;

	StaticWaterGenerator->AddWaterRegion(NewRegion);

	UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Added static water region at %s (radius: %.1f, level: %.1f)"), 
		*Center.ToString(), Radius, WaterLevel);
}

void AVoxelFluidActor::RemoveStaticWaterRegion(const FVector& Center, float Radius)
{
	if (!StaticWaterGenerator)
		return;

	// For now, clear all regions and let user re-add the ones they want
	// TODO: Implement region-specific removal based on position
	StaticWaterGenerator->ClearAllWaterRegions();

	UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Cleared static water regions"));
}

void AVoxelFluidActor::OnTerrainEdited(const FVector& EditPosition, float EditRadius, float HeightChange)
{
	// CRITICAL: In edit-triggered mode, activate chunks FIRST before any other operations
	if (ChunkManager && ChunkActivationMode != EChunkActivationMode::DistanceBased)
	{
		// Activate chunks in the edit area - this ensures chunks exist before we try to add water
		ChunkManager->OnVoxelEditOccurred(EditPosition, EditRadius);
		UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Activated chunks for terrain edit at %s"), 
			*EditPosition.ToString());
	}
	
	// First, refresh the terrain data in the affected area
	if (VoxelIntegration && VoxelIntegration->IsVoxelWorldValid())
	{
		VoxelIntegration->RefreshTerrainInRadius(EditPosition, EditRadius);
	}

	// Check if there's static water at this location
	bool bHasWater = false;
	float WaterLevel = 0.0f;
	if (StaticWaterGenerator)
	{
		bHasWater = StaticWaterGenerator->HasStaticWaterAtLocation(EditPosition);
		if (bHasWater)
		{
			WaterLevel = StaticWaterGenerator->GetWaterLevelAtLocation(EditPosition);
		}
		
		// Notify static water generator about terrain changes
		const FBox ChangedBounds = FBox::BuildAABB(EditPosition, FVector(EditRadius));
		StaticWaterGenerator->OnTerrainChanged(ChangedBounds);
	}

	// If there's water, spawn MINIMAL fluid simulation only where edited
	if (bHasWater && ChunkManager)
	{
		// Make sure simulation is running
		if (!bIsSimulating)
		{
			StartSimulation();
		}
		
		// Only spawn water in a small radius around the edit point
		// This creates a localized dynamic water effect instead of converting entire chunks
		const float LocalizedSpawnRadius = FMath::Min(EditRadius * 1.5f, CellSize * 4.0f); // Max 4 cells radius
		const float SpawnSpacing = CellSize;
		const int32 SpawnGridSize = FMath::CeilToInt(LocalizedSpawnRadius / SpawnSpacing);
		
		int32 SpawnedCount = 0;
		for (int32 X = -SpawnGridSize; X <= SpawnGridSize; ++X)
		{
			for (int32 Y = -SpawnGridSize; Y <= SpawnGridSize; ++Y)
			{
				const FVector SpawnPos = EditPosition + FVector(X * SpawnSpacing, Y * SpawnSpacing, 0);
				const float DistFromCenter = FVector::Dist2D(SpawnPos, EditPosition);
				
				if (DistFromCenter <= LocalizedSpawnRadius)
				{
					// Sample terrain at this specific location
					float TerrainHeight = EditPosition.Z - HeightChange;
					if (VoxelIntegration)
					{
						TerrainHeight = VoxelIntegration->SampleVoxelHeight(SpawnPos.X, SpawnPos.Y);
					}
					
					// Only spawn water if it's above terrain and below water level
					if (TerrainHeight < WaterLevel)
					{
						// Spawn a thin layer of water just above terrain
						float SpawnHeight = FMath::Max(TerrainHeight + CellSize * 0.5f, EditPosition.Z);
						FVector FluidSpawnPos = FVector(SpawnPos.X, SpawnPos.Y, SpawnHeight);
						
						// Add less fluid for smoother transition
						const float FluidAmount = 0.5f; // Half-filled cells for gentler flow
						AddFluidAtLocation(FluidSpawnPos, FluidAmount);
						SpawnedCount++;
						
						// Add one extra layer if it's a deep cut
						if (HeightChange > CellSize * 2.0f)
						{
							FluidSpawnPos.Z += CellSize;
							AddFluidAtLocation(FluidSpawnPos, FluidAmount);
							SpawnedCount++;
						}
					}
				}
			}
		}
		
		UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Spawned %d localized fluid cells"), SpawnedCount);
	}

	// Trigger water activation manager if needed (for region tracking)
	if (WaterActivationManager)
	{
		WaterActivationManager->OnTerrainEdited(EditPosition, EditRadius, HeightChange);
	}

	// Force update the static water renderer to hide water in active simulation areas
	if (StaticWaterRenderer)
	{
		StaticWaterRenderer->RebuildChunksInRadius(EditPosition, EditRadius * 2.0f);
	}
}

void AVoxelFluidActor::OnVoxelTerrainModified(const FVector& ModifiedPosition, float ModifiedRadius)
{
	UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: OnVoxelTerrainModified called at %s (radius: %.1f)"), 
		*ModifiedPosition.ToString(), ModifiedRadius);
	
	// CRITICAL: In edit-triggered mode, explicitly activate chunks where edits occur
	if (ChunkManager && ChunkActivationMode != EChunkActivationMode::DistanceBased)
	{
		// Notify chunk manager about the edit to activate affected chunks
		ChunkManager->OnVoxelEditOccurred(ModifiedPosition, ModifiedRadius);
		
		UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Activated chunks for edit at %s"), 
			*ModifiedPosition.ToString());
	}
	
	// Call OnTerrainEdited with a default height change value
	// In practice, you might want to calculate the actual height change
	const float EstimatedHeightChange = 100.0f; // Assume significant change
	OnTerrainEdited(ModifiedPosition, ModifiedRadius, EstimatedHeightChange);
	
	// Also notify the VoxelIntegration directly
	if (VoxelIntegration)
	{
		const FBox ModifiedBounds = FBox::BuildAABB(ModifiedPosition, FVector(ModifiedRadius));
		VoxelIntegration->OnVoxelTerrainModified(ModifiedBounds);
	}
	
	// Force an immediate update of the static water renderer
	if (StaticWaterRenderer)
	{
		StaticWaterRenderer->RebuildChunksInRadius(ModifiedPosition, ModifiedRadius);
	}
}

bool AVoxelFluidActor::IsRegionActiveForSimulation(const FVector& Position) const
{
	if (!WaterActivationManager)
		return false;

	return WaterActivationManager->IsRegionActive(Position);
}

void AVoxelFluidActor::ForceActivateWaterAtLocation(const FVector& Position, float Radius)
{
	if (!WaterActivationManager)
		return;

	const bool bSuccess = WaterActivationManager->ActivateWaterInRegion(Position, Radius);
	
	UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: %s water activation at %s (radius: %.1f)"), 
		bSuccess ? TEXT("Successful") : TEXT("Failed"), *Position.ToString(), Radius);
}

void AVoxelFluidActor::ForceDeactivateAllWaterRegions()
{
	if (!WaterActivationManager)
		return;

	WaterActivationManager->ForceDeactivateAllRegions();
	
	UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Deactivated all water regions"));
}

int32 AVoxelFluidActor::GetActiveWaterRegionCount() const
{
	if (!WaterActivationManager)
		return 0;

	return WaterActivationManager->GetActiveRegionCount();
}

void AVoxelFluidActor::SetupTestWaterSystem()
{
	if (!StaticWaterGenerator || !StaticWaterRenderer)
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelFluidActor: Static water components not initialized"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Setting up test water system..."));

	// Disable debug visualization by default (user can enable if needed)
	StaticWaterGenerator->bShowTileBounds = false;
	StaticWaterGenerator->bShowWaterRegions = false;
	StaticWaterRenderer->bShowRenderChunkBounds = false;
	
	// Disable verbose logging by default
	StaticWaterGenerator->bEnableLogging = false;
	StaticWaterRenderer->bEnableLogging = false;
	if (WaterActivationManager)
	{
		WaterActivationManager->bEnableLogging = false;
		WaterActivationManager->bShowActiveRegions = false;
	}

	// Force connection between generator and renderer
	StaticWaterRenderer->SetWaterGenerator(StaticWaterGenerator);
	UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Connected renderer to generator"));

	// Create a simple test ocean around the player
	if (UWorld* World = GetWorld())
	{
		FVector PlayerPos = FVector::ZeroVector;
		if (APawn* PlayerPawn = World->GetFirstPlayerController()->GetPawn())
		{
			PlayerPos = PlayerPawn->GetActorLocation();
		}

		// Create ocean at a reasonable level relative to terrain
		float OceanLevel = -100.0f; // Fixed ocean level instead of relative to player
		AddStaticWaterRegion(PlayerPos, 10000.0f, OceanLevel);
		
		// Completely reset the renderer and set new viewer position
		StaticWaterRenderer->ResetRenderer();
		StaticWaterRenderer->SetViewerPosition(PlayerPos);
		UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Reset renderer and set viewer position to %s"), *PlayerPos.ToString());
		
		// Force immediate update of render chunks - this is key!
		StaticWaterRenderer->SetComponentTickEnabled(true);
		StaticWaterRenderer->RegenerateAroundViewer();
		
		UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Forced renderer regeneration at player position"));
		
		// Re-enable auto-tracking so the water follows the player as they move
		StaticWaterRenderer->EnableAutoTracking(true);
		
		// Wait a frame then enable auto tracking to ensure the initial setup completes
		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(TimerHandle, [this]()
		{
			if (StaticWaterRenderer)
			{
				StaticWaterRenderer->EnableAutoTracking(true);
				UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Auto-tracking re-enabled via timer"));
			}
		}, 0.1f, false);
		
		UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Test water system setup complete - Ocean at %s, Level: %.1f (auto-tracking enabled)"), 
			*PlayerPos.ToString(), OceanLevel);
	}
}

void AVoxelFluidActor::CreateTestOcean()
{
	if (!StaticWaterGenerator)
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelFluidActor: StaticWaterGenerator not initialized"));
		return;
	}

	// Clear existing regions
	StaticWaterGenerator->ClearAllWaterRegions();
	
	// Get current player position for ocean center
	FVector PlayerPos = FVector::ZeroVector;
	if (UWorld* World = GetWorld())
	{
		if (APawn* PlayerPawn = World->GetFirstPlayerController()->GetPawn())
		{
			PlayerPos = PlayerPawn->GetActorLocation();
		}
	}
	
	// Create a massive ocean centered on player with reasonable water level
	FStaticWaterRegionDef Ocean;
	Ocean.Bounds = FBox(PlayerPos + FVector(-100000, -100000, -2000), PlayerPos + FVector(100000, 100000, 500));
	Ocean.WaterLevel = -100.0f;  // Fixed ocean level
	Ocean.bInfiniteDepth = true;
	Ocean.MinDepth = 500.0f;
	Ocean.Priority = 0;

	StaticWaterGenerator->AddWaterRegion(Ocean);
	
	// Set renderer viewer position
	if (StaticWaterRenderer)
	{
		StaticWaterRenderer->SetViewerPosition(PlayerPos);
	}
	
	// Force regeneration
	StaticWaterGenerator->ForceRegenerateAll();
	
	UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Created massive ocean centered at %s, water level: -100"), *PlayerPos.ToString());
}

void AVoxelFluidActor::RecenterOceanOnPlayer()
{
	CreateTestOcean(); // Just calls the same function which now centers on player
}

void AVoxelFluidActor::ManageSimulationWaterAroundPlayer(const FVector& PlayerPos)
{
	// Disabled automatic water spawning - only spawn water when terrain is edited
	// This prevents the jarring visual disconnect between static and dynamic water
	return;
	
	/* DISABLED - Keeping code for reference
	if (!ChunkManager || !StaticWaterGenerator)
		return;
	
	// Check water level at player position
	float WaterLevel = StaticWaterGenerator->GetWaterLevelAtLocation(PlayerPos);
	if (!StaticWaterGenerator->HasStaticWaterAtLocation(PlayerPos))
		return; // No water in this area
	
	// Match the simulation radius to where static water stops rendering
	const float SimulationRadius = StaticWaterRenderer ? 
		StaticWaterRenderer->RenderSettings.MinRenderDistance : 1500.0f;
	
	// Only manage water in the immediate vicinity - let static water handle the rest
	FBox SimulationBounds = FBox::BuildAABB(PlayerPos, FVector(SimulationRadius));
	TArray<FFluidChunkCoord> NearbyChunks = ChunkManager->GetChunksInBounds(SimulationBounds);
	
	// Keep track of chunks to avoid excessive spawning
	static TSet<FFluidChunkCoord> ProcessedChunks;
	static float LastPlayerZ = PlayerPos.Z;
	
	// Clear processed chunks if player moved significantly vertically (e.g., teleported)
	if (FMath::Abs(PlayerPos.Z - LastPlayerZ) > 1000.0f)
	{
		ProcessedChunks.Empty();
	}
	LastPlayerZ = PlayerPos.Z;
	
	for (const FFluidChunkCoord& ChunkCoord : NearbyChunks)
	{
		// Skip if we already processed this chunk recently
		if (ProcessedChunks.Contains(ChunkCoord))
			continue;
			
		UFluidChunk* Chunk = ChunkManager->GetChunk(ChunkCoord);
		if (!Chunk)
			continue;
		
		FBox ChunkBounds = Chunk->GetWorldBounds();
		FVector ChunkCenter = ChunkBounds.GetCenter();
		float DistFromPlayer = FVector::Dist(ChunkCenter, PlayerPos);
		
		// Only spawn in chunks that are within the inner simulation zone (where static water doesn't render)
		if (DistFromPlayer <= SimulationRadius * 0.8f)
		{
			// Check if this chunk already has water
			bool bHasWater = false;
			for (const FCAFluidCell& Cell : Chunk->Cells)
			{
				if (Cell.FluidLevel > 0.1f)
				{
					bHasWater = true;
					break;
				}
			}
			
			// If chunk doesn't have water but should, spawn minimal amount
			if (!bHasWater)
			{
				// Fill water more densely for proper surface continuity
				const int32 CellsPerAxis = ChunkSize;
				const float SpawnSpacing = CellSize;
				int32 SpawnedCells = 0;
				const int32 MaxCellsPerChunk = 100; // Allow more cells for better coverage
				
				// Use denser spawning near player, sparser further away
				int32 SpawnStep = (DistFromPlayer < SimulationRadius * 0.25f) ? 1 : 2;
				
				for (int32 X = 0; X < CellsPerAxis && SpawnedCells < MaxCellsPerChunk; X += SpawnStep)
				{
					for (int32 Y = 0; Y < CellsPerAxis && SpawnedCells < MaxCellsPerChunk; Y += SpawnStep)
					{
						FVector CellWorldPos = ChunkBounds.Min + FVector(X * SpawnSpacing, Y * SpawnSpacing, 0);
						
						// Sample terrain
						float TerrainHeight = WaterLevel - 100.0f;
						if (VoxelIntegration)
						{
							TerrainHeight = VoxelIntegration->SampleVoxelHeight(CellWorldPos.X, CellWorldPos.Y);
						}
						
						// Spawn water if terrain is below water level
						if (TerrainHeight < WaterLevel)
						{
							// Calculate proper water depth at this location
							float WaterDepth = WaterLevel - TerrainHeight;
							
							// Fill cells from terrain up to water level for proper volume
							int32 CellsToFill = FMath::CeilToInt(WaterDepth / CellSize);
							for (int32 ZCell = 0; ZCell < CellsToFill && SpawnedCells < MaxCellsPerChunk; ++ZCell)
							{
								float CellZ = TerrainHeight + (ZCell + 0.5f) * CellSize;
								if (CellZ <= WaterLevel)
								{
									FVector SpawnPos = FVector(CellWorldPos.X, CellWorldPos.Y, CellZ);
									
									// Use full fluid amount for proper water surface
									float FluidAmount = 1.0f;
									
									// Top cell might be partial
									if (CellZ + CellSize * 0.5f > WaterLevel)
									{
										FluidAmount = (WaterLevel - (CellZ - CellSize * 0.5f)) / CellSize;
									}
									
									ChunkManager->AddFluidAtWorldPosition(SpawnPos, FluidAmount);
									SpawnedCells++;
								}
							}
						}
					}
				}
				
				// Mark this chunk as processed
				ProcessedChunks.Add(ChunkCoord);
				
				UE_LOG(LogTemp, VeryVerbose, TEXT("VoxelFluidActor: Spawned %d water cells in chunk at %s"), 
					SpawnedCells, *ChunkCenter.ToString());
			}
			else
			{
				// Still mark as processed even if it has water
				ProcessedChunks.Add(ChunkCoord);
			}
		}
	}
	
	// Clean up old processed chunks that are now far away
	if (ProcessedChunks.Num() > 100) // Limit memory usage
	{
		TArray<FFluidChunkCoord> ChunksToRemove;
		for (const FFluidChunkCoord& ProcessedCoord : ProcessedChunks)
		{
			// Calculate chunk world position manually
			FVector ChunkWorldPos = FVector(
				ProcessedCoord.X * ChunkSize * CellSize,
				ProcessedCoord.Y * ChunkSize * CellSize,
				ProcessedCoord.Z * ChunkSize * CellSize
			);
			
			if (FVector::Dist(ChunkWorldPos, PlayerPos) > SimulationRadius * 2.0f)
			{
				ChunksToRemove.Add(ProcessedCoord);
			}
		}
		for (const FFluidChunkCoord& CoordToRemove : ChunksToRemove)
		{
			ProcessedChunks.Remove(CoordToRemove);
		}
	}
	*/
}

void AVoxelFluidActor::TestEditTriggeredActivation(const FVector& TestPosition, float TestRadius)
{
	UE_LOG(LogTemp, Warning, TEXT("=== Testing Edit-Triggered Activation ==="));
	UE_LOG(LogTemp, Warning, TEXT("Position: %s, Radius: %.1f"), *TestPosition.ToString(), TestRadius);
	UE_LOG(LogTemp, Warning, TEXT("Activation Mode: %s"), 
		ChunkActivationMode == EChunkActivationMode::EditTriggered ? TEXT("Edit Triggered") :
		ChunkActivationMode == EChunkActivationMode::DistanceBased ? TEXT("Distance Based") : 
		TEXT("Hybrid"));
	
	if (!ChunkManager)
	{
		UE_LOG(LogTemp, Error, TEXT("ChunkManager not initialized!"));
		return;
	}
	
	// Get chunk stats before
	int32 ChunksBefore = ChunkManager->GetLoadedChunkCount();
	int32 ActiveBefore = ChunkManager->GetActiveChunkCount();
	UE_LOG(LogTemp, Warning, TEXT("Before: %d loaded, %d active chunks"), ChunksBefore, ActiveBefore);
	
	// Simulate a terrain edit
	UE_LOG(LogTemp, Warning, TEXT("Simulating terrain edit..."));
	OnVoxelTerrainModified(TestPosition, TestRadius);
	
	// Force immediate update
	ChunkManager->ForceUpdateChunkStates();
	
	// Get chunk stats after
	int32 ChunksAfter = ChunkManager->GetLoadedChunkCount();
	int32 ActiveAfter = ChunkManager->GetActiveChunkCount();
	UE_LOG(LogTemp, Warning, TEXT("After: %d loaded, %d active chunks"), ChunksAfter, ActiveAfter);
	
	// Report results
	if (ChunksAfter > ChunksBefore || ActiveAfter > ActiveBefore)
	{
		UE_LOG(LogTemp, Warning, TEXT("SUCCESS: Chunks were activated! (+%d loaded, +%d active)"), 
			ChunksAfter - ChunksBefore, ActiveAfter - ActiveBefore);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("FAILED: No chunks were activated. Check configuration."));
	}
	
	UE_LOG(LogTemp, Warning, TEXT("=== Test Complete ==="));
}

void AVoxelFluidActor::SpawnSimulationWaterAroundPlayer()
{
	if (!ChunkManager)
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelFluidActor: ChunkManager not initialized"));
		return;
	}

	// Get current player position
	FVector PlayerPos = FVector::ZeroVector;
	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APawn* PlayerPawn = PC->GetPawn())
			{
				PlayerPos = PlayerPawn->GetActorLocation();
			}
		}
	}

	// Make sure simulation is running
	if (!bIsSimulating)
	{
		StartSimulation();
		UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Started simulation for water spawning"));
	}

	// Get water level from static water generator
	float WaterLevel = OceanWaterLevel;
	if (StaticWaterGenerator)
	{
		WaterLevel = StaticWaterGenerator->GetWaterLevelAtLocation(PlayerPos);
	}

	UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Spawning simulation water around player at %s, water level: %.1f"), 
		*PlayerPos.ToString(), WaterLevel);

	// Spawn water in a radius around the player (within MinRenderDistance of static water)
	// We want to fill the "hole" in the donut where static water doesn't render
	const float SpawnRadius = StaticWaterRenderer ? StaticWaterRenderer->RenderSettings.MinRenderDistance * 0.9f : 2000.0f;
	const float SpawnSpacing = CellSize * 2.0f; // Space out the spawns
	const int32 GridSize = FMath::CeilToInt(SpawnRadius * 2.0f / SpawnSpacing);
	
	int32 SpawnedCount = 0;
	int32 SkippedAboveTerrain = 0;
	
	for (int32 X = -GridSize/2; X <= GridSize/2; ++X)
	{
		for (int32 Y = -GridSize/2; Y <= GridSize/2; ++Y)
		{
			const FVector SpawnPos = PlayerPos + FVector(X * SpawnSpacing, Y * SpawnSpacing, 0);
			const float DistFromPlayer = FVector::Dist2D(SpawnPos, PlayerPos);
			
			// Only spawn within radius
			if (DistFromPlayer <= SpawnRadius)
			{
				// Sample terrain height
				float TerrainHeight = WaterLevel - 100.0f; // Default below water
				if (VoxelIntegration)
				{
					TerrainHeight = VoxelIntegration->SampleVoxelHeight(SpawnPos.X, SpawnPos.Y);
				}
				
				// Only spawn where water should exist (terrain below water level)
				if (TerrainHeight < WaterLevel)
				{
					// Spawn water at water level
					FVector FluidSpawnPos = FVector(SpawnPos.X, SpawnPos.Y, WaterLevel);
					
					// Add fluid
					const float FluidAmount = 1.0f; // Full cell
					AddFluidAtLocation(FluidSpawnPos, FluidAmount);
					SpawnedCount++;
					
					// Also add some below for depth (up to 3 cells deep)
					for (int32 Z = 1; Z <= 3; ++Z)
					{
						FVector BelowPos = FluidSpawnPos - FVector(0, 0, Z * CellSize);
						if (BelowPos.Z > TerrainHeight + CellSize/2) // Make sure we're above terrain
						{
							AddFluidAtLocation(BelowPos, FluidAmount);
							SpawnedCount++;
						}
					}
				}
				else
				{
					SkippedAboveTerrain++;
				}
			}
		}
	}
	
	UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Spawned %d fluid cells around player (skipped %d above terrain)"), 
		SpawnedCount, SkippedAboveTerrain);
}

