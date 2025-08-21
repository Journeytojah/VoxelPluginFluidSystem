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

// Temporary definitions until build system picks up VoxelFluidDebug.cpp
DEFINE_LOG_CATEGORY(LogVoxelFluidDebug);
TAutoConsoleVariable<bool> CVarEnableVoxelFluidDebugLogging(
	TEXT("voxelfluid.EnableDebugLogging"),
	false,
	TEXT("Enable debug logging for VoxelFluid system components"),
	ECVF_Default
);

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
	
	// Ensure ocean water level is reasonable (not at exact ground level)
	if (OceanWaterLevel >= 0.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("Ocean water level %.1f is at or above ground! Setting to -500"), OceanWaterLevel);
		OceanWaterLevel = -500.0f; // Slightly below typical terrain
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
			// Clear any existing static water first
			if (StaticWaterManager)
			{
				StaticWaterManager->ClearAllStaticWaterRegions();
			}
			
			// Now create ocean with proper level
			CreateOcean(OceanWaterLevel, OceanSize);
			UE_LOG(LogTemp, Log, TEXT("Created ocean with water level %.1f"), OceanWaterLevel);
		}, 1.0f, false); // Wait 1 second for terrain to be ready
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
						UE_LOG(LogTemp, Warning, TEXT("CLEANUP: Removed water from %d solid cells in chunk %s"), 
							CleanedCells, *ChunkCoord.ToString());
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
								StaticWaterManager->ApplyStaticWaterToChunkWithTerrain(Chunk, ChunkManager);
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
				UE_LOG(LogTemp, Error, TEXT("ERROR: VoxelIntegration not available for chunk %s!"), *ChunkCoord.ToString());
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
	
	// Apply optimization settings to chunk manager
	ChunkManager->bUseSleepChains = bUseSleepChains;
	ChunkManager->bUsePredictiveSettling = bUsePredictiveSettling;
	ChunkManager->SleepChainMergeDistance = SleepChainMergeDistance;
	ChunkManager->PredictiveSettlingConfidenceThreshold = PredictiveSettlingConfidenceThreshold;
	
	// Apply sparse grid settings
	ChunkManager->bUseSparseGrid = bUseSparseGrid;
	ChunkManager->SparseGridThreshold = SparseGridThreshold;
	
	// Apply memory compression if enabled
	if (bEnableMemoryCompression)
	{
		ChunkManager->EnableCompressedMode(true);
	}
	
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

void AVoxelFluidActor::ToggleMemoryCompression()
{
	bEnableMemoryCompression = !bEnableMemoryCompression;
	
	if (ChunkManager)
	{
		ChunkManager->EnableCompressedMode(bEnableMemoryCompression);
		
		UE_LOG(LogTemp, Warning, TEXT("Memory compression %s"), 
			bEnableMemoryCompression ? TEXT("ENABLED") : TEXT("DISABLED"));
		
		// Display memory stats after toggling
		UE_LOG(LogTemp, Warning, TEXT("%s"), *GetMemoryUsageStats());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ChunkManager not initialized"));
	}
}

