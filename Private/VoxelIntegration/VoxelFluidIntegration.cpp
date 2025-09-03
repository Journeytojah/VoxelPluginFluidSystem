#include "VoxelIntegration/VoxelFluidIntegration.h"
#include "VoxelIntegration/VoxelTerrainSampler.h"
#include "CellularAutomata/FluidChunkManager.h"
#include "CellularAutomata/FluidChunk.h"
#include "CellularAutomata/StaticWaterBody.h"
#include "Actors/VoxelFluidActor.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Components/SceneComponent.h"
#include "VoxelFluidStats.h"
#include "VoxelFluidDebug.h"
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
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_TerrainSampling);
	
	// Check cache first if enabled
	if (bEnableTerrainCaching)
	{
		float CachedHeight;
		if (GetCachedHeight(WorldX, WorldY, CachedHeight))
		{
			return CachedHeight;
		}
	}
	
	FVector SampleLocation(WorldX, WorldY, 0);
	UObject* WorldContext = IsVoxelWorldValid() ? static_cast<UObject*>(VoxelWorld) : static_cast<UObject*>(GetWorld());
	
	float Height = 0.0f;
	
	// Check if we need to combine multiple layers
	if (bEnableCombinedSampling && SecondaryVolumeLayer.Layer != nullptr)
	{
		// Sample both the base terrain and the runtime volume layer
		float BaseHeight = 0.0f;
		float VolumeHeight = 0.0f;
		
		if (bUseVoxelLayerSampling && TerrainLayer.Layer != nullptr)
		{
			BaseHeight = UVoxelTerrainSampler::SampleTerrainHeightAtLocationWithLayer(WorldContext, SampleLocation, TerrainLayer, SamplingMethod);
		}
		else
		{
			BaseHeight = UVoxelTerrainSampler::SampleTerrainHeightAtLocation(WorldContext, SampleLocation);
		}
		
		// Sample the secondary volume layer (runtime modifications)
		VolumeHeight = UVoxelTerrainSampler::SampleTerrainHeightAtLocationWithLayer(WorldContext, SampleLocation, SecondaryVolumeLayer, SamplingMethod);
		
		// Combine the heights - use the higher value (additive terrain)
		Height = FMath::Max(BaseHeight, VolumeHeight);
	}
	else if (bUseVoxelLayerSampling && TerrainLayer.Layer != nullptr)
	{
		Height = UVoxelTerrainSampler::SampleTerrainHeightAtLocationWithLayer(WorldContext, SampleLocation, TerrainLayer, SamplingMethod);
	}
	else
	{
		Height = UVoxelTerrainSampler::SampleTerrainHeightAtLocation(WorldContext, SampleLocation);
	}
	
	// Cache the result
	if (bEnableTerrainCaching)
	{
		CacheHeight(WorldX, WorldY, Height);
	}
	
	return Height;
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
					}
					
					// Update the cell's solid state
					FluidGrid->SetCellSolid(x, y, z, bIsSolid);
				}
			}
		}
		
		
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
	
	// OPTIMIZATION: Only check cells that are likely to interact with water
	// Skip very high cells (above terrain) and use terrain height as a guide
	int32 SolidCellCount = 0;
	
	for (int32 LocalX = 0; LocalX < ChunkSize; ++LocalX)
	{
		for (int32 LocalY = 0; LocalY < ChunkSize; ++LocalY)
		{
			// First, get the terrain height for this column
			const FVector ColumnPos = ChunkOrigin + FVector((LocalX + 0.5f) * CellSize, (LocalY + 0.5f) * CellSize, 0);
			const float TerrainHeight = SampleVoxelHeight(ColumnPos.X, ColumnPos.Y);
			
			// Set the terrain height (this marks cells below as solid automatically)
			Chunk->SetTerrainHeight(LocalX, LocalY, TerrainHeight);
			
			// Only do detailed 3D checks for cells near the terrain surface (within 5 cells up/down)
			const int32 TerrainCellZ = FMath::Clamp((int32)((TerrainHeight - ChunkOrigin.Z) / CellSize), 0, ChunkSize - 1);
			const int32 MinZ = FMath::Max(0, TerrainCellZ - 5);
			const int32 MaxZ = FMath::Min(ChunkSize - 1, TerrainCellZ + 5);
			
			for (int32 LocalZ = MinZ; LocalZ <= MaxZ; ++LocalZ)
			{
				const FVector CellCenter = ChunkOrigin + 
					FVector((LocalX + 0.5f) * CellSize, 
						   (LocalY + 0.5f) * CellSize, 
						   (LocalZ + 0.5f) * CellSize);
				
				// Only do detailed check for cells near the surface
				bool bIsSolid = CheckIfCellIsSolid(CellCenter, LocalX, LocalY, LocalZ);
				Chunk->SetCellSolid(LocalX, LocalY, LocalZ, bIsSolid);
				
				if (bIsSolid)
					SolidCellCount++;
			}
		}
	}
	
	
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
			
			// Special debug for problematic chunk (2,0,-1)
			if (ChunkCoord.X == 2 && ChunkCoord.Y == 0 && ChunkCoord.Z == -1)
			{
				if (LocalX % 8 == 0 && LocalY % 8 == 0) // Sample every 8th cell to reduce spam
				{
					
					// Check if this terrain height would make all cells solid
					float ChunkTopZ = ChunkWorldMin.Z + ChunkSize * CellSize;
					if (TerrainHeight > ChunkTopZ)
					{
					}
					else if (TerrainHeight > ChunkWorldMin.Z)
					{
						float PercentSolid = ((TerrainHeight - ChunkWorldMin.Z) / (ChunkSize * CellSize)) * 100.0f;
					}
					else
					{
					}
				}
			}
			
			// Set terrain height for this column - this will mark cells as solid based on terrain height
			Chunk->SetTerrainHeight(LocalX, LocalY, TerrainHeight);
			
			// NOTE: SetTerrainHeight already marks cells as solid in the chunk
			// We don't need to manually iterate through all Z levels here
		}
	}
	
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
	
	// Notify chunk manager about voxel edit for edit-triggered activation
	if (bUseChunkedSystem && ChunkManager)
	{
		ChunkManager->OnVoxelEditOccurredInBounds(ModifiedBounds);
	}
	
	// Add this region to pending updates
	{
		FScopeLock Lock(&TerrainUpdateMutex);
		PendingTerrainUpdates.Add(ModifiedBounds);
	}
	
	
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
	}
}

