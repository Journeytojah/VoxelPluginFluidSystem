#include "Actors/VoxelFluidActor.h"
#include "Actors/VoxelStaticWaterActor.h"
#include "CellularAutomata/FluidChunkManager.h"
#include "CellularAutomata/FluidChunk.h"
#include "CellularAutomata/CAFluidGrid.h"
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
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"


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

	StaticWaterGenerator = CreateDefaultSubobject<UStaticWaterGenerator>(TEXT("StaticWaterGenerator"));
	// Disabled - VoxelStaticWaterActor handles static water rendering now
	// StaticWaterRenderer = CreateDefaultSubobject<UStaticWaterRenderer>(TEXT("StaticWaterRenderer"));
	StaticWaterRenderer = nullptr;
	WaterActivationManager = CreateDefaultSubobject<UWaterActivationManager>(TEXT("WaterActivationManager"));

	// Default performance-friendly settings
	ChunkSize = 64;  // Increased to 64 for better resolution
	CellSize = 25.0f;  // Reduced to 25cm for maximum resolution
	ChunkLoadDistance = 8000.0f;
	ChunkActiveDistance = 5000.0f;
	MaxActiveChunks = 50;  // Reasonable limit for performance (reverted from 200)
	MaxLoadedChunks = 100;  // Reasonable limit for memory (reverted from 400)
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
	
	// Initialize high resolution settings
	// Static water properties removed - now handled by AVoxelStaticWaterActor
	// bUseHighResolution = true;
	// WaterSpawnDensity = 0.5f;
	// WaterEdgeSmoothness = 0.2f;
	// bUseParallelTerrainSampling = false; // Disabled - VoxelPlugin requires game thread

	// Initialize dynamic water activation properties
	bAcceptStaticWaterActivation = true;
	StaticToDynamicConversionRate = 10.0f;
}

