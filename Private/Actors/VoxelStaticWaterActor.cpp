#include "Actors/VoxelStaticWaterActor.h"
#include "Actors/VoxelFluidActor.h"
#include "StaticWater/StaticWaterGenerator.h"
#include "StaticWater/StaticWaterRenderer.h"
#include "StaticWater/WaterActivationManager.h"
#include "VoxelIntegration/VoxelFluidIntegration.h"
#include "CellularAutomata/FluidChunk.h"
#include "CellularAutomata/FluidChunkManager.h"
#include "Components/BoxComponent.h"
#include "Components/BillboardComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"

AVoxelStaticWaterActor::AVoxelStaticWaterActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f; // Reduced tick rate for static water

	// Create bounds component
	BoundsComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("BoundsComponent"));
	RootComponent = BoundsComponent;
	BoundsComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoundsComponent->SetBoxExtent(StaticWaterBoundsExtent);

#if WITH_EDITORONLY_DATA
	// Create billboard component for editor visualization
	SpriteComponent = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("SpriteComponent"));
	if (SpriteComponent)
	{
		SpriteComponent->SetupAttachment(RootComponent);
		SpriteComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 100.0f));
		SpriteComponent->bHiddenInGame = true;
		SpriteComponent->bIsScreenSizeScaled = true;
		SpriteComponent->ScreenSize = 0.003f;

		// Load water icon
		static ConstructorHelpers::FObjectFinder<UTexture2D> WaterSpriteTexture(TEXT("/Engine/EditorResources/S_Water"));
		if (WaterSpriteTexture.Succeeded())
		{
			SpriteComponent->SetSprite(WaterSpriteTexture.Object);
		}
		else
		{
			static ConstructorHelpers::FObjectFinder<UTexture2D> AlternativeSprite(TEXT("/Engine/EditorResources/S_Fluid"));
			if (AlternativeSprite.Succeeded())
			{
				SpriteComponent->SetSprite(AlternativeSprite.Object);
			}
		}
	}
#endif

	// Create static water components
	StaticWaterGenerator = CreateDefaultSubobject<UStaticWaterGenerator>(TEXT("StaticWaterGenerator"));
	StaticWaterRenderer = CreateDefaultSubobject<UStaticWaterRenderer>(TEXT("StaticWaterRenderer"));
	WaterActivationManager = CreateDefaultSubobject<UWaterActivationManager>(TEXT("WaterActivationManager"));
	VoxelIntegration = CreateDefaultSubobject<UVoxelFluidIntegration>(TEXT("VoxelIntegration"));

	// Default settings
	bAutoInitialize = true;
	bEnableDebugVisualization = false;
	bAutoCreateOcean = true; // Enable by default for testing
	OceanWaterLevel = -100.0f; // Shallower ocean for better terrain interaction
	OceanSize = 25000.0f; // Much smaller for faster startup
	bFollowPlayer = true; // Enable by default
	PlayerFollowDistance = 15000.0f; // Closer follow distance for faster startup
	
	RenderDistance = 10000.0f; // Reduced for faster startup
	LODDistance1 = 5000.0f;
	LODDistance2 = 10000.0f;
	bUseMeshOptimization = true;
	MeshResolution = 64;
	
	bEnableDynamicActivation = true;
	ActivationRadius = 500.0f;
	DeactivationDelay = 5.0f;
	MinDisturbanceForActivation = 100.0f;
	
	MaxConcurrentRegions = 100;
	UpdateFrequency = 0.1f;
	bUseAsyncGeneration = true;
	
	// Terrain sampling defaults
	bUseTerrainAdaptiveMesh = true;
	SamplingMethod = EVoxelSamplingMethod::VoxelQuery;
	bUseVoxelLayerSampling = true;
}