void UVoxelFluidIntegration::RefreshTerrainAfterSculpting()
{
	if (!bUse3DVoxelTerrain)
	{
		return;
	}
	
	
	// Store old cache size for comparison
	int32 OldCacheSize = CachedVoxelStates.Num();
	
	// Clear the cache to force full refresh
	CachedVoxelStates.Empty();
	
	
	// Do a full 3D terrain update
	Update3DVoxelTerrain();
	
	// Mark that we've refreshed
	bTerrainNeedsRefresh = false;
	LastTerrainRefreshTime = 0.0f;
	
}

void UVoxelFluidIntegration::RefreshTerrainInRadius(const FVector& Center, float Radius)
{
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_TerrainRefresh);
	
	if (!bUse3DVoxelTerrain)
		return;
	
	// Notify chunk manager about voxel edit for edit-triggered activation
	if (bUseChunkedSystem && ChunkManager)
	{
		ChunkManager->OnVoxelEditOccurred(Center, Radius);
	}
	
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
				
				// Notify fluid actor about terrain changes
				if (AActor* Owner = GetOwner())
				{
					if (AVoxelFluidActor* FluidActor = Cast<AVoxelFluidActor>(Owner))
					{
						// FluidActor will handle any water spawning if needed
						FluidActor->OnTerrainModified(Center, Radius);
					}
				}
			}
		}
		
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
			
		}
	}
	
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
	
}

