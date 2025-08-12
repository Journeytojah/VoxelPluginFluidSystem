#include "VoxelIntegration/VoxelFluidIntegration.h"
#include "VoxelIntegration/VoxelTerrainSampler.h"
#include "CellularAutomata/FluidChunkManager.h"
#include "CellularAutomata/FluidChunk.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Components/SceneComponent.h"
#include "VoxelFluidStats.h"
#include "VoxelLayersBlueprintLibrary.h"

UVoxelFluidIntegration::UVoxelFluidIntegration()
{
	PrimaryComponentTick.bCanEverTick = true;
	
	GridResolutionX = 128;
	GridResolutionY = 128;
	GridResolutionZ = 32;
	CellWorldSize = 100.0f;
	bAutoUpdateTerrain = true;
	TerrainUpdateInterval = 1.0f;
	// bDebugDrawCells removed - debug drawing handled by FluidVisualizationComponent
	MinFluidToRender = 0.01f;
}

void UVoxelFluidIntegration::BeginPlay()
{
	Super::BeginPlay();
	
	// Check if using chunked system or grid system
	if (bUseChunkedSystem && ChunkManager)
	{
		// Chunked system handles its own initialization
		if (IsVoxelWorldValid() && bAutoUpdateTerrain)
		{
			if (bUse3DVoxelTerrain)
			{
				// Use 3D voxel terrain for caves, overhangs, and tunnels
				Update3DVoxelTerrain();
			}
			else
			{
				// Use height-based terrain
				UpdateChunkedTerrainHeights();
			}
		}
	}
	else
	{
		if (!FluidGrid)
		{
			FluidGrid = NewObject<UCAFluidGrid>(this, UCAFluidGrid::StaticClass());
		}
		
		if (FluidGrid)
		{
			FluidGrid->InitializeGrid(GridResolutionX, GridResolutionY, GridResolutionZ, CellWorldSize);
		}
		
		// GridWorldOrigin will be set when FluidGrid is initialized
		
		if (IsVoxelWorldValid() && bAutoUpdateTerrain)
		{
			if (bUse3DVoxelTerrain)
			{
				// Use 3D voxel terrain for caves, overhangs, and tunnels
				Update3DVoxelTerrain();
			}
			else
			{
				// Use height-based terrain
				UpdateTerrainHeights();
			}
		}
	}
}

void UVoxelFluidIntegration::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	// Handle automatic terrain refresh for 3D voxel terrain
	if (bUse3DVoxelTerrain && bAutoRefreshAfterSculpting)
	{
		LastTerrainRefreshTime += DeltaTime;
		
		if (LastTerrainRefreshTime >= TerrainRefreshInterval)
		{
			if (bTerrainNeedsRefresh || PendingTerrainUpdates.Num() > 0)
			{
				DetectTerrainChangesAndUpdate();
				bTerrainNeedsRefresh = false;
				LastTerrainRefreshTime = 0.0f;
			}
		}
	}
	
	// Debug drawing for solid cells
	if (bUse3DVoxelTerrain && bDebugDrawSolidCells)
	{
		DrawDebugSolidCells();
	}
}

void UVoxelFluidIntegration::InitializeFluidSystem(AActor* InVoxelWorld)
{
	VoxelWorld = InVoxelWorld;
	
	if (!FluidGrid)
	{
		FluidGrid = NewObject<UCAFluidGrid>(this, UCAFluidGrid::StaticClass());
	}
	
	if (FluidGrid)
	{
		FluidGrid->InitializeGrid(GridResolutionX, GridResolutionY, GridResolutionZ, CellWorldSize);
	}
	
	if (IsVoxelWorldValid())
	{
		SyncWithVoxelTerrain();
	}
}

void UVoxelFluidIntegration::SyncWithVoxelTerrain()
{
	if (!IsVoxelWorldValid() || !FluidGrid)
		return;
	
	UpdateTerrainHeights();
}

void UVoxelFluidIntegration::UpdateTerrainHeights()
{
	if (!FluidGrid)
		return;

	// Use optimized voxel layer sampling if available
	if (bUseVoxelLayerSampling && TerrainLayer.Layer != nullptr && SamplingMethod == EVoxelSamplingMethod::VoxelQuery)
	{
		UpdateTerrainHeightsWithVoxelLayer();
		return;
	}
	
	// Original individual sampling method
	const FVector CurrentGridOrigin = FluidGrid->GridOrigin;
	
	for (int32 x = 0; x < GridResolutionX; ++x)
	{
		for (int32 y = 0; y < GridResolutionY; ++y)
		{
			const FVector WorldPos = CurrentGridOrigin + FVector(x * CellWorldSize, y * CellWorldSize, 0);
			const float TerrainHeight = SampleVoxelHeight(WorldPos.X, WorldPos.Y);
			
			FluidGrid->SetTerrainHeight(x, y, TerrainHeight);
		}
	}
}

float UVoxelFluidIntegration::SampleVoxelHeight(float WorldX, float WorldY)
{
	FVector SampleLocation(WorldX, WorldY, 0);
	
	UObject* WorldContext = IsVoxelWorldValid() ? static_cast<UObject*>(VoxelWorld) : static_cast<UObject*>(GetWorld());
	
	if (bUseVoxelLayerSampling && TerrainLayer.Layer != nullptr)
	{
		return UVoxelTerrainSampler::SampleTerrainHeightAtLocationWithLayer(WorldContext, SampleLocation, TerrainLayer, SamplingMethod);
	}
	else
	{
		return UVoxelTerrainSampler::SampleTerrainHeightAtLocation(WorldContext, SampleLocation);
	}
}