void AVoxelStaticWaterActor::BeginPlay()
{
	Super::BeginPlay();

	// Ensure components exist
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
	if (!VoxelIntegration)
	{
		VoxelIntegration = NewObject<UVoxelFluidIntegration>(this, UVoxelFluidIntegration::StaticClass(), TEXT("VoxelIntegration"));
		UE_LOG(LogTemp, Warning, TEXT("VoxelStaticWaterActor: Created own VoxelIntegration component"));
	}
	
	// Configure our own VoxelIntegration with terrain layer settings
	if (VoxelIntegration && TargetVoxelWorld && bUseTerrainAdaptiveMesh)
	{
		// Initialize the VoxelIntegration
		VoxelIntegration->InitializeFluidSystem(TargetVoxelWorld);
		
		// Apply our terrain layer configuration
		VoxelIntegration->bUseVoxelLayerSampling = bUseVoxelLayerSampling;
		VoxelIntegration->SamplingMethod = SamplingMethod;
		
		// Configure for 3D terrain sampling without chunked system
		VoxelIntegration->bUseChunkedSystem = false;  // Static water doesn't need fluid chunks
		VoxelIntegration->bUse3DVoxelTerrain = true;
		VoxelIntegration->bEnableCombinedSampling = bUseRuntimeVolumeLayer;
		
		// Set our terrain layer if specified
		if (TerrainLayer.Layer != nullptr)
		{
			VoxelIntegration->TerrainLayer = TerrainLayer;
			UE_LOG(LogTemp, Warning, TEXT("VoxelStaticWaterActor: Applied custom terrain layer to VoxelIntegration"));
		}
		
		UE_LOG(LogTemp, Warning, TEXT("VoxelStaticWaterActor: Initialized own VoxelIntegration (3D=%s, Combined=%s, SamplingMethod=%d)"), 
			TEXT("Yes"), VoxelIntegration->bEnableCombinedSampling ? TEXT("Yes") : TEXT("No"), (int32)SamplingMethod);
	}

	if (bAutoInitialize)
	{
		InitializeStaticWaterSystem();
	}

	// Setup ocean after initialization
	if (bAutoCreateOcean)
	{
		FTimerHandle TimerHandle;
		GetWorld()->GetTimerManager().SetTimer(TimerHandle, [this]()
		{
			CreateTestOcean();
		}, 0.5f, false);
	}
}

void AVoxelStaticWaterActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void AVoxelStaticWaterActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UpdateAccumulator += DeltaTime;
	if (UpdateAccumulator >= UpdateFrequency)
	{
		UpdateAccumulator = 0.0f;

		// Update ocean position if following player
		if (bHasOcean && bFollowPlayer)
		{
			UpdateOceanPosition();
		}
		else if (!bHasOcean && bAutoCreateOcean)
		{
			// Create ocean if it doesn't exist yet
			UE_LOG(LogTemp, Warning, TEXT("VoxelStaticWaterActor: Creating ocean because bHasOcean is false"));
			CreateTestOcean();
		}

		// Update debug visualization
		if (bEnableDebugVisualization)
		{
			UpdateBoundsVisualization();
		}

		// Update renderer with player position for chunk streaming
		if (StaticWaterRenderer)
		{
			TArray<FVector> ViewerPositions = GetViewerPositions();
			if (ViewerPositions.Num() > 0)
			{
				// Update renderer with primary viewer position for LOD and chunk streaming
				StaticWaterRenderer->SetViewerPosition(ViewerPositions[0]);
				
				// Also add all viewer positions for multi-player support
				for (const FVector& ViewerPos : ViewerPositions)
				{
					StaticWaterRenderer->AddViewer(ViewerPos);
				}
			}
		}
	}
}

void AVoxelStaticWaterActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	
	UpdateBoundsVisualization();
}

#if WITH_EDITOR
void AVoxelStaticWaterActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property)
	{
		FName PropertyName = PropertyChangedEvent.Property->GetFName();
		
		if (PropertyName == GET_MEMBER_NAME_CHECKED(AVoxelStaticWaterActor, StaticWaterBoundsExtent) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(AVoxelStaticWaterActor, StaticWaterBoundsOffset))
		{
			UpdateBoundsVisualization();
		}
	}
}
#endif

