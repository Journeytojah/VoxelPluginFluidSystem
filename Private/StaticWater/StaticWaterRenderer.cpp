#include "StaticWater/StaticWaterRenderer.h"
#include "StaticWater/StaticWaterGenerator.h"
#include "VoxelIntegration/VoxelFluidIntegration.h"
#include "Actors/VoxelFluidActor.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "DrawDebugHelpers.h"
#include "Materials/MaterialInterface.h"

bool FStaticWaterRenderChunk::IsValid() const 
{ 
	return MeshComponent != nullptr && MeshComponent->IsValidLowLevel(); 
}

UStaticWaterRenderer::UStaticWaterRenderer()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	SetComponentTickInterval(0.05f); // 20 FPS by default
}

void UStaticWaterRenderer::BeginPlay()
{
	Super::BeginPlay();
	
	// Find water generator on the same actor
	if (AActor* Owner = GetOwner())
	{
		WaterGenerator = Owner->FindComponentByClass<UStaticWaterGenerator>();
		if (!WaterGenerator)
		{
			UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: No StaticWaterGenerator found on actor %s"), *Owner->GetName());
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("StaticWaterRenderer: Found StaticWaterGenerator on actor %s"), *Owner->GetName());
		}
	}

	bIsInitialized = true;
	
	UE_LOG(LogTemp, Log, TEXT("StaticWaterRenderer: BeginPlay - bRenderingEnabled: %s, bIsInitialized: %s"), 
		bRenderingEnabled ? TEXT("true") : TEXT("false"),
		bIsInitialized ? TEXT("true") : TEXT("false"));
	
	if (bRenderingEnabled)
	{
		// Get initial viewer position from player
		if (UWorld* World = GetWorld())
		{
			if (APawn* PlayerPawn = World->GetFirstPlayerController()->GetPawn())
			{
				FVector PlayerPos = PlayerPawn->GetActorLocation();
				SetViewerPosition(PlayerPos);
				UE_LOG(LogTemp, Log, TEXT("StaticWaterRenderer: Set initial viewer position to %s"), *PlayerPos.ToString());
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: No player pawn found"));
			}
		}
	}
}

void UStaticWaterRenderer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clean up all mesh components
	FScopeLock Lock(&RenderChunkMutex);
	
	for (auto& ChunkPair : LoadedRenderChunks)
	{
		FStaticWaterRenderChunk& Chunk = ChunkPair.Value;
		if (Chunk.MeshComponent && Chunk.MeshComponent->IsValidLowLevel())
		{
			DestroyMeshComponent(Chunk.MeshComponent);
		}
	}
	
	for (UProceduralMeshComponent* MeshComp : AvailableMeshComponents)
	{
		if (MeshComp && MeshComp->IsValidLowLevel())
		{
			MeshComp->DestroyComponent();
		}
	}
	
	LoadedRenderChunks.Empty();
	AvailableMeshComponents.Empty();
	UsedMeshComponents.Empty();
	
	Super::EndPlay(EndPlayReason);
}

void UStaticWaterRenderer::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	if (!bIsInitialized || !bRenderingEnabled)
	{
		if (bEnableLogging)
		{
			UE_LOG(LogTemp, VeryVerbose, TEXT("StaticWaterRenderer: Tick skipped - bIsInitialized: %s, bRenderingEnabled: %s"), 
				bIsInitialized ? TEXT("true") : TEXT("false"),
				bRenderingEnabled ? TEXT("true") : TEXT("false"));
		}
		return;
	}

	// Update viewer position from player if auto-tracking is enabled
	if (bAutoTrackPlayer)
	{
		ViewerPositions.Empty();
		if (UWorld* World = GetWorld())
		{
			if (APawn* PlayerPawn = World->GetFirstPlayerController()->GetPawn())
			{
				FVector NewPlayerPos = PlayerPawn->GetActorLocation();
				ViewerPositions.Add(NewPlayerPos);
				
				if (bEnableLogging)
				{
					UE_LOG(LogTemp, VeryVerbose, TEXT("StaticWaterRenderer: Auto-tracked viewer position to %s"), 
						*NewPlayerPos.ToString());
				}
			}
		}
	}

	if (bEnableLogging)
	{
		UE_LOG(LogTemp, VeryVerbose, TEXT("StaticWaterRenderer: Tick - %d viewers, %d active chunks"), 
			ViewerPositions.Num(), GetActiveRenderChunkCount());
	}

	UpdateRenderChunks(DeltaTime);

#if WITH_EDITOR
	if (bShowRenderChunkBounds || bShowLODColors)
	{
		DrawDebugInfo();
	}
#endif
}

void UStaticWaterRenderer::SetWaterGenerator(UStaticWaterGenerator* InGenerator)
{
	WaterGenerator = InGenerator;
	
	UE_LOG(LogTemp, Log, TEXT("StaticWaterRenderer: SetWaterGenerator called with %s"), 
		InGenerator ? *InGenerator->GetName() : TEXT("nullptr"));
	
	// Force rebuild all chunks when generator changes
	ForceRebuildAllChunks();
}