void UVoxelFluidIntegration::UpdateTerrainHeightsWithVoxelLayer()
{
	if (!FluidGrid || !bUseVoxelLayerSampling || TerrainLayer.Layer == nullptr)
	{
		UpdateTerrainHeights(); // Fallback to original method
		return;
	}

	const FVector CurrentGridOrigin = FluidGrid->GridOrigin;
	UObject* WorldContext = IsVoxelWorldValid() ? static_cast<UObject*>(VoxelWorld) : static_cast<UObject*>(GetWorld());

	if (SamplingMethod == EVoxelSamplingMethod::VoxelQuery)
	{
		// Collect all sample positions for bulk query
		TArray<FVector> SamplePositions;
		TArray<TPair<int32, int32>> CellIndices; // Store x,y indices for each position
		
		SamplePositions.Reserve(GridResolutionX * GridResolutionY);
		CellIndices.Reserve(GridResolutionX * GridResolutionY);

		for (int32 x = 0; x < GridResolutionX; ++x)
		{
			for (int32 y = 0; y < GridResolutionY; ++y)
			{
				const FVector WorldPos = CurrentGridOrigin + FVector(x * CellWorldSize, y * CellWorldSize, 0);
				SamplePositions.Add(WorldPos);
				CellIndices.Add(TPair<int32, int32>(x, y));
			}
		}

		// Use multi-query for better performance
		TArray<float> Heights;
		TArray<FVector> Positions;
		
		UVoxelTerrainSampler::SampleTerrainInBoundsWithLayer(
			WorldContext,
			CurrentGridOrigin,
			CurrentGridOrigin + FVector(GridResolutionX * CellWorldSize, GridResolutionY * CellWorldSize, 0),
			CellWorldSize,
			TerrainLayer,
			Heights,
			Positions,
			SamplingMethod
		);

		// Apply the heights to the fluid grid
		if (Heights.Num() == CellIndices.Num())
		{
			for (int32 i = 0; i < CellIndices.Num(); ++i)
			{
				FluidGrid->SetTerrainHeight(CellIndices[i].Key, CellIndices[i].Value, Heights[i]);
			}
		}
	}
	else
	{
		// Fall back to individual sampling
		UpdateTerrainHeights();
	}
}

void UVoxelFluidIntegration::Update3DVoxelTerrain()
{
	if (bUseChunkedSystem && ChunkManager)
	{
		// Update all active chunks with 3D voxel data
		const TArray<UFluidChunk*> ActiveChunks = ChunkManager->GetActiveChunks();
		
		for (UFluidChunk* Chunk : ActiveChunks)
		{
			if (!Chunk)
				continue;
			
			UpdateChunk3DVoxelTerrain(Chunk->ChunkCoord);
		}
	}
	else if (FluidGrid)
	{
		// Update the non-chunked grid with 3D voxel data
		if (!IsVoxelWorldValid() || !TerrainLayer.Layer)
			return;
		
		UObject* WorldContext = static_cast<UObject*>(VoxelWorld);
		const FVector CurrentGridOrigin = FluidGrid->GridOrigin;
		
		int32 TotalCells = 0;
		int32 SolidCells = 0;
		int32 ChangedCells = 0;
		
		// Process each cell to determine if it's solid or empty
		for (int32 x = 0; x < GridResolutionX; ++x)
		{
			for (int32 y = 0; y < GridResolutionY; ++y)
			{
				for (int32 z = 0; z < GridResolutionZ; ++z)
				{
					// Get the world position of the cell center
					const FVector CellCenter = CurrentGridOrigin + 
						FVector((x + 0.5f) * CellWorldSize, 
							   (y + 0.5f) * CellWorldSize, 
							   (z + 0.5f) * CellWorldSize);
					
					// Check if this position is inside solid voxel terrain
					bool bWasSolid = FluidGrid->IsCellSolid(x, y, z);
					bool bIsSolid = CheckIfCellIsSolid(CellCenter, x, y, z);
					
					TotalCells++;
					if (bIsSolid) SolidCells++;
					if (bWasSolid != bIsSolid) ChangedCells++;
					
					// Log sample cells for debugging
					if (z == 5 && x % 20 == 0 && y % 20 == 0)
					{
						UE_LOG(LogTemp, Warning, TEXT("Cell[%d,%d,%d] at %s: %s -> %s"), 
							x, y, z, *CellCenter.ToString(), 
							bWasSolid ? TEXT("SOLID") : TEXT("EMPTY"),
							bIsSolid ? TEXT("SOLID") : TEXT("EMPTY"));
					}
					
					// Update the cell's solid state
					FluidGrid->SetCellSolid(x, y, z, bIsSolid);
				}
			}
		}
		
		UE_LOG(LogTemp, Warning, TEXT("Update3DVoxelTerrain: Total:%d Solid:%d Changed:%d (%.1f%% solid)"), 
			TotalCells, SolidCells, ChangedCells, (SolidCells * 100.0f) / TotalCells);
		
		// If terrain changed, force the fluid grid to re-evaluate
		if (ChangedCells > 0)
		{
			// Force wake up all fluid cells
			FluidGrid->ForceWakeAllFluid();
			
			// Force multiple simulation updates to ensure fluid flows into new spaces
			for (int32 i = 0; i < 10; i++)
			{
				FluidGrid->UpdateSimulation(0.016f);
			}
			
			UE_LOG(LogTemp, Warning, TEXT("Forced fluid re-evaluation after %d terrain changes"), ChangedCells);
		}
	}
}

void UVoxelFluidIntegration::UpdateChunk3DVoxelTerrain(const FFluidChunkCoord& ChunkCoord)
{
	if (!bUseChunkedSystem || !ChunkManager || !IsVoxelWorldValid())
		return;
	
	UFluidChunk* Chunk = ChunkManager->GetChunk(ChunkCoord);
	if (!Chunk || !TerrainLayer.Layer)
		return;
	
	UObject* WorldContext = static_cast<UObject*>(VoxelWorld);
	const int32 ChunkSize = Chunk->ChunkSize;
	const float CellSize = Chunk->CellSize;
	const FVector ChunkOrigin = Chunk->ChunkWorldPosition;
	
	// Process each cell in the chunk to determine if it's solid or empty
	int32 SolidCellCount = 0;
	for (int32 LocalX = 0; LocalX < ChunkSize; ++LocalX)
	{
		for (int32 LocalY = 0; LocalY < ChunkSize; ++LocalY)
		{
			for (int32 LocalZ = 0; LocalZ < ChunkSize; ++LocalZ)
			{
				// Get the world position of the cell center
				const FVector CellCenter = ChunkOrigin + 
					FVector((LocalX + 0.5f) * CellSize, 
						   (LocalY + 0.5f) * CellSize, 
						   (LocalZ + 0.5f) * CellSize);
				
				// Check if this position is inside solid voxel terrain
				bool bIsSolid = CheckIfCellIsSolid(CellCenter, LocalX, LocalY, LocalZ);
				
				// Update the cell's solid state
				Chunk->SetCellSolid(LocalX, LocalY, LocalZ, bIsSolid);
				
				if (bIsSolid)
					SolidCellCount++;
			}
		}
	}
	
	UE_LOG(LogTemp, Log, TEXT("UpdateChunk3DVoxelTerrain: Chunk %s updated with %d solid cells out of %d total"), 
		*ChunkCoord.ToString(), SolidCellCount, ChunkSize * ChunkSize * ChunkSize);
	
	// Mark chunk as dirty to force mesh regeneration
	Chunk->bDirty = true;
	Chunk->MarkMeshDataDirty();
}