bool UVoxelFluidIntegration::QueryVoxelAtPosition(const FVector& WorldPosition, float& OutVoxelValue)
{
	if (!IsVoxelWorldValid())
		return false;
	
	// Check if we need to combine with secondary volume layer FIRST
	if (bEnableCombinedSampling && SecondaryVolumeLayer.Layer != nullptr)
	{
		// Query both the base layer and the secondary volume layer
		float BaseValue = FLT_MAX;
		float VolumeValue = FLT_MAX;
		
		// Query base layer
		const FVoxelStackLayer* BaseLayer = nullptr;
		if (bUse3DVoxelTerrain && bUseSeparate3DLayer && Terrain3DLayer.Layer != nullptr)
		{
			BaseLayer = &Terrain3DLayer;
		}
		else if (TerrainLayer.Layer != nullptr)
		{
			BaseLayer = &TerrainLayer;
		}
		
		if (BaseLayer != nullptr)
		{
			FVoxelQueryResult QueryResult;
			TArray<UVoxelFloatMetadata*> EmptyMetadata;
			
			if (UVoxelLayersBlueprintLibrary::QueryVoxelLayer(
				VoxelWorld, 
				*BaseLayer, 
				WorldPosition, 
				false, 
				EmptyMetadata, 
				0, 
				QueryResult))
			{
				BaseValue = QueryResult.Value;
			}
		}
		
		// Query secondary volume layer (runtime modifications)
		{
			FVoxelQueryResult QueryResult;
			TArray<UVoxelFloatMetadata*> EmptyMetadata;
			
			if (UVoxelLayersBlueprintLibrary::QueryVoxelLayer(
				VoxelWorld, 
				SecondaryVolumeLayer, 
				WorldPosition, 
				false, 
				EmptyMetadata, 
				0, 
				QueryResult))
			{
				VolumeValue = QueryResult.Value;
			}
		}
		
		// Combine values - use MAX for additive terrain (positive values = empty space)
		OutVoxelValue = FMath::Max(BaseValue, VolumeValue);
		
		if (bLogVoxelValues)
		{
			static int32 LogCounter = 0;
			if (LogCounter++ % 1000 == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("Combined voxel query at %s: Base=%.2f, Volume=%.2f, Final=%.2f"), 
					*WorldPosition.ToString(), BaseValue, VolumeValue, OutVoxelValue);
			}
		}
		
		return true;
	}
	
	// Original single layer logic
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
				}
			}
			
			return true;
		}
	}
	
	return false;
}

bool UVoxelFluidIntegration::CheckIfCellIsSolid(const FVector& CellCenter, int32 GridX, int32 GridY, int32 GridZ)
{
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_TerrainSampling);
	// INC_DWORD_STAT(STAT_VoxelFluid_TerrainQueries); // Hidden - not in top 20
	
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
	}
	
	// Mark terrain needs refresh
	bTerrainNeedsRefresh = true;
	
}

void UVoxelFluidIntegration::LogAvailableVoxelLayers()
{
	
	if (!IsVoxelWorldValid())
	{
		return;
	}
	
	
	// Log configured layers
	if (TerrainLayer.Layer)
	{
	}
	else
	{
	}
	
	if (bUse3DVoxelTerrain)
	{
		if (Terrain3DLayer.Layer)
		{
		}
		else
		{
		}
		
		
		if (Additional3DLayers.Num() > 0)
		{
			for (int32 i = 0; i < Additional3DLayers.Num(); i++)
			{
				if (Additional3DLayers[i].Layer)
				{
				}
				else
				{
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
						
					}
					else
					{
					}
				}
			}
		}
	}
	else
	{
	}
	
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

// =====================================================
// TERRAIN CACHING IMPLEMENTATION
// =====================================================

FIntPoint UVoxelFluidIntegration::WorldPositionToCacheKey(float WorldX, float WorldY) const
{
	// Snap to cache grid for better cache hit rates
	int32 GridX = FMath::FloorToInt(WorldX / TerrainCacheGridSize);
	int32 GridY = FMath::FloorToInt(WorldY / TerrainCacheGridSize);
	return FIntPoint(GridX, GridY);
}

