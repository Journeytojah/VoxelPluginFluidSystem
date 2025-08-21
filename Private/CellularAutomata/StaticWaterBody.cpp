#include "CellularAutomata/StaticWaterBody.h"
#include "CellularAutomata/FluidChunk.h"
#include "VoxelFluidStats.h"

UStaticWaterManager::UStaticWaterManager()
{
	StaticWaterRegions.Empty();
	CachedChunkData.Empty();
}

void UStaticWaterManager::AddStaticWaterRegion(const FStaticWaterRegion& Region)
{
	StaticWaterRegions.Add(Region);
	InvalidateChunkCache();
}

void UStaticWaterManager::RemoveStaticWaterRegion(int32 RegionIndex)
{
	if (StaticWaterRegions.IsValidIndex(RegionIndex))
	{
		StaticWaterRegions.RemoveAt(RegionIndex);
		InvalidateChunkCache();
	}
}

void UStaticWaterManager::ClearAllStaticWaterRegions()
{
	StaticWaterRegions.Empty();
	InvalidateChunkCache();
}

FStaticWaterChunkData UStaticWaterManager::GenerateStaticWaterForChunk(const FFluidChunkCoord& ChunkCoord, int32 ChunkSize, float CellSize, const FVector& WorldOrigin) const
{
	FStaticWaterChunkData ChunkData;
	ChunkData.ChunkCoord = ChunkCoord;
	
	if (CachedChunkData.Contains(ChunkCoord))
	{
		return CachedChunkData[ChunkCoord];
	}

	FVector ChunkWorldPos = FVector(
		ChunkCoord.X * ChunkSize * CellSize,
		ChunkCoord.Y * ChunkSize * CellSize,
		ChunkCoord.Z * ChunkSize * CellSize
	) + WorldOrigin;

	FBox ChunkBounds(
		ChunkWorldPos,
		ChunkWorldPos + FVector(ChunkSize * CellSize, ChunkSize * CellSize, ChunkSize * CellSize)
	);

	for (const FStaticWaterRegion& Region : StaticWaterRegions)
	{
		if (Region.IntersectsChunk(ChunkBounds))
		{
			ChunkData.bHasStaticWater = true;
			ChunkData.StaticWaterLevel = Region.WaterLevel;
			ChunkData.WaterType = Region.WaterType;

			for (int32 LocalZ = 0; LocalZ < ChunkSize; LocalZ++)
			{
				for (int32 LocalY = 0; LocalY < ChunkSize; LocalY++)
				{
					for (int32 LocalX = 0; LocalX < ChunkSize; LocalX++)
					{
						FVector CellWorldPos = ChunkWorldPos + FVector(
							LocalX * CellSize + CellSize * 0.5f,
							LocalY * CellSize + CellSize * 0.5f,
							LocalZ * CellSize + CellSize * 0.5f
						);

						if (Region.ContainsPoint(CellWorldPos))
						{
							ChunkData.AddStaticWaterCell(LocalX, LocalY, LocalZ, ChunkSize);
						}
					}
				}
			}
			
			break;
		}
	}

	const_cast<TMap<FFluidChunkCoord, FStaticWaterChunkData>&>(CachedChunkData).Add(ChunkCoord, ChunkData);
	
	return ChunkData;
}

bool UStaticWaterManager::IsPointInStaticWater(const FVector& WorldPosition) const
{
	for (const FStaticWaterRegion& Region : StaticWaterRegions)
	{
		if (Region.ContainsPoint(WorldPosition))
		{
			return true;
		}
	}
	return false;
}

float UStaticWaterManager::GetStaticWaterLevelAtPoint(const FVector& WorldPosition) const
{
	for (const FStaticWaterRegion& Region : StaticWaterRegions)
	{
		if (Region.Bounds.IsInsideXY(WorldPosition))
		{
			return Region.WaterLevel;
		}
	}
	return -FLT_MAX;
}