void UVoxelFluidIntegration::AddFluidAtWorldPosition(const FVector& WorldPosition, float Amount)
{
	if (!FluidGrid)
		return;
	
	int32 CellX, CellY, CellZ;
	if (FluidGrid->GetCellFromWorldPosition(WorldPosition - FluidGrid->GridOrigin, CellX, CellY, CellZ))
	{
		FluidGrid->AddFluid(CellX, CellY, CellZ, Amount);
	}
}

void UVoxelFluidIntegration::RemoveFluidAtWorldPosition(const FVector& WorldPosition, float Amount)
{
	if (!FluidGrid)
		return;
	
	int32 CellX, CellY, CellZ;
	if (FluidGrid->GetCellFromWorldPosition(WorldPosition - FluidGrid->GridOrigin, CellX, CellY, CellZ))
	{
		FluidGrid->RemoveFluid(CellX, CellY, CellZ, Amount);
	}
}

void UVoxelFluidIntegration::DrawDebugFluid()
{
	if (!FluidGrid || !GetWorld())
		return;
	
	for (int32 x = 0; x < GridResolutionX; ++x)
	{
		for (int32 y = 0; y < GridResolutionY; ++y)
		{
			for (int32 z = 0; z < GridResolutionZ; ++z)
			{
				const float FluidLevel = FluidGrid->GetFluidAt(x, y, z);
				
				if (FluidLevel > MinFluidToRender)
				{
					const FVector CellWorldPos = FluidGrid->GetWorldPositionFromCell(x, y, z);
					const float BoxSize = CellWorldSize * 0.9f * FluidLevel;
					
					const FColor FluidColor = FColor::MakeRedToGreenColorFromScalar(1.0f - FluidLevel);
					
					DrawDebugBox(GetWorld(), CellWorldPos, FVector(BoxSize * 0.5f), FluidColor, false, -1.0f, 0, 2.0f);
				}
			}
		}
	}
}

bool UVoxelFluidIntegration::IsVoxelWorldValid() const
{
	return VoxelWorld != nullptr;
}

void UVoxelFluidIntegration::SetChunkManager(UFluidChunkManager* InChunkManager)
{
	ChunkManager = InChunkManager;
	bUseChunkedSystem = (ChunkManager != nullptr);
	
	if (bUseChunkedSystem)
	{
		FluidGrid = nullptr; // Clear grid system when using chunked system
		UE_LOG(LogTemp, Log, TEXT("VoxelFluidIntegration: Switched to chunked system"));
	}
}

void UVoxelFluidIntegration::UpdateChunkedTerrainHeights()
{
	if (!bUseChunkedSystem || !ChunkManager || !IsVoxelWorldValid())
		return;
	
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_VoxelIntegration);
	
	// Get all active chunks and update their terrain data
	const TArray<UFluidChunk*> ActiveChunks = ChunkManager->GetActiveChunks();
	
	for (UFluidChunk* Chunk : ActiveChunks)
	{
		if (!Chunk)
			continue;
		
		const FBox ChunkBounds = Chunk->GetWorldBounds();
		UpdateTerrainForChunk(ChunkBounds.Min, ChunkBounds.Max, Chunk->ChunkSize, Chunk->CellSize);
	}
}

void UVoxelFluidIntegration::UpdateTerrainForChunk(const FVector& ChunkWorldMin, const FVector& ChunkWorldMax, int32 ChunkSize, float CellSize)
{
	if (!bUseChunkedSystem || !ChunkManager)
		return;
	
	// Get the chunk from the ChunkManager
	const FFluidChunkCoord ChunkCoord = ChunkManager->GetChunkCoordFromWorldPosition(ChunkWorldMin + FVector(CellSize * 0.5f));
	UFluidChunk* Chunk = ChunkManager->GetChunk(ChunkCoord);
	
	if (!Chunk)
		return;
	
	// Sample terrain heights for this chunk and set them directly
	for (int32 LocalX = 0; LocalX < ChunkSize; ++LocalX)
	{
		for (int32 LocalY = 0; LocalY < ChunkSize; ++LocalY)
		{
			// Sample at the center of the cell for more accurate terrain collision
			const FVector WorldPos = ChunkWorldMin + FVector((LocalX + 0.5f) * CellSize, (LocalY + 0.5f) * CellSize, 0);
			const float TerrainHeight = SampleVoxelHeight(WorldPos.X, WorldPos.Y);
			
			// Set terrain height for this column - this will mark cells as solid based on terrain height
			Chunk->SetTerrainHeight(LocalX, LocalY, TerrainHeight);
		}
	}
	
	UE_LOG(LogTemp, VeryVerbose, TEXT("UpdateTerrainForChunk: Updated terrain for chunk %s"), *ChunkCoord.ToString());
}

void UVoxelFluidIntegration::UpdateTerrainForChunkCoord(const FFluidChunkCoord& ChunkCoord)
{
	if (!bUseChunkedSystem || !ChunkManager || !IsVoxelWorldValid())
		return;
	
	UFluidChunk* Chunk = ChunkManager->GetChunk(ChunkCoord);
	if (!Chunk)
		return;
	
	const FBox ChunkBounds = Chunk->GetWorldBounds();
	UpdateTerrainForChunk(ChunkBounds.Min, ChunkBounds.Max, Chunk->ChunkSize, Chunk->CellSize);
}

void UVoxelFluidIntegration::DrawChunkedDebugFluid()
{
	if (!bUseChunkedSystem || !ChunkManager || !GetWorld())
		return;
	
	const TArray<UFluidChunk*> ActiveChunks = ChunkManager->GetActiveChunks();
	
	for (UFluidChunk* Chunk : ActiveChunks)
	{
		if (!Chunk)
			continue;
		
		const int32 ChunkSizeLocal = Chunk->ChunkSize;
		const float CellSizeLocal = Chunk->CellSize;
		const FVector ChunkOrigin = Chunk->ChunkWorldPosition;
		
		// Sample every 4th cell for performance
		for (int32 x = 0; x < ChunkSizeLocal; x += 4)
		{
			for (int32 y = 0; y < ChunkSizeLocal; y += 4)
			{
				for (int32 z = 0; z < ChunkSizeLocal; z += 2)
				{
					const float FluidLevel = Chunk->GetFluidAt(x, y, z);
					
					if (FluidLevel > MinFluidToRender)
					{
						const FVector CellWorldPos = ChunkOrigin + FVector(x * CellSizeLocal, y * CellSizeLocal, z * CellSizeLocal);
						const float BoxSize = CellSizeLocal * 3.6f * FluidLevel; // Larger boxes for sparse sampling
						
						const FColor FluidColor = FColor::MakeRedToGreenColorFromScalar(1.0f - FluidLevel);
						
						DrawDebugBox(GetWorld(), CellWorldPos, FVector(BoxSize * 0.5f), FluidColor, false, -1.0f, 0, 2.0f);
					}
				}
			}
		}
	}
}