FString AVoxelFluidActor::GetMemoryUsageStats() const
{
	if (!ChunkManager)
	{
		return TEXT("ChunkManager not initialized");
	}
	
	const FChunkManagerStats Stats = ChunkManager->GetStats();
	const int32 TotalCells = Stats.TotalActiveCells;
	
	// Calculate memory usage
	const float UncompressedMemoryMB = (TotalCells * 44.0f) / (1024.0f * 1024.0f); // 44 bytes per cell
	const float CompressedMemoryMB = (TotalCells * 4.0f) / (1024.0f * 1024.0f); // 4 bytes per cell
	const float CurrentMemoryMB = bEnableMemoryCompression ? CompressedMemoryMB : UncompressedMemoryMB;
	const float SavingsMB = UncompressedMemoryMB - CompressedMemoryMB;
	const float CompressionRatio = UncompressedMemoryMB > 0 ? (CompressedMemoryMB / UncompressedMemoryMB) : 0.0f;
	
	// Calculate sparse grid stats
	int32 SparseChunkCount = 0;
	int32 DenseChunkCount = 0;
	float TotalSparseRatio = 0.0f;
	float SparseMemorySavings = 0.0f;
	
	if (ChunkManager)
	{
		TArray<UFluidChunk*> ActiveChunks = ChunkManager->GetActiveChunks();
		for (UFluidChunk* Chunk : ActiveChunks)
		{
			if (Chunk)
			{
				if (Chunk->bUseSparseRepresentation)
				{
					SparseChunkCount++;
					TotalSparseRatio += Chunk->GetCompressionRatio();
					SparseMemorySavings += (Chunk->GetDenseMemoryUsage() - Chunk->GetSparseMemoryUsage()) / (1024.0f * 1024.0f);
				}
				else
				{
					DenseChunkCount++;
				}
			}
		}
	}
	
	const float AvgSparseRatio = SparseChunkCount > 0 ? TotalSparseRatio / SparseChunkCount : 0.0f;
	
	return FString::Printf(
		TEXT("=== Memory Usage Stats ===\n")
		TEXT("Total Active Cells: %d\n")
		TEXT("Compression: %s\n")
		TEXT("Current Memory: %.2f MB\n")
		TEXT("Uncompressed Size: %.2f MB\n")
		TEXT("Compressed Size: %.2f MB\n")
		TEXT("Memory Saved: %.2f MB (%.1f%% reduction)\n")
		TEXT("Compression Ratio: 1:%.1f\n")
		TEXT("\n=== Sparse Grid Stats ===\n")
		TEXT("Sparse Grid: %s\n")
		TEXT("Sparse Chunks: %d / %d (%.1f%%)\n")
		TEXT("Average Sparse Ratio: %.2fx\n")
		TEXT("Sparse Memory Saved: %.2f MB\n")
		TEXT("Combined Savings: %.2f MB"),
		TotalCells,
		bEnableMemoryCompression ? TEXT("ENABLED") : TEXT("DISABLED"),
		CurrentMemoryMB,
		UncompressedMemoryMB,
		CompressedMemoryMB,
		SavingsMB,
		(1.0f - CompressionRatio) * 100.0f,
		UncompressedMemoryMB > 0 ? (UncompressedMemoryMB / CompressedMemoryMB) : 0.0f,
		bUseSparseGrid ? TEXT("ENABLED") : TEXT("DISABLED"),
		SparseChunkCount, SparseChunkCount + DenseChunkCount,
		(SparseChunkCount + DenseChunkCount) > 0 ? (100.0f * SparseChunkCount / (SparseChunkCount + DenseChunkCount)) : 0.0f,
		AvgSparseRatio,
		SparseMemorySavings,
		SavingsMB + SparseMemorySavings
	);
}