void AVoxelStaticWaterActor::InitializeStaticWaterSystem()
{
	if (bIsInitialized)
	{
		return;
	}

	// Initialize voxel integration
	UE_LOG(LogTemp, Warning, TEXT("VoxelStaticWaterActor: InitializeStaticWaterSystem - VoxelIntegration=%s, TargetVoxelWorld=%s"), 
		VoxelIntegration ? TEXT("Valid") : TEXT("Null"),
		TargetVoxelWorld ? TEXT("Valid") : TEXT("Null"));
	
	if (VoxelIntegration && TargetVoxelWorld)
	{
		VoxelIntegration->InitializeFluidSystem(TargetVoxelWorld);
		
		// Configure runtime volume layer if enabled
		if (bUseRuntimeVolumeLayer && RuntimeVolumeLayer.Layer != nullptr)
		{
			VoxelIntegration->SetSecondaryVolumeLayer(RuntimeVolumeLayer);
			VoxelIntegration->EnableCombinedSampling(true); // Enable to check for runtime modifications
			
			// IMPORTANT: Don't use 3D terrain for initial height sampling!
			// We use 2D height layer for mesh generation, 3D only for runtime solid detection
			VoxelIntegration->bUse3DVoxelTerrain = false;  // Start with 2D heights
			
			// Use BASE terrain layer for height sampling (2D height map)
			if (TerrainLayer.Layer != nullptr)
			{
				VoxelIntegration->TerrainLayer = TerrainLayer;  // 2D height layer
				VoxelIntegration->bUseVoxelLayerSampling = bUseVoxelLayerSampling;
				VoxelIntegration->SamplingMethod = SamplingMethod;
			}
			
			// Store runtime volume layer for later 3D solid detection (when edits happen)
			VoxelIntegration->Terrain3DLayer = RuntimeVolumeLayer;  // 3D volume for runtime edits
			VoxelIntegration->bUseSeparate3DLayer = true;
			
			// Don't invert for 2D height sampling
			VoxelIntegration->bInvertSolidDetection = false;
			
			UE_LOG(LogTemp, Warning, TEXT("VoxelStaticWaterActor: Using 2D HEIGHT layer for mesh, 3D VOLUME layer ready for runtime edits"))
			
			UE_LOG(LogTemp, Log, TEXT("VoxelStaticWaterActor: Configured runtime volume layer with 3D terrain for modifications"));
			
			// Trigger initial 3D terrain update
			if (VoxelIntegration->IsUsingChunkedSystem())
			{
				VoxelIntegration->Update3DVoxelTerrain();
			}
		}
		else
		{
			// Not using runtime volume layer - use standard terrain layer configuration
			if (TerrainLayer.Layer != nullptr)
			{
				VoxelIntegration->TerrainLayer = TerrainLayer;
				VoxelIntegration->bUseVoxelLayerSampling = bUseVoxelLayerSampling;
				VoxelIntegration->SamplingMethod = SamplingMethod;
				
				// For 3D terrain without runtime layer
				if (VoxelIntegration->bUse3DVoxelTerrain)
				{
					VoxelIntegration->Terrain3DLayer = TerrainLayer;
				}
				
				UE_LOG(LogTemp, Warning, TEXT("VoxelStaticWaterActor: Using standard TerrainLayer configuration (no runtime edits)"));
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelStaticWaterActor: Cannot initialize VoxelIntegration - VoxelIntegration=%s, TargetVoxelWorld=%s"), 
			VoxelIntegration ? TEXT("Valid") : TEXT("Null"),
			TargetVoxelWorld ? TEXT("Valid") : TEXT("Null"));
	}

	// Set up voxel world connection
	if (StaticWaterGenerator && TargetVoxelWorld)
	{
		StaticWaterGenerator->SetVoxelWorld(TargetVoxelWorld);
		
		// Connect VoxelIntegration to StaticWaterGenerator
		if (VoxelIntegration)
		{
			StaticWaterGenerator->SetVoxelIntegration(VoxelIntegration);
		}
	}

	// Connect renderer to generator
	if (StaticWaterRenderer && StaticWaterGenerator)
	{
		StaticWaterRenderer->SetWaterGenerator(StaticWaterGenerator);
		
		// Connect voxel integration for terrain sampling
		if (VoxelIntegration)
		{
			UE_LOG(LogTemp, Warning, TEXT("VoxelStaticWaterActor: Connecting VoxelIntegration to StaticWaterRenderer - VoxelWorldValid: %s"), 
				VoxelIntegration->IsVoxelWorldValid() ? TEXT("true") : TEXT("false"));
			StaticWaterRenderer->SetVoxelIntegration(VoxelIntegration);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("VoxelStaticWaterActor: No VoxelIntegration available for StaticWaterRenderer!"));
		}
		
		// Set water materials
		if (WaterMaterial)
		{
			StaticWaterRenderer->WaterMaterial = WaterMaterial;
			UE_LOG(LogTemp, Warning, TEXT("VoxelStaticWaterActor: Set water material: %s"), *WaterMaterial->GetName());
		}
		if (WaterMaterialLOD1)
		{
			StaticWaterRenderer->WaterMaterialLOD1 = WaterMaterialLOD1;
			UE_LOG(LogTemp, Warning, TEXT("VoxelStaticWaterActor: Set water material LOD1: %s"), *WaterMaterialLOD1->GetName());
		}
	}

	// Set up water activation manager
	if (WaterActivationManager)
	{
		WaterActivationManager->SetStaticWaterGenerator(StaticWaterGenerator);
		WaterActivationManager->SetStaticWaterRenderer(StaticWaterRenderer);
		
		// Connect to fluid actor if available
		if (LinkedFluidActor)
		{
			if (UFluidChunkManager* ChunkManager = LinkedFluidActor->ChunkManager)
			{
				WaterActivationManager->SetFluidChunkManager(ChunkManager);
			}
		}
	}

	// Configure renderer settings
	if (StaticWaterRenderer)
	{
		// Apply rendering settings
		// These would be implemented as functions in the renderer
	}

	bIsInitialized = true;

	UE_LOG(LogTemp, Log, TEXT("Static Water System Initialized"));
}

