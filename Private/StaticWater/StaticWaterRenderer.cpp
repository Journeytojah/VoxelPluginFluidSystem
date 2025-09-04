#include "StaticWater/StaticWaterRenderer.h"
#include "StaticWater/StaticWaterGenerator.h"
#include "VoxelIntegration/VoxelFluidIntegration.h"
#include "Actors/VoxelFluidActor.h"
#include "Actors/VoxelStaticWaterActor.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "DrawDebugHelpers.h"
#include "Materials/MaterialInterface.h"
#include "Templates/UnrealTemplate.h"

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

	// Initialize startup optimization
	StartupTime = 0.0f;
	OriginalMaxRenderDistance = RenderSettings.MaxRenderDistance;
	
	// Start with much smaller render distance for faster startup
	if (RenderSettings.bUseProgressiveLoading)
	{
		// Use chunk size * 2 as minimum to ensure we get at least some chunks
		const float MinStartDistance = RenderSettings.RenderChunkSize * 2.0f; // ~25600 for our 12800 chunk size
		RenderSettings.MaxRenderDistance = FMath::Min(MinStartDistance, OriginalMaxRenderDistance * 0.25f);
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

	// Progressive render distance increase for faster startup
	if (RenderSettings.bUseProgressiveLoading && StartupTime < StartupProgressionTime)
	{
		StartupTime += DeltaTime;
		const float ProgressAlpha = FMath::Clamp(StartupTime / StartupProgressionTime, 0.0f, 1.0f);
		// Use chunk size * 2 as minimum to ensure we get at least some chunks
		const float MinStartDistance = RenderSettings.RenderChunkSize * 2.0f;
		const float StartDistance = FMath::Min(MinStartDistance, OriginalMaxRenderDistance * 0.25f);
		RenderSettings.MaxRenderDistance = FMath::Lerp(StartDistance, OriginalMaxRenderDistance, ProgressAlpha);
		
		if (bEnableLogging && FMath::FloorToInt(StartupTime) != FMath::FloorToInt(StartupTime - DeltaTime))
		{
			UE_LOG(LogTemp, Log, TEXT("StaticWaterRenderer: Progressive loading - render distance: %.0f/%.0f"), 
				RenderSettings.MaxRenderDistance, OriginalMaxRenderDistance);
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
	
	if (VoxelIntegration)
	{
		UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: VoxelIntegration details - VoxelWorldValid=%s, TerrainLayer=%s, bUseVoxelLayerSampling=%s"), 
			VoxelIntegration->IsVoxelWorldValid() ? TEXT("true") : TEXT("false"),
			VoxelIntegration->TerrainLayer.Layer ? TEXT("valid") : TEXT("null"),
			VoxelIntegration->bUseVoxelLayerSampling ? TEXT("true") : TEXT("false"));
	}
	
	// Force rebuild all chunks when voxel integration changes
	ForceRebuildAllChunks();
}

void UStaticWaterRenderer::SetViewerPosition(const FVector& Position)
{
	// Disable auto-tracking when manually setting viewer position
	bAutoTrackPlayer = false;
	
	ViewerPositions.Empty();
	ViewerPositions.Add(Position);
	
	// Viewer position updated silently
}

void UStaticWaterRenderer::AddViewer(const FVector& Position)
{
	// Disable auto-tracking when manually adding viewers
	bAutoTrackPlayer = false;
	
	ViewerPositions.Add(Position);
	// Viewer added silently
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
	// Regenerating chunks around viewer
	
	// Just update which chunks should be active, don't force immediate update
	UpdateActiveRenderChunks();
	
	// Regeneration completed
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

void UStaticWaterRenderer::GetLODStatistics(int32& OutLOD0Count, int32& OutLOD1Count, int32& OutLOD2Count) const
{
	FScopeLock Lock(&RenderChunkMutex);
	
	OutLOD0Count = 0;
	OutLOD1Count = 0;
	OutLOD2Count = 0;
	
	for (const auto& Pair : LoadedRenderChunks)
	{
		const FStaticWaterRenderChunk& Chunk = Pair.Value;
		if (Chunk.MeshComponent && Chunk.MeshComponent->IsValidLowLevel() && Chunk.MeshComponent->IsVisible())
		{
			switch (Chunk.LODLevel)
			{
				case 0: OutLOD0Count++; break;
				case 1: OutLOD1Count++; break;
				default: OutLOD2Count++; break;
			}
		}
	}
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
	
	// Load new chunks (use progressive loading setting)
	const int32 MaxChunksToCreate = RenderSettings.bUseProgressiveLoading ? 
		RenderSettings.MaxChunksToCreatePerFrame : RenderSettings.MaxChunksToUpdatePerFrame;
	
	while (!ChunkLoadQueue.IsEmpty() && ChunksUpdatedThisFrame < MaxChunksToCreate)
	{
		FIntVector ChunkCoord;
		if (ChunkLoadQueue.Dequeue(ChunkCoord))
		{
			UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: Loading chunk (%d, %d) - Frame limit: %d/%d"), 
				ChunkCoord.X, ChunkCoord.Y, ChunksUpdatedThisFrame + 1, MaxChunksToCreate);
			LoadRenderChunk(ChunkCoord);
			++ChunksUpdatedThisFrame;
		}
	}
	
	// Debug: Show queue status
	if (ChunksUpdatedThisFrame == 0 && ChunkLoadQueue.IsEmpty())
	{
		static float LastLogTime = 0.0f;
		float CurrentTime = FPlatformTime::Seconds();
		if (CurrentTime - LastLogTime > 2.0f) // Log every 2 seconds
		{
			UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: No chunks in load queue, %d chunks loaded"), LoadedRenderChunks.Num());
			LastLogTime = CurrentTime;
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
	
	// Build meshes for chunks that need it (use progressive loading setting)
	FScopeLock Lock(&RenderChunkMutex);
	const int32 MaxChunksToBuild = RenderSettings.bUseProgressiveLoading ? 
		RenderSettings.MaxChunksToCreatePerFrame : RenderSettings.MaxChunksToUpdatePerFrame;
		
	for (auto& ChunkPair : LoadedRenderChunks)
	{
		FStaticWaterRenderChunk& Chunk = ChunkPair.Value;
		if (Chunk.bNeedsRebuild && ChunksBuiltThisFrame < MaxChunksToBuild)
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
			// No viewer positions available
		}
		return;
	}
		
	const float MaxDistance = RenderSettings.MaxRenderDistance;
	const float ChunkSize = RenderSettings.RenderChunkSize;
	const int32 ChunkRadius = FMath::CeilToInt(MaxDistance / ChunkSize);
	
	TSet<FIntVector> NewActiveChunks;
	
	// // Debug viewer positions
	// UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: Updating chunks for %d viewers, ChunkRadius: %d, MaxDistance: %.0f"), 
	// 	ViewerPositions.Num(), ChunkRadius, MaxDistance);
	
	// Determine which chunks should be active based on all viewers
	for (int32 ViewerIndex = 0; ViewerIndex < ViewerPositions.Num(); ++ViewerIndex)
	{
		const FVector& ViewerPos = ViewerPositions[ViewerIndex];
		const FIntVector ViewerChunk = WorldPositionToRenderChunkCoord(ViewerPos);
		
		// UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: Viewer %d at %s -> Chunk (%d, %d)"), 
		// 	ViewerIndex, *ViewerPos.ToString(), ViewerChunk.X, ViewerChunk.Y);
		
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
					// if (ViewerIndex == 0 && FMath::Abs(X) <= 1 && FMath::Abs(Y) <= 1) // Log nearby chunks only
					// {
					// 	UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: Added chunk (%d, %d) at distance %.0f"), 
					// 		ChunkCoord.X, ChunkCoord.Y, Distance);
					// }
				}
			}
		}
	}
	
	// Find chunks to load and unload
	FScopeLock Lock(&RenderChunkMutex);
	
	// Queue chunks for loading
	int32 ChunksQueued = 0;
	for (const FIntVector& ChunkCoord : NewActiveChunks)
	{
		if (!ActiveRenderChunkCoords.Contains(ChunkCoord) && !LoadedRenderChunks.Contains(ChunkCoord))
		{
			ChunkLoadQueue.Enqueue(ChunkCoord);
			ChunksQueued++;
		}
	}
	
	if (ChunksQueued > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: Queued %d chunks for loading from %d active chunks"), 
			ChunksQueued, NewActiveChunks.Num());
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
	
	// Force detailed logging for initial chunk loading to debug terrain sampling issue
	UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: 🌊 LOADED render chunk (%d, %d) at distance %.1fm, LOD%d [LOD0Dist=%.0f, LOD1Dist=%.0f]"), 
		ChunkCoord.X, ChunkCoord.Y, Distance, NewChunk.LODLevel, RenderSettings.LOD0Distance, RenderSettings.LOD1Distance);
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
	UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: 🔨 BuildChunkMesh called for chunk (%d, %d) at %s"), 
		Chunk.ChunkCoord.X, Chunk.ChunkCoord.Y, *Chunk.WorldBounds.GetCenter().ToString());
		
	if (!WaterGenerator || !Chunk.MeshComponent || !Chunk.MeshComponent->IsValidLowLevel())
		return;
	
	// Don't automatically hide static water - let it render alongside dynamic water
	// This prevents visual pops when transitioning between systems
	/* DISABLED - Static and dynamic water can coexist
	if (AActor* Owner = GetOwner())
	{
		if (AVoxelFluidActor* FluidActor = Cast<AVoxelFluidActor>(Owner))
		{
			// Check if this region has active simulation - use a smaller radius for smoother transition
			const FVector ChunkCenter = Chunk.WorldBounds.GetCenter();
			const float ActiveSimRadius = RenderSettings.MinRenderDistance * 0.9f; // Slightly smaller than min render distance
			
			// Get player position for distance check
			FVector PlayerPos = FVector::ZeroVector;
			if (ViewerPositions.Num() > 0)
			{
				PlayerPos = ViewerPositions[0];
			}
			
			// Hide static water only if very close to player where simulation is active
			float DistToPlayer = FVector::Dist(ChunkCenter, PlayerPos);
			if (DistToPlayer < ActiveSimRadius)
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
	*/
		
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
	
	// Check if water exists at this location
	bool bHasWater = WaterGenerator->HasStaticWaterAtLocation(ChunkCenter);
	float WaterLevel = WaterGenerator->GetWaterLevelAtLocation(ChunkCenter);
	
	// Log for debugging
	UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: BuildChunkMesh - Chunk (%d,%d) at %s - HasWater: %s, WaterLevel: %.1f"), 
		Chunk.ChunkCoord.X, Chunk.ChunkCoord.Y, *ChunkCenter.ToString(),
		bHasWater ? TEXT("YES") : TEXT("NO"), WaterLevel);
	
	// Basic validation - only reject if water level is completely invalid
	if (bHasWater && (WaterLevel < -99000.0f || WaterLevel > 99000.0f))
	{
		UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: Rejecting chunk - invalid water level"));
		bHasWater = false;
	}
	
	// Always log water checks to debug the issue
	UE_LOG(LogTemp, Log, TEXT("StaticWaterRenderer: Checking water at chunk center %s: %s"), 
		*ChunkCenter.ToString(), bHasWater ? TEXT("HAS WATER") : TEXT("NO WATER"));
	
	// Additional validation: Check if water level makes sense
	if (bHasWater)
	{
		// WaterLevel already declared above, just reuse it
		WaterLevel = WaterGenerator->GetWaterLevelAtLocation(ChunkCenter);
		
		// Sanity check - if water level is extremely low or high, it might be invalid
		if (WaterLevel < -10000.0f || WaterLevel > 10000.0f)
		{
			UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: Invalid water level %.1f at %s - skipping chunk"), 
				WaterLevel, *ChunkCenter.ToString());
			bHasWater = false;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: ✅ FOUND WATER at %s, level: %.1f"), 
				*ChunkCenter.ToString(), WaterLevel);
		}
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
		
		const FVector ChunkCenter = Chunk.WorldBounds.GetCenter();
		const float DistanceToPlayer = GetClosestViewerDistance(ChunkCenter);
		
		// Check if the owning actor wants terrain adaptive meshes
		bool bShouldUseAdaptiveMesh = (Chunk.LODLevel == 0);
		bool bOwnerWantsAdaptive = true;
		bool bHasValidVoxelIntegration = (VoxelIntegration && VoxelIntegration->IsVoxelWorldValid());
		
		if (!bHasValidVoxelIntegration)
		{
			UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: Invalid VoxelIntegration for chunk (%d, %d) - VoxelIntegration=%s, VoxelWorldValid=%s"), 
				Chunk.ChunkCoord.X, Chunk.ChunkCoord.Y,
				VoxelIntegration ? TEXT("Valid") : TEXT("Null"),
				(VoxelIntegration && VoxelIntegration->IsVoxelWorldValid()) ? TEXT("true") : TEXT("false"));
		}
		
		if (AActor* Owner = GetOwner())
		{
			if (AVoxelStaticWaterActor* StaticWaterActor = Cast<AVoxelStaticWaterActor>(Owner))
			{
				bOwnerWantsAdaptive = StaticWaterActor->bUseTerrainAdaptiveMesh;
				bShouldUseAdaptiveMesh = bShouldUseAdaptiveMesh && bOwnerWantsAdaptive;
			}
		}
		
		if (bShouldUseAdaptiveMesh && bHasValidVoxelIntegration)
		{
			UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: 🌊 ADAPTIVE mesh for LOD%d chunk (%d, %d) at distance %.0f [LOD0=%s, OwnerWants=%s, VoxelValid=%s]"), 
				Chunk.LODLevel, Chunk.ChunkCoord.X, Chunk.ChunkCoord.Y, DistanceToPlayer,
				(Chunk.LODLevel == 0) ? TEXT("Y") : TEXT("N"),
				bOwnerWantsAdaptive ? TEXT("Y") : TEXT("N"),
				bHasValidVoxelIntegration ? TEXT("Y") : TEXT("N"));
			GenerateAdaptiveWaterMesh(Chunk);
		}
		else
		{
			FString Reason;
			if (!bHasValidVoxelIntegration) Reason += TEXT("NoVoxel ");
			if (!bOwnerWantsAdaptive) Reason += TEXT("OwnerDisabled ");
			if (Chunk.LODLevel != 0) Reason += FString::Printf(TEXT("LOD%d "), Chunk.LODLevel);
			
			UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: 📦 PLANAR mesh for LOD%d chunk (%d, %d) at distance %.0f [Reason: %s]"), 
				Chunk.LODLevel, Chunk.ChunkCoord.X, Chunk.ChunkCoord.Y, DistanceToPlayer, *Reason.TrimEnd());
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
	
	// Adaptive resolution - use finer resolution for better water edge quality
	float Resolution = RenderSettings.MeshResolution;
	
	// Further reduce resolution for even smoother edges (higher vertex density)
	// This gives us approximately 256 vertices per side with 50cm resolution
	Resolution = FMath::Min(Resolution, 50.0f);
	
	const float WaterLevel = Chunk.WaterLevel;
	
	// Use higher resolution for LOD0 adaptive meshes
	// Ensure higher resolution mesh with more vertices for smoother water edges
	// Increased minimum from 16 to 32 for better quality, max of 256 for performance
	const int32 VertsPerSide = FMath::Clamp(FMath::CeilToInt(RenderSettings.RenderChunkSize / Resolution), 32, 256);
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
	
	// Batch sample all terrain heights for better performance
	TArray<FVector> SamplePositions;
	SamplePositions.Reserve(VertsPerSide * VertsPerSide);
	
	for (int32 Y = 0; Y < VertsPerSide; ++Y)
	{
		for (int32 X = 0; X < VertsPerSide; ++X)
		{
			const float WorldX = Bounds.Min.X + X * StepSize;
			const float WorldY = Bounds.Min.Y + Y * StepSize;
			SamplePositions.Add(FVector(WorldX, WorldY, 0.0f));
		}
	}
	
	// Batch sample terrain heights
	TArray<float> BatchHeights;
	if (VoxelIntegration && VoxelIntegration->IsValidLowLevel())
	{
		if (VoxelIntegration->IsVoxelWorldValid())
		{
			BatchHeights = VoxelIntegration->SampleVoxelHeightsBatch(SamplePositions);
			UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: Batch sampled %d/%d terrain heights for chunk (%d, %d), WaterLevel: %.1f"), 
				BatchHeights.Num(), SamplePositions.Num(), Chunk.ChunkCoord.X, Chunk.ChunkCoord.Y, WaterLevel);
				
			// Log some sample heights for debugging
			if (BatchHeights.Num() > 0)
			{
				float MinHeight = BatchHeights[0];
				float MaxHeight = BatchHeights[0];
				for (float Height : BatchHeights)
				{
					MinHeight = FMath::Min(MinHeight, Height);
					MaxHeight = FMath::Max(MaxHeight, Height);
				}
				UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: Terrain height range for chunk (%d, %d): %.1f to %.1f"), 
					Chunk.ChunkCoord.X, Chunk.ChunkCoord.Y, MinHeight, MaxHeight);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: VoxelIntegration has no valid voxel world for chunk (%d, %d)"), 
				Chunk.ChunkCoord.X, Chunk.ChunkCoord.Y);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: No valid VoxelIntegration for terrain sampling for chunk (%d, %d)"), 
			Chunk.ChunkCoord.X, Chunk.ChunkCoord.Y);
	}
	
	// Create a regular grid of vertices - always create all vertices for proper UV mapping
	int32 SampleIndex = 0;
	for (int32 Y = 0; Y < VertsPerSide; ++Y)
	{
		for (int32 X = 0; X < VertsPerSide; ++X)
		{
			const float WorldX = Bounds.Min.X + X * StepSize;
			const float WorldY = Bounds.Min.Y + Y * StepSize;
			
			// Get terrain height from batch sampling or use default
			float TerrainHeight = WaterLevel - 100.0f; // Default to below water if no terrain data
			if (SampleIndex < BatchHeights.Num())
			{
				TerrainHeight = BatchHeights[SampleIndex];
			}
			SampleIndex++;
			
			// Store the terrain height for later use when creating triangles
			VertexTerrainHeights.Add(TerrainHeight);
			
			// Water leveling with smooth transitions at edges
			float WaterSurfaceHeight = WaterLevel;
			
			// Calculate how much the terrain is above water level
			float TerrainAboveWater = TerrainHeight - WaterLevel;
			
			// Create smooth transition zones with more aggressive overshoot into terrain
			if (TerrainAboveWater > 50.0f)  // Terrain is well above water - increased threshold
			{
				// Mark as invalid - no water should be here
				WaterSurfaceHeight = -99999.0f;
			}
			else if (TerrainAboveWater > -50.0f)  // Wider transition zone: -50cm to +50cm for more overshoot
			{
				// Smooth falloff at water edges with more aggressive push into terrain
				// Map the range [-50, 50] to [1, 0] for smooth blending
				float TransitionRange = 100.0f;  // Much wider transition distance for overshoot
				float NormalizedHeight = (TerrainAboveWater + 50.0f) / TransitionRange;
				NormalizedHeight = FMath::Clamp(NormalizedHeight, 0.0f, 1.0f);
				
				// Use a more aggressive curve for water to push into terrain
				// Power of 3 creates more aggressive overshoot while maintaining smoothness
				float FalloffFactor = 1.0f - FMath::Pow(NormalizedHeight, 3.0f);
				
				// Apply the falloff - water can drop more to flow into carved areas
				// Increased drop from 15 to 30 for more aggressive flow
				WaterSurfaceHeight = WaterLevel - (1.0f - FalloffFactor) * 30.0f;
				
				// Allow water to get closer to terrain for better coverage
				// Reduced clearance from 5 to 2 units
				if (WaterSurfaceHeight > TerrainHeight - 2.0f)
				{
					WaterSurfaceHeight = -99999.0f;
				}
			}
			// else: terrain is well below water, keep water at its natural level
			
			TempVertices.Add(FVector(WorldX, WorldY, WaterSurfaceHeight));
			TempUVs.Add(FVector2D((float)X / (VertsPerSide - 1), (float)Y / (VertsPerSide - 1)));
			TempNormals.Add(FVector(0, 0, 1));
		}
	}
	
	// Apply smoothing pass to vertex heights for more natural water flow
	// This helps eliminate harsh diagonal patterns
	TArray<float> SmoothedHeights;
	SmoothedHeights.SetNum(TempVertices.Num());
	for (int32 i = 0; i < TempVertices.Num(); i++)
	{
		SmoothedHeights[i] = TempVertices[i].Z;
	}
	
	// Apply multiple smoothing iterations for smoother results
	const int32 SmoothIterations = 2;
	for (int32 Iter = 0; Iter < SmoothIterations; Iter++)
	{
		TArray<float> NewHeights = SmoothedHeights;
		
		for (int32 Y = 1; Y < VertsPerSide - 1; ++Y)
		{
			for (int32 X = 1; X < VertsPerSide - 1; ++X)
			{
				const int32 Index = Y * VertsPerSide + X;
				const float TerrainHeight = VertexTerrainHeights[Index];
				
				// Only smooth valid water vertices (not marked as invalid)
				if (SmoothedHeights[Index] > -99000.0f)
				{
					// Sample neighboring heights for smoothing
					float NeighborSum = 0.0f;
					float NeighborWeight = 0.0f;
					
					// 3x3 kernel with distance-based weights
					for (int32 DY = -1; DY <= 1; DY++)
					{
						for (int32 DX = -1; DX <= 1; DX++)
						{
							const int32 NeighborIndex = (Y + DY) * VertsPerSide + (X + DX);
							const float NeighborTerrain = VertexTerrainHeights[NeighborIndex];
							
							// Only include neighbors that are also water
							if (SmoothedHeights[NeighborIndex] > NeighborTerrain - 50.0f)
							{
								float Weight = (DX == 0 && DY == 0) ? 4.0f : (FMath::Abs(DX) + FMath::Abs(DY) == 1 ? 2.0f : 1.0f);
								NeighborSum += SmoothedHeights[NeighborIndex] * Weight;
								NeighborWeight += Weight;
							}
						}
					}
					
					if (NeighborWeight > 0.0f)
					{
						// Blend with original height to preserve some detail
						float SmoothedHeight = FMath::Lerp(SmoothedHeights[Index], NeighborSum / NeighborWeight, 0.5f);
						
						// IMPORTANT: Don't let water climb above its natural level!
						// Water finds its level and stays flat
						if (SmoothedHeight > WaterLevel + 25.0f) // Allow tiny meniscus
						{
							SmoothedHeight = FMath::Min(SmoothedHeight, WaterLevel);
						}
						
						NewHeights[Index] = SmoothedHeight;
					}
				}
			}
		}
		
		SmoothedHeights = NewHeights;
	}
	
	// Apply smoothed heights back to vertices
	for (int32 i = 0; i < TempVertices.Num(); i++)
	{
		TempVertices[i].Z = SmoothedHeights[i];
	}
	
	// Add subtle horizontal displacement to break up grid pattern at water edges
	for (int32 Y = 1; Y < VertsPerSide - 1; ++Y)
	{
		for (int32 X = 1; X < VertsPerSide - 1; ++X)
		{
			const int32 Index = Y * VertsPerSide + X;
			const float TerrainHeight = VertexTerrainHeights[Index];
			const float WaterHeight = TempVertices[Index].Z;
			
			// Check if this vertex is near a water edge
			bool bIsNearEdge = false;
			for (int32 DY = -1; DY <= 1; DY++)
			{
				for (int32 DX = -1; DX <= 1; DX++)
				{
					if (DX == 0 && DY == 0) continue;
					const int32 NeighborIndex = (Y + DY) * VertsPerSide + (X + DX);
					const float NeighborTerrain = VertexTerrainHeights[NeighborIndex];
					
					// Check if neighbor is above water while we're at water level
					if (WaterHeight > TerrainHeight - 50.0f && NeighborTerrain > WaterLevel + 100.0f)
					{
						bIsNearEdge = true;
						break;
					}
				}
				if (bIsNearEdge) break;
			}
			
			// Apply small displacement at edges to create more organic boundaries
			if (bIsNearEdge)
			{
				// Use position-based pseudo-random offset
				float Hash = FMath::Frac(FMath::Sin(Index * 12.9898f + Y * 78.233f) * 43758.5453f);
				float DisplacementX = (Hash - 0.5f) * StepSize * 0.15f; // 15% of grid size max
				Hash = FMath::Frac(FMath::Sin(Index * 45.233f + X * 12.898f) * 93758.5453f);
				float DisplacementY = (Hash - 0.5f) * StepSize * 0.15f;
				
				TempVertices[Index].X += DisplacementX;
				TempVertices[Index].Y += DisplacementY;
			}
		}
	}
	
	// Recalculate normals based on smoothed vertex positions
	for (int32 Y = 0; Y < VertsPerSide; ++Y)
	{
		for (int32 X = 0; X < VertsPerSide; ++X)
		{
			const int32 Index = Y * VertsPerSide + X;
			FVector Normal = FVector(0, 0, 1); // Default up normal
			
			// Calculate normal from neighboring vertices for smoother shading
			if (X > 0 && X < VertsPerSide - 1 && Y > 0 && Y < VertsPerSide - 1)
			{
				const FVector& Center = TempVertices[Index];
				const FVector& Left = TempVertices[Index - 1];
				const FVector& Right = TempVertices[Index + 1];
				const FVector& Up = TempVertices[Index - VertsPerSide];
				const FVector& Down = TempVertices[Index + VertsPerSide];
				
				// Calculate two tangent vectors
				FVector TangentX = (Right - Left).GetSafeNormal();
				FVector TangentY = (Down - Up).GetSafeNormal();
				
				// Cross product gives normal
				Normal = FVector::CrossProduct(TangentX, TangentY).GetSafeNormal();
				
				// Ensure normal points up
				if (Normal.Z < 0) Normal *= -1.0f;
			}
			
			TempNormals[Index] = Normal;
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
			
			// Water stays level - only create triangles where terrain is actually below water
			int32 ValidWaterCorners = 0;
			const float WaterThreshold = WaterLevel + 50.0f; // Small tolerance for meniscus
			
			// Count corners where terrain is below water level
			if (TerrainHeight1 < WaterThreshold) ValidWaterCorners++;
			if (TerrainHeight2 < WaterThreshold) ValidWaterCorners++;
			if (TerrainHeight3 < WaterThreshold) ValidWaterCorners++;
			if (TerrainHeight4 < WaterThreshold) ValidWaterCorners++;
			
			// Check for our special marker indicating "no water here"
			const bool bVertex1Valid = TempVertices[VertIndex].Z > -90000.0f;
			const bool bVertex2Valid = TempVertices[VertIndex + 1].Z > -90000.0f;
			const bool bVertex3Valid = TempVertices[NextRowIndex].Z > -90000.0f;
			const bool bVertex4Valid = TempVertices[NextRowIndex + 1].Z > -90000.0f;
			
			// Only create triangles if we have enough valid vertices
			// We need at least 3 valid vertices to form a triangle
			int32 ValidVertexCount = 0;
			if (bVertex1Valid) ValidVertexCount++;
			if (bVertex2Valid) ValidVertexCount++;
			if (bVertex3Valid) ValidVertexCount++;
			if (bVertex4Valid) ValidVertexCount++;
			
			// Need terrain below water AND valid vertex heights
			const bool bHasWater = ValidWaterCorners >= 2 && ValidVertexCount >= 3;
			
			if (bHasWater)
			{
				// Create triangles only with valid vertices
				// Check each triangle individually
				
				// Triangle 1: vertices 1, 3, 2
				if (bVertex1Valid && bVertex3Valid && bVertex2Valid)
				{
					TempTriangles.Add(VertIndex);
					TempTriangles.Add(NextRowIndex);
					TempTriangles.Add(VertIndex + 1);
				}
				
				// Triangle 2: vertices 2, 3, 4
				if (bVertex2Valid && bVertex3Valid && bVertex4Valid)
				{
					TempTriangles.Add(VertIndex + 1);
					TempTriangles.Add(NextRowIndex);
					TempTriangles.Add(NextRowIndex + 1);
				}
				// If one triangle is invalid but we have 3 valid verts, try alternate triangulation
				else if (!bVertex4Valid && bVertex1Valid && bVertex2Valid && bVertex3Valid)
				{
					// Can still make one triangle with vertices 1, 2, 3
					// Already added as Triangle 1
				}
				else if (!bVertex1Valid && bVertex2Valid && bVertex3Valid && bVertex4Valid)
				{
					// Make one triangle with vertices 2, 3, 4
					TempTriangles.Add(VertIndex + 1);
					TempTriangles.Add(NextRowIndex);
					TempTriangles.Add(NextRowIndex + 1);
				}
			}
		}
	}
	
	// Before storing, check if this chunk has a valid water source nearby
	// This prevents isolated water from spawning in dry areas
	bool bHasValidWaterSource = false;
	if (TempTriangles.Num() > 0)
	{
		// Check if any significant portion of the chunk is below water level
		int32 VerticesBelowWater = 0;
		int32 ValidVertices = 0;
		
		for (int32 i = 0; i < TempVertices.Num(); i++)
		{
			// Skip invalid vertices
			if (TempVertices[i].Z > -90000.0f)
			{
				ValidVertices++;
				if (VertexTerrainHeights.IsValidIndex(i))
				{
					// Check if terrain is below water level with some tolerance
					if (VertexTerrainHeights[i] < WaterLevel - 10.0f)
					{
						VerticesBelowWater++;
					}
				}
			}
		}
		
		// Need at least 5% of valid vertices to be below water to consider this a water area
		// Reduced threshold for better overshoot into carved areas
		float WaterCoverage = ValidVertices > 0 ? (float)VerticesBelowWater / (float)ValidVertices : 0.0f;
		bHasValidWaterSource = WaterCoverage > 0.05f;  // Reduced from 0.1f to 0.05f
		
		// Additional check: if we have very few triangles compared to potential area, likely isolated
		int32 MaxPossibleTriangles = (VertsPerSide - 1) * (VertsPerSide - 1) * 2;
		float TriangleDensity = (float)(TempTriangles.Num() / 3) / (float)MaxPossibleTriangles;
		
		// If triangle density is too low, likely just isolated water pockets
		// Reduced threshold to allow more aggressive water flow
		if (TriangleDensity < 0.02f)  // Reduced from 5% to 2% coverage
		{
			bHasValidWaterSource = false;
		}
	}
	
	// Store the generated mesh data only if we have a valid water source
	if (bHasValidWaterSource)
	{
		Chunk.Vertices = TempVertices;
		Chunk.Triangles = TempTriangles;
		Chunk.Normals = TempNormals;
		Chunk.UVs = TempUVs;
		Chunk.bHasWater = true;
	}
	else
	{
		// Clear the chunk - no valid water here
		Chunk.Vertices.Empty();
		Chunk.Triangles.Empty();
		Chunk.Normals.Empty();
		Chunk.UVs.Empty();
		Chunk.bHasWater = false;
	}
	
	// Always log adaptive mesh generation for debugging
	UE_LOG(LogTemp, Warning, TEXT("StaticWaterRenderer: Generated ADAPTIVE mesh for chunk (%d, %d) with %d vertices, %d triangles, HasWater: %s"), 
		Chunk.ChunkCoord.X, Chunk.ChunkCoord.Y, TempVertices.Num(), TempTriangles.Num() / 3, 
		(TempTriangles.Num() > 0) ? TEXT("YES") : TEXT("NO"));
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