void AVoxelFluidActor::TestSparseGridPerformance()
{
	if (!ChunkManager)
	{
		UE_LOG(LogTemp, Error, TEXT("ChunkManager not initialized"));
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("=== SPARSE GRID PERFORMANCE TEST ==="));
	
	// Test 1: Create a sparse fluid distribution (10% fill)
	UE_LOG(LogTemp, Warning, TEXT("1. Creating sparse fluid distribution (10%% fill)..."));
	
	const FVector TestCenter = GetActorLocation();
	const float SpawnSpacing = CellSize * 10.0f; // Every 10th cell
	const int32 TestRange = 5;
	
	// Spawn fluid in a sparse pattern
	int32 TotalSpawned = 0;
	for (int32 x = -TestRange; x <= TestRange; ++x)
	{
		for (int32 y = -TestRange; y <= TestRange; ++y)
		{
			for (int32 z = 0; z <= 2; ++z)
			{
				FVector SpawnPos = TestCenter + FVector(x * SpawnSpacing, y * SpawnSpacing, z * CellSize);
				ChunkManager->AddFluidAtWorldPosition(SpawnPos, 0.8f);
				TotalSpawned++;
			}
		}
	}
	
	UE_LOG(LogTemp, Warning, TEXT("  Spawned %d fluid cells in sparse pattern"), TotalSpawned);
	
	// Force update to trigger sparse conversion
	ChunkManager->UpdateSimulation(0.016f);
	
	// Test 2: Measure memory usage
	UE_LOG(LogTemp, Warning, TEXT("2. Memory usage comparison:"));
	
	const FString MemStats = GetMemoryUsageStats();
	UE_LOG(LogTemp, Warning, TEXT("%s"), *MemStats);
	
	// Test 3: Performance timing
	UE_LOG(LogTemp, Warning, TEXT("3. Performance timing test:"));
	
	const double StartTime = FPlatformTime::Seconds();
	const int32 TestIterations = 100;
	
	for (int32 i = 0; i < TestIterations; ++i)
	{
		ChunkManager->UpdateSimulation(0.016f);
	}
	
	const double EndTime = FPlatformTime::Seconds();
	const double TotalTime = (EndTime - StartTime) * 1000.0; // Convert to ms
	const double AvgTime = TotalTime / TestIterations;
	
	UE_LOG(LogTemp, Warning, TEXT("  Simulation time for %d iterations: %.2f ms"), TestIterations, TotalTime);
	UE_LOG(LogTemp, Warning, TEXT("  Average time per frame: %.3f ms"), AvgTime);
	
	// Test 4: Count sparse vs dense chunks
	TArray<UFluidChunk*> ActiveChunks = ChunkManager->GetActiveChunks();
	int32 SparseCount = 0;
	int32 DenseCount = 0;
	
	for (UFluidChunk* Chunk : ActiveChunks)
	{
		if (Chunk)
		{
			if (Chunk->bUseSparseRepresentation)
			{
				SparseCount++;
				UE_LOG(LogTemp, Log, TEXT("  Chunk %s: SPARSE (%.1f%% occupancy, %.2fx compression)"),
					*Chunk->ChunkCoord.ToString(),
					Chunk->SparseGridOccupancy * 100.0f,
					Chunk->GetCompressionRatio());
			}
			else
			{
				DenseCount++;
				UE_LOG(LogTemp, Log, TEXT("  Chunk %s: DENSE (%.1f%% occupancy)"),
					*Chunk->ChunkCoord.ToString(),
					Chunk->SparseGridOccupancy * 100.0f);
			}
		}
	}
	
	UE_LOG(LogTemp, Warning, TEXT("4. Chunk distribution:"));
	UE_LOG(LogTemp, Warning, TEXT("  Sparse chunks: %d"), SparseCount);
	UE_LOG(LogTemp, Warning, TEXT("  Dense chunks: %d"), DenseCount);
	UE_LOG(LogTemp, Warning, TEXT("  Total chunks: %d"), SparseCount + DenseCount);
	
	// Test 5: Toggle sparse grid and compare
	UE_LOG(LogTemp, Warning, TEXT("5. Toggling sparse grid for comparison..."));
	
	const bool OriginalSparseState = bUseSparseGrid;
	bUseSparseGrid = !bUseSparseGrid;
	ChunkManager->bUseSparseGrid = bUseSparseGrid;
	
	// Force all chunks to update representation
	for (UFluidChunk* Chunk : ActiveChunks)
	{
		if (Chunk)
		{
			if (bUseSparseGrid)
			{
				Chunk->ConvertToSparse();
			}
			else
			{
				Chunk->ConvertToDense();
			}
		}
	}
	
	// Measure performance again
	const double StartTime2 = FPlatformTime::Seconds();
	
	for (int32 i = 0; i < TestIterations; ++i)
	{
		ChunkManager->UpdateSimulation(0.016f);
	}
	
	const double EndTime2 = FPlatformTime::Seconds();
	const double TotalTime2 = (EndTime2 - StartTime2) * 1000.0;
	const double AvgTime2 = TotalTime2 / TestIterations;
	
	UE_LOG(LogTemp, Warning, TEXT("  Sparse Grid %s:"), bUseSparseGrid ? TEXT("ENABLED") : TEXT("DISABLED"));
	UE_LOG(LogTemp, Warning, TEXT("  Average time per frame: %.3f ms"), AvgTime2);
	UE_LOG(LogTemp, Warning, TEXT("  Performance difference: %.1fx %s"),
		FMath::Abs(AvgTime / AvgTime2),
		AvgTime < AvgTime2 ? TEXT("faster") : TEXT("slower"));
	
	// Restore original state
	bUseSparseGrid = OriginalSparseState;
	ChunkManager->bUseSparseGrid = bUseSparseGrid;
	
	UE_LOG(LogTemp, Warning, TEXT("=== TEST COMPLETE ==="));
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

// Static Water Implementation
void AVoxelFluidActor::CreateOcean(float WaterLevel, float Size)
{
	if (!StaticWaterManager)
	{
		UE_LOG(LogTemp, Error, TEXT("StaticWaterManager not initialized"));
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
			UE_LOG(LogTemp, Log, TEXT("Performing delayed retry of static water application"));
			RetryStaticWaterApplication();
		}, 1.0f, false);
	}
	
	UE_LOG(LogTemp, Log, TEXT("Created ocean with water level %f and size %f"), WaterLevel, Size);
}