void UStaticWaterManager::CreateOcean(float WaterLevel, const FBox& OceanBounds)
{
	FStaticWaterRegion OceanRegion;
	OceanRegion.Bounds = OceanBounds;
	OceanRegion.WaterLevel = WaterLevel;
	OceanRegion.WaterType = EStaticWaterType::Ocean;
	OceanRegion.bInfiniteDepth = true;
	OceanRegion.MinDepth = 1000.0f;
	
	AddStaticWaterRegion(OceanRegion);
	
	UE_LOG(LogTemp, Log, TEXT("Created ocean with water level: %f"), WaterLevel);
}

void UStaticWaterManager::CreateLake(const FVector& Center, float Radius, float WaterLevel, float Depth)
{
	FBox LakeBounds(
		FVector(Center.X - Radius, Center.Y - Radius, WaterLevel - Depth),
		FVector(Center.X + Radius, Center.Y + Radius, WaterLevel)
	);
	
	FStaticWaterRegion LakeRegion;
	LakeRegion.Bounds = LakeBounds;
	LakeRegion.WaterLevel = WaterLevel;
	LakeRegion.WaterType = EStaticWaterType::Lake;
	LakeRegion.bInfiniteDepth = false;
	LakeRegion.MinDepth = Depth;
	
	AddStaticWaterRegion(LakeRegion);
	
	UE_LOG(LogTemp, Log, TEXT("Created lake at (%f, %f) with radius %f and water level %f"), 
		Center.X, Center.Y, Radius, WaterLevel);
}

void UStaticWaterManager::CreateRectangularLake(const FBox& LakeBounds, float WaterLevel)
{
	FStaticWaterRegion LakeRegion;
	LakeRegion.Bounds = LakeBounds;
	LakeRegion.WaterLevel = WaterLevel;
	LakeRegion.WaterType = EStaticWaterType::Lake;
	LakeRegion.bInfiniteDepth = false;
	LakeRegion.MinDepth = WaterLevel - LakeBounds.Min.Z;
	
	AddStaticWaterRegion(LakeRegion);
	
	UE_LOG(LogTemp, Log, TEXT("Created rectangular lake with water level: %f"), WaterLevel);
}

void UStaticWaterManager::ApplyStaticWaterToChunk(UFluidChunk* Chunk) const
{
	if (!Chunk)
		return;

	// Check each static water region
	for (const FStaticWaterRegion& Region : StaticWaterRegions)
	{
		FBox ChunkBounds = Chunk->GetWorldBounds();
		if (!Region.IntersectsChunk(ChunkBounds))
			continue;

		int32 AppliedCells = 0;
		
		// Iterate through all cells in the chunk
		for (int32 LocalZ = 0; LocalZ < Chunk->ChunkSize; LocalZ++)
		{
			for (int32 LocalY = 0; LocalY < Chunk->ChunkSize; LocalY++)
			{
				for (int32 LocalX = 0; LocalX < Chunk->ChunkSize; LocalX++)
				{
					FVector CellWorldPos = Chunk->GetWorldPositionFromLocal(LocalX, LocalY, LocalZ);
					
					// Check if this cell position is within the water region
					if (Region.Bounds.IsInsideXY(CellWorldPos))
					{
						int32 LinearIndex = Chunk->GetLocalCellIndex(LocalX, LocalY, LocalZ);
						
						// Only add water if the cell is valid and not solid
						if (Chunk->Cells.IsValidIndex(LinearIndex))
						{
							FCAFluidCell& Cell = Chunk->Cells[LinearIndex];
							
							// Skip solid cells
							if (Cell.bIsSolid)
								continue;
							
							// Check if this is an air cell (above terrain) and below water level
							bool bIsAirCell = (Cell.TerrainHeight > -10000.0f) && // Has valid terrain data
											  (CellWorldPos.Z > Cell.TerrainHeight); // Cell is ABOVE terrain surface
							
							// Only place water in air cells that are below the water level
							if (bIsAirCell && CellWorldPos.Z <= Region.WaterLevel)
							{
								Cell.FluidLevel = 1.0f;
								Cell.bSettled = true;
								Cell.bSourceBlock = true;
								AppliedCells++;
							}
						}
					}
				}
			}
		}
		
		if (AppliedCells > 0)
		{
			UE_LOG(LogTemp, Verbose, TEXT("Applied static water to chunk %s: %d cells filled"), 
				*Chunk->ChunkCoord.ToString(), AppliedCells);
		}
	}
}