void UStaticWaterRenderer::SetWaterMaterial(UMaterialInterface* InMaterial)
{
	WaterMaterial = InMaterial;
	
	// Update all existing mesh components with new material
	FScopeLock Lock(&RenderChunkMutex);
	for (auto& ChunkPair : LoadedRenderChunks)
	{
		FStaticWaterRenderChunk& Chunk = ChunkPair.Value;
		if (Chunk.MeshComponent && Chunk.MeshComponent->IsValidLowLevel())
		{
			UpdateComponentMaterial(Chunk.MeshComponent, Chunk.LODLevel);
		}
	}
}

void UStaticWaterRenderer::SetVoxelIntegration(UVoxelFluidIntegration* InVoxelIntegration)
{
	VoxelIntegration = InVoxelIntegration;
	UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: VoxelIntegration set to %s"), 
		VoxelIntegration ? TEXT("Valid") : TEXT("Null"));
	
	// Force rebuild all chunks when voxel integration changes
	ForceRebuildAllChunks();
}

void UStaticWaterRenderer::SetViewerPosition(const FVector& Position)
{
	// Disable auto-tracking when manually setting viewer position
	bAutoTrackPlayer = false;
	
	ViewerPositions.Empty();
	ViewerPositions.Add(Position);
	
	UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: SetViewerPosition called with %s (auto-tracking disabled)"), *Position.ToString());
	UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: ViewerPositions now has %d entries"), ViewerPositions.Num());
}

void UStaticWaterRenderer::AddViewer(const FVector& Position)
{
	// Disable auto-tracking when manually adding viewers
	bAutoTrackPlayer = false;
	
	ViewerPositions.Add(Position);
	UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: AddViewer called with %s (auto-tracking disabled)"), *Position.ToString());
}

void UStaticWaterRenderer::RemoveViewer(int32 ViewerIndex)
{
	if (ViewerPositions.IsValidIndex(ViewerIndex))
	{
		ViewerPositions.RemoveAt(ViewerIndex);
	}
}

void UStaticWaterRenderer::ClearViewers()
{
	ViewerPositions.Empty();
}

void UStaticWaterRenderer::SetRenderingEnabled(bool bEnabled)
{
	if (bRenderingEnabled == bEnabled)
		return;
		
	bRenderingEnabled = bEnabled;
	
	// Show/hide all mesh components
	FScopeLock Lock(&RenderChunkMutex);
	for (auto& ChunkPair : LoadedRenderChunks)
	{
		FStaticWaterRenderChunk& Chunk = ChunkPair.Value;
		if (Chunk.MeshComponent && Chunk.MeshComponent->IsValidLowLevel())
		{
			Chunk.MeshComponent->SetVisibility(bEnabled);
		}
	}
}

void UStaticWaterRenderer::ForceRebuildAllChunks()
{
	FScopeLock Lock(&RenderChunkMutex);
	for (auto& ChunkPair : LoadedRenderChunks)
	{
		ChunkPair.Value.bNeedsRebuild = true;
	}
}

void UStaticWaterRenderer::RebuildChunksInRadius(const FVector& Center, float Radius)
{
	const float RadiusSquared = Radius * Radius;
	
	FScopeLock Lock(&RenderChunkMutex);
	for (auto& ChunkPair : LoadedRenderChunks)
	{
		FStaticWaterRenderChunk& Chunk = ChunkPair.Value;
		const FVector ChunkCenter = Chunk.WorldBounds.GetCenter();
		
		if (FVector::DistSquared(Center, ChunkCenter) <= RadiusSquared)
		{
			Chunk.bNeedsRebuild = true;
		}
	}
}

void UStaticWaterRenderer::RegenerateAroundViewer()
{
	UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: RegenerateAroundViewer called - updating active chunks"));
	
	// Just update which chunks should be active, don't force immediate update
	UpdateActiveRenderChunks();
	
	UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: RegenerateAroundViewer completed - %d active chunks"), GetActiveRenderChunkCount());
}

void UStaticWaterRenderer::ResetRenderer()
{
	UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: Resetting renderer - clearing all chunks"));
	
	// Keep logging disabled to reduce spam
	bEnableLogging = false;
	
	// Clear all chunks and viewer positions
	{
		FScopeLock Lock(&RenderChunkMutex);
		
		// Destroy all mesh components
		for (auto& ChunkPair : LoadedRenderChunks)
		{
			if (ChunkPair.Value.MeshComponent && ChunkPair.Value.MeshComponent->IsValidLowLevel())
			{
				DestroyMeshComponent(ChunkPair.Value.MeshComponent);
			}
		}
		
		LoadedRenderChunks.Empty();
		ActiveRenderChunkCoords.Empty();
		
		// Clear queues
		while (!ChunkLoadQueue.IsEmpty())
		{
			FIntVector Dummy;
			ChunkLoadQueue.Dequeue(Dummy);
		}
		while (!ChunkUnloadQueue.IsEmpty())
		{
			FIntVector Dummy;
			ChunkUnloadQueue.Dequeue(Dummy);
		}
	}
	
	// Clear viewer positions and re-enable auto-tracking
	ViewerPositions.Empty();
	bAutoTrackPlayer = true;
	
	UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: Reset complete - ready for new viewer position, auto-tracking re-enabled"));
}

void UStaticWaterRenderer::EnableAutoTracking(bool bEnable)
{
	bAutoTrackPlayer = bEnable;
	UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: Auto-tracking %s"), bEnable ? TEXT("ENABLED") : TEXT("DISABLED"));
}

