#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"

/**
 * Complete Marching Cubes implementation for fluid surface generation.
 * This class contains the full lookup tables and algorithms for generating
 * smooth surfaces from 3D scalar fields (fluid density values).
 */
class VOXELFLUIDSYSTEM_API FMarchingCubes
{
public:
    // Vertex structure for generated mesh
    struct FMarchingCubesVertex
    {
        FVector Position;
        FVector Normal;
        FVector2D UV;
        
        FMarchingCubesVertex() : Position(FVector::ZeroVector), Normal(FVector::ZeroVector), UV(FVector2D::ZeroVector) {}
        FMarchingCubesVertex(const FVector& InPos, const FVector& InNormal, const FVector2D& InUV)
            : Position(InPos), Normal(InNormal), UV(InUV) {}
    };

    // Triangle structure
    struct FMarchingCubesTriangle
    {
        int32 VertexIndices[3];
        
        FMarchingCubesTriangle(int32 V0, int32 V1, int32 V2)
        {
            VertexIndices[0] = V0;
            VertexIndices[1] = V1;
            VertexIndices[2] = V2;
        }
    };

    // Configuration for a single cube
    struct FCubeConfiguration
    {
        float DensityValues[8];    // 8 corner density values
        FVector Positions[8];      // 8 corner world positions
        
        FCubeConfiguration()
        {
            FMemory::Memzero(DensityValues, sizeof(DensityValues));
            for (int32 i = 0; i < 8; ++i)
            {
                Positions[i] = FVector::ZeroVector;
            }
        }
    };

private:
    // === COMPLETE MARCHING CUBES LOOKUP TABLES ===
    
    // Edge connection table - maps cube configuration to which edges contain vertices
    // Each entry corresponds to one of the 256 possible cube configurations
    static const int32 EdgeTable[256];
    
    // Triangle table - defines which triangles to generate for each configuration
    // Each row contains up to 16 values (5 triangles max, terminated by -1)
    static const int32 TriangleTable[256][16];
    
    // Edge vertex positions - defines where vertices are interpolated along edges
    // 12 edges per cube, each edge connects two corners
    static const int32 EdgeVertexIndices[12][2];
    
    // Corner positions for a unit cube (0,0,0) to (1,1,1)
    static const FVector CubeCorners[8];

public:
    /**
     * Generate mesh data for a single marching cube
     * @param Config - The cube configuration with density values and positions
     * @param IsoLevel - The density threshold for surface generation (typically 0.5)
     * @param OutVertices - Generated vertices
     * @param OutTriangles - Generated triangles
     */
    static void GenerateCube(const FCubeConfiguration& Config, float IsoLevel,
                           TArray<FMarchingCubesVertex>& OutVertices, 
                           TArray<FMarchingCubesTriangle>& OutTriangles);

    /**
     * Generate mesh data for a 3D grid of density values
     * @param DensityGrid - 3D array of density values [x][y][z]
     * @param GridSize - Dimensions of the density grid
     * @param CellSize - Size of each cell in world units
     * @param GridOrigin - World position of the grid origin
     * @param IsoLevel - The density threshold for surface generation
     * @param OutVertices - Generated vertices
     * @param OutTriangles - Generated triangles
     */
    static void GenerateGridMesh(const TArray<float>& DensityGrid,
                               const FIntVector& GridSize,
                               float CellSize,
                               const FVector& GridOrigin,
                               float IsoLevel,
                               TArray<FMarchingCubesVertex>& OutVertices,
                               TArray<FMarchingCubesTriangle>& OutTriangles);

    /**
     * Generate mesh for a fluid chunk using marching cubes
     * @param FluidChunk - The chunk containing fluid density data
     * @param IsoLevel - The density threshold for surface generation
     * @param OutVertices - Generated vertices
     * @param OutTriangles - Generated triangles
     */
    static void GenerateChunkMesh(class UFluidChunk* FluidChunk,
                                float IsoLevel,
                                TArray<FMarchingCubesVertex>& OutVertices,
                                TArray<FMarchingCubesTriangle>& OutTriangles);

private:
    /**
     * Interpolate vertex position along an edge based on density values
     */
    static FVector InterpolateVertex(const FVector& P1, const FVector& P2, float V1, float V2, float IsoLevel);
    
    /**
     * Calculate surface normal at a vertex using gradient approximation
     */
    static FVector CalculateNormal(const TArray<float>& DensityGrid, const FIntVector& GridSize, 
                                 const FIntVector& GridPos, float CellSize);
    
    /**
     * Generate UV coordinates for a vertex based on its position
     */
    static FVector2D CalculateUV(const FVector& Position, const FVector& GridOrigin, float GridExtent);
    
    /**
     * Get cube configuration index (0-255) based on density values and iso level
     */
    static int32 GetCubeIndex(const float DensityValues[8], float IsoLevel);
    
    /**
     * Get density value at grid position with bounds checking
     */
    static float GetDensityAt(const TArray<float>& DensityGrid, const FIntVector& GridSize, 
                            int32 X, int32 Y, int32 Z);
};