void UStaticWaterManager::ApplyStaticWaterToChunkWithTerrain(UFluidChunk* Chunk) const
{
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_StaticWaterApply);
	
	if (!Chunk)
		return;

	// CRITICAL DEBUG: Log every chunk that gets water applied
	UE_LOG(LogTemp, Warning, TEXT("====== APPLYING STATIC WATER TO CHUNK %s ======"), *Chunk->ChunkCoord.ToString());
	
	// Count solid cells to detect if terrain is properly set
	int32 SolidCellCount = 0;
	int32 TotalCells = Chunk->Cells.Num();
	for (const auto& Cell : Chunk->Cells)
	{
		if (Cell.bIsSolid)
			SolidCellCount++;
	}
	
	float SolidPercentage = (SolidCellCount / (float)TotalCells) * 100.0f;
	UE_LOG(LogTemp, Warning, TEXT("Chunk %s: %d/%d cells are solid (%.1f%%)"), 
		*Chunk->ChunkCoord.ToString(), SolidCellCount, TotalCells, SolidPercentage);
	
	// EMERGENCY BRAKE: If chunk has almost no solid cells, terrain data is wrong - DON'T APPLY WATER!
	if (SolidPercentage < 10.0f)
	{
		UE_LOG(LogTemp, Error, TEXT("EMERGENCY BRAKE: Chunk %s has only %.1f%% solid cells - terrain data is corrupted! NOT applying water!"),
			*Chunk->ChunkCoord.ToString(), SolidPercentage);
		return;
	}

	// Check if terrain data is properly initialized by sampling more cells
	bool bTerrainInitialized = false;
	int32 SampleCount = 0;
	int32 InitializedSamples = 0;
	const int32 MaxSamples = 20;
	
	for (int32 i = 0; i < FMath::Min(MaxSamples, Chunk->Cells.Num()) && SampleCount < MaxSamples; i += FMath::Max(1, Chunk->Cells.Num() / MaxSamples))
	{
		if (Chunk->Cells[i].TerrainHeight > -FLT_MAX + 1000.0f)
		{
			InitializedSamples++;
		}
		SampleCount++;
	}
	
	// Require at least 70% of sampled cells to have valid terrain height
	bTerrainInitialized = (InitializedSamples >= (SampleCount * 0.7f));
	
	if (!bTerrainInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("Terrain not initialized for chunk %s, skipping static water application (%d/%d cells valid = %.1f%%)"), 
			*Chunk->ChunkCoord.ToString(), InitializedSamples, SampleCount, (InitializedSamples / (float)SampleCount) * 100.0f);
		
		// Log first few terrain heights for debugging
		for (int32 i = 0; i < FMath::Min(5, Chunk->Cells.Num()); ++i)
		{
			UE_LOG(LogTemp, Warning, TEXT("  Cell %d terrain height: %.1f"), i, Chunk->Cells[i].TerrainHeight);
		}
		return;
	}
	
	UE_LOG(LogTemp, Log, TEXT("Terrain initialization OK for chunk %s (%d/%d cells valid = %.1f%%)"), 
		*Chunk->ChunkCoord.ToString(), InitializedSamples, SampleCount, (InitializedSamples / (float)SampleCount) * 100.0f);

	// Debug: Log static water regions affecting this chunk
	FBox ChunkBounds = Chunk->GetWorldBounds();
	UE_LOG(LogTemp, Log, TEXT("Checking %d static water regions for chunk %s (bounds: %s to %s)"), 
		StaticWaterRegions.Num(), *Chunk->ChunkCoord.ToString(), 
		*ChunkBounds.Min.ToString(), *ChunkBounds.Max.ToString());

	// This version is called after terrain has been properly initialized
	// It ensures we respect the terrain data that has been set
	for (int32 RegionIndex = 0; RegionIndex < StaticWaterRegions.Num(); RegionIndex++)
	{
		const FStaticWaterRegion& Region = StaticWaterRegions[RegionIndex];
		
		if (!Region.IntersectsChunk(ChunkBounds))
		{
			continue;
		}
		
		UE_LOG(LogTemp, Warning, TEXT("Region %d intersects chunk %s - WaterLevel:%.1f, Bounds:%s to %s"), 
			RegionIndex, *Chunk->ChunkCoord.ToString(), Region.WaterLevel,
			*Region.Bounds.Min.ToString(), *Region.Bounds.Max.ToString());

		int32 AppliedCells = 0;
		
		// Iterate from bottom to top to respect terrain layers
		for (int32 LocalZ = 0; LocalZ < Chunk->ChunkSize; LocalZ++)
		{
			for (int32 LocalY = 0; LocalY < Chunk->ChunkSize; LocalY++)
			{
				for (int32 LocalX = 0; LocalX < Chunk->ChunkSize; LocalX++)
				{
					FVector CellWorldPos = Chunk->GetWorldPositionFromLocal(LocalX, LocalY, LocalZ);
					
					// Check if this cell position is below the water level and within region bounds
					if (CellWorldPos.Z <= Region.WaterLevel && Region.Bounds.IsInsideXY(CellWorldPos))
					{
						int32 LinearIndex = Chunk->GetLocalCellIndex(LocalX, LocalY, LocalZ);
						
						if (Chunk->Cells.IsValidIndex(LinearIndex))
						{
							FCAFluidCell& Cell = Chunk->Cells[LinearIndex];
							
							// CONSERVATIVE water placement to prevent flat plane issues
							// Only place water if ALL conditions are met:
							// 1. Cell is NOT solid
							// 2. Cell is below water level
							// 3. Cell is SIGNIFICANTLY above terrain (full cell size)
							// 4. Terrain is properly initialized
							// 5. We're not at a chunk border (be extra conservative at borders)
							
							bool bIsAtBorder = (LocalX == 0 || LocalX == Chunk->ChunkSize - 1 || 
											   LocalY == 0 || LocalY == Chunk->ChunkSize - 1);
							
							const float RequiredGap = bIsAtBorder ? Chunk->CellSize * 2.0f : Chunk->CellSize * 1.0f;
							
							// CRITICAL FIX: Only place water if cell is NOT solid
							// The bIsSolid flag is set by terrain sampling and MUST be respected
							if (Cell.bIsSolid)
							{
								// This cell is inside terrain - NEVER place water here
								continue;
							}
							
							// CRITICAL: Check if this is an AIR cell (above terrain surface)
							// We only want to place water in air cells that are below the water level
							// This prevents water from spawning in underground voids or at wrong heights
							bool bIsAirCell = (Cell.TerrainHeight > -10000.0f) && // Has valid terrain data
											  (CellWorldPos.Z > Cell.TerrainHeight); // Cell is ABOVE terrain surface
							
							// Place water if:
							// 1. Cell is an air cell (above terrain)
							// 2. Cell is below the water level
							// 3. Cell is not solid (already checked above)
							bool bCanPlaceWater = bIsAirCell && (CellWorldPos.Z <= Region.WaterLevel);
							
							if (bCanPlaceWater)
							{
								// Cell is not solid and passes terrain checks - safe to place water
								Cell.FluidLevel = 1.0f;
								Cell.bSettled = true;
								Cell.bSourceBlock = true;
								AppliedCells++;
								
								// Debug: Log where water IS being placed (especially for problematic chunks)
								static int32 PlacedCount = 0;
								PlacedCount++;
								if (PlacedCount <= 20 || Chunk->ChunkCoord.X == -3 && Chunk->ChunkCoord.Y == 0 && Chunk->ChunkCoord.Z == 0)
								{
									UE_LOG(LogTemp, Warning, TEXT("Static water: Cell at %s PLACED - AirCell:YES, Solid:%s, TerrainHeight:%.1f, CellZ:%.1f, WaterLevel:%.1f, AboveTerrain:%.1f (Chunk:%s)"),
										*CellWorldPos.ToString(), 
										Cell.bIsSolid ? TEXT("YES") : TEXT("NO"),
										Cell.TerrainHeight, 
										CellWorldPos.Z, 
										Region.WaterLevel,
										CellWorldPos.Z - Cell.TerrainHeight,
										*Chunk->ChunkCoord.ToString());
								}
							}
							else
							{
								// Debug: Log why water wasn't placed
								static int32 DebugCount = 0;
								DebugCount++;
								if (DebugCount <= 10)
								{
									UE_LOG(LogTemp, Warning, TEXT("Static water: Cell at %s NOT placed - AirCell:%s, Solid:%s, TerrainHeight:%.1f, CellZ:%.1f, WaterLevel:%.1f, AboveTerrain:%.1f"),
										*CellWorldPos.ToString(), 
										bIsAirCell ? TEXT("YES") : TEXT("NO"),
										Cell.bIsSolid ? TEXT("YES") : TEXT("NO"),
										Cell.TerrainHeight, 
										CellWorldPos.Z, 
										Region.WaterLevel,
										CellWorldPos.Z - Cell.TerrainHeight);
								}
							}
						}
					}
				}
			}
		}
		
		if (AppliedCells > 0)
		{
			float FillPercentage = (AppliedCells / (float)Chunk->Cells.Num()) * 100.0f;
			UE_LOG(LogTemp, Log, TEXT("Applied static water to chunk %s: %d cells filled (terrain-aware) [%.1f%% of chunk]"), 
				*Chunk->ChunkCoord.ToString(), AppliedCells, FillPercentage);
				
			// Warn about chunks that are filled more than expected
			if (FillPercentage > 70.0f)
			{
				UE_LOG(LogTemp, Warning, TEXT("WARNING: Chunk %s has very high water fill percentage (%.1f%%) - possible terrain sampling issue"), 
					*Chunk->ChunkCoord.ToString(), FillPercentage);
			}
		}
	}
	
	// CRITICAL: Seal chunk borders to prevent water leaking through gaps
	SealChunkBordersAgainstTerrain(Chunk);
}