int32 UStaticWaterRenderer::GetActiveRenderChunkCount() const
{
	FScopeLock Lock(&RenderChunkMutex);
	return ActiveRenderChunkCoords.Num();
}

int32 UStaticWaterRenderer::GetVisibleRenderChunkCount() const
{
	FScopeLock Lock(&RenderChunkMutex);
	int32 VisibleCount = 0;
	
	for (const auto& ChunkPair : LoadedRenderChunks)
	{
		const FStaticWaterRenderChunk& Chunk = ChunkPair.Value;
		if (Chunk.MeshComponent && Chunk.MeshComponent->IsValidLowLevel() && Chunk.MeshComponent->IsVisible())
		{
			++VisibleCount;
		}
	}
	
	return VisibleCount;
}

TArray<FIntVector> UStaticWaterRenderer::GetActiveRenderChunkCoords() const
{
	FScopeLock Lock(&RenderChunkMutex);
	return ActiveRenderChunkCoords.Array();
}

void UStaticWaterRenderer::UpdateRenderChunks(float DeltaTime)
{
	RenderUpdateTimer += DeltaTime;
	
	if (RenderUpdateTimer >= RenderSettings.UpdateFrequency)
	{
		RenderUpdateTimer = 0.0f;
		UpdateActiveRenderChunks();
	}
	
	// Update LODs less frequently
	LODUpdateTimer += DeltaTime;
	if (LODUpdateTimer >= RenderSettings.UpdateFrequency * 2.0f)
	{
		LODUpdateTimer = 0.0f;
		UpdateChunkLODs();
	}
	
	// Update visibility even less frequently
	VisibilityUpdateTimer += DeltaTime;
	if (VisibilityUpdateTimer >= RenderSettings.UpdateFrequency * 3.0f)
	{
		VisibilityUpdateTimer = 0.0f;
		UpdateChunkVisibility();
	}
	
	// Process chunk load/unload queues
	ChunksUpdatedThisFrame = 0;
	ChunksBuiltThisFrame = 0;
	
	// Load new chunks
	while (!ChunkLoadQueue.IsEmpty() && ChunksUpdatedThisFrame < RenderSettings.MaxChunksToUpdatePerFrame)
	{
		FIntVector ChunkCoord;
		if (ChunkLoadQueue.Dequeue(ChunkCoord))
		{
			LoadRenderChunk(ChunkCoord);
			++ChunksUpdatedThisFrame;
		}
	}
	
	// Unload distant chunks
	while (!ChunkUnloadQueue.IsEmpty())
	{
		FIntVector ChunkCoord;
		if (ChunkUnloadQueue.Dequeue(ChunkCoord))
		{
			UnloadRenderChunk(ChunkCoord);
		}
	}
	
	// Build meshes for chunks that need it
	FScopeLock Lock(&RenderChunkMutex);
	for (auto& ChunkPair : LoadedRenderChunks)
	{
		FStaticWaterRenderChunk& Chunk = ChunkPair.Value;
		if (Chunk.bNeedsRebuild && ChunksBuiltThisFrame < RenderSettings.MaxChunksToUpdatePerFrame)
		{
			BuildChunkMesh(Chunk);
			Chunk.bNeedsRebuild = false;
			++ChunksBuiltThisFrame;
		}
	}
}

void UStaticWaterRenderer::UpdateChunkLODs()
{
	FScopeLock Lock(&RenderChunkMutex);
	
	for (auto& ChunkPair : LoadedRenderChunks)
	{
		FStaticWaterRenderChunk& Chunk = ChunkPair.Value;
		const float Distance = GetDistanceToChunk(ChunkPair.Key);
		const int32 NewLOD = CalculateLODLevel(Distance);
		
		if (NewLOD != Chunk.LODLevel)
		{
			Chunk.LODLevel = NewLOD;
			Chunk.bNeedsRebuild = true;
			
			// Update material immediately
			if (Chunk.MeshComponent && Chunk.MeshComponent->IsValidLowLevel())
			{
				UpdateComponentMaterial(Chunk.MeshComponent, NewLOD);
			}
		}
	}
}

void UStaticWaterRenderer::UpdateChunkVisibility()
{
	FScopeLock Lock(&RenderChunkMutex);
	
	for (auto& ChunkPair : LoadedRenderChunks)
	{
		FStaticWaterRenderChunk& Chunk = ChunkPair.Value;
		if (Chunk.MeshComponent && Chunk.MeshComponent->IsValidLowLevel())
		{
			const bool bShouldBeVisible = IsChunkVisible(Chunk);
			Chunk.MeshComponent->SetVisibility(bShouldBeVisible && bRenderingEnabled);
		}
	}
}

FIntVector UStaticWaterRenderer::WorldPositionToRenderChunkCoord(const FVector& WorldPosition) const
{
	return FIntVector(
		FMath::FloorToInt(WorldPosition.X / RenderSettings.RenderChunkSize),
		FMath::FloorToInt(WorldPosition.Y / RenderSettings.RenderChunkSize),
		0
	);
}

FVector UStaticWaterRenderer::RenderChunkCoordToWorldPosition(const FIntVector& ChunkCoord) const
{
	return FVector(
		ChunkCoord.X * RenderSettings.RenderChunkSize,
		ChunkCoord.Y * RenderSettings.RenderChunkSize,
		0.0f
	);
}