void AVoxelFluidActor::BeginPlay()
{
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_BeginPlay);
	
	UE_LOG(LogTemp, Warning, TEXT("[PROFILING] VoxelFluidActor BeginPlay started"));
	double StartTime = FPlatformTime::Seconds();
	
	Super::BeginPlay();

	// Link with static water actor if one exists in the scene
	if (!LinkedStaticWaterActor)
	{
		// Try to find a static water actor in the scene
		TArray<AActor*> FoundActors;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), AVoxelStaticWaterActor::StaticClass(), FoundActors);
		if (FoundActors.Num() > 0)
		{
			LinkedStaticWaterActor = Cast<AVoxelStaticWaterActor>(FoundActors[0]);
			if (LinkedStaticWaterActor)
			{
				LinkedStaticWaterActor->SetFluidActor(this);
				UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Linked with static water actor %s"), *LinkedStaticWaterActor->GetName());
			}
		}
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_SystemInit);
		double InitStartTime = FPlatformTime::Seconds();
		UE_LOG(LogTemp, Warning, TEXT("[PROFILING] InitializeFluidSystem started"));
		
		InitializeFluidSystem();
		
		double SystemInitTime = (FPlatformTime::Seconds() - InitStartTime) * 1000.0;
		UE_LOG(LogTemp, Warning, TEXT("[PROFILING] InitializeFluidSystem completed in %.2f ms"), SystemInitTime);
	}

	if (bAutoStart)
	{
		SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_AutoStart);
		double AutoStartTime = FPlatformTime::Seconds();
		UE_LOG(LogTemp, Warning, TEXT("[PROFILING] StartSimulation started"));
		
		StartSimulation();
		
		double SimTime = (FPlatformTime::Seconds() - AutoStartTime) * 1000.0;
		UE_LOG(LogTemp, Warning, TEXT("[PROFILING] StartSimulation completed in %.2f ms"), SimTime);
	}
	
	double TotalTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;
	UE_LOG(LogTemp, Warning, TEXT("[PROFILING] VoxelFluidActor BeginPlay completed in %.2f ms"), TotalTime);

	// Force load chunks in distance-based mode
	if (ChunkManager && ChunkActivationMode == EChunkActivationMode::DistanceBased)
	{
		FTimerHandle TimerHandle;
		GetWorld()->GetTimerManager().SetTimer(TimerHandle, [this]()
		{
			SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_FirstChunkLoad);
			
			TArray<FVector> ViewerPositions = GetViewerPositions();
			// Force an update to load chunks
			ChunkManager->UpdateChunks(0.1f, ViewerPositions);
			
			// Make sure simulation is running
			if (!bIsSimulating)
			{
				StartSimulation();
				UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Started fluid simulation"));
			}
			
			// Only spawn initial water in distance-based mode
			if (ChunkActivationMode == EChunkActivationMode::DistanceBased)
			{
				SpawnDynamicWaterAroundPlayer();
			}
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
					
					// Static water and hybrid system stats now handled by AVoxelStaticWaterActor
					
					// Dynamic simulation stats
					if (ChunkManager)
					{
						int32 SimChunks = ChunkManager->GetActiveChunkCount();
						SET_DWORD_STAT(STAT_VoxelFluid_SimulationChunks, SimChunks);
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

	// Static water rendering performance stats now handled by AVoxelStaticWaterActor
	if (false) // StaticWaterRenderer removed
	{
		// Static water rendering is much cheaper - estimate based on chunk count
		int32 StaticChunks = 0; // StaticWaterRenderer->GetActiveRenderChunkCount();
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
		// SET_DWORD_STAT(STAT_VoxelFluid_ActiveSources, FluidSources.Num()); // Hidden - source detail
		// SET_FLOAT_STAT(STAT_VoxelFluid_TotalSourceFlow, TotalFlowRate); // Hidden - source detail
	}
	else
	{
		// SET_DWORD_STAT(STAT_VoxelFluid_ActiveSources, 0); // Hidden - source detail
		// SET_FLOAT_STAT(STAT_VoxelFluid_TotalSourceFlow, 0.0f); // Hidden - source detail
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
	{
		SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_ChunkSystemInit);
		InitializeChunkSystem();
	}

	// Try to auto-link with static water actor if not already linked
	if (!LinkedStaticWaterActor)
	{
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<AVoxelStaticWaterActor> ActorIterator(World); ActorIterator; ++ActorIterator)
			{
				AVoxelStaticWaterActor* StaticWaterActor = *ActorIterator;
				if (StaticWaterActor && IsValid(StaticWaterActor))
				{
					// Link the actors together
					LinkedStaticWaterActor = StaticWaterActor;
					StaticWaterActor->SetFluidActor(this);
					
					UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Auto-linked to VoxelStaticWaterActor: %s"), 
						*StaticWaterActor->GetName());
					break;
				}
			}
		}
	}

	if (VoxelIntegration && ChunkManager)
	{
		SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_VoxelIntegrationInit);
		
		VoxelIntegration->SetChunkManager(ChunkManager);
		VoxelIntegration->CellWorldSize = CellSize;

		// Set static water generator if enabled
		// TODO: Fix ChunkManager to use StaticWaterGenerator instead of deprecated StaticWaterManager
		/*if (StaticWaterGenerator && bEnableStaticWater)
		{
			ChunkManager->SetStaticWaterGenerator(StaticWaterGenerator);
		}*/

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
				if (false) // Static water handled by AVoxelStaticWaterActor
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
								/*if (StaticWaterManager->ChunkIntersectsStaticWater(ChunkBounds))
								{
									StaticWaterManager->ApplyStaticWaterToChunkWithTerrain(Chunk, ChunkManager);
								}*/
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

										// StaticWaterManager->ApplyStaticWaterToChunkWithTerrain(RetryChunk, ChunkManager);
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
		SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_VisualizationInit);
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

	UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor::AddFluidSource at %s, flow rate: %.2f (final: %.2f)"), 
		*WorldPosition.ToString(), ActualFlowRate, FinalFlowRate);

	if (FluidSources.Contains(WorldPosition))
	{
		FluidSources[WorldPosition] = FinalFlowRate;
		UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Updated existing fluid source. Total sources: %d"), FluidSources.Num());
	}
	else
	{
		FluidSources.Add(WorldPosition, FinalFlowRate);
		UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Added new fluid source. Total sources: %d"), FluidSources.Num());
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

	UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor::AddFluidAtLocation at %s, amount: %.2f (adjusted: %.2f)"), 
		*WorldPosition.ToString(), Amount, AdjustedAmount);

	// Notify water activation manager about fluid being added
	// Water activation manager now handled by AVoxelStaticWaterActor
	/*if (WaterActivationManager && AdjustedAmount > 0.0f)
	{
		WaterActivationManager->OnFluidAdded(WorldPosition, AdjustedAmount);
	}*/

	if (ChunkManager)
	{
		ChunkManager->AddFluidAtWorldPosition(WorldPosition, AdjustedAmount);
		UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Added fluid via ChunkManager"));
	}
	else if (VoxelIntegration)
	{
		VoxelIntegration->AddFluidAtWorldPosition(WorldPosition, AdjustedAmount);
		UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Added fluid via VoxelIntegration"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelFluidActor: No ChunkManager or VoxelIntegration available!"));
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
		// SET_DWORD_STAT(STAT_VoxelFluid_ActiveSources, FluidSources.Num()); // Hidden - source detail

		float TotalFlowRate = 0.0f;
		for (const auto& Source : FluidSources)
		{
			const FVector& SourcePos = Source.Key;
			const float SourceFlowRate = Source.Value;
			TotalFlowRate += SourceFlowRate;

			// Use the source's specific flow rate, not the global one
			ChunkManager->AddFluidAtWorldPosition(SourcePos, SourceFlowRate * DeltaTime);
		}

		// SET_FLOAT_STAT(STAT_VoxelFluid_TotalSourceFlow, TotalFlowRate); // Hidden - source detail
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
	// Static water bounds visualization moved to AVoxelStaticWaterActor
	/*if (false)
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
	}*/
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
	
	// Static Water Stats removed
	/*if (false) // StaticWaterRenderer removed
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
	}*/
	
	// Water Activation Stats removed
	/*if (false) // WaterActivationManager removed
	{
		int32 ActiveRegions = WaterActivationManager->GetActiveRegionCount();
		Stats += FString::Printf(TEXT("Active Water Regions: %d\n"), ActiveRegions);
	}*/
	
	// Performance Comparison
	/*if (ChunkManager && false) // StaticWaterRenderer removed
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
	}*/
	
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
	UE_LOG(LogTemp, Warning, TEXT("[PROFILING] InitializeChunkSystem - Creating ChunkManager"));
	double StepStartTime = FPlatformTime::Seconds();
	
	if (!ChunkManager)
	{
		ChunkManager = NewObject<UFluidChunkManager>(this, UFluidChunkManager::StaticClass());
	}
	
	double ChunkCreationTime = (FPlatformTime::Seconds() - StepStartTime) * 1000.0;
	UE_LOG(LogTemp, Warning, TEXT("[PROFILING] ChunkManager creation: %.2f ms"), ChunkCreationTime);

	UE_LOG(LogTemp, Warning, TEXT("[PROFILING] InitializeChunkSystem - Initializing ChunkManager"));
	StepStartTime = FPlatformTime::Seconds();
	
	const FVector ActorLocation = GetActorLocation();
	SimulationOrigin = ActorLocation - SimulationBoundsExtent + SimulationBoundsOffset;
	ActiveBoundsExtent = SimulationBoundsExtent;

	const FVector WorldSize = ActiveBoundsExtent * 2.0f;

	ChunkManager->Initialize(ChunkSize, CellSize, SimulationOrigin, WorldSize);
	
	double ChunkInitTime = (FPlatformTime::Seconds() - StepStartTime) * 1000.0;
	UE_LOG(LogTemp, Warning, TEXT("[PROFILING] ChunkManager Initialize: %.2f ms"), ChunkInitTime);

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

	UE_LOG(LogTemp, Warning, TEXT("[PROFILING] InitializeChunkSystem - Setting streaming config"));
	StepStartTime = FPlatformTime::Seconds();
	
	ChunkManager->SetStreamingConfig(Config);
	ChunkManager->Viscosity = FluidViscosity;
	ChunkManager->Gravity = GravityStrength;
	ChunkManager->EvaporationRate = FluidEvaporationRate;
	
	double StreamingConfigTime = (FPlatformTime::Seconds() - StepStartTime) * 1000.0;
	UE_LOG(LogTemp, Warning, TEXT("[PROFILING] Streaming config setup: %.2f ms"), StreamingConfigTime);

	// Optimization settings removed - using default behavior


	// Sync debug settings
	ChunkManager->bShowChunkBorders = bShowChunkBorders;
	ChunkManager->bShowChunkStates = bShowChunkStates;
	ChunkManager->DebugUpdateInterval = ChunkDebugUpdateInterval;

	// Initialize static water components
	{
		SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_StaticWaterInit);
		
		if (StaticWaterRenderer && VoxelIntegration)
		{
			StaticWaterRenderer->SetVoxelIntegration(VoxelIntegration);
		}

		if (StaticWaterGenerator && VoxelIntegration)
		{
			StaticWaterGenerator->SetVoxelWorld(TargetVoxelWorld);
		}

		if (WaterActivationManager && ChunkManager)
		{
			WaterActivationManager->SetFluidChunkManager(ChunkManager);
			WaterActivationManager->SetStaticWaterGenerator(StaticWaterGenerator);
			WaterActivationManager->SetStaticWaterRenderer(StaticWaterRenderer);
		}
	}
}

