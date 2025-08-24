#include "StaticWater/StaticWaterGenerator.h"
#include "VoxelIntegration/VoxelFluidIntegration.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

UStaticWaterGenerator::UStaticWaterGenerator()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetComponentTickInterval(0.1f); // 10 FPS by default
}

void UStaticWaterGenerator::BeginPlay()
{
	Super::BeginPlay();
	
	// Find VoxelFluidIntegration component on the same actor
	if (AActor* Owner = GetOwner())
	{
		VoxelIntegration = Owner->FindComponentByClass<UVoxelFluidIntegration>();
		if (!VoxelIntegration)
		{
			UE_LOG(LogTemp, Warning, TEXT("StaticWaterGenerator: No VoxelFluidIntegration found on actor %s"), *Owner->GetName());
		}
	}

	if (GenerationSettings.bUseGPUGeneration)
	{
		InitializeGPUResources();
	}

	bIsInitialized = true;
	
	// Initial tile generation around viewer
	RegenerateAroundViewer();
}

void UStaticWaterGenerator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GenerationSettings.bUseGPUGeneration)
	{
		ReleaseGPUResources();
	}
	
	Super::EndPlay(EndPlayReason);
}

void UStaticWaterGenerator::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	if (!bIsInitialized)
		return;

	UpdateTileGeneration(DeltaTime);

#if WITH_EDITOR
	if (bShowTileBounds || bShowWaterRegions)
	{
		DrawDebugInfo();
	}
#endif
}

void UStaticWaterGenerator::SetVoxelWorld(AActor* InVoxelWorld)
{
	TargetVoxelWorld = InVoxelWorld;
	VoxelWorldPtr = InVoxelWorld;
	
	// Clear cache when voxel world changes
	{
		FScopeLock Lock(&TileCacheMutex);
		LoadedTiles.Empty();
		ActiveTileCoords.Empty();
	}
	
	RegenerateAroundViewer();
}

void UStaticWaterGenerator::AddWaterRegion(const FStaticWaterRegionDef& Region)
{
	WaterRegions.Add(Region);
	
	// Mark affected tiles for regeneration
	const FBox& Bounds = Region.Bounds;
	const FIntVector MinTile = WorldPositionToTileCoord(Bounds.Min);
	const FIntVector MaxTile = WorldPositionToTileCoord(Bounds.Max);
	
	FScopeLock Lock(&TileCacheMutex);
	for (int32 X = MinTile.X; X <= MaxTile.X; ++X)
	{
		for (int32 Y = MinTile.Y; Y <= MaxTile.Y; ++Y)
		{
			const FIntVector TileCoord(X, Y, 0);
			if (FStaticWaterTile* Tile = LoadedTiles.Find(TileCoord))
			{
				Tile->bNeedsUpdate = true;
			}
		}
	}
}

void UStaticWaterGenerator::RemoveWaterRegion(int32 RegionIndex)
{
	if (!WaterRegions.IsValidIndex(RegionIndex))
		return;
		
	const FStaticWaterRegionDef RemovedRegion = WaterRegions[RegionIndex];
	WaterRegions.RemoveAt(RegionIndex);
	
	// Mark affected tiles for regeneration
	const FBox& Bounds = RemovedRegion.Bounds;
	const FIntVector MinTile = WorldPositionToTileCoord(Bounds.Min);
	const FIntVector MaxTile = WorldPositionToTileCoord(Bounds.Max);
	
	FScopeLock Lock(&TileCacheMutex);
	for (int32 X = MinTile.X; X <= MaxTile.X; ++X)
	{
		for (int32 Y = MinTile.Y; Y <= MaxTile.Y; ++Y)
		{
			const FIntVector TileCoord(X, Y, 0);
			if (FStaticWaterTile* Tile = LoadedTiles.Find(TileCoord))
			{
				Tile->bNeedsUpdate = true;
			}
		}
	}
}

void UStaticWaterGenerator::ClearAllWaterRegions()
{
	WaterRegions.Empty();
	
	// Mark all loaded tiles for regeneration
	FScopeLock Lock(&TileCacheMutex);
	for (auto& TilePair : LoadedTiles)
	{
		TilePair.Value.bNeedsUpdate = true;
	}
}

void UStaticWaterGenerator::SetViewerPosition(const FVector& Position)
{
	ViewerPosition = Position;
}