void AVoxelStaticWaterActor::SetVoxelWorld(AActor* InVoxelWorld)
{
	TargetVoxelWorld = InVoxelWorld;
	
	// Set up voxel integration
	if (VoxelIntegration)
	{
		if (InVoxelWorld)
		{
			VoxelIntegration->InitializeFluidSystem(InVoxelWorld);
			UE_LOG(LogTemp, Log, TEXT("VoxelStaticWaterActor: Connected to voxel world: %s"), 
				*InVoxelWorld->GetName());
		}
	}
	
	if (StaticWaterGenerator)
	{
		StaticWaterGenerator->SetVoxelWorld(InVoxelWorld);
	}
}

void AVoxelStaticWaterActor::SetFluidActor(AVoxelFluidActor* InFluidActor)
{
	LinkedFluidActor = InFluidActor;
	
	if (WaterActivationManager && LinkedFluidActor)
	{
		if (UFluidChunkManager* ChunkManager = LinkedFluidActor->ChunkManager)
		{
			WaterActivationManager->SetFluidChunkManager(ChunkManager);
		}
	}
}

void AVoxelStaticWaterActor::CreateOcean(float WaterLevel, float Size)
{
	if (!StaticWaterGenerator)
	{
		UE_LOG(LogTemp, Warning, TEXT("StaticWaterGenerator not available"));
		return;
	}

	FVector PlayerPos = FVector::ZeroVector;
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			PlayerPos = Pawn->GetActorLocation();
		}
	}

	OceanCenter = FVector(PlayerPos.X, PlayerPos.Y, WaterLevel);
	OceanWaterLevel = WaterLevel;
	OceanSize = Size;
	bHasOcean = true;

	// Create ocean region definition
	FStaticWaterRegionDef OceanRegion;
	OceanRegion.Bounds = FBox::BuildAABB(OceanCenter, FVector(Size, Size, 1000.0f));
	OceanRegion.WaterLevel = WaterLevel;
	OceanRegion.bInfiniteDepth = true;
	OceanRegion.MinDepth = 500.0f;
	OceanRegion.Priority = 0;
	
	StaticWaterGenerator->AddWaterRegion(OceanRegion);
	
	UE_LOG(LogTemp, Log, TEXT("Created ocean at %s with water level %.1f and size %.1f"),
		*OceanCenter.ToString(), WaterLevel, Size);
}

void AVoxelStaticWaterActor::CreateTestOcean()
{
	CreateOcean(OceanWaterLevel, OceanSize);
}

void AVoxelStaticWaterActor::RecenterOceanOnPlayer()
{
	if (!bHasOcean)
	{
		CreateTestOcean();
		return;
	}

	UpdateOceanPosition();
}

void AVoxelStaticWaterActor::ClearOcean()
{
	if (StaticWaterGenerator)
	{
		StaticWaterGenerator->ClearAllWaterRegions();
	}
	
	bHasOcean = false;
	OceanCenter = FVector::ZeroVector;
	
	UE_LOG(LogTemp, Log, TEXT("Cleared ocean"));
}