void UStaticWaterRenderer::UpdateActiveRenderChunks()
{
	if (ViewerPositions.Num() == 0)
	{
		if (bEnableLogging)
		{
			UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: UpdateActiveRenderChunks - No viewer positions"));
		}
		return;
	}
		
	const float MaxDistance = RenderSettings.MaxRenderDistance;
	const float ChunkSize = RenderSettings.RenderChunkSize;
	const int32 ChunkRadius = FMath::CeilToInt(MaxDistance / ChunkSize);
	
	TSet<FIntVector> NewActiveChunks;
	
	// Determine which chunks should be active based on all viewers
	for (const FVector& ViewerPos : ViewerPositions)
	{
		const FIntVector ViewerChunk = WorldPositionToRenderChunkCoord(ViewerPos);
		
		for (int32 X = -ChunkRadius; X <= ChunkRadius; ++X)
		{
			for (int32 Y = -ChunkRadius; Y <= ChunkRadius; ++Y)
			{
				const FIntVector ChunkCoord = ViewerChunk + FIntVector(X, Y, 0);
				const FVector ChunkCenter = RenderChunkCoordToWorldPosition(ChunkCoord) + 
					FVector(ChunkSize * 0.5f, ChunkSize * 0.5f, 0.0f);
				
				const float Distance = GetClosestViewerDistance(ChunkCenter);
				if (Distance <= MaxDistance)
				{
					NewActiveChunks.Add(ChunkCoord);
				}
			}
		}
	}
	
	// Find chunks to load and unload
	FScopeLock Lock(&RenderChunkMutex);
	
	// Queue chunks for loading
	for (const FIntVector& ChunkCoord : NewActiveChunks)
	{
		if (!ActiveRenderChunkCoords.Contains(ChunkCoord) && !LoadedRenderChunks.Contains(ChunkCoord))
		{
			ChunkLoadQueue.Enqueue(ChunkCoord);
		}
	}
	
	// Queue chunks for unloading
	TArray<FIntVector> ChunksToUnload;
	for (const FIntVector& ChunkCoord : ActiveRenderChunkCoords)
	{
		if (!NewActiveChunks.Contains(ChunkCoord))
		{
			ChunksToUnload.Add(ChunkCoord);
		}
	}
	
	for (const FIntVector& ChunkCoord : ChunksToUnload)
	{
		ChunkUnloadQueue.Enqueue(ChunkCoord);
	}
	
	ActiveRenderChunkCoords = MoveTemp(NewActiveChunks);
	
	// Enforce chunk limit
	if (LoadedRenderChunks.Num() > RenderSettings.MaxRenderChunks)
	{
		// Remove oldest chunks that are not active
		auto Iterator = LoadedRenderChunks.CreateIterator();
		int32 ChunksToRemove = LoadedRenderChunks.Num() - RenderSettings.MaxRenderChunks;
		
		while (Iterator && ChunksToRemove > 0)
		{
			if (!ActiveRenderChunkCoords.Contains(Iterator.Key()))
			{
				FStaticWaterRenderChunk& Chunk = Iterator.Value();
				if (Chunk.MeshComponent && Chunk.MeshComponent->IsValidLowLevel())
				{
					DestroyMeshComponent(Chunk.MeshComponent);
				}
				Iterator.RemoveCurrent();
				--ChunksToRemove;
			}
			else
			{
				++Iterator;
			}
		}
	}
}

void UStaticWaterRenderer::LoadRenderChunk(const FIntVector& ChunkCoord)
{
	FScopeLock Lock(&RenderChunkMutex);
	
	if (LoadedRenderChunks.Contains(ChunkCoord))
		return;
		
	FStaticWaterRenderChunk& NewChunk = LoadedRenderChunks.Add(ChunkCoord);
	NewChunk.ChunkCoord = ChunkCoord;
	
	// Set world bounds
	const FVector ChunkOrigin = RenderChunkCoordToWorldPosition(ChunkCoord);
	const FVector ChunkSize = FVector(RenderSettings.RenderChunkSize, RenderSettings.RenderChunkSize, 1000.0f);
	NewChunk.WorldBounds = FBox(ChunkOrigin, ChunkOrigin + ChunkSize);
	
	// Calculate initial LOD
	const float Distance = GetDistanceToChunk(ChunkCoord);
	NewChunk.LODLevel = CalculateLODLevel(Distance);
	
	// Create mesh component
	NewChunk.MeshComponent = CreateMeshComponent(ChunkCoord);
	NewChunk.bNeedsRebuild = true;
	
	UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: 🌊 LOADED render chunk (%d, %d) at distance %.1fm, LOD%d"), 
		ChunkCoord.X, ChunkCoord.Y, Distance, NewChunk.LODLevel);
}

void UStaticWaterRenderer::UnloadRenderChunk(const FIntVector& ChunkCoord)
{
	FScopeLock Lock(&RenderChunkMutex);
	
	if (FStaticWaterRenderChunk* Chunk = LoadedRenderChunks.Find(ChunkCoord))
	{
		if (Chunk->MeshComponent && Chunk->MeshComponent->IsValidLowLevel())
		{
			DestroyMeshComponent(Chunk->MeshComponent);
		}
		
		LoadedRenderChunks.Remove(ChunkCoord);
		
		UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: ❌ UNLOADED render chunk (%d, %d)"), 
			ChunkCoord.X, ChunkCoord.Y);
	}
	
	ActiveRenderChunkCoords.Remove(ChunkCoord);
}