void AVoxelFluidActor::CreateLake(const FVector& Center, float Radius, float WaterLevel, float Depth)
{
	if (!StaticWaterManager)
	{
		UE_LOG(LogTemp, Error, TEXT("StaticWaterManager not initialized"));
		return;
	}
	
	StaticWaterManager->CreateLake(Center, Radius, WaterLevel, Depth);
	
	// Apply to all loaded chunks
	if (bEnableStaticWater)
	{
		ApplyStaticWaterToAllChunks();
	}
	
	UE_LOG(LogTemp, Log, TEXT("Created lake at (%f, %f) with radius %f, water level %f, depth %f"), 
		Center.X, Center.Y, Radius, WaterLevel, Depth);
}

void AVoxelFluidActor::CreateRectangularLake(const FVector& Min, const FVector& Max, float WaterLevel)
{
	if (!StaticWaterManager)
	{
		UE_LOG(LogTemp, Error, TEXT("StaticWaterManager not initialized"));
		return;
	}
	
	FBox LakeBounds(Min, FVector(Max.X, Max.Y, WaterLevel));
	StaticWaterManager->CreateRectangularLake(LakeBounds, WaterLevel);
	
	// Apply to all loaded chunks
	if (bEnableStaticWater)
	{
		ApplyStaticWaterToAllChunks();
	}
	
	UE_LOG(LogTemp, Log, TEXT("Created rectangular lake with water level %f"), WaterLevel);
}

void AVoxelFluidActor::ClearStaticWater()
{
	if (!StaticWaterManager)
	{
		UE_LOG(LogTemp, Error, TEXT("StaticWaterManager not initialized"));
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
	
	UE_LOG(LogTemp, Log, TEXT("Cleared all static water regions"));
}

void AVoxelFluidActor::ApplyStaticWaterToAllChunks()
{
	if (!StaticWaterManager || !ChunkManager || !bEnableStaticWater)
	{
		return;
	}
	
	TArray<UFluidChunk*> AllChunks = ChunkManager->GetActiveChunks();
	int32 AppliedCount = 0;
	
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
		}
	}
	
	UE_LOG(LogTemp, Log, TEXT("Applied static water to %d chunks (terrain-aware)"), AppliedCount);
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
		UE_LOG(LogTemp, Warning, TEXT("Cannot retry static water application - system not ready"));
		return;
	}
	
	TArray<UFluidChunk*> AllChunks = ChunkManager->GetActiveChunks();
	int32 RetriedCount = 0;
	
	UE_LOG(LogTemp, Log, TEXT("Retrying static water application on %d chunks"), AllChunks.Num());
	
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
	
	UE_LOG(LogTemp, Log, TEXT("Retried static water application on %d chunks"), RetriedCount);
}