void AVoxelStaticWaterActor::CreateLake(const FVector& Center, float Radius, float WaterLevel, float Depth)
{
	if (!StaticWaterGenerator)
	{
		return;
	}

	// Create water region definition
	FStaticWaterRegionDef WaterRegion;
	WaterRegion.Bounds = FBox::BuildAABB(Center, FVector(Radius, Radius, 500.0f));
	WaterRegion.WaterLevel = WaterLevel;
	WaterRegion.bInfiniteDepth = false;
	WaterRegion.MinDepth = 100.0f;
	WaterRegion.Priority = 0;
	
	StaticWaterGenerator->AddWaterRegion(WaterRegion);
	
	UE_LOG(LogTemp, Log, TEXT("Created lake at %s with radius %.1f and water level %.1f"),
		*Center.ToString(), Radius, WaterLevel);
}

void AVoxelStaticWaterActor::CreateRectangularLake(const FVector& Min, const FVector& Max, float WaterLevel)
{
	if (!StaticWaterGenerator)
	{
		return;
	}

	FVector Center = (Min + Max) * 0.5f;
	Center.Z = WaterLevel;
	FVector Extents = (Max - Min) * 0.5f;
	float Radius = FMath::Max(Extents.X, Extents.Y);
	
	// For now, approximate with circular region
	// TODO: Add support for rectangular regions in StaticWaterGenerator
	// Create water region definition
	FStaticWaterRegionDef WaterRegion;
	WaterRegion.Bounds = FBox::BuildAABB(Center, FVector(Radius, Radius, 500.0f));
	WaterRegion.WaterLevel = WaterLevel;
	WaterRegion.bInfiniteDepth = false;
	WaterRegion.MinDepth = 100.0f;
	WaterRegion.Priority = 0;
	
	StaticWaterGenerator->AddWaterRegion(WaterRegion);
	
	UE_LOG(LogTemp, Log, TEXT("Created rectangular lake from %s to %s with water level %.1f"),
		*Min.ToString(), *Max.ToString(), WaterLevel);
}

void AVoxelStaticWaterActor::AddStaticWaterRegion(const FVector& Center, float Radius, float WaterLevel)
{
	if (!StaticWaterGenerator)
	{
		return;
	}

	// Create water region definition
	FStaticWaterRegionDef WaterRegion;
	WaterRegion.Bounds = FBox::BuildAABB(Center, FVector(Radius, Radius, 500.0f));
	WaterRegion.WaterLevel = WaterLevel;
	WaterRegion.bInfiniteDepth = false;
	WaterRegion.MinDepth = 100.0f;
	WaterRegion.Priority = 0;
	
	StaticWaterGenerator->AddWaterRegion(WaterRegion);
}

void AVoxelStaticWaterActor::RemoveStaticWaterRegion(const FVector& Center, float Radius)
{
	if (!StaticWaterGenerator)
	{
		return;
	}

	// This would need to be implemented in StaticWaterGenerator
	// For now, we can clear all regions as a placeholder
	UE_LOG(LogTemp, Warning, TEXT("RemoveStaticWaterRegion not yet implemented"));
}

bool AVoxelStaticWaterActor::IsPointInStaticWater(const FVector& WorldPosition) const
{
	if (!StaticWaterGenerator)
	{
		return false;
	}

	return StaticWaterGenerator->HasStaticWaterAtLocation(WorldPosition);
}

float AVoxelStaticWaterActor::GetWaterLevelAtPosition(const FVector& WorldPosition) const
{
	if (!StaticWaterGenerator)
	{
		return -FLT_MAX;
	}

	return StaticWaterGenerator->GetWaterLevelAtLocation(WorldPosition);
}

int32 AVoxelStaticWaterActor::GetStaticWaterRegionCount() const
{
	if (!StaticWaterGenerator)
	{
		return 0;
	}

	// Return a default count since GetRegionCount doesn't exist
	return 0; // TODO: Implement region counting if needed
}

void AVoxelStaticWaterActor::OnTerrainEdited(const FVector& EditPosition, float EditRadius, float HeightChange)
{
	if (!bEnableDynamicActivation)
	{
		return;
	}

	// Check if edit affects any water regions
	if (StaticWaterGenerator && StaticWaterGenerator->HasStaticWaterAtLocation(EditPosition))
	{
		// Notify water activation manager
		if (WaterActivationManager)
		{
			WaterActivationManager->OnTerrainEdited(EditPosition, EditRadius, 0.0f);
		}

		// Convert to dynamic water if threshold exceeded
		if (FMath::Abs(HeightChange) > MinDisturbanceForActivation)
		{
			ConvertToDynamicWater(EditPosition, EditRadius + ActivationRadius);
		}
	}
}