void UStaticWaterGenerator::RegenerateAroundViewer()
{
	if (!bIsInitialized)
		return;
		
	UpdateActiveTiles();
}

void UStaticWaterGenerator::ForceRegenerateAll()
{
	FScopeLock Lock(&TileCacheMutex);
	for (auto& TilePair : LoadedTiles)
	{
		TilePair.Value.bNeedsUpdate = true;
	}
}

void UStaticWaterGenerator::OnTerrainChanged(const FBox& ChangedBounds)
{
	// Mark affected tiles for regeneration
	const FIntVector MinTile = WorldPositionToTileCoord(ChangedBounds.Min);
	const FIntVector MaxTile = WorldPositionToTileCoord(ChangedBounds.Max);
	
	FScopeLock Lock(&TileCacheMutex);
	for (int32 X = MinTile.X; X <= MaxTile.X; ++X)
	{
		for (int32 Y = MinTile.Y; Y <= MaxTile.Y; ++Y)
		{
			const FIntVector TileCoord(X, Y, 0);
			if (FStaticWaterTile* Tile = LoadedTiles.Find(TileCoord))
			{
				Tile->bNeedsUpdate = true;
				
				if (bEnableLogging)
				{
					UE_LOG(LogTemp, Log, TEXT("StaticWaterGenerator: Marked tile (%d, %d) for regeneration due to terrain change"), TileCoord.X, TileCoord.Y);
				}
			}
		}
	}
}

bool UStaticWaterGenerator::HasStaticWaterAtLocation(const FVector& WorldPosition) const
{
	float WaterLevel;
	return EvaluateWaterAtPosition(WorldPosition, WaterLevel);
}

float UStaticWaterGenerator::GetWaterLevelAtLocation(const FVector& WorldPosition) const
{
	float WaterLevel;
	if (EvaluateWaterAtPosition(WorldPosition, WaterLevel))
	{
		return WaterLevel;
	}
	return -MAX_flt;
}

float UStaticWaterGenerator::GetWaterDepthAtLocation(const FVector& WorldPosition) const
{
	const FStaticWaterRegionDef* Region = GetHighestPriorityRegionAtPosition(WorldPosition);
	if (Region)
	{
		return Region->GetWaterDepthAtPoint(WorldPosition);
	}
	return 0.0f;
}

TArray<FIntVector> UStaticWaterGenerator::GetActiveTileCoords() const
{
	FScopeLock Lock(&TileCacheMutex);
	return ActiveTileCoords.Array();
}

void UStaticWaterGenerator::UpdateTileGeneration(float DeltaTime)
{
	TileUpdateTimer += DeltaTime;
	
	if (TileUpdateTimer >= GenerationSettings.UpdateFrequency)
	{
		TileUpdateTimer = 0.0f;
		UpdateActiveTiles();
	}
	
	// Process tile load/unload queues
	TilesGeneratedThisFrame = 0;
	
	// Load new tiles
	while (!TileLoadQueue.IsEmpty() && TilesGeneratedThisFrame < GenerationSettings.MaxTilesPerFrame)
	{
		FIntVector TileCoord;
		if (TileLoadQueue.Dequeue(TileCoord))
		{
			LoadTile(TileCoord);
			++TilesGeneratedThisFrame;
		}
	}
	
	// Unload distant tiles
	while (!TileUnloadQueue.IsEmpty())
	{
		FIntVector TileCoord;
		if (TileUnloadQueue.Dequeue(TileCoord))
		{
			UnloadTile(TileCoord);
		}
	}
	
	// Generate tile data for tiles that need updates
	FScopeLock Lock(&TileCacheMutex);
	for (auto& TilePair : LoadedTiles)
	{
		FStaticWaterTile& Tile = TilePair.Value;
		if (Tile.bNeedsUpdate && TilesGeneratedThisFrame < GenerationSettings.MaxTilesPerFrame)
		{
			GenerateTileData(Tile);
			Tile.bNeedsUpdate = false;
			++TilesGeneratedThisFrame;
		}
	}
}