bool UStaticWaterRenderer::ShouldLoadRenderChunk(const FIntVector& ChunkCoord) const
{
	const FVector ChunkCenter = RenderChunkCoordToWorldPosition(ChunkCoord) + 
		FVector(RenderSettings.RenderChunkSize * 0.5f);
	const float Distance = GetClosestViewerDistance(ChunkCenter);
	
	// Only load chunks that are in the ring between MinRenderDistance and MaxRenderDistance
	// This creates a donut/ring of static water around the player
	return Distance >= RenderSettings.MinRenderDistance && Distance <= RenderSettings.MaxRenderDistance;
}

bool UStaticWaterRenderer::ShouldUnloadRenderChunk(const FIntVector& ChunkCoord) const
{
	return !ShouldLoadRenderChunk(ChunkCoord);
}

void UStaticWaterRenderer::BuildChunkMesh(FStaticWaterRenderChunk& Chunk)
{
	UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: BuildChunkMesh called for chunk (%d, %d)"), 
		Chunk.ChunkCoord.X, Chunk.ChunkCoord.Y);
		
	if (!WaterGenerator || !Chunk.MeshComponent || !Chunk.MeshComponent->IsValidLowLevel())
		return;
	
	// Check if there's active fluid simulation in this area
	// If so, don't render static water (let the fluid simulation render instead)
	if (AActor* Owner = GetOwner())
	{
		if (AVoxelFluidActor* FluidActor = Cast<AVoxelFluidActor>(Owner))
		{
			// Check if this region has active simulation
			const FVector ChunkCenter = Chunk.WorldBounds.GetCenter();
			if (FluidActor->IsRegionActiveForSimulation(ChunkCenter))
			{
				// Active simulation in this area, hide static water
				Chunk.MeshComponent->ClearAllMeshSections();
				Chunk.bHasWater = false;
				
				if (bEnableLogging)
				{
					UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: Chunk (%d, %d) has active fluid simulation - hiding static water"), 
						Chunk.ChunkCoord.X, Chunk.ChunkCoord.Y);
				}
				return;
			}
		}
	}
		
	const double StartTime = FPlatformTime::Seconds();
	
	// Clear previous mesh data
	Chunk.Clear();
	
	// Check if this chunk has water from the generator
	const FVector ChunkCenter = Chunk.WorldBounds.GetCenter();
	
	if (!WaterGenerator)
	{
		UE_LOG(LogTemp, Error, TEXT("StaticWaterRenderer: WaterGenerator is null! Cannot build mesh for chunk (%d, %d)"), 
			Chunk.ChunkCoord.X, Chunk.ChunkCoord.Y);
		return;
	}
	
	bool bHasWater = WaterGenerator->HasStaticWaterAtLocation(ChunkCenter);
	
	// Always log water checks to debug the issue
	UE_LOG(LogTemp, Log, TEXT("StaticWaterRenderer: Checking water at chunk center %s: %s"), 
		*ChunkCenter.ToString(), bHasWater ? TEXT("HAS WATER") : TEXT("NO WATER"));
	
	// Debug: Also check the water level
	if (bHasWater)
	{
		float WaterLevel = WaterGenerator->GetWaterLevelAtLocation(ChunkCenter);
		UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: ✅ FOUND WATER at %s, level: %.1f"), 
			*ChunkCenter.ToString(), WaterLevel);
	}
	
	if (!bHasWater)
	{
		// No water in this chunk, clear the mesh
		Chunk.MeshComponent->ClearAllMeshSections();
		
		if (bEnableLogging)
		{
			UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: No water found at chunk center %s - clearing mesh"), 
				*ChunkCenter.ToString());
		}
		return;
	}
	
	// Generate water surface mesh
	GenerateWaterSurface(Chunk);
	
	if (bEnableLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("StaticWaterRenderer: Generated %d vertices, %d triangles for chunk (%d, %d)"), 
			Chunk.Vertices.Num(), Chunk.Triangles.Num() / 3, Chunk.ChunkCoord.X, Chunk.ChunkCoord.Y);
	}
	
	// Update the mesh component
	if (Chunk.Vertices.Num() > 0 && Chunk.Triangles.Num() > 0)
	{
		UpdateChunkMesh(Chunk);
		Chunk.bHasWater = true;
		
		if (bEnableLogging)
		{
			UE_LOG(LogTemp, Log, TEXT("StaticWaterRenderer: Successfully created mesh for chunk (%d, %d)"), 
				Chunk.ChunkCoord.X, Chunk.ChunkCoord.Y);
		}
	}
	else
	{
		Chunk.MeshComponent->ClearAllMeshSections();
		Chunk.bHasWater = false;
		
		if (bEnableLogging)
		{
			UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: No mesh data generated for chunk (%d, %d)"), 
				Chunk.ChunkCoord.X, Chunk.ChunkCoord.Y);
		}
	}
	
	LastRenderTime = FPlatformTime::Seconds() - StartTime;
	
	if (bEnableLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("StaticWaterRenderer: Built mesh for chunk (%d, %d) in %.3fms - %d vertices, %d triangles"), 
			Chunk.ChunkCoord.X, Chunk.ChunkCoord.Y, LastRenderTime * 1000.0f, 
			Chunk.Vertices.Num(), Chunk.Triangles.Num() / 3);
	}
}