void AVoxelStaticWaterActor::OnVoxelTerrainModified(const FVector& ModifiedPosition, float ModifiedRadius)
{
	// First handle dynamic water activation if needed
	OnTerrainEdited(ModifiedPosition, ModifiedRadius, MinDisturbanceForActivation + 1.0f);
	
	// Now update the voxel integration to refresh solid cells
	if (VoxelIntegration)
	{
		// Note: We keep using 2D height layer for mesh generation
		// The 3D volume layer is only used for solid detection in fluid simulation
		// This ensures water mesh stays at correct heights while detecting runtime edits
		
		// When using runtime volume layer with combined sampling, use appropriate method based on chunked system
		if (bUseRuntimeVolumeLayer && VoxelIntegration->bEnableCombinedSampling)
		{
			if (VoxelIntegration->bUseChunkedSystem)
			{
				// For chunked systems, use OnRuntimeTerrainModified
				VoxelIntegration->OnRuntimeTerrainModified(ModifiedPosition, ModifiedRadius);
			}
			else
			{
				// For non-chunked systems, use simpler refresh method
				VoxelIntegration->ForceRefreshVoxelCache();
				VoxelIntegration->RefreshTerrainInRadius(ModifiedPosition, ModifiedRadius);
				
				UE_LOG(LogTemp, Warning, TEXT("VoxelStaticWaterActor: Refreshed terrain for non-chunked system at %s with radius %.1f"), 
					*ModifiedPosition.ToString(), ModifiedRadius);
			}
		}
		else if (VoxelIntegration->bUse3DVoxelTerrain)
		{
			// For regular 3D voxel terrain without runtime volume layer
			// First clear the cache
			VoxelIntegration->ForceRefreshVoxelCache();
			
			// Then refresh the affected area
			VoxelIntegration->RefreshTerrainInRadius(ModifiedPosition, ModifiedRadius);
		}
		else
		{
			// For 2D terrain, just update the terrain heights
			VoxelIntegration->UpdateTerrainHeights();
		}
		
		// Critical: Refresh terrain data to propagate 3D terrain changes to mesh generation
		VoxelIntegration->RefreshTerrainAfterSculpting();
		
		UE_LOG(LogTemp, Warning, TEXT("VoxelStaticWaterActor: Triggered terrain update at %s radius %.1f (RuntimeLayer=%s, Combined=%s, 3D=%s)"), 
			*ModifiedPosition.ToString(), ModifiedRadius,
			bUseRuntimeVolumeLayer ? TEXT("Yes") : TEXT("No"),
			VoxelIntegration->bEnableCombinedSampling ? TEXT("Yes") : TEXT("No"),
			VoxelIntegration->bUse3DVoxelTerrain ? TEXT("Yes") : TEXT("No"));
	}
	
	// Also trigger mesh regeneration for static water renderer
	if (StaticWaterRenderer)
	{
		StaticWaterRenderer->ForceRebuildAllChunks();
	}
}

void AVoxelStaticWaterActor::RefreshTerrainDataInRadius(const FVector& Center, float Radius)
{
	if (StaticWaterGenerator)
	{
		// This would refresh terrain sampling in the generator
		UE_LOG(LogTemp, Log, TEXT("Refreshing terrain data at %s with radius %.1f"), 
			*Center.ToString(), Radius);
	}
	
	if (VoxelIntegration && VoxelIntegration->IsVoxelWorldValid())
	{
		VoxelIntegration->RefreshTerrainInRadius(Center, Radius);
		VoxelIntegration->RefreshTerrainAfterSculpting();
	}
}