bool UVoxelFluidIntegration::GetCachedHeight(float WorldX, float WorldY, float& OutHeight) const
{
	if (!bEnableTerrainCaching)
		return false;
		
	FIntPoint CacheKey = WorldPositionToCacheKey(WorldX, WorldY);
	const FTerrainCacheEntry* CacheEntry = TerrainHeightCache.Find(CacheKey);
	
	if (CacheEntry)
	{
		// Check if cache entry is still valid
		float CurrentTime = FPlatformTime::Seconds();
		if (CurrentTime - CacheEntry->CacheTime <= TerrainCacheLifetime)
		{
			OutHeight = CacheEntry->Height;
			return true;
		}
	}
	
	return false;
}

void UVoxelFluidIntegration::CacheHeight(float WorldX, float WorldY, float Height)
{
	if (!bEnableTerrainCaching)
		return;
		
	FIntPoint CacheKey = WorldPositionToCacheKey(WorldX, WorldY);
	FVector2D Position(WorldX, WorldY);
	TerrainHeightCache.Add(CacheKey, FTerrainCacheEntry(Position, Height));
	
	// Periodic cleanup to prevent memory bloat
	float CurrentTime = FPlatformTime::Seconds();
	if (CurrentTime - LastCacheCleanupTime > 60.0f) // Cleanup every minute
	{
		CleanupTerrainCache();
		LastCacheCleanupTime = CurrentTime;
	}
}

void UVoxelFluidIntegration::CleanupTerrainCache()
{
	if (!bEnableTerrainCaching)
		return;
		
	float CurrentTime = FPlatformTime::Seconds();
	
	// Remove expired entries
	for (auto It = TerrainHeightCache.CreateIterator(); It; ++It)
	{
		if (CurrentTime - It.Value().CacheTime > TerrainCacheLifetime)
		{
			It.RemoveCurrent();
		}
	}
	
	UE_LOG(LogTemp, Log, TEXT("VoxelFluidIntegration: Cleaned up terrain cache, %d entries remaining"), TerrainHeightCache.Num());
}

// =====================================================
// BATCH SAMPLING IMPLEMENTATION  
// =====================================================

TArray<float> UVoxelFluidIntegration::SampleVoxelHeightsBatch(const TArray<FVector>& Positions)
{
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_TerrainSampling);
	
	TArray<float> Heights;
	Heights.Reserve(Positions.Num());
	
	if (Positions.Num() == 0)
		return Heights;
	
	UObject* WorldContext = IsVoxelWorldValid() ? static_cast<UObject*>(VoxelWorld) : static_cast<UObject*>(GetWorld());
	
	if (bUseVoxelLayerSampling && TerrainLayer.Layer != nullptr)
	{
		UVoxelTerrainSampler::SampleTerrainAtPositionsWithLayer(WorldContext, Positions, TerrainLayer, Heights, SamplingMethod);
	}
	else
	{
		UVoxelTerrainSampler::SampleTerrainAtPositions(WorldContext, Positions, Heights);
	}
	
	// Cache all the results if caching is enabled
	if (bEnableTerrainCaching)
	{
		for (int32 i = 0; i < Positions.Num() && i < Heights.Num(); ++i)
		{
			CacheHeight(Positions[i].X, Positions[i].Y, Heights[i]);
		}
	}
	
	return Heights;
}