void UStaticWaterRenderer::GenerateWaterSurface(FStaticWaterRenderChunk& Chunk)
{
	// Generate water surface based on LOD level
	// LOD0 uses adaptive mesh that culls triangles above terrain
	// Higher LODs use simple planar mesh for performance
	
	const float WaterLevel = WaterGenerator->GetWaterLevelAtLocation(Chunk.WorldBounds.GetCenter());
	
	if (bEnableLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("StaticWaterRenderer: Water level at chunk center %s: %.1f"), 
			*Chunk.WorldBounds.GetCenter().ToString(), WaterLevel);
	}
	
	if (WaterLevel > -MAX_flt)
	{
		Chunk.WaterLevel = WaterLevel;
		
		if (Chunk.LODLevel == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: 🌊 ADAPTIVE mesh for LOD0 chunk (%d, %d)"), 
				Chunk.ChunkCoord.X, Chunk.ChunkCoord.Y);
			GenerateAdaptiveWaterMesh(Chunk);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: 📦 PLANAR mesh for LOD%d chunk (%d, %d)"), 
				Chunk.LODLevel, Chunk.ChunkCoord.X, Chunk.ChunkCoord.Y);
			GeneratePlanarWaterMesh(Chunk, WaterLevel);
		}
	}
	else
	{
		if (bEnableLogging)
		{
			UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: No water found at chunk center %s"), 
				*Chunk.WorldBounds.GetCenter().ToString());
		}
	}
}

void UStaticWaterRenderer::GeneratePlanarWaterMesh(FStaticWaterRenderChunk& Chunk, float WaterLevel)
{
	const FBox& Bounds = Chunk.WorldBounds;
	const float Resolution = RenderSettings.MeshResolution * (1 << Chunk.LODLevel); // Increase resolution for higher LODs
	
	const int32 VertsPerSide = FMath::Max(2, FMath::CeilToInt(RenderSettings.RenderChunkSize / Resolution));
	const float StepSize = RenderSettings.RenderChunkSize / (VertsPerSide - 1);
	
	if (bEnableLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("StaticWaterRenderer: Generating planar mesh - WaterLevel: %.1f, VertsPerSide: %d, StepSize: %.1f"), 
			WaterLevel, VertsPerSide, StepSize);
	}
	
	// Generate vertices
	Chunk.Vertices.Reserve(VertsPerSide * VertsPerSide);
	Chunk.UVs.Reserve(VertsPerSide * VertsPerSide);
	Chunk.Normals.Reserve(VertsPerSide * VertsPerSide);
	
	for (int32 Y = 0; Y < VertsPerSide; ++Y)
	{
		for (int32 X = 0; X < VertsPerSide; ++X)
		{
			const FVector WorldPos = FVector(
				Bounds.Min.X + X * StepSize,
				Bounds.Min.Y + Y * StepSize,
				WaterLevel
			);
			
			Chunk.Vertices.Add(WorldPos);
			Chunk.UVs.Add(FVector2D(X / (float)(VertsPerSide - 1), Y / (float)(VertsPerSide - 1)));
			Chunk.Normals.Add(FVector::UpVector);
		}
	}
	
	// Generate triangles
	Chunk.Triangles.Reserve((VertsPerSide - 1) * (VertsPerSide - 1) * 6);
	
	for (int32 Y = 0; Y < VertsPerSide - 1; ++Y)
	{
		for (int32 X = 0; X < VertsPerSide - 1; ++X)
		{
			const int32 I0 = Y * VertsPerSide + X;
			const int32 I1 = Y * VertsPerSide + X + 1;
			const int32 I2 = (Y + 1) * VertsPerSide + X;
			const int32 I3 = (Y + 1) * VertsPerSide + X + 1;
			
			// Triangle 1
			Chunk.Triangles.Add(I0);
			Chunk.Triangles.Add(I2);
			Chunk.Triangles.Add(I1);
			
			// Triangle 2
			Chunk.Triangles.Add(I1);
			Chunk.Triangles.Add(I2);
			Chunk.Triangles.Add(I3);
		}
	}
}