void UVoxelFluidIntegration::DetectTerrainChangesAndUpdate()
{
	if (!bUse3DVoxelTerrain)
	{
		// For height-based terrain, just update normally
		if (bUseChunkedSystem)
			UpdateChunkedTerrainHeights();
		else
			UpdateTerrainHeights();
		return;
	}
	
	// Process any pending terrain updates from OnVoxelTerrainModified
	TArray<FBox> UpdateRegions;
	{
		FScopeLock Lock(&TerrainUpdateMutex);
		UpdateRegions = PendingTerrainUpdates;
		PendingTerrainUpdates.Empty();
	}
	
	if (UpdateRegions.Num() == 0)
	{
		// No specific regions to update, do a full scan for changes
		Update3DVoxelTerrain();
		return;
	}
	
	// Update only the affected regions
	for (const FBox& Region : UpdateRegions)
	{
		UpdateTerrainInRegion(Region);
	}
}

void UVoxelFluidIntegration::OnVoxelTerrainModified(const FBox& ModifiedBounds)
{
	if (!bUse3DVoxelTerrain)
		return;
	
	// Add this region to pending updates
	{
		FScopeLock Lock(&TerrainUpdateMutex);
		PendingTerrainUpdates.Add(ModifiedBounds);
	}
	
	UE_LOG(LogTemp, Log, TEXT("OnVoxelTerrainModified: Queued terrain update for region Min:%s Max:%s"), 
		*ModifiedBounds.Min.ToString(), *ModifiedBounds.Max.ToString());
	
	// If immediate update is preferred (for responsiveness), uncomment:
	// UpdateTerrainInRegion(ModifiedBounds);
}

void UVoxelFluidIntegration::UpdateTerrainInRegion(const FBox& Region)
{
	if (bUseChunkedSystem && ChunkManager)
	{
		// Find all chunks that overlap with this region
		TArray<FFluidChunkCoord> AffectedChunks = ChunkManager->GetChunksInBounds(Region);
		
		for (const FFluidChunkCoord& ChunkCoord : AffectedChunks)
		{
			UFluidChunk* Chunk = ChunkManager->GetChunk(ChunkCoord);
			if (!Chunk)
				continue;
			
			// Update only cells within the modified region
			UpdateChunkCellsInRegion(Chunk, Region);
		}
	}
	else if (FluidGrid)
	{
		// Update grid cells within the region
		UpdateGridCellsInRegion(Region);
	}
}

void UVoxelFluidIntegration::UpdateChunkCellsInRegion(UFluidChunk* Chunk, const FBox& Region)
{
	if (!Chunk || !IsVoxelWorldValid() || !TerrainLayer.Layer)
		return;
	
	UObject* WorldContext = static_cast<UObject*>(VoxelWorld);
	const int32 ChunkSize = Chunk->ChunkSize;
	const float CellSize = Chunk->CellSize;
	const FVector ChunkOrigin = Chunk->ChunkWorldPosition;
	
	int32 UpdatedCells = 0;
	
	for (int32 LocalX = 0; LocalX < ChunkSize; ++LocalX)
	{
		for (int32 LocalY = 0; LocalY < ChunkSize; ++LocalY)
		{
			for (int32 LocalZ = 0; LocalZ < ChunkSize; ++LocalZ)
			{
				const FVector CellCenter = ChunkOrigin + 
					FVector((LocalX + 0.5f) * CellSize, 
						   (LocalY + 0.5f) * CellSize, 
						   (LocalZ + 0.5f) * CellSize);
				
				// Skip cells outside the modified region
				if (!Region.IsInside(CellCenter))
					continue;
				
				// Check if this position is inside solid voxel terrain
				bool bIsSolid = CheckIfCellIsSolid(CellCenter, LocalX, LocalY, LocalZ);
				
				// Cache the new state
				FIntVector CellKey(
					Chunk->ChunkCoord.X * ChunkSize + LocalX,
					Chunk->ChunkCoord.Y * ChunkSize + LocalY,
					Chunk->ChunkCoord.Z * ChunkSize + LocalZ
				);
				
				bool* CachedState = CachedVoxelStates.Find(CellKey);
				if (!CachedState || *CachedState != bIsSolid)
				{
					// State changed, update the cell
					Chunk->SetCellSolid(LocalX, LocalY, LocalZ, bIsSolid);
					CachedVoxelStates.Add(CellKey, bIsSolid);
					UpdatedCells++;
				}
			}
		}
	}
	
	if (UpdatedCells > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("UpdateChunkCellsInRegion: Updated %d cells in chunk %s"), 
			UpdatedCells, *Chunk->ChunkCoord.ToString());
	}
}

void UVoxelFluidIntegration::UpdateGridCellsInRegion(const FBox& Region)
{
	if (!FluidGrid || !IsVoxelWorldValid() || !TerrainLayer.Layer)
		return;
	
	UObject* WorldContext = static_cast<UObject*>(VoxelWorld);
	const FVector CurrentGridOrigin = FluidGrid->GridOrigin;
	
	int32 UpdatedCells = 0;
	
	for (int32 x = 0; x < GridResolutionX; ++x)
	{
		for (int32 y = 0; y < GridResolutionY; ++y)
		{
			for (int32 z = 0; z < GridResolutionZ; ++z)
			{
				const FVector CellCenter = CurrentGridOrigin + 
					FVector((x + 0.5f) * CellWorldSize, 
						   (y + 0.5f) * CellWorldSize, 
						   (z + 0.5f) * CellWorldSize);
				
				// Skip cells outside the modified region
				if (!Region.IsInside(CellCenter))
					continue;
				
				// Check if this position is inside solid voxel terrain
				bool bIsSolid = CheckIfCellIsSolid(CellCenter, x, y, z);
				
				// Cache the new state
				FIntVector CellKey(x, y, z);
				bool* CachedState = CachedVoxelStates.Find(CellKey);
				if (!CachedState || *CachedState != bIsSolid)
				{
					// State changed, update the cell
					FluidGrid->SetCellSolid(x, y, z, bIsSolid);
					CachedVoxelStates.Add(CellKey, bIsSolid);
					UpdatedCells++;
				}
			}
		}
	}
	
	if (UpdatedCells > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("UpdateGridCellsInRegion: Updated %d cells"), UpdatedCells);
	}
}

