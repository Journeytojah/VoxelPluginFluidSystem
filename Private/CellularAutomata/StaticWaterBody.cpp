#include "CellularAutomata/StaticWaterBody.h"
#include "CellularAutomata/FluidChunk.h"

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
					
					// Check if this cell position is below the water level
					if (CellWorldPos.Z <= Region.WaterLevel && Region.Bounds.IsInsideXY(CellWorldPos))
					{
						int32 LinearIndex = Chunk->GetLocalCellIndex(LocalX, LocalY, LocalZ);
						
						// Only add water if the cell is not solid (respects terrain)
						if (Chunk->Cells.IsValidIndex(LinearIndex) && !Chunk->Cells[LinearIndex].bIsSolid)
						{
							// Check if there's solid terrain above this cell (for underwater terrain)
							bool bHasSolidAbove = false;
							for (int32 CheckZ = LocalZ + 1; CheckZ < Chunk->ChunkSize; CheckZ++)
							{
								int32 CheckIndex = Chunk->GetLocalCellIndex(LocalX, LocalY, CheckZ);
								if (Chunk->Cells.IsValidIndex(CheckIndex) && Chunk->Cells[CheckIndex].bIsSolid)
								{
									bHasSolidAbove = true;
									break;
								}
							}
							
							// Only fill with water if not enclosed by terrain
							if (!bHasSolidAbove || Region.WaterType == EStaticWaterType::Ocean)
							{
								Chunk->Cells[LinearIndex].FluidLevel = 1.0f;
								Chunk->Cells[LinearIndex].bSettled = true;
								Chunk->Cells[LinearIndex].bSourceBlock = true;
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
	if (!Chunk)
		return;

	// Check if terrain data is properly initialized by sampling a few cells
	bool bTerrainInitialized = false;
	int32 SampleCount = 0;
	const int32 MaxSamples = 10;
	
	for (int32 i = 0; i < FMath::Min(MaxSamples, Chunk->Cells.Num()) && SampleCount < MaxSamples; i += Chunk->Cells.Num() / MaxSamples)
	{
		if (Chunk->Cells[i].TerrainHeight > -FLT_MAX + 1000.0f)
		{
			bTerrainInitialized = true;
			break;
		}
		SampleCount++;
	}
	
	if (!bTerrainInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("Terrain not initialized for chunk %s, skipping static water application"), 
			*Chunk->ChunkCoord.ToString());
		return;
	}

	// This version is called after terrain has been properly initialized
	// It ensures we respect the terrain data that has been set
	for (const FStaticWaterRegion& Region : StaticWaterRegions)
	{
		FBox ChunkBounds = Chunk->GetWorldBounds();
		if (!Region.IntersectsChunk(ChunkBounds))
			continue;

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
							
							// Only add water if:
							// 1. Cell is not solid
							// 2. Cell is below the water surface level
							// 3. Cell height (terrain) allows for water
							bool bCanPlaceWater = !Cell.bIsSolid && 
												  (Cell.TerrainHeight < CellWorldPos.Z) &&
												  (CellWorldPos.Z <= Region.WaterLevel);
							
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
			UE_LOG(LogTemp, Log, TEXT("Applied static water to chunk %s: %d cells filled (terrain-aware)"), 
				*Chunk->ChunkCoord.ToString(), AppliedCells);
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