void UStaticWaterRenderer::GenerateAdaptiveWaterMesh(FStaticWaterRenderChunk& Chunk)
{
	// Generate terrain-adaptive water mesh that culls triangles above terrain
	const FBox& Bounds = Chunk.WorldBounds;
	const float Resolution = RenderSettings.MeshResolution;
	const float WaterLevel = Chunk.WaterLevel;
	
	// Use higher resolution for LOD0 adaptive meshes
	const int32 VertsPerSide = FMath::Max(16, FMath::CeilToInt(RenderSettings.RenderChunkSize / Resolution));
	const float StepSize = RenderSettings.RenderChunkSize / (VertsPerSide - 1);
	
	if (bEnableLogging)
	{
		UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: Generating ADAPTIVE mesh - WaterLevel: %.1f, VertsPerSide: %d, StepSize: %.1f"), 
			WaterLevel, VertsPerSide, StepSize);
	}
	
	// Create arrays for mesh data
	TArray<FVector> TempVertices;
	TArray<int32> TempTriangles;
	TArray<FVector> TempNormals;
	TArray<FVector2D> TempUVs;
	TArray<float> VertexTerrainHeights;
	
	// Create a regular grid of vertices - always create all vertices for proper UV mapping
	for (int32 Y = 0; Y < VertsPerSide; ++Y)
	{
		for (int32 X = 0; X < VertsPerSide; ++X)
		{
			const float WorldX = Bounds.Min.X + X * StepSize;
			const float WorldY = Bounds.Min.Y + Y * StepSize;
			
			// Sample terrain height at this vertex position
			float TerrainHeight = WaterLevel - 100.0f; // Default to below water if no terrain data
			if (VoxelIntegration && VoxelIntegration->IsValidLowLevel())
			{
				TerrainHeight = VoxelIntegration->SampleVoxelHeight(WorldX, WorldY);
			}
			
			// Store the terrain height for later use when creating triangles
			VertexTerrainHeights.Add(TerrainHeight);
			
			// Water surface is always at water level (flat ocean surface)
			const float WaterSurfaceHeight = WaterLevel;
			
			TempVertices.Add(FVector(WorldX, WorldY, WaterSurfaceHeight));
			TempUVs.Add(FVector2D((float)X / (VertsPerSide - 1), (float)Y / (VertsPerSide - 1)));
			TempNormals.Add(FVector(0, 0, 1));
		}
	}
	
	// Generate triangle indices only where water should be visible
	for (int32 Y = 0; Y < VertsPerSide - 1; ++Y)
	{
		for (int32 X = 0; X < VertsPerSide - 1; ++X)
		{
			const int32 VertIndex = Y * VertsPerSide + X;
			const int32 NextRowIndex = VertIndex + VertsPerSide;
			
			// Check if any of the quad's vertices have terrain below water level
			const float TerrainHeight1 = VertexTerrainHeights[VertIndex];
			const float TerrainHeight2 = VertexTerrainHeights[VertIndex + 1];
			const float TerrainHeight3 = VertexTerrainHeights[NextRowIndex];
			const float TerrainHeight4 = VertexTerrainHeights[NextRowIndex + 1];
			
			// Only create triangles if at least one corner is underwater
			const bool bHasWater = (TerrainHeight1 < WaterLevel) || (TerrainHeight2 < WaterLevel) ||
			                       (TerrainHeight3 < WaterLevel) || (TerrainHeight4 < WaterLevel);
			
			if (bHasWater)
			{
				// Create two triangles per quad
				// Triangle 1
				TempTriangles.Add(VertIndex);
				TempTriangles.Add(NextRowIndex);
				TempTriangles.Add(VertIndex + 1);
				
				// Triangle 2  
				TempTriangles.Add(VertIndex + 1);
				TempTriangles.Add(NextRowIndex);
				TempTriangles.Add(NextRowIndex + 1);
			}
		}
	}
	
	// Store the generated mesh data
	Chunk.Vertices = TempVertices;
	Chunk.Triangles = TempTriangles;
	Chunk.Normals = TempNormals;
	Chunk.UVs = TempUVs;
	Chunk.bHasWater = TempTriangles.Num() > 0; // Only has water if we have triangles
	
	if (bEnableLogging)
	{
		UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: Generated ADAPTIVE mesh with %d vertices, %d triangles"), 
			TempVertices.Num(), TempTriangles.Num() / 3);
	}
}

void UStaticWaterRenderer::UpdateChunkMesh(FStaticWaterRenderChunk& Chunk)
{
	if (!Chunk.MeshComponent || !Chunk.MeshComponent->IsValidLowLevel())
		return;
		
	// Update the procedural mesh component
	Chunk.MeshComponent->CreateMeshSection_LinearColor(
		0, // Section index
		Chunk.Vertices,
		Chunk.Triangles,
		Chunk.Normals,
		Chunk.UVs,
		TArray<FLinearColor>(), // Vertex colors
		TArray<FProcMeshTangent>(), // Tangents
		false // Don't create collision for static water
	);
	
	// Apply material
	UpdateComponentMaterial(Chunk.MeshComponent, Chunk.LODLevel);
	
	// Make sure the component is visible
	Chunk.MeshComponent->SetVisibility(true);
	Chunk.MeshComponent->SetCastShadow(false); // Water doesn't need to cast shadows
}

UProceduralMeshComponent* UStaticWaterRenderer::CreateMeshComponent(const FIntVector& ChunkCoord)
{
	UProceduralMeshComponent* MeshComp = nullptr;
	
	// Try to reuse a component from the pool
	if (AvailableMeshComponents.Num() > 0)
	{
		MeshComp = AvailableMeshComponents.Pop();
		MeshComp->ClearAllMeshSections();
	}
	else
	{
		// Create new component
		MeshComp = NewObject<UProceduralMeshComponent>(GetOwner());
		MeshComp->AttachToComponent(GetOwner()->GetRootComponent(), 
			FAttachmentTransformRules::KeepWorldTransform);
		MeshComp->RegisterComponent();
	}
	
	if (MeshComp)
	{
		MeshComp->SetComponentTickEnabled(false);
		MeshComp->SetCastShadow(true);
		MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		UsedMeshComponents.Add(MeshComp);
		
		// Note: Skipping component renaming to avoid name conflicts when reusing pooled components
		// Component names are not critical for functionality
	}
	
	return MeshComp;
}