void AVoxelFluidActor::UpdateChunkSystem(float DeltaTime)
{
	if (!ChunkManager)
		return;

	// In edit-triggered mode, don't update chunk streaming based on viewer position
	// This prevents constant loading/unloading that causes hitching
	if (ChunkActivationMode == EChunkActivationMode::DistanceBased)
	{
		// Only update chunk streaming in distance-based mode
		TArray<FVector> ViewerPositions = GetViewerPositions();
		ChunkManager->UpdateChunks(DeltaTime, ViewerPositions);
	}
	else if (ChunkActivationMode == EChunkActivationMode::EditTriggered)
	{
		// In edit-triggered mode, only update simulation for already-active chunks
		// Don't load/unload based on distance
		static TArray<FVector> EmptyPositions;
		ChunkManager->UpdateChunks(DeltaTime, EmptyPositions);
	}
	else // Hybrid mode
	{
		// Hybrid mode uses both distance and edit triggers
		TArray<FVector> ViewerPositions = GetViewerPositions();
		ChunkManager->UpdateChunks(DeltaTime, ViewerPositions);
	}

	// Add fluid from all active sources using their individual flow rates (unless paused)
	if (!bPauseFluidSources && FluidSources.Num() > 0)
	{
		for (const auto& Source : FluidSources)
		{
			const FVector& SourcePos = Source.Key;
			const float SourceFlowRate = Source.Value;
			ChunkManager->AddFluidAtWorldPosition(SourcePos, SourceFlowRate * DeltaTime);
		}
		
		// Debug log to verify sources are working
		static float DebugTimer = 0.0f;
		DebugTimer += DeltaTime;
		if (DebugTimer > 1.0f)
		{
			UE_LOG(LogTemp, Warning, TEXT("VoxelFluid: Processing %d fluid sources"), FluidSources.Num());
			DebugTimer = 0.0f;
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

// Static Water Implementation - Deprecated (moved to AVoxelStaticWaterActor)





// IsPointInStaticWater removed - use LinkedStaticWaterActor directly


// DEPRECATED: Static water refill moved to AVoxelStaticWaterActor
/*
void AVoxelFluidActor::RefillStaticWaterInRadius(const FVector& Center, float Radius)
{
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_DynamicRefill);

	// StaticWaterManager removed - function disabled
	if (!ChunkManager) // || !StaticWaterManager || !bEnableStaticWater)
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
*/

// DEPRECATED: Dynamic to static conversion moved to AVoxelStaticWaterActor
/*
void AVoxelFluidActor::StartDynamicToStaticConversion(const FVector& Center, float Radius)
{
	// Schedule conversion after settling time
	FTimerHandle ConversionTimerHandle;
	GetWorld()->GetTimerManager().SetTimer(ConversionTimerHandle, [this, Center, Radius]()
	{
		ConvertSettledFluidToStatic(Center, Radius);
	}, DynamicToStaticSettleTime, false);

}
*/

// DEPRECATED: Settled fluid conversion moved to AVoxelStaticWaterActor  
/*
void AVoxelFluidActor::ConvertSettledFluidToStatic(const FVector& Center, float Radius)
{
	// StaticWaterManager removed - function disabled
	if (!ChunkManager) // || !StaticWaterManager || !bEnableStaticWater)
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
*/

// DEPRECATED: Terrain refresh testing moved to AVoxelStaticWaterActor
/*
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
	// RefillStaticWaterInRadius(Location, Radius); // Removed - handled by static water actor
}
*/

// DEPRECATED: Static water regions moved to AVoxelStaticWaterActor
/*
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
*/

// DEPRECATED: Static water region removal moved to AVoxelStaticWaterActor
/*
void AVoxelFluidActor::RemoveStaticWaterRegion(const FVector& Center, float Radius)
{
	if (!StaticWaterGenerator)
		return;

	// For now, clear all regions and let user re-add the ones they want
	// TODO: Implement region-specific removal based on position
	StaticWaterGenerator->ClearAllWaterRegions();

	UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Cleared static water regions"));
}
*/

// OnTerrainEdited function removed - not declared in header
// This functionality should be handled through OnTerrainModified or through the linked static water actor

/* OnTerrainEdited function body commented out - not declared in header
		const int32 SpawnGridSize = FMath::CeilToInt(LocalizedSpawnRadius / SpawnSpacing);
		
		// Pre-calculate terrain heights in parallel for performance
		TArray<float> TerrainHeights;
		TArray<FVector> SpawnPositions;
		TerrainHeights.Reserve(SpawnGridSize * SpawnGridSize * 4);
		SpawnPositions.Reserve(SpawnGridSize * SpawnGridSize * 4);
		
		// Gather all spawn positions first
		for (int32 X = -SpawnGridSize; X <= SpawnGridSize; ++X)
		{
			for (int32 Y = -SpawnGridSize; Y <= SpawnGridSize; ++Y)
			{
				const FVector SpawnPos = EditPosition + FVector(X * SpawnSpacing, Y * SpawnSpacing, 0);
				const float DistFromCenter = FVector::Dist2D(SpawnPos, EditPosition);
				
				if (DistFromCenter <= LocalizedSpawnRadius)
				{
					SpawnPositions.Add(SpawnPos);
				}
			}
		}
		
		// Optimized terrain sampling with interpolation to reduce stutter
		if (VoxelIntegration)
		{
			TerrainHeights.SetNum(SpawnPositions.Num());
			
			if (bUseHighResolution && SpawnPositions.Num() > 100)
			{
				// Smart sampling: Sample fewer points and interpolate
				const int32 SampleStep = 3; // Sample every 3rd point
				TArray<float> SampledHeights;
				TArray<int32> SampledIndices;
				
				// Sample key points on game thread (much faster with fewer samples)
				for (int32 i = 0; i < SpawnPositions.Num(); i += SampleStep)
				{
					float Height = VoxelIntegration->SampleVoxelHeight(
						SpawnPositions[i].X,
						SpawnPositions[i].Y
					);
					TerrainHeights[i] = Height;
					SampledHeights.Add(Height);
					SampledIndices.Add(i);
				}
				
				// Always sample the last point if not already sampled
				if (SpawnPositions.Num() > 0 && (SpawnPositions.Num() - 1) % SampleStep != 0)
				{
					int32 LastIdx = SpawnPositions.Num() - 1;
					TerrainHeights[LastIdx] = VoxelIntegration->SampleVoxelHeight(
						SpawnPositions[LastIdx].X,
						SpawnPositions[LastIdx].Y
					);
				}
				
				// Interpolate remaining points in parallel (no VoxelPlugin calls)
				ParallelFor(SpawnPositions.Num(), [&](int32 i)
				{
					// Skip already sampled points
					if (i % SampleStep == 0) return;
					
					// Find surrounding sampled points
					int32 PrevSample = (i / SampleStep) * SampleStep;
					int32 NextSample = FMath::Min(PrevSample + SampleStep, SpawnPositions.Num() - 1);
					
					// Bilinear interpolation based on 2D distance
					if (NextSample < SpawnPositions.Num())
					{
						const FVector& CurrentPos = SpawnPositions[i];
						const FVector& PrevPos = SpawnPositions[PrevSample];
						const FVector& NextPos = SpawnPositions[NextSample];
						
						float DistToPrev = FVector::Dist2D(CurrentPos, PrevPos);
						float DistToNext = FVector::Dist2D(CurrentPos, NextPos);
						float TotalDist = DistToPrev + DistToNext;
						
						if (TotalDist > 0.0f)
						{
							// Weighted average based on inverse distance
							float PrevWeight = 1.0f - (DistToPrev / TotalDist);
							float NextWeight = 1.0f - (DistToNext / TotalDist);
							float WeightSum = PrevWeight + NextWeight;
							
							TerrainHeights[i] = (TerrainHeights[PrevSample] * PrevWeight + 
												 TerrainHeights[NextSample] * NextWeight) / WeightSum;
						}
						else
						{
							TerrainHeights[i] = TerrainHeights[PrevSample];
						}
					}
					else
					{
						TerrainHeights[i] = TerrainHeights[PrevSample];
					}
				});
			}
			else
			{
				// For smaller areas, sample directly
				for (int32 i = 0; i < SpawnPositions.Num(); ++i)
				{
					TerrainHeights[i] = VoxelIntegration->SampleVoxelHeight(
						SpawnPositions[i].X, 
						SpawnPositions[i].Y
					);
				}
			}
		}
		
		int32 SpawnedCount = 0;
		for (int32 i = 0; i < SpawnPositions.Num(); ++i)
		{
			const FVector& SpawnPos = SpawnPositions[i];
			const float TerrainHeight = TerrainHeights.IsValidIndex(i) ? 
				TerrainHeights[i] : (EditPosition.Z - HeightChange);
			
			// Only spawn water if it's above terrain and below water level
			if (TerrainHeight < WaterLevel)
			{
				const float DistFromCenter = FVector::Dist2D(SpawnPos, EditPosition);
				const float DistanceRatio = 1.0f - (DistFromCenter / LocalizedSpawnRadius);
				
				// Calculate water depth with smooth falloff
				const float WaterDepth = WaterLevel - TerrainHeight;
				const float MaxSpawnDepth = FMath::Min(WaterDepth, CellSize * 10.0f);
				const int32 LayersToSpawn = FMath::CeilToInt(MaxSpawnDepth / CellSize);
				
				for (int32 Layer = 0; Layer < LayersToSpawn; ++Layer)
				{
					float SpawnHeight = TerrainHeight + (Layer + 0.5f) * CellSize;
					if (SpawnHeight > WaterLevel) break;
					
					FVector FluidSpawnPos = FVector(SpawnPos.X, SpawnPos.Y, SpawnHeight);
					
					// Smooth interpolation: more fluid at center, less at edges
					// Also account for depth - partial fill at water surface
					float FluidAmount = 1.0f;
					if (SpawnHeight + CellSize * 0.5f > WaterLevel)
					{
						// Partial fill for surface cells
						FluidAmount = (WaterLevel - (SpawnHeight - CellSize * 0.5f)) / CellSize;
					}
					
					// Apply distance-based falloff for smoother edges
					const float SmoothnessFactor = bUseHighResolution ? 
						WaterEdgeSmoothness : 0.2f;
					FluidAmount *= FMath::SmoothStep(SmoothnessFactor, 1.0f, DistanceRatio);
					
					if (FluidAmount > 0.01f)
					{
						AddFluidAtLocation(FluidSpawnPos, FluidAmount);
						SpawnedCount++;
					}
				}
			}
		}
		
		UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Spawned %d localized fluid cells"), SpawnedCount);
	}

	// Water activation now handled by AVoxelStaticWaterActor

	// Static water rendering now handled by AVoxelStaticWaterActor
	if (false) // StaticWaterRenderer removed
	{
		// Hide static water where dynamic simulation is active
		// StaticWaterRenderer->RebuildChunksInRadius(EditPosition, EditRadius * 2.0f);
		
		// TODO: Implement exclusion zones in StaticWaterRenderer to prevent 
		// static water from rendering where dynamic water exists
		// This would require adding AddDynamicWaterExclusionZone method to StaticWaterRenderer
		
		/* Future implementation:
		if (ChunkManager && bUseHighResolution)
		{
			// Get all active chunks and mark them as exclusion zones for static water
			TArray<FFluidChunkCoord> ActiveChunkCoords = ChunkManager->GetChunksInBounds(
				FBox(EditPosition - FVector(EditRadius * 2.0f), 
					 EditPosition + FVector(EditRadius * 2.0f))
			);
			
			for (const FFluidChunkCoord& ChunkCoord : ActiveChunkCoords)
			{
				if (UFluidChunk* Chunk = ChunkManager->GetChunk(ChunkCoord))
				{
					if (Chunk->State == EChunkState::Active && Chunk->HasActiveFluid())
					{
						// Mark this area as having dynamic water
						FBox ChunkBounds = Chunk->GetWorldBounds();
						// StaticWaterRenderer->AddDynamicWaterExclusionZone(ChunkBounds);
					}
				}
			}
		}
		*/
	// }
// }
// End of OnTerrainEdited function body - commented out */

// OnVoxelTerrainModified removed - not declared in header
/*
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
	
	// OnTerrainEdited call removed - function not in header
	// Terrain editing should be handled through linked static water actor if needed
	
	// Also notify the VoxelIntegration directly
	if (VoxelIntegration)
	{
		const FBox ModifiedBounds = FBox::BuildAABB(ModifiedPosition, FVector(ModifiedRadius));
		VoxelIntegration->OnVoxelTerrainModified(ModifiedBounds);
	}
	
	// Static water renderer updates now handled by AVoxelStaticWaterActor
}
*/

// IsRegionActiveForSimulation removed - not declared in header
/*
bool AVoxelFluidActor::IsRegionActiveForSimulation(const FVector& Position) const
{
	// Water activation manager removed - delegate to linked static water actor
	return false;
}
*/

// ForceActivateWaterAtLocation removed - not declared in header
/*
void AVoxelFluidActor::ForceActivateWaterAtLocation(const FVector& Position, float Radius)
{
	// Water activation manager removed - delegate to linked static water actor
	return;

	const bool bSuccess = false; // WaterActivationManager removed
	
	UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: %s water activation at %s (radius: %.1f)"), 
		bSuccess ? TEXT("Successful") : TEXT("Failed"), *Position.ToString(), Radius);
}
*/

// ForceDeactivateAllWaterRegions removed - not declared in header
/*
void AVoxelFluidActor::ForceDeactivateAllWaterRegions()
{
	// Water activation manager removed - delegate to linked static water actor
	return;
	
	UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Deactivated all water regions"));
}
*/

// GetActiveWaterRegionCount removed - not declared in header
/*
int32 AVoxelFluidActor::GetActiveWaterRegionCount() const
{
	// Water activation manager removed - delegate to linked static water actor
	return 0;
}
*/

// DEPRECATED: Test water system moved to AVoxelStaticWaterActor
/*
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
		// Static water MUST tick to render, but we can control update frequency
		StaticWaterRenderer->SetComponentTickEnabled(true);
		StaticWaterRenderer->RegenerateAroundViewer();
		
		UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Forced renderer regeneration at player position"));
		
		// Always enable auto-tracking so static water renders
		StaticWaterRenderer->EnableAutoTracking(true);
		
		// Wait a frame then ensure tracking is enabled
		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(TimerHandle, [this]()
		{
			if (StaticWaterRenderer)
			{
				StaticWaterRenderer->EnableAutoTracking(true);
				UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Auto-tracking enabled"));
			}
		}, 0.1f, false);
		
		UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Test water system setup complete - Ocean at %s, Level: %.1f (auto-tracking enabled)"), 
			*PlayerPos.ToString(), OceanLevel);
	}
}
*/

// DEPRECATED: Test ocean creation moved to AVoxelStaticWaterActor
/*
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
*/

// RecenterOceanOnPlayer removed - not declared in header
/*
void AVoxelFluidActor::RecenterOceanOnPlayer()
{
	// CreateTestOcean(); // Removed - handled by static water actor
}
*/

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
	// OnVoxelTerrainModified removed - function not declared in header
	// OnVoxelTerrainModified(TestPosition, TestRadius);
	
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

// SpawnSimulationWaterAroundPlayer removed - not declared in header
/*
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

	// Get water level from linked static water actor
	float WaterLevel = -100.0f; // Default ocean level
	if (LinkedStaticWaterActor)
	{
		WaterLevel = LinkedStaticWaterActor->GetWaterLevelAtPosition(PlayerPos);
	}

	UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Spawning simulation water around player at %s, water level: %.1f"), 
		*PlayerPos.ToString(), WaterLevel);

	// Spawn water in a radius around the player (within MinRenderDistance of static water)
	// We want to fill the "hole" in the donut where static water doesn't render
	const float SpawnRadius = 2000.0f; // Default spawn radius since StaticWaterRenderer is removed
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
*/

void AVoxelFluidActor::DebugStuttering()
{
	UE_LOG(LogTemp, Warning, TEXT("VoxelFluidActor: Debug stuttering analysis"));
	
	if (!ChunkManager)
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelFluidActor: ChunkManager is null"));
		return;
	}
	
	// Log current system state for stuttering diagnosis
	const auto& Stats = ChunkManager->GetStats();
	UE_LOG(LogTemp, Warning, TEXT("Active Chunks: %d, Total: %d"), 
		Stats.ActiveChunks, Stats.TotalChunks);
	
	// Log activation mode from streaming config
	const auto& Config = ChunkManager->GetStreamingConfig();
	UE_LOG(LogTemp, Warning, TEXT("Activation Mode: %s"), 
		Config.ActivationMode == EChunkActivationMode::EditTriggered ? TEXT("EditTriggered") : 
		Config.ActivationMode == EChunkActivationMode::DistanceBased ? TEXT("DistanceBased") : TEXT("Hybrid"));
}

// ===== Communication with Static Water Actor =====

void AVoxelFluidActor::OnStaticWaterActivationRequest(const FVector& Position, float Radius, float WaterLevel)
{
	if (!bAcceptStaticWaterActivation)
	{
		return;
	}

	// Activate chunks in the area if using edit-triggered mode
	if (ChunkManager && ChunkActivationMode != EChunkActivationMode::DistanceBased)
	{
		ChunkManager->OnVoxelEditOccurred(Position, Radius);
	}

	// Calculate amount of water to spawn based on radius and conversion rate
	float WaterAmount = (Radius * Radius / 10000.0f) * StaticToDynamicConversionRate;
	
	// Add dynamic water at the specified location
	AddFluidAtLocation(FVector(Position.X, Position.Y, WaterLevel), WaterAmount);
	
	UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Activated dynamic water at %s (radius: %.1f, amount: %.1f)"),
		*Position.ToString(), Radius, WaterAmount);
}

void AVoxelFluidActor::SetStaticWaterActor(AVoxelStaticWaterActor* InStaticWaterActor)
{
	LinkedStaticWaterActor = InStaticWaterActor;
	
	if (LinkedStaticWaterActor)
	{
		LinkedStaticWaterActor->SetFluidActor(this);
		UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Linked with static water actor %s"), 
			*LinkedStaticWaterActor->GetName());
	}
}