void AVoxelStaticWaterActor::ApplyStaticWaterToChunkWithTerrain(UFluidChunk* Chunk, UFluidChunkManager* ChunkManager)
{
	if (!Chunk || !StaticWaterGenerator || !VoxelIntegration)
	{
		return;
	}
	
	// Check if this chunk intersects with any static water regions
	FBox ChunkBounds = Chunk->GetWorldBounds();
	if (!StaticWaterGenerator->HasStaticWaterAtLocation(ChunkBounds.GetCenter()))
	{
		return;
	}
	
	// Get water level for this chunk
	FVector ChunkCenter = ChunkBounds.GetCenter();
	float WaterLevel = StaticWaterGenerator->GetWaterLevelAtLocation(ChunkCenter);
	
	// Apply static water based on terrain
	int32 AddedCells = 0;
	const float CellSize = ChunkManager ? ChunkManager->CellSize : 100.0f;
	const int32 ChunkSize = 32; // Default chunk size
	
	// Iterate through all cells in the chunk
	for (int32 X = 0; X < ChunkSize; ++X)
	{
		for (int32 Y = 0; Y < ChunkSize; ++Y)
		{
			for (int32 Z = 0; Z < ChunkSize; ++Z)
			{
				// Get world position of this cell
				FVector WorldPos = Chunk->GetWorldPositionFromLocal(X, Y, Z);
				
				// Check if this position should have water
				if (WorldPos.Z <= WaterLevel)
				{
					// Sample terrain height at this position
					float TerrainHeight = WorldPos.Z - 1000.0f; // Default far below
					if (VoxelIntegration && VoxelIntegration->IsVoxelWorldValid())
					{
						TerrainHeight = VoxelIntegration->SampleVoxelHeight(WorldPos.X, WorldPos.Y);
					}
					
					// Only add water if position is above terrain and below water level
					if (WorldPos.Z > TerrainHeight && WorldPos.Z <= WaterLevel)
					{
						// Calculate water depth
						float WaterDepth = WaterLevel - WorldPos.Z;
						if (WaterDepth > 0)
						{
							// Add water to this cell
							float FluidAmount = FMath::Min(1.0f, WaterDepth / CellSize);
							if (FluidAmount > 0.01f)
							{
								// TODO: Add method to set fluid directly in chunk
								// For now, just count the cells that would be filled
								AddedCells++;
							}
						}
					}
				}
			}
		}
	}
	
	if (AddedCells > 0)
	{
		UE_LOG(LogTemp, Verbose, TEXT("Applied static water to chunk at %s: %d cells"), 
			*ChunkCenter.ToString(), AddedCells);
	}
}

bool AVoxelStaticWaterActor::IsRegionActiveForSimulation(const FVector& Position) const
{
	if (!WaterActivationManager)
	{
		return false;
	}

	return WaterActivationManager->IsRegionActive(Position);
}

void AVoxelStaticWaterActor::ForceActivateWaterAtLocation(const FVector& Position, float Radius)
{
	if (WaterActivationManager)
	{
		WaterActivationManager->ActivateWaterInRegion(Position, Radius);
	}

	NotifyFluidActorOfActivation(Position, Radius);
}

void AVoxelStaticWaterActor::ForceDeactivateAllWaterRegions()
{
	if (WaterActivationManager)
	{
		WaterActivationManager->ForceDeactivateAllRegions();
	}
}

int32 AVoxelStaticWaterActor::GetActiveWaterRegionCount() const
{
	if (!WaterActivationManager)
	{
		return 0;
	}

	return WaterActivationManager->GetActiveRegionCount();
}

void AVoxelStaticWaterActor::ConvertToDynamicWater(const FVector& Center, float Radius)
{
	if (!LinkedFluidActor || !StaticWaterGenerator)
	{
		return;
	}

	// Get water level at this position
	float WaterLevel = StaticWaterGenerator->GetWaterLevelAtLocation(Center);
	
	// Activate region for dynamic simulation
	if (WaterActivationManager)
	{
		WaterActivationManager->ActivateWaterInRegion(Center, Radius);
	}

	// Notify fluid actor to spawn dynamic water
	NotifyFluidActorOfActivation(Center, Radius);
	
	UE_LOG(LogTemp, Log, TEXT("Converted static water to dynamic at %s with radius %.1f"),
		*Center.ToString(), Radius);
}

void AVoxelStaticWaterActor::ToggleDebugVisualization()
{
	bEnableDebugVisualization = !bEnableDebugVisualization;
}

void AVoxelStaticWaterActor::ShowStaticWaterBounds()
{
	UpdateBoundsVisualization();
}