void UVoxelFluidIntegration::RefreshTerrainAfterSculpting()
{
	if (!bUse3DVoxelTerrain)
	{
		UE_LOG(LogTemp, Warning, TEXT("RefreshTerrainAfterSculpting: 3D Voxel Terrain is not enabled"));
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("=== RefreshTerrainAfterSculpting START ==="));
	UE_LOG(LogTemp, Warning, TEXT("Layer: %s, SamplingMethod: %d"), 
		TerrainLayer.Layer ? TEXT("Set") : TEXT("NULL"), 
		(int32)SamplingMethod);
	
	// Store old cache size for comparison
	int32 OldCacheSize = CachedVoxelStates.Num();
	
	// Clear the cache to force full refresh
	CachedVoxelStates.Empty();
	
	UE_LOG(LogTemp, Warning, TEXT("Cleared cache of %d entries"), OldCacheSize);
	
	// Do a full 3D terrain update
	Update3DVoxelTerrain();
	
	// Mark that we've refreshed
	bTerrainNeedsRefresh = false;
	LastTerrainRefreshTime = 0.0f;
	
	UE_LOG(LogTemp, Warning, TEXT("=== RefreshTerrainAfterSculpting END - New cache size: %d ==="), CachedVoxelStates.Num());
}

void UVoxelFluidIntegration::RefreshTerrainInRadius(const FVector& Center, float Radius)
{
	if (!bUse3DVoxelTerrain)
		return;
	
	UE_LOG(LogTemp, Warning, TEXT("=== RefreshTerrainInRadius START ==="));
	UE_LOG(LogTemp, Warning, TEXT("Center: %s, Radius: %.1f"), *Center.ToString(), Radius);
	
	if (bUseChunkedSystem && ChunkManager)
	{
		// Chunked system - update affected chunks
		FBox RefreshBounds(Center - FVector(Radius), Center + FVector(Radius));
		TArray<FFluidChunkCoord> AffectedChunks = ChunkManager->GetChunksInBounds(RefreshBounds);
		
		int32 TotalChangedCells = 0;
		
		for (const FFluidChunkCoord& ChunkCoord : AffectedChunks)
		{
			UFluidChunk* Chunk = ChunkManager->GetChunk(ChunkCoord);
			if (!Chunk)
				continue;
			
			int32 ChangedInChunk = UpdateChunkCellsInRadius(Chunk, Center, Radius);
			TotalChangedCells += ChangedInChunk;
			
			if (ChangedInChunk > 0)
			{
				// Mark chunk dirty and force mesh update
				Chunk->bDirty = true;
				Chunk->MarkMeshDataDirty();
			}
		}
		
		UE_LOG(LogTemp, Warning, TEXT("Updated %d chunks with %d total cell changes"), 
			AffectedChunks.Num(), TotalChangedCells);
	}
	else if (FluidGrid)
	{
		// Grid system - update affected cells
		int32 ChangedCells = UpdateGridCellsInRadius(Center, Radius);
		
		if (ChangedCells > 0)
		{
			// Wake up only fluid cells near the changed area
			WakeFluidInRadius(Center, Radius * 1.5f); // Slightly larger radius for wake
			
			// Run a few simulation steps
			for (int32 i = 0; i < 5; i++)
			{
				FluidGrid->UpdateSimulation(0.016f);
			}
			
			UE_LOG(LogTemp, Warning, TEXT("Updated %d cells in radius"), ChangedCells);
		}
	}
	
	UE_LOG(LogTemp, Warning, TEXT("=== RefreshTerrainInRadius END ==="));
}

int32 UVoxelFluidIntegration::UpdateGridCellsInRadius(const FVector& Center, float Radius)
{
	if (!FluidGrid || !IsVoxelWorldValid())
		return 0;
	
	const FVector GridOrigin = FluidGrid->GridOrigin;
	const float RadiusSq = Radius * Radius;
	int32 ChangedCells = 0;
	
	// Calculate grid bounds to check
	int32 MinX = FMath::Max(0, FMath::FloorToInt((Center.X - Radius - GridOrigin.X) / CellWorldSize));
	int32 MaxX = FMath::Min(GridResolutionX - 1, FMath::CeilToInt((Center.X + Radius - GridOrigin.X) / CellWorldSize));
	int32 MinY = FMath::Max(0, FMath::FloorToInt((Center.Y - Radius - GridOrigin.Y) / CellWorldSize));
	int32 MaxY = FMath::Min(GridResolutionY - 1, FMath::CeilToInt((Center.Y + Radius - GridOrigin.Y) / CellWorldSize));
	int32 MinZ = FMath::Max(0, FMath::FloorToInt((Center.Z - Radius - GridOrigin.Z) / CellWorldSize));
	int32 MaxZ = FMath::Min(GridResolutionZ - 1, FMath::CeilToInt((Center.Z + Radius - GridOrigin.Z) / CellWorldSize));
	
	// Only check cells within the radius
	for (int32 x = MinX; x <= MaxX; ++x)
	{
		for (int32 y = MinY; y <= MaxY; ++y)
		{
			for (int32 z = MinZ; z <= MaxZ; ++z)
			{
				const FVector CellCenter = GridOrigin + 
					FVector((x + 0.5f) * CellWorldSize, 
						   (y + 0.5f) * CellWorldSize, 
						   (z + 0.5f) * CellWorldSize);
				
				// Check if cell is within radius
				if (FVector::DistSquared(CellCenter, Center) > RadiusSq)
					continue;
				
				// Check current solid state
				bool bWasSolid = FluidGrid->IsCellSolid(x, y, z);
				bool bIsSolid = CheckIfCellIsSolid(CellCenter, x, y, z);
				
				if (bWasSolid != bIsSolid)
				{
					// Update the cell
					FluidGrid->SetCellSolid(x, y, z, bIsSolid);
					ChangedCells++;
					
					// Log sample changes for debugging
					if (ChangedCells <= 5)
					{
						UE_LOG(LogTemp, VeryVerbose, TEXT("Cell[%d,%d,%d]: %s -> %s"), 
							x, y, z,
							bWasSolid ? TEXT("SOLID") : TEXT("EMPTY"),
							bIsSolid ? TEXT("SOLID") : TEXT("EMPTY"));
					}
				}
			}
		}
	}
	
	return ChangedCells;
}