void UVoxelFluidIntegration::SampleChunkTerrainBatch(const FFluidChunkCoord& ChunkCoord)
{
	if (!ChunkManager)
		return;
		
	// Calculate chunk boundaries using chunk coordinate and manager settings
	const float ChunkWorldSize = ChunkManager->ChunkSize * ChunkManager->CellSize;
	FVector ChunkMin = ChunkManager->WorldOrigin + FVector(
		ChunkCoord.X * ChunkWorldSize,
		ChunkCoord.Y * ChunkWorldSize,
		ChunkCoord.Z * ChunkWorldSize
	);
	FVector ChunkMax = ChunkMin + FVector(ChunkWorldSize, ChunkWorldSize, ChunkWorldSize);
	
	// Calculate sample resolution based on chunk size
	const int32 SamplesPerDimension = FMath::CeilToInt(ChunkWorldSize / CellWorldSize);
	const float SampleSpacing = ChunkWorldSize / SamplesPerDimension;
	
	// Generate sample positions
	TArray<FVector> SamplePositions;
	SamplePositions.Reserve(SamplesPerDimension * SamplesPerDimension);
	
	for (int32 X = 0; X < SamplesPerDimension; ++X)
	{
		for (int32 Y = 0; Y < SamplesPerDimension; ++Y)
		{
			FVector SamplePos = ChunkMin + FVector(
				(X + 0.5f) * SampleSpacing,
				(Y + 0.5f) * SampleSpacing,
				(ChunkMin.Z + ChunkMax.Z) * 0.5f
			);
			SamplePositions.Add(SamplePos);
		}
	}
	
	// Perform batch sampling
	TArray<float> Heights = SampleVoxelHeightsBatch(SamplePositions);
	
	// Apply heights to chunk terrain using bilinear interpolation
	UFluidChunk* Chunk = ChunkManager->GetChunk(ChunkCoord);
	if (Chunk && Heights.Num() == SamplePositions.Num())
	{
		const int32 ChunkCellCount = ChunkManager->ChunkSize;
		const float ChunkCellSize = ChunkManager->CellSize;
		
		for (int32 LocalX = 0; LocalX < ChunkCellCount; ++LocalX)
		{
			for (int32 LocalY = 0; LocalY < ChunkCellCount; ++LocalY)
			{
				// Calculate world position for this cell
				FVector CellWorldPos = Chunk->GetWorldPositionFromLocal(LocalX, LocalY, 0);
				
				// Find interpolation weights in sample grid
				float SampleX = (CellWorldPos.X - ChunkMin.X) / SampleSpacing - 0.5f;
				float SampleY = (CellWorldPos.Y - ChunkMin.Y) / SampleSpacing - 0.5f;
				
				int32 X0 = FMath::Clamp(FMath::FloorToInt(SampleX), 0, SamplesPerDimension - 1);
				int32 Y0 = FMath::Clamp(FMath::FloorToInt(SampleY), 0, SamplesPerDimension - 1);
				int32 X1 = FMath::Clamp(X0 + 1, 0, SamplesPerDimension - 1);
				int32 Y1 = FMath::Clamp(Y0 + 1, 0, SamplesPerDimension - 1);
				
				float FracX = SampleX - X0;
				float FracY = SampleY - Y0;
				
				// Bilinear interpolation
				int32 Idx00 = Y0 * SamplesPerDimension + X0;
				int32 Idx10 = Y0 * SamplesPerDimension + X1;
				int32 Idx01 = Y1 * SamplesPerDimension + X0;
				int32 Idx11 = Y1 * SamplesPerDimension + X1;
				
				float H00 = (Idx00 < Heights.Num()) ? Heights[Idx00] : 0.0f;
				float H10 = (Idx10 < Heights.Num()) ? Heights[Idx10] : 0.0f;
				float H01 = (Idx01 < Heights.Num()) ? Heights[Idx01] : 0.0f;
				float H11 = (Idx11 < Heights.Num()) ? Heights[Idx11] : 0.0f;
				
				float InterpolatedHeight = 
					H00 * (1.0f - FracX) * (1.0f - FracY) +
					H10 * FracX * (1.0f - FracY) +
					H01 * (1.0f - FracX) * FracY +
					H11 * FracX * FracY;
				
				// Update terrain for all Z levels in this column
				for (int32 LocalZ = 0; LocalZ < ChunkCellCount; ++LocalZ)
				{
					FVector CellCenter = Chunk->GetWorldPositionFromLocal(LocalX, LocalY, LocalZ);
					bool bIsSolid = (CellCenter.Z <= InterpolatedHeight);
					
					if (Chunk->IsCellSolid(LocalX, LocalY, LocalZ) != bIsSolid)
					{
						Chunk->SetCellSolid(LocalX, LocalY, LocalZ, bIsSolid);
					}
				}
			}
		}
	}
}