bool AVoxelFluidActor::QueryStaticWaterLevel(const FVector& Position, float& OutWaterLevel) const
{
	if (StaticWaterGenerator && bEnableStaticWater)
	{
		if (StaticWaterGenerator->HasStaticWaterAtLocation(Position))
		{
			OutWaterLevel = StaticWaterGenerator->GetWaterLevelAtLocation(Position);
			return true;
		}
	}
	
	// Fallback to linked static water actor if available
	if (LinkedStaticWaterActor)
	{
		OutWaterLevel = LinkedStaticWaterActor->GetWaterLevelAtPosition(Position);
		return LinkedStaticWaterActor->IsPointInStaticWater(Position);
	}

	return false;
}

bool AVoxelFluidActor::IsPointInStaticWater(const FVector& WorldPosition, float& OutWaterLevel) const
{
	return QueryStaticWaterLevel(WorldPosition, OutWaterLevel);
}

void AVoxelFluidActor::OnTerrainModified(const FVector& ModifiedPosition, float ModifiedRadius)
{
	// Refresh terrain data
	if (VoxelIntegration && VoxelIntegration->IsVoxelWorldValid())
	{
		VoxelIntegration->RefreshTerrainInRadius(ModifiedPosition, ModifiedRadius);
	}

	// Call OnTerrainEdited to handle static water refresh
	OnTerrainEdited(ModifiedPosition, ModifiedRadius);

	// Notify static water actor if linked (for backward compatibility)
	if (LinkedStaticWaterActor)
	{
		LinkedStaticWaterActor->OnVoxelTerrainModified(ModifiedPosition, ModifiedRadius);
	}
}