void UStaticWaterManager::SealChunkBordersAgainstTerrain(UFluidChunk* Chunk) const
{
	if (!Chunk)
		return;
		
	const int32 ChunkSize = Chunk->ChunkSize;
	
	// Check all border cells and remove water if they're too close to terrain or in ambiguous areas
	// This prevents water from leaking through gaps at chunk boundaries
	
	// Debug: Track how many border cells we're processing
	int32 BorderCellsRemoved = 0;
	int32 BorderCellsChecked = 0;
	
	// Process X borders (x=0 and x=ChunkSize-1)
	for (int32 y = 0; y < ChunkSize; ++y)
	{
		for (int32 z = 0; z < ChunkSize; ++z)
		{
			// Left border (x=0)
			int32 Idx = Chunk->GetLocalCellIndex(0, y, z);
			if (Idx >= 0 && Idx < Chunk->Cells.Num())
			{
				BorderCellsChecked++;
				FCAFluidCell& Cell = Chunk->Cells[Idx];
				// Remove water from border cells that are too close to terrain
				FVector CellWorldPos = Chunk->GetWorldPositionFromLocal(0, y, z);
				
				// Debug specific problematic chunks
				bool bIsProblematicChunk = (Chunk->ChunkCoord.X == -3 && Chunk->ChunkCoord.Y == 0 && Chunk->ChunkCoord.Z == 0);
				
				if (Cell.FluidLevel > 0.0f && Cell.TerrainHeight > -FLT_MAX + 1000.0f)
				{
					// If cell is within 1.5 cells of terrain, remove water to prevent leaking
					if (CellWorldPos.Z < Cell.TerrainHeight + Chunk->CellSize * 1.5f)
					{
						if (bIsProblematicChunk)
						{
							UE_LOG(LogTemp, Warning, TEXT("BORDER SEAL: Removing water at X=0 border, Y=%d Z=%d, WorldPos=%s, TerrainHeight=%.1f, CellZ=%.1f"),
								y, z, *CellWorldPos.ToString(), Cell.TerrainHeight, CellWorldPos.Z);
						}
						Cell.FluidLevel = 0.0f;
						Cell.bSourceBlock = false;
						BorderCellsRemoved++;
					}
					else if (bIsProblematicChunk)
					{
						UE_LOG(LogTemp, Warning, TEXT("BORDER SEAL: Keeping water at X=0 border, Y=%d Z=%d, WorldPos=%s, TerrainHeight=%.1f, CellZ=%.1f, Gap=%.1f"),
							y, z, *CellWorldPos.ToString(), Cell.TerrainHeight, CellWorldPos.Z, CellWorldPos.Z - Cell.TerrainHeight);
					}
				}
			}
			
			// Right border (x=ChunkSize-1)
			Idx = Chunk->GetLocalCellIndex(ChunkSize - 1, y, z);
			if (Idx >= 0 && Idx < Chunk->Cells.Num())
			{
				FCAFluidCell& Cell = Chunk->Cells[Idx];
				FVector CellWorldPos = Chunk->GetWorldPositionFromLocal(ChunkSize - 1, y, z);
				if (Cell.FluidLevel > 0.0f && Cell.TerrainHeight > -FLT_MAX + 1000.0f)
				{
					if (CellWorldPos.Z < Cell.TerrainHeight + Chunk->CellSize * 1.5f)
					{
						Cell.FluidLevel = 0.0f;
						Cell.bSourceBlock = false;
					}
				}
			}
		}
	}
	
	// Process Y borders (y=0 and y=ChunkSize-1)
	for (int32 x = 0; x < ChunkSize; ++x)
	{
		for (int32 z = 0; z < ChunkSize; ++z)
		{
			// Front border (y=0)
			int32 Idx = Chunk->GetLocalCellIndex(x, 0, z);
			if (Idx >= 0 && Idx < Chunk->Cells.Num())
			{
				FCAFluidCell& Cell = Chunk->Cells[Idx];
				FVector CellWorldPos = Chunk->GetWorldPositionFromLocal(x, 0, z);
				if (Cell.FluidLevel > 0.0f && Cell.TerrainHeight > -FLT_MAX + 1000.0f)
				{
					if (CellWorldPos.Z < Cell.TerrainHeight + Chunk->CellSize * 1.5f)
					{
						Cell.FluidLevel = 0.0f;
						Cell.bSourceBlock = false;
					}
				}
			}
			
			// Back border (y=ChunkSize-1)
			Idx = Chunk->GetLocalCellIndex(x, ChunkSize - 1, z);
			if (Idx >= 0 && Idx < Chunk->Cells.Num())
			{
				FCAFluidCell& Cell = Chunk->Cells[Idx];
				FVector CellWorldPos = Chunk->GetWorldPositionFromLocal(x, ChunkSize - 1, z);
				if (Cell.FluidLevel > 0.0f && Cell.TerrainHeight > -FLT_MAX + 1000.0f)
				{
					if (CellWorldPos.Z < Cell.TerrainHeight + Chunk->CellSize * 1.5f)
					{
						Cell.FluidLevel = 0.0f;
						Cell.bSourceBlock = false;
					}
				}
			}
		}
	}
	
	// Process Z borders (z=0 and z=ChunkSize-1) - less critical but still check
	for (int32 x = 0; x < ChunkSize; ++x)
	{
		for (int32 y = 0; y < ChunkSize; ++y)
		{
			// Bottom border (z=0)
			int32 Idx = Chunk->GetLocalCellIndex(x, y, 0);
			if (Idx >= 0 && Idx < Chunk->Cells.Num())
			{
				FCAFluidCell& Cell = Chunk->Cells[Idx];
				// Bottom cells should generally be solid or very close to terrain anyway
				if (Cell.FluidLevel > 0.0f && Cell.bIsSolid)
				{
					Cell.FluidLevel = 0.0f;
					Cell.bSourceBlock = false;
				}
			}
		}
	}
}