void UVoxelFluidIntegration::SetSecondaryVolumeLayer(const FVoxelStackLayer& InSecondaryLayer)
{
	SecondaryVolumeLayer = InSecondaryLayer;
	
	if (bEnableCombinedSampling && SecondaryVolumeLayer.Layer != nullptr)
	{
		UE_LOG(LogTemp, Log, TEXT("VoxelFluidIntegration: Set secondary volume layer for runtime terrain modifications"));
		// Clear terrain cache to force resampling with combined layers
		TerrainHeightCache.Empty();
		bTerrainNeedsRefresh = true;
	}
}

void UVoxelFluidIntegration::EnableCombinedSampling(bool bEnable)
{
	if (bEnableCombinedSampling != bEnable)
	{
		bEnableCombinedSampling = bEnable;
		
		if (bEnable && SecondaryVolumeLayer.Layer != nullptr)
		{
			UE_LOG(LogTemp, Log, TEXT("VoxelFluidIntegration: Enabled combined sampling with secondary volume layer"));
			// Clear terrain cache to force resampling
			TerrainHeightCache.Empty();
			bTerrainNeedsRefresh = true;
		}
		else if (!bEnable)
		{
			UE_LOG(LogTemp, Log, TEXT("VoxelFluidIntegration: Disabled combined sampling"));
			// Clear cache and refresh with single layer
			TerrainHeightCache.Empty();
			bTerrainNeedsRefresh = true;
		}
	}
}

void UVoxelFluidIntegration::OnRuntimeTerrainModified(const FVector& ModifiedCenter, float ModifiedRadius)
{
	if (!bEnableCombinedSampling || !IsVoxelWorldValid())
	{
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("VoxelFluidIntegration: Runtime terrain modified at %s with radius %.1f"), 
		*ModifiedCenter.ToString(), ModifiedRadius);
	
	// Clear cache in modified area first
	FBox ModifiedBounds = FBox::BuildAABB(ModifiedCenter, FVector(ModifiedRadius));
	
	// Remove cached terrain height entries within modified bounds
	TArray<FIntPoint> KeysToRemove;
	for (const auto& CacheEntry : TerrainHeightCache)
	{
		FVector WorldPos(CacheEntry.Value.Position.X, CacheEntry.Value.Position.Y, 0.0f);
		if (ModifiedBounds.IsInsideXY(WorldPos))
		{
			KeysToRemove.Add(CacheEntry.Key);
		}
	}
	
	for (const FIntPoint& Key : KeysToRemove)
	{
		TerrainHeightCache.Remove(Key);
	}
	
	// Clear cached voxel states in the modified region
	if (bUse3DVoxelTerrain)
	{
		// Clear cached voxel solid states for cells in the modified area
		TArray<FIntVector> VoxelKeysToRemove;
		for (const auto& CachedState : CachedVoxelStates)
		{
			// Convert cached key to world position (rough approximation)
			FVector CellWorldPos = GridWorldOrigin + FVector(
				CachedState.Key.X * CellWorldSize,
				CachedState.Key.Y * CellWorldSize,
				CachedState.Key.Z * CellWorldSize
			);
			
			if (ModifiedBounds.IsInside(CellWorldPos))
			{
				VoxelKeysToRemove.Add(CachedState.Key);
			}
		}
		
		for (const FIntVector& VoxelKey : VoxelKeysToRemove)
		{
			CachedVoxelStates.Remove(VoxelKey);
		}
		
		UE_LOG(LogTemp, Warning, TEXT("VoxelFluidIntegration: Cleared %d cached voxel states in modified region"), VoxelKeysToRemove.Num());
	}
	
	// Add to pending terrain updates
	{
		FScopeLock Lock(&TerrainUpdateMutex);
		PendingTerrainUpdates.Add(ModifiedBounds);
	}
	
	// Update terrain in modified region - this will re-query the combined layers
	UpdateTerrainInRegion(ModifiedBounds);
	
	// Wake fluid in radius to handle new terrain
	WakeFluidInRadius(ModifiedCenter, ModifiedRadius);
	
	UE_LOG(LogTemp, Warning, TEXT("VoxelFluidIntegration: Completed terrain update for runtime modification"));
}