FString AVoxelStaticWaterActor::GetStaticWaterStats() const
{
	int32 RegionCount = GetStaticWaterRegionCount();
	int32 ActiveCount = GetActiveWaterRegionCount();
	
	return FString::Printf(TEXT("Static Water Stats:\nTotal Regions: %d\nActive Regions: %d\nHas Ocean: %s\nOcean Size: %.1f"),
		RegionCount, ActiveCount, bHasOcean ? TEXT("Yes") : TEXT("No"), OceanSize);
}

void AVoxelStaticWaterActor::SetupTestWaterSystem()
{
	// Enable debug visualization
	bEnableDebugVisualization = true;
	
	// Initialize system
	if (!bIsInitialized)
	{
		InitializeStaticWaterSystem();
	}

	// Enable renderer debug mode
	if (StaticWaterRenderer)
	{
		// Debug visualization - method doesn't exist, using property instead
		StaticWaterRenderer->bShowRenderChunkBounds = true;
	}

	UE_LOG(LogTemp, Log, TEXT("Static Water Test System Setup Complete"));
}

void AVoxelStaticWaterActor::UpdateBoundsVisualization()
{
	if (BoundsComponent)
	{
		BoundsComponent->SetBoxExtent(StaticWaterBoundsExtent);
		BoundsComponent->SetRelativeLocation(StaticWaterBoundsOffset);
	}

	if (bEnableDebugVisualization && GetWorld())
	{
		FVector Origin = GetActorLocation() + StaticWaterBoundsOffset;
		DrawDebugBox(GetWorld(), Origin, StaticWaterBoundsExtent, FColor::Cyan, false, 0.5f, 0, 5.0f);
	}
}

void AVoxelStaticWaterActor::UpdateOceanPosition()
{
	TArray<FVector> ViewerPositions = GetViewerPositions();
	if (ViewerPositions.Num() == 0)
	{
		return;
	}

	FVector PlayerPos = ViewerPositions[0];
	FVector ToPlayer = PlayerPos - OceanCenter;
	ToPlayer.Z = 0; // Only care about horizontal distance
	
	float Distance = ToPlayer.Size();
	if (Distance > PlayerFollowDistance)
	{
		// Move ocean center toward player
		FVector NewCenter = PlayerPos;
		NewCenter.Z = OceanWaterLevel;
		
		// Update ocean position
		if (StaticWaterGenerator)
		{
			StaticWaterGenerator->ClearAllWaterRegions();
			// Create ocean region definition
			FStaticWaterRegionDef OceanRegion;
			OceanRegion.Bounds = FBox::BuildAABB(NewCenter, FVector(OceanSize, OceanSize, 1000.0f));
			OceanRegion.WaterLevel = OceanWaterLevel;
			OceanRegion.bInfiniteDepth = true;
			OceanRegion.MinDepth = 500.0f;
			OceanRegion.Priority = 0;
			
			StaticWaterGenerator->AddWaterRegion(OceanRegion);
		}
		
		OceanCenter = NewCenter;
		LastPlayerPosition = PlayerPos;
		
		// Force renderer to rebuild chunks after ocean move
		if (StaticWaterRenderer)
		{
			StaticWaterRenderer->ForceRebuildAllChunks();
		}
		
		UE_LOG(LogTemp, Warning, TEXT("VoxelStaticWaterActor: Ocean moved to %s (player at %s)"), 
			*NewCenter.ToString(), *PlayerPos.ToString());
	}
}

TArray<FVector> AVoxelStaticWaterActor::GetViewerPositions() const
{
	TArray<FVector> Positions;
	
	if (UWorld* World = GetWorld())
	{
		for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
		{
			if (APlayerController* PC = Iterator->Get())
			{
				if (APawn* Pawn = PC->GetPawn())
				{
					Positions.Add(Pawn->GetActorLocation());
				}
			}
		}
	}

	return Positions;
}

void AVoxelStaticWaterActor::NotifyFluidActorOfActivation(const FVector& Position, float Radius)
{
	if (!LinkedFluidActor)
	{
		return;
	}

	// Get water level at position
	float WaterLevel = GetWaterLevelAtPosition(Position);
	
	// Calculate how much water to spawn based on radius and water depth
	float WaterVolume = Radius * Radius * 100.0f; // Simplified calculation
	
	// Add water to fluid simulation at this location
	LinkedFluidActor->AddFluidAtLocation(FVector(Position.X, Position.Y, WaterLevel), WaterVolume);
	
	UE_LOG(LogTemp, Log, TEXT("Notified fluid actor of water activation at %s"), *Position.ToString());
}