int32 UVoxelFluidIntegration::UpdateChunkCellsInRadius(UFluidChunk* Chunk, const FVector& Center, float Radius)
{
	if (!Chunk || !IsVoxelWorldValid())
		return 0;
	
	const float RadiusSq = Radius * Radius;
	const int32 ChunkSize = Chunk->ChunkSize;
	const float CellSize = Chunk->CellSize;
	const FVector ChunkOrigin = Chunk->ChunkWorldPosition;
	int32 ChangedCells = 0;
	
	// Check all cells in chunk that might be affected
	for (int32 LocalX = 0; LocalX < ChunkSize; ++LocalX)
	{
		for (int32 LocalY = 0; LocalY < ChunkSize; ++LocalY)
		{
			for (int32 LocalZ = 0; LocalZ < ChunkSize; ++LocalZ)
			{
				const FVector CellCenter = ChunkOrigin + 
					FVector((LocalX + 0.5f) * CellSize, 
						   (LocalY + 0.5f) * CellSize, 
						   (LocalZ + 0.5f) * CellSize);
				
				// Check if cell is within radius
				if (FVector::DistSquared(CellCenter, Center) > RadiusSq)
					continue;
				
				// Check current solid state
				bool bWasSolid = Chunk->IsCellSolid(LocalX, LocalY, LocalZ);
				bool bIsSolid = CheckIfCellIsSolid(CellCenter, LocalX, LocalY, LocalZ);
				
				if (bWasSolid != bIsSolid)
				{
					// Update the cell
					Chunk->SetCellSolid(LocalX, LocalY, LocalZ, bIsSolid);
					ChangedCells++;
				}
			}
		}
	}
	
	return ChangedCells;
}

void UVoxelFluidIntegration::WakeFluidInRadius(const FVector& Center, float Radius)
{
	if (!FluidGrid)
		return;
	
	const FVector GridOrigin = FluidGrid->GridOrigin;
	const float RadiusSq = Radius * Radius;
	
	// Calculate grid bounds to check
	int32 MinX = FMath::Max(0, FMath::FloorToInt((Center.X - Radius - GridOrigin.X) / CellWorldSize));
	int32 MaxX = FMath::Min(GridResolutionX - 1, FMath::CeilToInt((Center.X + Radius - GridOrigin.X) / CellWorldSize));
	int32 MinY = FMath::Max(0, FMath::FloorToInt((Center.Y - Radius - GridOrigin.Y) / CellWorldSize));
	int32 MaxY = FMath::Min(GridResolutionY - 1, FMath::CeilToInt((Center.Y + Radius - GridOrigin.Y) / CellWorldSize));
	int32 MinZ = FMath::Max(0, FMath::FloorToInt((Center.Z - Radius - GridOrigin.Z) / CellWorldSize));
	int32 MaxZ = FMath::Min(GridResolutionZ - 1, FMath::CeilToInt((Center.Z + Radius - GridOrigin.Z) / CellWorldSize));
	
	int32 WokenCells = 0;
	
	// Wake up cells within radius
	for (int32 x = MinX; x <= MaxX; ++x)
	{
		for (int32 y = MinY; y <= MaxY; ++y)
		{
			for (int32 z = MinZ; z <= MaxZ; ++z)
			{
				const FVector CellCenter = GridOrigin + 
					FVector((x + 0.5f) * CellWorldSize, 
						   (y + 0.5f) * CellWorldSize, 
						   (z + 0.5f) * CellWorldSize);
				
				// Check if cell is within radius
				if (FVector::DistSquared(CellCenter, Center) <= RadiusSq)
				{
					// Calculate index directly (same formula as GetCellIndex)
					const int32 Idx = x + y * GridResolutionX + z * GridResolutionX * GridResolutionY;
					if (Idx >= 0 && Idx < FluidGrid->Cells.Num())
					{
						FCAFluidCell& Cell = FluidGrid->Cells[Idx];
						if (Cell.FluidLevel > FluidGrid->MinFluidLevel && !Cell.bIsSolid)
						{
							Cell.bSettled = false;
							Cell.SettledCounter = 0;
							WokenCells++;
						}
					}
				}
			}
		}
	}
	
	UE_LOG(LogTemp, VeryVerbose, TEXT("Woke %d fluid cells in radius %.1f"), WokenCells, Radius);
}