void UStaticWaterGenerator::GenerateTileData(FStaticWaterTile& Tile)
{
	const double StartTime = FPlatformTime::Seconds();
	
	if (GenerationSettings.bUseGPUGeneration && bGPUResourcesInitialized)
	{
		GenerateTileDataGPU(Tile);
	}
	else
	{
		GenerateTileDataCPU(Tile);
	}
	
	LastGenerationTime = FPlatformTime::Seconds() - StartTime;
	
	if (bEnableLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("StaticWaterGenerator: Generated tile (%d, %d) in %.3fms"), 
			Tile.TileCoord.X, Tile.TileCoord.Y, LastGenerationTime * 1000.0f);
	}
}

void UStaticWaterGenerator::GenerateTileDataGPU(FStaticWaterTile& Tile)
{
	// TODO: Implement GPU generation using compute shader
	// For now, fall back to CPU generation
	GenerateTileDataCPU(Tile);
}

void UStaticWaterGenerator::GenerateTileDataCPU(FStaticWaterTile& Tile)
{
	const int32 CellsPerTile = FMath::CeilToInt(GenerationSettings.TileSize / GenerationSettings.CellSize);
	const float CellSize = GenerationSettings.CellSize;
	
	// Sample terrain heights on game thread (VoxelPlugin requirement)
	// Cannot use ParallelFor because VoxelPlugin operations must be on game thread
	for (int32 Index = 0; Index < CellsPerTile * CellsPerTile; ++Index)
	{
		const int32 X = Index % CellsPerTile;
		const int32 Y = Index / CellsPerTile;
		
		const FVector WorldPos = FVector(
			Tile.WorldBounds.Min.X + X * CellSize + CellSize * 0.5f,
			Tile.WorldBounds.Min.Y + Y * CellSize + CellSize * 0.5f,
			0.0f
		);
		
		float TerrainHeight;
		if (SampleTerrainHeight(WorldPos, TerrainHeight))
		{
			Tile.TerrainHeights[Index] = TerrainHeight;
		}
		else
		{
			Tile.TerrainHeights[Index] = Tile.WorldBounds.Min.Z;
		}
		
		// Calculate water depth at this position
		const FStaticWaterRegionDef* Region = GetHighestPriorityRegionAtPosition(WorldPos);
		if (Region)
		{
			const float WaterDepth = Region->GetWaterDepthAtPoint(FVector(WorldPos.X, WorldPos.Y, TerrainHeight));
			Tile.WaterDepths[Index] = FMath::Max(0.0f, WaterDepth);
			
			if (WaterDepth > 0.0f)
			{
				Tile.bHasWater = true;
				Tile.WaterLevel = FMath::Max(Tile.WaterLevel, Region->WaterLevel);
			}
		}
		else
		{
			Tile.WaterDepths[Index] = 0.0f;
		}
	}
}

bool UStaticWaterGenerator::SampleTerrainHeight(const FVector& WorldPosition, float& OutHeight) const
{
	if (!VoxelIntegration || !VoxelIntegration->IsValidLowLevel())
	{
		OutHeight = 0.0f;
		return false;
	}
	
	// Sample terrain height from voxel world
	if (!VoxelIntegration || !VoxelIntegration->IsValidLowLevel())
	{
		OutHeight = 0.0f;
		return false;
	}
	
	// Try to get terrain height through VoxelIntegration
	// First check if the voxel world is valid
	if (!VoxelIntegration->IsVoxelWorldValid())
	{
		OutHeight = 0.0f;
		return false;
	}
	
	// Sample the voxel world at this position
	float SampleHeight = VoxelIntegration->SampleVoxelHeight(WorldPosition.X, WorldPosition.Y);
	OutHeight = SampleHeight;
	return true;
}

void UStaticWaterGenerator::SampleTerrainHeightsInBounds(const FBox& Bounds, int32 Resolution, TArray<float>& OutHeights) const
{
	OutHeights.SetNumZeroed(Resolution * Resolution);
	
	const FVector BoundsSize = Bounds.GetSize();
	const float CellSizeX = BoundsSize.X / Resolution;
	const float CellSizeY = BoundsSize.Y / Resolution;
	
	// Cannot use ParallelFor due to VoxelPlugin game thread requirement
	for (int32 Index = 0; Index < Resolution * Resolution; ++Index)
	{
		const int32 X = Index % Resolution;
		const int32 Y = Index / Resolution;
		
		const FVector WorldPos = FVector(
			Bounds.Min.X + X * CellSizeX + CellSizeX * 0.5f,
			Bounds.Min.Y + Y * CellSizeY + CellSizeY * 0.5f,
			0.0f
		);
		
		float Height;
		if (SampleTerrainHeight(WorldPos, Height))
		{
			OutHeights[Index] = Height;
		}
		else
		{
			OutHeights[Index] = Bounds.Min.Z;
		}
	}
}