void AVoxelFluidActor::RefillStaticWaterInRadius(const FVector& Center, float Radius)
{
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_DynamicRefill);
	
	if (!StaticWaterManager || !ChunkManager || !bEnableStaticWater)
	{
		UE_LOG(LogTemp, Warning, TEXT("Cannot refill static water - system not ready"));
		return;
	}
	
	// Find all chunks within the radius
	FBox RefreshBounds(Center - FVector(Radius), Center + FVector(Radius));
	TArray<FFluidChunkCoord> AffectedChunks = ChunkManager->GetChunksInBounds(RefreshBounds);
	
	int32 ActivatedChunks = 0;
	int32 TotalActivatedSources = 0;
	
	UE_LOG(LogTemp, Log, TEXT("Creating dynamic fluid sources in radius %.1f around %s - checking %d chunks"), 
		Radius, *Center.ToString(), AffectedChunks.Num());
	
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
	
	UE_LOG(LogTemp, Log, TEXT("Dynamic fluid refill complete: %d chunks activated, %d fluid sources created"), 
		ActivatedChunks, TotalActivatedSources);
}

void AVoxelFluidActor::StartDynamicToStaticConversion(const FVector& Center, float Radius)
{
	// Schedule conversion after settling time
	FTimerHandle ConversionTimerHandle;
	GetWorld()->GetTimerManager().SetTimer(ConversionTimerHandle, [this, Center, Radius]()
	{
		ConvertSettledFluidToStatic(Center, Radius);
	}, DynamicToStaticSettleTime, false);
	
	UE_LOG(LogTemp, Log, TEXT("Scheduled dynamic-to-static conversion in %.1f seconds for radius %.1f around %s"), 
		DynamicToStaticSettleTime, Radius, *Center.ToString());
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
	
	UE_LOG(LogTemp, Log, TEXT("Converting settled fluid to static in radius %.1f around %s"), 
		Radius, *Center.ToString());
	
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
			UE_LOG(LogTemp, Verbose, TEXT("Converted %d cells to static in chunk %s"), 
				ConvertedInChunk, *ChunkCoord.ToString());
		}
	}
	
	UE_LOG(LogTemp, Log, TEXT("Dynamic-to-static conversion complete: %d chunks processed, %d cells converted"), 
		ConvertedChunks, TotalConvertedCells);
}

void AVoxelFluidActor::TestTerrainRefreshAtLocation(const FVector& Location, float Radius)
{
	UE_LOG_VOXELFLUID_DEBUG(this, Warning, TEXT("=== TESTING TERRAIN REFRESH ==="));
	UE_LOG_VOXELFLUID_DEBUG(this, Warning, TEXT("Location: %s, Radius: %.1f"), *Location.ToString(), Radius);

	if (!ChunkManager)
	{
		UE_LOG_VOXELFLUID_DEBUG(this, Warning, TEXT("ChunkManager is null!"));
		return;
	}

	// Find chunks in the area
	FBox TestBounds(Location - FVector(Radius), Location + FVector(Radius));
	TArray<FFluidChunkCoord> AffectedChunks = ChunkManager->GetChunksInBounds(TestBounds);
	
	UE_LOG_VOXELFLUID_DEBUG(this, Warning, TEXT("Found %d chunks in bounds"), AffectedChunks.Num());
	
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
			
			UE_LOG_VOXELFLUID_DEBUG(this, Warning, TEXT("Chunk %s: State=%d, CellsNum=%d"), 
				*ChunkCoord.ToString(), (int32)Chunk->State, Chunk->Cells.Num());
			UE_LOG_VOXELFLUID_DEBUG(this, Warning, TEXT("Chunk %s has %d fluid cells before refresh"), 
				*ChunkCoord.ToString(), FluidCellCount);
		}
	}

	// Test the terrain refresh
	UE_LOG_VOXELFLUID_DEBUG(this, Warning, TEXT("Calling RefreshTerrainInRadius..."));
	if (VoxelIntegration && VoxelIntegration->IsVoxelWorldValid())
	{
		VoxelIntegration->RefreshTerrainInRadius(Location, Radius);
	}
	else
	{
		UE_LOG_VOXELFLUID_DEBUG(this, Warning, TEXT("VoxelIntegration is null or voxel world is invalid"));
	}
	
	UE_LOG_VOXELFLUID_DEBUG(this, Warning, TEXT("=== TERRAIN REFRESH TEST COMPLETE ==="));
	
	// Now test the static water refill
	RefillStaticWaterInRadius(Location, Radius);
}