bool UVoxelFluidIntegration::QueryVoxelAtPosition(const FVector& WorldPosition, float& OutVoxelValue)
{
	if (!IsVoxelWorldValid())
		return false;
	
	// Determine which layer to use
	const FVoxelStackLayer* LayerToUse = nullptr;
	
	if (bUse3DVoxelTerrain && bUseSeparate3DLayer && Terrain3DLayer.Layer != nullptr)
	{
		// Use the dedicated 3D terrain layer
		LayerToUse = &Terrain3DLayer;
		
		if (bLogVoxelValues)
		{
			static bool bLoggedLayer = false;
			if (!bLoggedLayer)
			{
				UE_LOG(LogTemp, Warning, TEXT("Using 3D Terrain Layer: Set"));
				bLoggedLayer = true;
			}
		}
	}
	else if (TerrainLayer.Layer != nullptr)
	{
		// Fall back to the regular terrain layer
		LayerToUse = &TerrainLayer;
		
		if (bLogVoxelValues)
		{
			static bool bLoggedLayer = false;
			if (!bLoggedLayer)
			{
				UE_LOG(LogTemp, Warning, TEXT("Using Regular Terrain Layer: Set"));
				bLoggedLayer = true;
			}
		}
	}
	else
	{
		// No valid layer
		static bool bLoggedNoLayer = false;
		if (!bLoggedNoLayer)
		{
			UE_LOG(LogTemp, Error, TEXT("QueryVoxelAtPosition: No valid voxel layer configured!"));
			bLoggedNoLayer = true;
		}
		return false;
	}
	
	// Handle different query modes
	if (Terrain3DQueryMode == E3DVoxelQueryMode::SingleLayer)
	{
		// Query single layer
		FVoxelQueryResult QueryResult;
		TArray<UVoxelFloatMetadata*> EmptyMetadata;
		
		bool bSuccess = UVoxelLayersBlueprintLibrary::QueryVoxelLayer(
			VoxelWorld, 
			*LayerToUse, 
			WorldPosition, 
			false, 
			EmptyMetadata, 
			0, 
			QueryResult
		);
		
		if (bSuccess)
		{
			OutVoxelValue = QueryResult.Value;
			
			// Log detailed values for debugging
			if (bLogVoxelValues)
			{
				static int32 LogCounter = 0;
				if (LogCounter++ % 100 == 0) // Log every 100th query
				{
					UE_LOG(LogTemp, VeryVerbose, TEXT("Voxel at %s: Value=%.3f"), 
						*WorldPosition.ToString(), 
						QueryResult.Value);
				}
			}
			
			return true;
		}
	}
	else if (Terrain3DQueryMode == E3DVoxelQueryMode::CombineLayers || 
	         Terrain3DQueryMode == E3DVoxelQueryMode::MinValue || 
	         Terrain3DQueryMode == E3DVoxelQueryMode::MaxValue)
	{
		// Query multiple layers
		TArray<float> LayerValues;
		
		// Query primary layer
		FVoxelQueryResult QueryResult;
		TArray<UVoxelFloatMetadata*> EmptyMetadata;
		
		if (UVoxelLayersBlueprintLibrary::QueryVoxelLayer(VoxelWorld, *LayerToUse, WorldPosition, false, EmptyMetadata, 0, QueryResult))
		{
			LayerValues.Add(QueryResult.Value);
		}
		
		// Query additional layers
		for (const FVoxelStackLayer& AdditionalLayer : Additional3DLayers)
		{
			if (AdditionalLayer.Layer != nullptr)
			{
				if (UVoxelLayersBlueprintLibrary::QueryVoxelLayer(VoxelWorld, AdditionalLayer, WorldPosition, false, EmptyMetadata, 0, QueryResult))
				{
					LayerValues.Add(QueryResult.Value);
				}
			}
		}
		
		if (LayerValues.Num() > 0)
		{
			// Combine values based on mode
			if (Terrain3DQueryMode == E3DVoxelQueryMode::CombineLayers)
			{
				// Average the values
				float Sum = 0.0f;
				for (float Value : LayerValues)
				{
					Sum += Value;
				}
				OutVoxelValue = Sum / LayerValues.Num();
			}
			else if (Terrain3DQueryMode == E3DVoxelQueryMode::MinValue)
			{
				// Use minimum (most solid)
				OutVoxelValue = FMath::Min(LayerValues);
			}
			else if (Terrain3DQueryMode == E3DVoxelQueryMode::MaxValue)
			{
				// Use maximum (most empty)
				OutVoxelValue = FMath::Max(LayerValues);
			}
			
			if (bLogVoxelValues)
			{
				static int32 LogCounter = 0;
				if (LogCounter++ % 100 == 0)
				{
					UE_LOG(LogTemp, VeryVerbose, TEXT("Combined %d layers at %s: Result=%.3f"), 
						LayerValues.Num(), *WorldPosition.ToString(), OutVoxelValue);
				}
			}
			
			return true;
		}
	}
	
	return false;
}

bool UVoxelFluidIntegration::CheckIfCellIsSolid(const FVector& CellCenter, int32 GridX, int32 GridY, int32 GridZ)
{
	if (!IsVoxelWorldValid())
	{
		return false;
	}
	
	// Check if we have a valid layer configured
	bool bHasValidLayer = false;
	if (bUse3DVoxelTerrain && bUseSeparate3DLayer)
	{
		bHasValidLayer = (Terrain3DLayer.Layer != nullptr);
	}
	else
	{
		bHasValidLayer = (TerrainLayer.Layer != nullptr);
	}
	
	if (!bHasValidLayer)
	{
		static bool bLoggedMissingLayer = false;
		if (!bLoggedMissingLayer)
		{
			UE_LOG(LogTemp, Error, TEXT("CheckIfCellIsSolid: No valid voxel layer configured"));
			bLoggedMissingLayer = true;
		}
		return false;
	}
	
	UObject* WorldContext = static_cast<UObject*>(VoxelWorld);
	
	if (bUseMultipleSamplePoints)
	{
		// Sample multiple points within the cell to better detect partial solid cells
		int32 SolidCount = 0;
		const float HalfCell = CellWorldSize * 0.4f; // Sample 80% of cell
		
		// Sample 8 corners of the cell
		const FVector SampleOffsets[] = {
			FVector(-HalfCell, -HalfCell, -HalfCell),
			FVector(HalfCell, -HalfCell, -HalfCell),
			FVector(-HalfCell, HalfCell, -HalfCell),
			FVector(HalfCell, HalfCell, -HalfCell),
			FVector(-HalfCell, -HalfCell, HalfCell),
			FVector(HalfCell, -HalfCell, HalfCell),
			FVector(-HalfCell, HalfCell, HalfCell),
			FVector(HalfCell, HalfCell, HalfCell),
			FVector(0, 0, 0) // Center point
		};
		
		for (const FVector& Offset : SampleOffsets)
		{
			float VoxelValue = 0.0f;
			if (QueryVoxelAtPosition(CellCenter + Offset, VoxelValue))
			{
				// In SDF, negative values mean inside the surface (solid)
				// Use configurable threshold instead of 0
				bool bPointIsSolid = (VoxelValue < SolidThreshold);
				
				// Apply inversion if needed
				if (bInvertSolidDetection)
					bPointIsSolid = !bPointIsSolid;
				
				if (bPointIsSolid)
					SolidCount++;
			}
		}
		
		// Consider cell solid if majority of sample points are solid
		bool bResult = (SolidCount >= 5); // More than half of 9 points
		
		// Log detailed info for debugging
		if (bLogVoxelValues && GridX % 20 == 0 && GridY % 20 == 0 && GridZ == 5)
		{
			UE_LOG(LogTemp, Warning, TEXT("Cell[%d,%d,%d]: %d/9 points solid = %s"), 
				GridX, GridY, GridZ, SolidCount, bResult ? TEXT("SOLID") : TEXT("EMPTY"));
		}
		
		return bResult;
	}
	else
	{
		// Single point sample at cell center
		float VoxelValue = 0.0f;
		if (QueryVoxelAtPosition(CellCenter, VoxelValue))
		{
			// In SDF, negative values mean inside the surface (solid)
			// Use configurable threshold instead of 0
			bool bIsSolid = (VoxelValue < SolidThreshold);
			
			// Apply inversion if needed
			if (bInvertSolidDetection)
				bIsSolid = !bIsSolid;
			
			// Log for debugging
			if (bLogVoxelValues && GridX % 20 == 0 && GridY % 20 == 0 && GridZ == 5)
			{
				UE_LOG(LogTemp, Warning, TEXT("Cell[%d,%d,%d] at %s: VoxelValue=%.3f -> %s"), 
					GridX, GridY, GridZ, *CellCenter.ToString(), VoxelValue,
					bIsSolid ? TEXT("SOLID") : TEXT("EMPTY"));
			}
			
			return bIsSolid;
		}
		
		// Fallback if query fails
		return false;
	}
}