void UStaticWaterRenderer::DestroyMeshComponent(UProceduralMeshComponent* MeshComp)
{
	if (!MeshComp || !MeshComp->IsValidLowLevel())
		return;
		
	UsedMeshComponents.Remove(MeshComp);
	
	// Return to pool for reuse
	if (AvailableMeshComponents.Num() < 50) // Limit pool size
	{
		MeshComp->ClearAllMeshSections();
		MeshComp->SetVisibility(false);
		AvailableMeshComponents.Add(MeshComp);
	}
	else
	{
		// Destroy if pool is full
		MeshComp->DestroyComponent();
	}
}

void UStaticWaterRenderer::UpdateComponentMaterial(UProceduralMeshComponent* MeshComp, int32 LODLevel)
{
	if (!MeshComp || !MeshComp->IsValidLowLevel())
		return;
		
	UMaterialInterface* MaterialToUse = nullptr;
	
	if (LODLevel == 0 && WaterMaterial)
	{
		MaterialToUse = WaterMaterial;
	}
	else if (LODLevel > 0 && WaterMaterialLOD1)
	{
		MaterialToUse = WaterMaterialLOD1;
	}
	else if (WaterMaterial)
	{
		MaterialToUse = WaterMaterial;
	}
	
	// If no material is set, use default if available
	if (!MaterialToUse && WaterMaterial)
	{
		MaterialToUse = WaterMaterial;
	}
	
	if (MaterialToUse)
	{
		MeshComp->SetMaterial(0, MaterialToUse);
		
		if (bEnableLogging)
		{
			UE_LOG(LogTemp, Log, TEXT("StaticWaterRenderer: Applied material %s to chunk LOD%d"), 
				*MaterialToUse->GetName(), LODLevel);
		}
	}
	else
	{
		// Set a bright debug color so we can see the mesh even without material
		MeshComp->SetMaterial(0, nullptr);
		UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: No water material available for chunk - mesh will be unlit"));
	}
}

float UStaticWaterRenderer::GetDistanceToChunk(const FIntVector& ChunkCoord) const
{
	const FVector ChunkCenter = RenderChunkCoordToWorldPosition(ChunkCoord) + 
		FVector(RenderSettings.RenderChunkSize * 0.5f);
	return GetClosestViewerDistance(ChunkCenter);
}

float UStaticWaterRenderer::GetClosestViewerDistance(const FVector& Position) const
{
	if (ViewerPositions.Num() == 0)
		return MAX_flt;
		
	float MinDistance = MAX_flt;
	for (const FVector& ViewerPos : ViewerPositions)
	{
		const float Distance = FVector::Dist2D(Position, ViewerPos);
		MinDistance = FMath::Min(MinDistance, Distance);
	}
	
	return MinDistance;
}

bool UStaticWaterRenderer::IsChunkVisible(const FStaticWaterRenderChunk& Chunk) const
{
	if (!Chunk.bHasWater)
		return false;
		
	const float Distance = GetClosestViewerDistance(Chunk.WorldBounds.GetCenter());
	const float CullDistance = RenderSettings.MaxRenderDistance * RenderSettings.CullDistanceScale;
	
	return Distance <= CullDistance;
}

int32 UStaticWaterRenderer::CalculateLODLevel(float Distance) const
{
	if (Distance <= RenderSettings.LOD0Distance)
		return 0;
	else if (Distance <= RenderSettings.LOD1Distance)
		return 1;
	else
		return 2;
}

void UStaticWaterRenderer::DrawDebugInfo() const
{
#if WITH_EDITOR
	UWorld* World = GetWorld();
	if (!World)
		return;
		
	FScopeLock Lock(&RenderChunkMutex);
	
	// Draw render chunk bounds
	if (bShowRenderChunkBounds)
	{
		for (const auto& ChunkPair : LoadedRenderChunks)
		{
			const FStaticWaterRenderChunk& Chunk = ChunkPair.Value;
			FColor ChunkColor = Chunk.bHasWater ? FColor::Blue : FColor(128, 128, 128);
			
			if (bShowLODColors)
			{
				switch (Chunk.LODLevel)
				{
					case 0: ChunkColor = FColor::Green; break;
					case 1: ChunkColor = FColor::Yellow; break;
					case 2: ChunkColor = FColor::Red; break;
					default: ChunkColor = FColor::Purple; break;
				}
			}
			
			DrawDebugBox(World, Chunk.WorldBounds.GetCenter(), Chunk.WorldBounds.GetExtent(), 
				ChunkColor, false, -1.0f, 0, 10.0f);
				
			// Draw LOD level text
			if (bShowLODColors)
			{
				const FString LODText = FString::Printf(TEXT("LOD%d"), Chunk.LODLevel);
				DrawDebugString(World, Chunk.WorldBounds.GetCenter() + FVector(0, 0, 100), 
					LODText, nullptr, ChunkColor, 0.0f);
			}
		}
	}
#endif
}