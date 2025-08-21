#include "CellularAutomata/StaticWaterBody.h"
#include "CellularAutomata/FluidChunk.h"
#include "CellularAutomata/FluidChunkManager.h"
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
							
							// Check if we can place water in this cell
							bool bCanPlaceWater = false;
							
							if (Cell.TerrainHeight <= -10000.0f)
							{
								// No terrain data - likely upper ocean chunk
								// Allow water if below water level
								bCanPlaceWater = (CellWorldPos.Z <= Region.WaterLevel);
							}
							else
							{
								// Has terrain data - check if air cell
								bool bIsAirCell = (CellWorldPos.Z > Cell.TerrainHeight);
								bCanPlaceWater = bIsAirCell && (CellWorldPos.Z <= Region.WaterLevel);
							}
							
							// Only place water if conditions are met
							if (bCanPlaceWater)
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

void UStaticWaterManager::ApplyStaticWaterToChunkWithTerrain(UFluidChunk* Chunk, class UFluidChunkManager* ChunkManager) const
{
	SCOPE_CYCLE_COUNTER(STAT_VoxelFluid_StaticWaterApply);
	
	if (!Chunk)
		return;
	
	// Track water application history
	static TMap<FFluidChunkCoord, int32> ChunkWaterApplicationCount;
	int32& ApplicationCount = ChunkWaterApplicationCount.FindOrAdd(Chunk->ChunkCoord, 0);
	ApplicationCount++;
	
	if (ApplicationCount > 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("REAPPLYING: Chunk %s water application #%d"), 
			*Chunk->ChunkCoord.ToString(), ApplicationCount);
	}

	// CRITICAL DEBUG: Log every chunk that gets water applied
	FBox ChunkBounds = Chunk->GetWorldBounds();
	UE_LOG(LogTemp, Warning, TEXT("====== APPLYING STATIC WATER TO CHUNK %s ======"), *Chunk->ChunkCoord.ToString());
	UE_LOG(LogTemp, Warning, TEXT("Chunk World Bounds: Min=%s, Max=%s"), 
		*ChunkBounds.Min.ToString(), *ChunkBounds.Max.ToString());
	
	// Count existing water before we do anything
	int32 ExistingWaterCells = 0;
	for (const auto& Cell : Chunk->Cells)
	{
		if (Cell.FluidLevel > 0.0f && !Cell.bIsSolid)
			ExistingWaterCells++;
	}
	if (ExistingWaterCells > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("  Chunk already has %d water cells before processing"), ExistingWaterCells);
	}
	
	// Check what water regions affect this chunk
	bool bAnyRegionIntersects = false;
	UE_LOG(LogTemp, Warning, TEXT("Checking %d water regions for intersection..."), StaticWaterRegions.Num());
	
	for (int32 i = 0; i < StaticWaterRegions.Num(); i++)
	{
		const FStaticWaterRegion& Region = StaticWaterRegions[i];
		bool bIntersects = Region.IntersectsChunk(ChunkBounds);
		
		if (bIntersects)
		{
			bAnyRegionIntersects = true;
			UE_LOG(LogTemp, Warning, TEXT("  Region %d INTERSECTS: WaterLevel=%.1f, Type=%d, Bounds=(%s to %s)"), 
				i, Region.WaterLevel, (int32)Region.WaterType,
				*Region.Bounds.Min.ToString(), *Region.Bounds.Max.ToString());
		}
		else
		{
			// Log why it doesn't intersect
			UE_LOG(LogTemp, Verbose, TEXT("  Region %d NO INTERSECT: WaterLevel=%.1f, RegionBounds=(%s to %s)"), 
				i, Region.WaterLevel,
				*Region.Bounds.Min.ToString(), *Region.Bounds.Max.ToString());
		}
	}
	
	if (!bAnyRegionIntersects)
	{
		UE_LOG(LogTemp, Warning, TEXT("NO WATER REGIONS INTERSECT THIS CHUNK - Skipping"));
		return;
	}
	
	// Check if we have a chunk manager for terrain inheritance
	if (ChunkManager)
	{
		UE_LOG(LogTemp, Warning, TEXT("ChunkManager available - will check chunks below for terrain"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("WARNING: No ChunkManager - cannot check chunks below!"));
	}
	
	// Count solid cells to detect if terrain is properly set
	int32 SolidCellCount = 0;
	int32 TotalCells = Chunk->Cells.Num();
	int32 CellsWithTerrainData = 0;
	
	for (const auto& Cell : Chunk->Cells)
	{
		if (Cell.bIsSolid)
			SolidCellCount++;
		if (Cell.TerrainHeight > -10000.0f)
			CellsWithTerrainData++;
	}
	
	float SolidPercentage = (SolidCellCount / (float)TotalCells) * 100.0f;
	float TerrainDataPercentage = (CellsWithTerrainData / (float)TotalCells) * 100.0f;
	
	UE_LOG(LogTemp, Warning, TEXT("Chunk %s: %d/%d cells are solid (%.1f%%), %d/%d have terrain data (%.1f%%)"), 
		*Chunk->ChunkCoord.ToString(), SolidCellCount, TotalCells, SolidPercentage,
		CellsWithTerrainData, TotalCells, TerrainDataPercentage);
	
	// Special case: If ALL cells are solid, this chunk is completely underground
	if (SolidPercentage >= 99.9f)
	{
		UE_LOG(LogTemp, Warning, TEXT("Chunk %s is 100%% solid - completely underground, no water can be placed"),
			*Chunk->ChunkCoord.ToString());
		
		// But wait - check if the TOP of the chunk is above water level
		UE_LOG(LogTemp, Error, TEXT("ERROR: Chunk %s is marked 100%% solid!"),
			*Chunk->ChunkCoord.ToString());
		UE_LOG(LogTemp, Error, TEXT("  Chunk bounds: Z from %.1f to %.1f"),
			ChunkBounds.Min.Z, ChunkBounds.Max.Z);
		UE_LOG(LogTemp, Error, TEXT("  Water level: %.1f"), -500.0f);
		
		// Sample cells at different heights to see their terrain heights
		UE_LOG(LogTemp, Error, TEXT("  Sampling cells at different Z levels:"));
		
		// Check bottom cell
		int32 BottomIdx = Chunk->GetLocalCellIndex(0, 0, 0);
		if (BottomIdx >= 0 && BottomIdx < Chunk->Cells.Num())
		{
			FVector BottomPos = Chunk->GetWorldPositionFromLocal(0, 0, 0);
			UE_LOG(LogTemp, Error, TEXT("    Bottom cell: WorldZ=%.1f, TerrainHeight=%.1f, Solid=%s"),
				BottomPos.Z, Chunk->Cells[BottomIdx].TerrainHeight,
				Chunk->Cells[BottomIdx].bIsSolid ? TEXT("YES") : TEXT("NO"));
		}
		
		// Check middle cell
		int32 MiddleZ = Chunk->ChunkSize / 2;
		int32 MiddleIdx = Chunk->GetLocalCellIndex(0, 0, MiddleZ);
		if (MiddleIdx >= 0 && MiddleIdx < Chunk->Cells.Num())
		{
			FVector MiddlePos = Chunk->GetWorldPositionFromLocal(0, 0, MiddleZ);
			UE_LOG(LogTemp, Error, TEXT("    Middle cell: WorldZ=%.1f, TerrainHeight=%.1f, Solid=%s"),
				MiddlePos.Z, Chunk->Cells[MiddleIdx].TerrainHeight,
				Chunk->Cells[MiddleIdx].bIsSolid ? TEXT("YES") : TEXT("NO"));
		}
		
		// Check top cell
		int32 TopIdx = Chunk->GetLocalCellIndex(0, 0, Chunk->ChunkSize - 1);
		if (TopIdx >= 0 && TopIdx < Chunk->Cells.Num())
		{
			FVector TopPos = Chunk->GetWorldPositionFromLocal(0, 0, Chunk->ChunkSize - 1);
			UE_LOG(LogTemp, Error, TEXT("    Top cell: WorldZ=%.1f, TerrainHeight=%.1f, Solid=%s"),
				TopPos.Z, Chunk->Cells[TopIdx].TerrainHeight,
				Chunk->Cells[TopIdx].bIsSolid ? TEXT("YES") : TEXT("NO"));
		}
		
		UE_LOG(LogTemp, Error, TEXT("  This chunk should have air cells if terrain is below %.1f!"),
			ChunkBounds.Max.Z);
		return; // No point trying to add water to a completely solid chunk
	}
	
	// SPECIAL CASE: If chunk has NO terrain data at all, it's likely a water column chunk
	bool bIsWaterColumnChunk = (TerrainDataPercentage < 5.0f) && (SolidPercentage < 5.0f);
	
	// Also check if this is an INVERTED water column (terrain above but cells not solid)
	// This happens when terrain is ABOVE the chunk but chunk is BELOW water level  
	bool bIsInvertedWaterColumn = false;
	if ((TerrainDataPercentage > 95.0f) && (SolidPercentage < 5.0f))
	{
		// Check if any part of this chunk is below water level
		for (const FStaticWaterRegion& Region : StaticWaterRegions)
		{
			if (Region.IntersectsChunk(ChunkBounds))
			{
				// If chunk bottom is below water level, it's a valid water column
				if (ChunkBounds.Min.Z < Region.WaterLevel)
				{
					bIsInvertedWaterColumn = true;
					UE_LOG(LogTemp, Warning, TEXT("Valid inverted water column: Chunk %s bottom %.1f < WaterLevel %.1f"),
						*Chunk->ChunkCoord.ToString(), ChunkBounds.Min.Z, Region.WaterLevel);
					break;
				}
			}
		}
		
		// If we have terrain data but no valid water intersection, this is underground space
		if (!bIsInvertedWaterColumn)
		{
			UE_LOG(LogTemp, Warning, TEXT("SKIPPING false water column: Chunk %s has terrain above but not in water region"),
				*Chunk->ChunkCoord.ToString());
		}
	}
	
	if (bIsWaterColumnChunk)
	{
		UE_LOG(LogTemp, Warning, TEXT("WATER COLUMN CHUNK DETECTED: %s - No terrain data (%.1f%%), treating as pure water column"),
			*Chunk->ChunkCoord.ToString(), TerrainDataPercentage);
	}
	else if (bIsInvertedWaterColumn)
	{
		UE_LOG(LogTemp, Warning, TEXT("INVERTED WATER COLUMN DETECTED: %s - Has terrain data (%.1f%%) but not solid (%.1f%%), chunk is below water level"),
			*Chunk->ChunkCoord.ToString(), TerrainDataPercentage, SolidPercentage);
	}
	
	// Handle both types of water column chunks
	if (bIsWaterColumnChunk || bIsInvertedWaterColumn)
	{
		// Check if we should wait for chunks below (only for regular water columns, not inverted)
		if (bIsWaterColumnChunk && ChunkManager)
		{
			// Check if the chunk below exists and is loaded
			FFluidChunkCoord BelowCoord = Chunk->ChunkCoord;
			BelowCoord.Z -= 1;
			
			UFluidChunk* ChunkBelow = ChunkManager->GetChunk(BelowCoord);
			
			// If we expect a chunk below (we're not at the bottom), but it's not loaded yet
			if (Chunk->ChunkCoord.Z > -10 && !ChunkBelow) // Assuming -10 is reasonably low
			{
				UE_LOG(LogTemp, Warning, TEXT("DEFERRING: Water column chunk %s is waiting for chunk below to load first"),
					*Chunk->ChunkCoord.ToString());
				// Don't apply water yet - it will be applied when the chunk below loads
				return;
			}
		}
		
		// This chunk is entirely above/below terrain - check if it should have ocean water
		
		for (const FStaticWaterRegion& Region : StaticWaterRegions)
		{
			if (!Region.IntersectsChunk(ChunkBounds))
				continue;
				
			// Fill the entire chunk with water if it's below the water level
			if (ChunkBounds.Max.Z <= Region.WaterLevel)
			{
				UE_LOG(LogTemp, Warning, TEXT("Filling water column chunk %s with water (ChunkMaxZ:%.1f <= WaterLevel:%.1f)"),
					*Chunk->ChunkCoord.ToString(), ChunkBounds.Max.Z, Region.WaterLevel);
				
				// Fill all cells with water
				int32 FilledCells = 0;
				for (int32 i = 0; i < Chunk->Cells.Num(); ++i)
				{
					if (!Chunk->Cells[i].bIsSolid) // Double-check not solid
					{
						Chunk->Cells[i].FluidLevel = 1.0f;
						Chunk->Cells[i].bSettled = true;
						Chunk->Cells[i].bSourceBlock = true;
						FilledCells++;
					}
				}
				UE_LOG(LogTemp, Warning, TEXT("  Filled %d cells in fully submerged chunk"), FilledCells);
				return; // Done with this chunk
			}
			else if (ChunkBounds.Min.Z < Region.WaterLevel)
			{
				// Partially submerged water column
				UE_LOG(LogTemp, Warning, TEXT("Partially filling water column chunk %s (ChunkMinZ:%.1f < WaterLevel:%.1f < ChunkMaxZ:%.1f)"),
					*Chunk->ChunkCoord.ToString(), ChunkBounds.Min.Z, Region.WaterLevel, ChunkBounds.Max.Z);
				
				int32 FilledCells = 0;
				for (int32 LocalZ = 0; LocalZ < Chunk->ChunkSize; LocalZ++)
				{
					for (int32 LocalY = 0; LocalY < Chunk->ChunkSize; LocalY++)
					{
						for (int32 LocalX = 0; LocalX < Chunk->ChunkSize; LocalX++)
						{
							FVector CellWorldPos = Chunk->GetWorldPositionFromLocal(LocalX, LocalY, LocalZ);
							if (CellWorldPos.Z <= Region.WaterLevel)
							{
								int32 Idx = Chunk->GetLocalCellIndex(LocalX, LocalY, LocalZ);
								if (!Chunk->Cells[Idx].bIsSolid)
								{
									Chunk->Cells[Idx].FluidLevel = 1.0f;
									Chunk->Cells[Idx].bSettled = true;
									Chunk->Cells[Idx].bSourceBlock = true;
									FilledCells++;
								}
							}
						}
					}
				}
				UE_LOG(LogTemp, Warning, TEXT("  Filled %d cells in partially submerged chunk"), FilledCells);
				return; // Done with this chunk
			}
		}
		
		UE_LOG(LogTemp, Warning, TEXT("Water column chunk %s is above water level, no water added"),
			*Chunk->ChunkCoord.ToString());
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
	// ChunkBounds already defined at the top of the function
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
							
							// CRITICAL: Check if this is an AIR cell (above terrain surface) OR a deep water cell
							// We need to handle multiple cases:
							// 1. Cell has valid terrain data - check if above terrain
							// 2. Cell has no terrain data - check chunks below for terrain
							// 3. Deep water case - no terrain found within 2 chunks below
							
							bool bCanPlaceWater = false;
							float EffectiveTerrainHeight = Cell.TerrainHeight;
							
							// If this cell has no terrain data, check chunks below
							if (Cell.TerrainHeight <= -10000.0f && ChunkManager)
							{
								// Debug: Log when we're checking chunks below
								static int32 CheckBelowCount = 0;
								bool bShouldLog = (CheckBelowCount++ < 100) || (Chunk->ChunkCoord.Z >= 0);
								
								if (bShouldLog && LocalX == 0 && LocalY == 0) // Log once per chunk
								{
									UE_LOG(LogTemp, Warning, TEXT("Chunk %s has no terrain at cell [0,0,%d], checking chunks below..."),
										*Chunk->ChunkCoord.ToString(), LocalZ);
								}
								
								// Check up to 2 chunks below for terrain data
								bool bFoundTerrain = false;
								
								for (int32 ChunksBelow = 1; ChunksBelow <= 2; ChunksBelow++)
								{
									FFluidChunkCoord BelowCoord = Chunk->ChunkCoord;
									BelowCoord.Z -= ChunksBelow;
									
									UFluidChunk* ChunkBelow = ChunkManager->GetChunk(BelowCoord);
									
									if (bShouldLog && LocalX == 0 && LocalY == 0)
									{
										UE_LOG(LogTemp, Warning, TEXT("  Checking chunk %s (%d below): %s"),
											*BelowCoord.ToString(), ChunksBelow,
											ChunkBelow ? TEXT("EXISTS") : TEXT("NOT LOADED"));
									}
									
									if (ChunkBelow && ChunkBelow->Cells.Num() > 0)
									{
										// Sample terrain from the corresponding cell in the chunk below
										// For bottom cells (LocalZ == 0), check top cells of chunk below
										int32 BelowCellIndex = ChunkBelow->GetLocalCellIndex(LocalX, LocalY, ChunkBelow->ChunkSize - 1);
										
										if (ChunkBelow->Cells.IsValidIndex(BelowCellIndex))
										{
											float BelowTerrainHeight = ChunkBelow->Cells[BelowCellIndex].TerrainHeight;
											
											if (BelowTerrainHeight > -10000.0f)
											{
												// Found valid terrain data!
												EffectiveTerrainHeight = BelowTerrainHeight;
												bFoundTerrain = true;
												
												static int32 InheritCount = 0;
												if (InheritCount++ < 20)
												{
													UE_LOG(LogTemp, Log, TEXT("Inherited terrain from chunk %d below: Height=%.1f for cell at %s"),
														ChunksBelow, EffectiveTerrainHeight, *CellWorldPos.ToString());
												}
												break;
											}
										}
									}
								}
								
								// If we still haven't found terrain after checking 2 chunks below,
								// this is deep ocean - allow water if below water level
								if (!bFoundTerrain)
								{
									bCanPlaceWater = (CellWorldPos.Z <= Region.WaterLevel);
									
									static int32 DeepOceanCount = 0;
									if (DeepOceanCount++ < 10)
									{
										UE_LOG(LogTemp, Log, TEXT("Deep ocean (no terrain in 2 chunks): Cell at %s, WaterLevel:%.1f, Placing:%s"),
											*CellWorldPos.ToString(), Region.WaterLevel,
											bCanPlaceWater ? TEXT("YES") : TEXT("NO"));
									}
								}
								else
								{
									// We found terrain from a chunk below - check if we're above it
									bool bIsAirCell = (CellWorldPos.Z > EffectiveTerrainHeight);
									bCanPlaceWater = bIsAirCell && (CellWorldPos.Z <= Region.WaterLevel);
								}
							}
							else if (Cell.TerrainHeight > -10000.0f)
							{
								// Cell has valid terrain data - use normal logic
								bool bIsAirCell = (CellWorldPos.Z > Cell.TerrainHeight);
								bCanPlaceWater = bIsAirCell && (CellWorldPos.Z <= Region.WaterLevel);
							}
							else
							{
								// No chunk manager or unable to check - fall back to simple check
								bCanPlaceWater = (CellWorldPos.Z <= Region.WaterLevel);
							}
							
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
									bool bIsDeepWater = (Cell.TerrainHeight <= -10000.0f);
									UE_LOG(LogTemp, Warning, TEXT("Static water: Cell at %s PLACED - DeepWater:%s, Solid:%s, TerrainHeight:%.1f, CellZ:%.1f, WaterLevel:%.1f (Chunk:%s)"),
										*CellWorldPos.ToString(), 
										bIsDeepWater ? TEXT("YES") : TEXT("NO"),
										Cell.bIsSolid ? TEXT("YES") : TEXT("NO"),
										Cell.TerrainHeight, 
										CellWorldPos.Z, 
										Region.WaterLevel,
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
									bool bIsDeepWater = (Cell.TerrainHeight <= -10000.0f);
									bool bIsAirCell = !bIsDeepWater && (CellWorldPos.Z > Cell.TerrainHeight);
									UE_LOG(LogTemp, Warning, TEXT("Static water: Cell at %s NOT placed - DeepWater:%s, AirCell:%s, Solid:%s, TerrainHeight:%.1f, CellZ:%.1f, WaterLevel:%.1f"),
										*CellWorldPos.ToString(), 
										bIsDeepWater ? TEXT("YES") : TEXT("NO"),
										bIsAirCell ? TEXT("YES") : TEXT("NO"),
										Cell.bIsSolid ? TEXT("YES") : TEXT("NO"),
										Cell.TerrainHeight, 
										CellWorldPos.Z, 
										Region.WaterLevel);
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
	
	// FINAL SUMMARY: Count how much water was actually added to this chunk
	int32 FinalWaterCells = 0;
	for (const auto& Cell : Chunk->Cells)
	{
		if (Cell.FluidLevel > 0.0f && !Cell.bIsSolid)
		{
			FinalWaterCells++;
		}
	}
	
	float FinalWaterPercentage = (FinalWaterCells / (float)Chunk->Cells.Num()) * 100.0f;
	
	if (FinalWaterCells == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("WARNING: Chunk %s has NO WATER after processing! (0 cells with water)"),
			*Chunk->ChunkCoord.ToString());
		UE_LOG(LogTemp, Error, TEXT("  Chunk bounds: Z from %.1f to %.1f"),
			ChunkBounds.Min.Z, ChunkBounds.Max.Z);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Chunk %s final water count: %d cells (%.1f%% of chunk)"),
			*Chunk->ChunkCoord.ToString(), FinalWaterCells, FinalWaterPercentage);
	}
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
				
				// CRITICAL: Only fill areas that are CONNECTED to existing water
				// Check if there's existing water nearby (within 3 cells)
				bool bConnectedToWater = false;
				const int32 ConnectionRadius = 3;
				
				for (int32 CheckZ = -ConnectionRadius; CheckZ <= ConnectionRadius && !bConnectedToWater; CheckZ++)
				{
					for (int32 CheckY = -ConnectionRadius; CheckY <= ConnectionRadius && !bConnectedToWater; CheckY++)
					{
						for (int32 CheckX = -ConnectionRadius; CheckX <= ConnectionRadius && !bConnectedToWater; CheckX++)
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
								
								// Found existing water nearby (either static or dynamic)
								if (NeighborCell.FluidLevel > 0.5f)
								{
									bConnectedToWater = true;
								}
							}
						}
					}
				}
				
				// Skip this cell if it's not connected to existing water
				if (!bConnectedToWater)
				{
					static int32 SkipCount = 0;
					if (SkipCount++ < 10)
					{
						UE_LOG(LogTemp, Warning, TEXT("SKIPPING isolated excavation at %s - not connected to existing water"),
							*CellWorldPos.ToString());
					}
					continue;
				}
				
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