void UVoxelFluidIntegration::ForceRefreshVoxelCache()
{
	if (!IsVoxelWorldValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("ForceRefreshVoxelCache: VoxelWorld is NULL"));
		return;
	}
	
	// Clear our internal cache
	CachedVoxelStates.Empty();
	
	// The voxel plugin might have its own caching that needs to be invalidated
	// This would be plugin-specific, but we can try common approaches:
	
	// Force a bounds update on the voxel world actor
	if (AActor* VoxelActor = Cast<AActor>(VoxelWorld))
	{
		VoxelActor->UpdateComponentTransforms();
		UE_LOG(LogTemp, Warning, TEXT("ForceRefreshVoxelCache: Updated voxel actor transforms"));
	}
	
	// Mark terrain needs refresh
	bTerrainNeedsRefresh = true;
	
	UE_LOG(LogTemp, Warning, TEXT("ForceRefreshVoxelCache: Cache cleared, terrain marked for refresh"));
}

void UVoxelFluidIntegration::LogAvailableVoxelLayers()
{
	UE_LOG(LogTemp, Warning, TEXT("=== Available Voxel Layers ==="));
	
	if (!IsVoxelWorldValid())
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelWorld is NULL"));
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("VoxelWorld: %s"), *VoxelWorld->GetName());
	
	// Log configured layers
	if (TerrainLayer.Layer)
	{
		UE_LOG(LogTemp, Warning, TEXT("Regular Terrain Layer: Set"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Regular Terrain Layer: Not Set"));
	}
	
	if (bUse3DVoxelTerrain)
	{
		if (Terrain3DLayer.Layer)
		{
			UE_LOG(LogTemp, Warning, TEXT("3D Terrain Layer: Set"));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("3D Terrain Layer: Not Set"));
		}
		
		UE_LOG(LogTemp, Warning, TEXT("Use Separate 3D Layer: %s"), bUseSeparate3DLayer ? TEXT("Yes") : TEXT("No"));
		UE_LOG(LogTemp, Warning, TEXT("3D Query Mode: %s"), 
			*UEnum::GetValueAsString(Terrain3DQueryMode));
		UE_LOG(LogTemp, Warning, TEXT("Solid Threshold: %.3f"), SolidThreshold);
		UE_LOG(LogTemp, Warning, TEXT("Invert Solid Detection: %s"), bInvertSolidDetection ? TEXT("Yes") : TEXT("No"));
		
		if (Additional3DLayers.Num() > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("Additional 3D Layers:"));
			for (int32 i = 0; i < Additional3DLayers.Num(); i++)
			{
				if (Additional3DLayers[i].Layer)
				{
					UE_LOG(LogTemp, Warning, TEXT("  [%d] Set"), i);
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("  [%d] Not Set"), i);
				}
			}
		}
		
		// Test query at player position
		if (UWorld* World = GetWorld())
		{
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				if (APawn* Pawn = PC->GetPawn())
				{
					FVector TestPos = Pawn->GetActorLocation();
					float VoxelValue = 0.0f;
					
					if (QueryVoxelAtPosition(TestPos, VoxelValue))
					{
						bool bIsSolid = (VoxelValue < SolidThreshold);
						if (bInvertSolidDetection) bIsSolid = !bIsSolid;
						
						UE_LOG(LogTemp, Warning, TEXT("Test at player position %s:"), *TestPos.ToString());
						UE_LOG(LogTemp, Warning, TEXT("  Voxel Value: %.3f"), VoxelValue);
						UE_LOG(LogTemp, Warning, TEXT("  Is Solid: %s"), bIsSolid ? TEXT("Yes") : TEXT("No"));
					}
					else
					{
						UE_LOG(LogTemp, Error, TEXT("Failed to query voxel at player position"));
					}
				}
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("3D Voxel Terrain is DISABLED"));
	}
	
	UE_LOG(LogTemp, Warning, TEXT("=== End Voxel Layers ==="));
}

void UVoxelFluidIntegration::DrawDebugSolidCells()
{
	if (!GetWorld())
		return;
	
	if (bUseChunkedSystem && ChunkManager)
	{
		// Draw solid cells for active chunks
		const TArray<UFluidChunk*> ActiveChunks = ChunkManager->GetActiveChunks();
		
		for (UFluidChunk* Chunk : ActiveChunks)
		{
			if (!Chunk)
				continue;
			
			for (int32 x = 0; x < Chunk->ChunkSize; x += 2) // Sample every other cell for performance
			{
				for (int32 y = 0; y < Chunk->ChunkSize; y += 2)
				{
					for (int32 z = 0; z < Chunk->ChunkSize; z += 2)
					{
						if (Chunk->IsCellSolid(x, y, z))
						{
							FVector CellPos = Chunk->GetWorldPositionFromLocal(x, y, z);
							DrawDebugBox(GetWorld(), CellPos, FVector(Chunk->CellSize * 0.4f), FColor::Red, false, -1.0f, 0, 3.0f);
						}
					}
				}
			}
		}
	}
	else if (FluidGrid)
	{
		// Draw solid cells for grid
		for (int32 x = 0; x < GridResolutionX; x += 4) // Sample every 4th cell for performance
		{
			for (int32 y = 0; y < GridResolutionY; y += 4)
			{
				for (int32 z = 0; z < GridResolutionZ; z += 2)
				{
					if (FluidGrid->IsCellSolid(x, y, z))
					{
						FVector CellPos = FluidGrid->GetWorldPositionFromCell(x, y, z);
						DrawDebugBox(GetWorld(), CellPos, FVector(CellWorldSize * 0.4f), FColor::Red, false, -1.0f, 0, 3.0f);
					}
				}
			}
		}
	}
}