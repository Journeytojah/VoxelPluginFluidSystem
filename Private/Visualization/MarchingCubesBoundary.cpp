#include "Visualization/MarchingCubes.h"
#include "CellularAutomata/FluidChunk.h"
#include "CellularAutomata/FluidChunkManager.h"

void FMarchingCubes::GenerateChunkBoundaryStitching(UFluidChunk* FluidChunk, UFluidChunkManager* ChunkManager,
                                                  float IsoLevel, int32 ResolutionMultiplier,
                                                  TArray<FMarchingCubesVertex>& OutVertices,
                                                  TArray<FMarchingCubesTriangle>& OutTriangles)
{
    if (!FluidChunk || !ChunkManager || ResolutionMultiplier < 1)
        return;
    
    OutVertices.Empty();
    OutTriangles.Empty();
    
    const int32 ChunkSize = FluidChunk->ChunkSize;
    const float CellSize = FluidChunk->CellSize;
    const FVector ChunkOrigin = FluidChunk->ChunkWorldPosition;
    const float HighResCellSize = CellSize / ResolutionMultiplier;
    const int32 HighResSize = ChunkSize * ResolutionMultiplier;
    
    // Generate mesh for boundary region that overlaps with neighboring chunks
    // This fills the gaps between chunks by generating mesh in the overlap region
    
    // Process each face of the chunk
    for (int32 Face = 0; Face < 6; ++Face)
    {
        // Determine which axis and direction this face represents
        int32 Axis = Face / 2; // 0=X, 1=Y, 2=Z
        bool bPositiveDirection = (Face % 2) == 1;
        
        // Get the neighbor chunk in this direction
        FFluidChunkCoord NeighborCoord = FluidChunk->ChunkCoord;
        if (Axis == 0)
            NeighborCoord.X += bPositiveDirection ? 1 : -1;
        else if (Axis == 1)
            NeighborCoord.Y += bPositiveDirection ? 1 : -1;
        else
            NeighborCoord.Z += bPositiveDirection ? 1 : -1;
        
        UFluidChunk* NeighborChunk = ChunkManager->GetChunk(NeighborCoord);
        if (!NeighborChunk || NeighborChunk->State == EChunkState::Unloaded)
            continue;
        
        // Generate mesh for the boundary layer between these chunks
        // Process a thin slice at the boundary
        int32 BoundaryIndex = bPositiveDirection ? (ChunkSize - 1) : 0;
        int32 StartIdx = BoundaryIndex * ResolutionMultiplier;
        int32 EndIdx = StartIdx + ResolutionMultiplier + 1; // Include one extra for overlap
        
        // Process the boundary slice
        if (Axis == 0) // X boundary
        {
            for (int32 X = StartIdx; X <= EndIdx && X < HighResSize; ++X)
            {
                for (int32 Y = 0; Y < HighResSize; ++Y)
                {
                    for (int32 Z = 0; Z < HighResSize; ++Z)
                    {
                        ProcessBoundaryCube(FluidChunk, ChunkManager, X, Y, Z, 
                                          ResolutionMultiplier, HighResCellSize, 
                                          ChunkOrigin, IsoLevel, 
                                          OutVertices, OutTriangles);
                    }
                }
            }
        }
        else if (Axis == 1) // Y boundary
        {
            for (int32 Y = StartIdx; Y <= EndIdx && Y < HighResSize; ++Y)
            {
                for (int32 X = 0; X < HighResSize; ++X)
                {
                    for (int32 Z = 0; Z < HighResSize; ++Z)
                    {
                        // Skip corners already processed
                        float LocalX = (float)X / ResolutionMultiplier;
                        if (LocalX < 0.5f || LocalX > ChunkSize - 1.5f)
                            continue;
                        
                        ProcessBoundaryCube(FluidChunk, ChunkManager, X, Y, Z, 
                                          ResolutionMultiplier, HighResCellSize, 
                                          ChunkOrigin, IsoLevel, 
                                          OutVertices, OutTriangles);
                    }
                }
            }
        }
        else // Z boundary
        {
            for (int32 Z = StartIdx; Z <= EndIdx && Z < HighResSize; ++Z)
            {
                for (int32 X = 0; X < HighResSize; ++X)
                {
                    for (int32 Y = 0; Y < HighResSize; ++Y)
                    {
                        // Skip edges already processed
                        float LocalX = (float)X / ResolutionMultiplier;
                        float LocalY = (float)Y / ResolutionMultiplier;
                        if (LocalX < 0.5f || LocalX > ChunkSize - 1.5f)
                            continue;
                        if (LocalY < 0.5f || LocalY > ChunkSize - 1.5f)
                            continue;
                        
                        ProcessBoundaryCube(FluidChunk, ChunkManager, X, Y, Z, 
                                          ResolutionMultiplier, HighResCellSize, 
                                          ChunkOrigin, IsoLevel, 
                                          OutVertices, OutTriangles);
                    }
                }
            }
        }
    }
}

void FMarchingCubes::ProcessBoundaryCube(UFluidChunk* FluidChunk, UFluidChunkManager* ChunkManager,
                                       int32 X, int32 Y, int32 Z,
                                       int32 ResolutionMultiplier, float HighResCellSize,
                                       const FVector& ChunkOrigin, float IsoLevel,
                                       TArray<FMarchingCubesVertex>& OutVertices,
                                       TArray<FMarchingCubesTriangle>& OutTriangles)
{
    FCubeConfiguration Config;
    const FVector CubeOrigin = ChunkOrigin + FVector(X, Y, Z) * HighResCellSize;
    
    bool bHasValidDensity = false;
    for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
    {
        const FVector RelativeCorner = CubeCorners[CornerIndex];
        Config.Positions[CornerIndex] = CubeOrigin + RelativeCorner * HighResCellSize;
        
        FVector LocalPos = FVector(X, Y, Z) + RelativeCorner;
        LocalPos /= ResolutionMultiplier;
        
        Config.DensityValues[CornerIndex] = SampleDensityInterpolated(FluidChunk, ChunkManager, LocalPos);
        
        if (Config.DensityValues[CornerIndex] > 0.0f)
            bHasValidDensity = true;
    }
    
    if (bHasValidDensity)
    {
        GenerateCube(Config, IsoLevel, OutVertices, OutTriangles);
    }
}