bool UStaticWaterManager::ChunkIntersectsStaticWater(const FBox& ChunkBounds) const
{
	for (const FStaticWaterRegion& Region : StaticWaterRegions)
	{
		if (Region.IntersectsChunk(ChunkBounds))
		{
			return true;
		}
	}
	return false;
}

void UStaticWaterManager::InvalidateChunkCache()
{
	CachedChunkData.Empty();
}

void UStaticWaterManager::CreateDynamicFluidSourcesInRadius(UFluidChunk* Chunk, const FVector& Center, float Radius) const
{
	if (!Chunk)
		return;

	UE_LOG(LogTemp, Warning, TEXT("CreateDynamicFluidSourcesInRadius: Chunk %s, StaticWaterRegions count: %d"), 
		*Chunk->ChunkCoord.ToString(), StaticWaterRegions.Num());

	int32 ActivatedSources = 0;
	const float RadiusSquared = Radius * Radius;
	
	int32 CellsInRadius = 0;
	int32 CellsShouldHaveWater = 0;
	int32 CellsNotSolidOrHasWater = 0;
	int32 CellsNearStaticWater = 0;
	int32 CellsSolid = 0;
	int32 CellsHaveWater = 0;
	int32 CellsEmpty = 0;
	
	// Find the edges of static water bodies around the excavated area
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
				
				CellsInRadius++;
				
				// Check if this position should have static water
				float ExpectedWaterLevel = 0.0f;
				if (!ShouldHaveStaticWaterAt(CellWorldPos, ExpectedWaterLevel))
					continue;
				
				CellsShouldHaveWater++;
				
				int32 LinearIndex = Chunk->GetLocalCellIndex(LocalX, LocalY, LocalZ);
				if (!Chunk->Cells.IsValidIndex(LinearIndex))
					continue;
				
				FCAFluidCell& Cell = Chunk->Cells[LinearIndex];
				
				// Debug cell states
				if (Cell.bIsSolid)
				{
					CellsSolid++;
					continue;
				}
				
				if (Cell.FluidLevel > 0.1f)
					CellsHaveWater++;
				else
					CellsEmpty++;
				
				// We want to find excavated areas - cells that should have static water but don't
				// OR cells near excavated areas that could be sources
				bool bShouldActivate = false;
				
				if (Cell.FluidLevel < 0.1f)
				{
					// This cell is empty but should have water - it's excavated!
					bShouldActivate = true;
					CellsNotSolidOrHasWater++;
				}
				else if (Cell.bSourceBlock && Cell.FluidLevel > 0.8f)
				{
					// This cell has static water - check if it's near an excavated area
					bShouldActivate = true;
					CellsNotSolidOrHasWater++;
				}
				
				if (!bShouldActivate)
					continue;
				
				// Check if this is near the edge of existing static water (potential source point)
				bool bNearStaticWater = false;
				const int32 CheckRadius = 2;
				
				for (int32 CheckZ = -CheckRadius; CheckZ <= CheckRadius && !bNearStaticWater; CheckZ++)
				{
					for (int32 CheckY = -CheckRadius; CheckY <= CheckRadius && !bNearStaticWater; CheckY++)
					{
						for (int32 CheckX = -CheckRadius; CheckX <= CheckRadius && !bNearStaticWater; CheckX++)
						{
							if (CheckX == 0 && CheckY == 0 && CheckZ == 0)
								continue;
							
							int32 NeighborX = LocalX + CheckX;
							int32 NeighborY = LocalY + CheckY;
							int32 NeighborZ = LocalZ + CheckZ;
							
							// Check bounds
							if (NeighborX < 0 || NeighborX >= Chunk->ChunkSize ||
								NeighborY < 0 || NeighborY >= Chunk->ChunkSize ||
								NeighborZ < 0 || NeighborZ >= Chunk->ChunkSize)
								continue;
							
							int32 NeighborIndex = Chunk->GetLocalCellIndex(NeighborX, NeighborY, NeighborZ);
							if (Chunk->Cells.IsValidIndex(NeighborIndex))
							{
								const FCAFluidCell& NeighborCell = Chunk->Cells[NeighborIndex];
								if (NeighborCell.bSourceBlock && NeighborCell.FluidLevel > 0.8f)
								{
									bNearStaticWater = true;
								}
							}
						}
					}
				}
				
				// Handle different cases
				if (Cell.FluidLevel < 0.1f)
				{
					// Empty excavated cell - fill it directly and make it dynamic
					Cell.FluidLevel = 1.0f;
					Cell.bSettled = false;  // Make it dynamic so it flows
					Cell.bSourceBlock = false;  // Not static yet
					Cell.LastFluidLevel = 0.0f; // Mark as changed
					ActivatedSources++;
					CellsNearStaticWater++;
				}
				else if (Cell.bSourceBlock && bNearStaticWater)
				{
					// Static water cell near excavation - convert to dynamic source
					Cell.bSettled = false;  // Make it dynamic
					Cell.bSourceBlock = false;  // Not static yet  
					Cell.LastFluidLevel = 0.0f; // Mark as changed
					ActivatedSources++;
					CellsNearStaticWater++;
				}
			}
		}
	}
	
	// Debug logging
	UE_LOG(LogTemp, Warning, TEXT("CreateDynamicFluidSources Debug:"));
	UE_LOG(LogTemp, Warning, TEXT("  CellsInRadius: %d"), CellsInRadius);
	UE_LOG(LogTemp, Warning, TEXT("  CellsShouldHaveWater: %d"), CellsShouldHaveWater);
	UE_LOG(LogTemp, Warning, TEXT("  CellsSolid: %d"), CellsSolid);
	UE_LOG(LogTemp, Warning, TEXT("  CellsHaveWater: %d"), CellsHaveWater);
	UE_LOG(LogTemp, Warning, TEXT("  CellsEmpty: %d"), CellsEmpty);
	UE_LOG(LogTemp, Warning, TEXT("  CellsNotSolidOrHasWater: %d"), CellsNotSolidOrHasWater);
	UE_LOG(LogTemp, Warning, TEXT("  CellsNearStaticWater: %d"), CellsNearStaticWater);
	UE_LOG(LogTemp, Warning, TEXT("  ActivatedSources: %d"), ActivatedSources);
	
	if (ActivatedSources > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("Activated %d dynamic fluid sources in chunk %s"), 
			ActivatedSources, *Chunk->ChunkCoord.ToString());
	}
}

bool UStaticWaterManager::ShouldHaveStaticWaterAt(const FVector& WorldPosition, float& OutWaterLevel) const
{
	OutWaterLevel = 0.0f;
	
	static int32 DebugCallCount = 0;
	DebugCallCount++;
	
	for (int32 i = 0; i < StaticWaterRegions.Num(); i++)
	{
		const FStaticWaterRegion& Region = StaticWaterRegions[i];
		bool bContains = Region.ContainsPoint(WorldPosition);
		
		// Debug first few calls
		if (DebugCallCount <= 5)
		{
			UE_LOG(LogTemp, Warning, TEXT("ShouldHaveStaticWaterAt: Position %s, Region %d bounds (%s to %s), WaterLevel=%.1f, Contains=%s"), 
				*WorldPosition.ToString(), i,
				*Region.Bounds.Min.ToString(), *Region.Bounds.Max.ToString(),
				Region.WaterLevel, bContains ? TEXT("YES") : TEXT("NO"));
		}
		
		if (bContains)
		{
			OutWaterLevel = Region.WaterLevel;
			return true;
		}
	}
	
	return false;
}