FIntVector UStaticWaterGenerator::WorldPositionToTileCoord(const FVector& WorldPosition) const
{
	return FIntVector(
		FMath::FloorToInt(WorldPosition.X / GenerationSettings.TileSize),
		FMath::FloorToInt(WorldPosition.Y / GenerationSettings.TileSize),
		0
	);
}

FVector UStaticWaterGenerator::TileCoordToWorldPosition(const FIntVector& TileCoord) const
{
	return FVector(
		TileCoord.X * GenerationSettings.TileSize,
		TileCoord.Y * GenerationSettings.TileSize,
		0.0f
	);
}

void UStaticWaterGenerator::UpdateActiveTiles()
{
	const float MaxDistance = GenerationSettings.MaxGenerationDistance;
	const float TileSize = GenerationSettings.TileSize;
	const int32 TileRadius = FMath::CeilToInt(MaxDistance / TileSize);
	
	const FIntVector ViewerTile = WorldPositionToTileCoord(ViewerPosition);
	
	TSet<FIntVector> NewActiveTiles;
	
	// Determine which tiles should be active
	for (int32 X = -TileRadius; X <= TileRadius; ++X)
	{
		for (int32 Y = -TileRadius; Y <= TileRadius; ++Y)
		{
			const FIntVector TileCoord = ViewerTile + FIntVector(X, Y, 0);
			const FVector TileCenter = TileCoordToWorldPosition(TileCoord) + FVector(TileSize * 0.5f, TileSize * 0.5f, 0.0f);
			const float Distance = FVector::Dist2D(ViewerPosition, TileCenter);
			
			if (Distance <= MaxDistance)
			{
				NewActiveTiles.Add(TileCoord);
			}
		}
	}
	
	// Find tiles to load and unload
	FScopeLock Lock(&TileCacheMutex);
	
	// Queue tiles for loading
	for (const FIntVector& TileCoord : NewActiveTiles)
	{
		if (!ActiveTileCoords.Contains(TileCoord) && !LoadedTiles.Contains(TileCoord))
		{
			TileLoadQueue.Enqueue(TileCoord);
		}
	}
	
	// Queue tiles for unloading
	TArray<FIntVector> TilesToUnload;
	for (const FIntVector& TileCoord : ActiveTileCoords)
	{
		if (!NewActiveTiles.Contains(TileCoord))
		{
			TilesToUnload.Add(TileCoord);
		}
	}
	
	for (const FIntVector& TileCoord : TilesToUnload)
	{
		TileUnloadQueue.Enqueue(TileCoord);
	}
	
	ActiveTileCoords = MoveTemp(NewActiveTiles);
	
	// Enforce cache limit
	if (LoadedTiles.Num() > GenerationSettings.MaxCachedTiles)
	{
		// Remove oldest tiles (simple FIFO for now)
		auto Iterator = LoadedTiles.CreateIterator();
		int32 TilesToRemove = LoadedTiles.Num() - GenerationSettings.MaxCachedTiles;
		
		while (Iterator && TilesToRemove > 0)
		{
			if (!ActiveTileCoords.Contains(Iterator.Key()))
			{
				Iterator.RemoveCurrent();
				--TilesToRemove;
			}
			else
			{
				++Iterator;
			}
		}
	}
}

void UStaticWaterGenerator::LoadTile(const FIntVector& TileCoord)
{
	FScopeLock Lock(&TileCacheMutex);
	
	if (LoadedTiles.Contains(TileCoord))
		return;
		
	FStaticWaterTile& NewTile = LoadedTiles.Add(TileCoord);
	NewTile.Initialize(TileCoord, GenerationSettings.TileSize, GenerationSettings.CellSize);
	NewTile.bNeedsUpdate = true;
	
	if (bEnableLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("StaticWaterGenerator: Loaded tile (%d, %d)"), TileCoord.X, TileCoord.Y);
	}
}