void AVoxelFluidActor::OnTerrainEdited(const FVector& EditPosition, float EditRadius)
{
	// Refresh static water in the edited area
	if (StaticWaterRenderer && bEnableStaticWater)
	{
		StaticWaterRenderer->RebuildChunksInRadius(EditPosition, EditRadius);
	}
	
	// Activate water conversion if enabled
	if (WaterActivationManager && bAcceptStaticWaterActivation)
	{
		WaterActivationManager->OnTerrainEdited(EditPosition, EditRadius, 0.0f);
	}

	// Activate chunks if needed
	if (ChunkManager && ChunkActivationMode != EChunkActivationMode::DistanceBased)
	{
		ChunkManager->OnVoxelEditOccurred(EditPosition, EditRadius);
	}
}

void AVoxelFluidActor::SpawnDynamicWaterAroundPlayer()
{
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			FVector PlayerPos = Pawn->GetActorLocation();
			
			// Spawn a moderate amount of water near the player
			FVector SpawnPos = PlayerPos + FVector(0, 0, 200.0f); // Spawn above player
			AddFluidAtLocation(SpawnPos, 5.0f);
			
			UE_LOG(LogTemp, Log, TEXT("Spawned dynamic water at player location: %s"), *SpawnPos.ToString());
		}
	}
}

void AVoxelFluidActor::NotifyStaticWaterOfSettledFluid(const FVector& Center, float Radius)
{
	// If we have a linked static water actor, notify it that fluid has settled
	// This could be used to convert settled dynamic water back to static water
	if (LinkedStaticWaterActor && bAcceptStaticWaterActivation)
	{
		// For now, just log this event
		UE_LOG(LogTemp, Log, TEXT("VoxelFluidActor: Fluid settled at %s (radius: %.1f) - could convert to static"),
			*Center.ToString(), Radius);
	}
}