void UStaticWaterGenerator::UnloadTile(const FIntVector& TileCoord)
{
	FScopeLock Lock(&TileCacheMutex);
	
	if (LoadedTiles.Remove(TileCoord) > 0)
	{
		if (bEnableLogging)
		{
			UE_LOG(LogTemp, Log, TEXT("StaticWaterGenerator: Unloaded tile (%d, %d)"), TileCoord.X, TileCoord.Y);
		}
	}
	
	ActiveTileCoords.Remove(TileCoord);
}

bool UStaticWaterGenerator::ShouldLoadTile(const FIntVector& TileCoord) const
{
	const FVector TileCenter = TileCoordToWorldPosition(TileCoord) + FVector(GenerationSettings.TileSize * 0.5f);
	const float Distance = FVector::Dist2D(ViewerPosition, TileCenter);
	return Distance <= GenerationSettings.MaxGenerationDistance;
}

bool UStaticWaterGenerator::ShouldUnloadTile(const FIntVector& TileCoord) const
{
	return !ShouldLoadTile(TileCoord);
}

bool UStaticWaterGenerator::EvaluateWaterAtPosition(const FVector& Position, float& OutWaterLevel) const
{
	const FStaticWaterRegionDef* Region = GetHighestPriorityRegionAtPosition(Position);
	if (Region)
	{
		OutWaterLevel = Region->WaterLevel;
		return true;
	}
	
	OutWaterLevel = -MAX_flt;
	return false;
}

const FStaticWaterRegionDef* UStaticWaterGenerator::GetHighestPriorityRegionAtPosition(const FVector& Position) const
{
	const FStaticWaterRegionDef* BestRegion = nullptr;
	int32 HighestPriority = INT_MIN;
	
	for (const FStaticWaterRegionDef& Region : WaterRegions)
	{
		if (Region.ContainsPoint(Position) && Region.Priority > HighestPriority)
		{
			BestRegion = &Region;
			HighestPriority = Region.Priority;
		}
	}
	
	return BestRegion;
}

void UStaticWaterGenerator::InitializeGPUResources()
{
	// TODO: Initialize compute shader resources
	// This would include structured buffers, constant buffers, etc.
	bGPUResourcesInitialized = false; // Set to true when implemented
}

void UStaticWaterGenerator::ReleaseGPUResources()
{
	if (bGPUResourcesInitialized)
	{
		// TODO: Release GPU resources
		RenderFence.BeginFence();
		RenderFence.Wait();
		bGPUResourcesInitialized = false;
	}
}

void UStaticWaterGenerator::DrawDebugInfo() const
{
#if WITH_EDITOR
	UWorld* World = GetWorld();
	if (!World)
		return;
		
	// Draw tile bounds
	if (bShowTileBounds)
	{
		FScopeLock Lock(&TileCacheMutex);
		for (const auto& TilePair : LoadedTiles)
		{
			const FStaticWaterTile& Tile = TilePair.Value;
			const FColor TileColor = Tile.bHasWater ? FColor::Blue : FColor(128, 128, 128);
			
			DrawDebugBox(World, Tile.WorldBounds.GetCenter(), Tile.WorldBounds.GetExtent(), 
				TileColor, false, -1.0f, 0, 10.0f);
		}
	}
	
	// Draw water regions
	if (bShowWaterRegions)
	{
		for (int32 i = 0; i < WaterRegions.Num(); ++i)
		{
			const FStaticWaterRegionDef& Region = WaterRegions[i];
			const FColor RegionColor = FColor::Cyan;
			
			// Draw region bounds
			DrawDebugBox(World, Region.Bounds.GetCenter(), Region.Bounds.GetExtent(), 
				RegionColor, false, -1.0f, 0, 5.0f);
			
			// Draw water level plane
			const FVector WaterPlaneCenter = Region.Bounds.GetCenter();
			const FVector WaterPlaneSize = FVector(Region.Bounds.GetSize().X, Region.Bounds.GetSize().Y, 10.0f);
			const FVector WaterLevelPos = FVector(WaterPlaneCenter.X, WaterPlaneCenter.Y, Region.WaterLevel);
			
			DrawDebugBox(World, WaterLevelPos, WaterPlaneSize * 0.5f, FColor::Blue, false, -1.0f, 0, 2.0f);
		}
	}
#endif
}