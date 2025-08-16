#include "Visualization/MarchingCubes.h"
#include "CellularAutomata/FluidChunk.h"
#include "CellularAutomata/FluidChunkManager.h"

// === COMPLETE MARCHING CUBES LOOKUP TABLES ===

// Edge table: For each of the 256 possible cube configurations,
// this table specifies which of the 12 edges contain vertices
const int32 FMarchingCubes::EdgeTable[256] = {
    0x0  , 0x109, 0x203, 0x30a, 0x406, 0x50f, 0x605, 0x70c,
    0x80c, 0x905, 0xa0f, 0xb06, 0xc0a, 0xd03, 0xe09, 0xf00,
    0x190, 0x99 , 0x393, 0x29a, 0x596, 0x49f, 0x795, 0x69c,
    0x99c, 0x895, 0xb9f, 0xa96, 0xd9a, 0xc93, 0xf99, 0xe90,
    0x230, 0x339, 0x33 , 0x13a, 0x636, 0x73f, 0x435, 0x53c,
    0xa3c, 0xb35, 0x83f, 0x936, 0xe3a, 0xf33, 0xc39, 0xd30,
    0x3a0, 0x2a9, 0x1a3, 0xaa , 0x7a6, 0x6af, 0x5a5, 0x4ac,
    0xbac, 0xaa5, 0x9af, 0x8a6, 0xfaa, 0xea3, 0xda9, 0xca0,
    0x460, 0x569, 0x663, 0x76a, 0x66 , 0x16f, 0x265, 0x36c,
    0xc6c, 0xd65, 0xe6f, 0xf66, 0x86a, 0x963, 0xa69, 0xb60,
    0x5f0, 0x4f9, 0x7f3, 0x6fa, 0x1f6, 0xff , 0x3f5, 0x2fc,
    0xdfc, 0xcf5, 0xfff, 0xef6, 0x9fa, 0x8f3, 0xbf9, 0xaf0,
    0x650, 0x759, 0x453, 0x55a, 0x256, 0x35f, 0x55 , 0x15c,
    0xe5c, 0xf55, 0xc5f, 0xd56, 0xa5a, 0xb53, 0x859, 0x950,
    0x7c0, 0x6c9, 0x5c3, 0x4ca, 0x3c6, 0x2cf, 0x1c5, 0xcc ,
    0xfcc, 0xec5, 0xdcf, 0xcc6, 0xbca, 0xac3, 0x9c9, 0x8c0,
    0x8c0, 0x9c9, 0xac3, 0xbca, 0xcc6, 0xdcf, 0xec5, 0xfcc,
    0xcc , 0x1c5, 0x2cf, 0x3c6, 0x4ca, 0x5c3, 0x6c9, 0x7c0,
    0x950, 0x859, 0xb53, 0xa5a, 0xd56, 0xc5f, 0xf55, 0xe5c,
    0x15c, 0x55 , 0x35f, 0x256, 0x55a, 0x453, 0x759, 0x650,
    0xaf0, 0xbf9, 0x8f3, 0x9fa, 0xef6, 0xfff, 0xcf5, 0xdfc,
    0x2fc, 0x3f5, 0xff , 0x1f6, 0x6fa, 0x7f3, 0x4f9, 0x5f0,
    0xb60, 0xa69, 0x963, 0x86a, 0xf66, 0xe6f, 0xd65, 0xc6c,
    0x36c, 0x265, 0x16f, 0x66 , 0x76a, 0x663, 0x569, 0x460,
    0xca0, 0xda9, 0xea3, 0xfaa, 0x8a6, 0x9af, 0xaa5, 0xbac,
    0x4ac, 0x5a5, 0x6af, 0x7a6, 0xaa , 0x1a3, 0x2a9, 0x3a0,
    0xd30, 0xc39, 0xf33, 0xe3a, 0x936, 0x83f, 0xb35, 0xa3c,
    0x53c, 0x435, 0x73f, 0x636, 0x13a, 0x33 , 0x339, 0x230,
    0xe90, 0xf99, 0xc93, 0xd9a, 0xa96, 0xb9f, 0x895, 0x99c,
    0x69c, 0x795, 0x49f, 0x596, 0x29a, 0x393, 0x99 , 0x190,
    0xf00, 0xe09, 0xd03, 0xc0a, 0xb06, 0xa0f, 0x905, 0x80c,
    0x70c, 0x605, 0x50f, 0x406, 0x30a, 0x203, 0x109, 0x0
};

// Triangle table: For each cube configuration, defines which triangles to generate
// Each row contains up to 16 values (max 5 triangles), terminated by -1
const int32 FMarchingCubes::TriangleTable[256][16] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 8, 3, 9, 8, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 2, 10, 0, 2, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 8, 3, 2, 10, 8, 10, 9, 8, -1, -1, -1, -1, -1, -1, -1},
    {3, 11, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 11, 2, 8, 11, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 9, 0, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 11, 2, 1, 9, 11, 9, 8, 11, -1, -1, -1, -1, -1, -1, -1},
    {3, 10, 1, 11, 10, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 10, 1, 0, 8, 10, 8, 11, 10, -1, -1, -1, -1, -1, -1, -1},
    {3, 9, 0, 3, 11, 9, 11, 10, 9, -1, -1, -1, -1, -1, -1, -1},
    {9, 8, 10, 10, 8, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 3, 0, 7, 3, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 9, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 1, 9, 4, 7, 1, 7, 3, 1, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {3, 4, 7, 3, 0, 4, 1, 2, 10, -1, -1, -1, -1, -1, -1, -1},
    {9, 2, 10, 9, 0, 2, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1},
    {2, 10, 9, 2, 9, 7, 2, 7, 3, 7, 9, 4, -1, -1, -1, -1},
    {8, 4, 7, 3, 11, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {11, 4, 7, 11, 2, 4, 2, 0, 4, -1, -1, -1, -1, -1, -1, -1},
    {9, 0, 1, 8, 4, 7, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1},
    {4, 7, 11, 9, 4, 11, 9, 11, 2, 9, 2, 1, -1, -1, -1, -1},
    {3, 10, 1, 3, 11, 10, 7, 8, 4, -1, -1, -1, -1, -1, -1, -1},
    {1, 11, 10, 1, 4, 11, 1, 0, 4, 7, 11, 4, -1, -1, -1, -1},
    {4, 7, 8, 9, 0, 11, 9, 11, 10, 11, 0, 3, -1, -1, -1, -1},
    {4, 7, 11, 4, 11, 9, 9, 11, 10, -1, -1, -1, -1, -1, -1, -1},
    {9, 5, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 5, 4, 0, 8, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 5, 4, 1, 5, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {8, 5, 4, 8, 3, 5, 3, 1, 5, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, 9, 5, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {3, 0, 8, 1, 2, 10, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1},
    {5, 2, 10, 5, 4, 2, 4, 0, 2, -1, -1, -1, -1, -1, -1, -1},
    {2, 10, 5, 3, 2, 5, 3, 5, 4, 3, 4, 8, -1, -1, -1, -1},
    {9, 5, 4, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 11, 2, 0, 8, 11, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1},
    {0, 5, 4, 0, 1, 5, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1},
    {2, 1, 5, 2, 5, 8, 2, 8, 11, 4, 8, 5, -1, -1, -1, -1},
    {10, 3, 11, 10, 1, 3, 9, 5, 4, -1, -1, -1, -1, -1, -1, -1},
    {4, 9, 5, 0, 8, 1, 8, 10, 1, 8, 11, 10, -1, -1, -1, -1},
    {5, 4, 0, 5, 0, 11, 5, 11, 10, 11, 0, 3, -1, -1, -1, -1},
    {5, 4, 8, 5, 8, 10, 10, 8, 11, -1, -1, -1, -1, -1, -1, -1},
    {9, 7, 8, 5, 7, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 3, 0, 9, 5, 3, 5, 7, 3, -1, -1, -1, -1, -1, -1, -1},
    {0, 7, 8, 0, 1, 7, 1, 5, 7, -1, -1, -1, -1, -1, -1, -1},
    {1, 5, 3, 3, 5, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 7, 8, 9, 5, 7, 10, 1, 2, -1, -1, -1, -1, -1, -1, -1},
    {10, 1, 2, 9, 5, 0, 5, 3, 0, 5, 7, 3, -1, -1, -1, -1},
    {8, 0, 2, 8, 2, 5, 8, 5, 7, 10, 5, 2, -1, -1, -1, -1},
    {2, 10, 5, 2, 5, 3, 3, 5, 7, -1, -1, -1, -1, -1, -1, -1},
    {7, 9, 5, 7, 8, 9, 3, 11, 2, -1, -1, -1, -1, -1, -1, -1},
    {9, 5, 7, 9, 7, 2, 9, 2, 0, 2, 7, 11, -1, -1, -1, -1},
    {2, 3, 11, 0, 1, 8, 1, 7, 8, 1, 5, 7, -1, -1, -1, -1},
    {11, 2, 1, 11, 1, 7, 7, 1, 5, -1, -1, -1, -1, -1, -1, -1},
    {9, 5, 8, 8, 5, 7, 10, 1, 3, 10, 3, 11, -1, -1, -1, -1},
    {5, 7, 0, 5, 0, 9, 7, 11, 0, 1, 0, 10, 11, 10, 0, -1},
    {11, 10, 0, 11, 0, 3, 10, 5, 0, 8, 0, 7, 5, 7, 0, -1},
    {11, 10, 5, 7, 11, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {10, 6, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 0, 1, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 8, 3, 1, 9, 8, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1},
    {1, 6, 5, 2, 6, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 6, 5, 1, 2, 6, 3, 0, 8, -1, -1, -1, -1, -1, -1, -1},
    {9, 6, 5, 9, 0, 6, 0, 2, 6, -1, -1, -1, -1, -1, -1, -1},
    {5, 9, 8, 5, 8, 2, 5, 2, 6, 3, 2, 8, -1, -1, -1, -1},
    {2, 3, 11, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {11, 0, 8, 11, 2, 0, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 9, 2, 3, 11, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1},
    {5, 10, 6, 1, 9, 2, 9, 11, 2, 9, 8, 11, -1, -1, -1, -1},
    {6, 3, 11, 6, 5, 3, 5, 1, 3, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 11, 0, 11, 5, 0, 5, 1, 5, 11, 6, -1, -1, -1, -1},
    {3, 11, 6, 0, 3, 6, 0, 6, 5, 0, 5, 9, -1, -1, -1, -1},
    {6, 5, 9, 6, 9, 11, 11, 9, 8, -1, -1, -1, -1, -1, -1, -1},
    {5, 10, 6, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 3, 0, 4, 7, 3, 6, 5, 10, -1, -1, -1, -1, -1, -1, -1},
    {1, 9, 0, 5, 10, 6, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1},
    {10, 6, 5, 1, 9, 7, 1, 7, 3, 7, 9, 4, -1, -1, -1, -1},
    {6, 1, 2, 6, 5, 1, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 5, 5, 2, 6, 3, 0, 4, 3, 4, 7, -1, -1, -1, -1},
    {8, 4, 7, 9, 0, 5, 0, 6, 5, 0, 2, 6, -1, -1, -1, -1},
    {7, 3, 9, 7, 9, 4, 3, 2, 9, 5, 9, 6, 2, 6, 9, -1},
    {3, 11, 2, 7, 8, 4, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1},
    {5, 10, 6, 4, 7, 2, 4, 2, 0, 2, 7, 11, -1, -1, -1, -1},
    {0, 1, 9, 4, 7, 8, 2, 3, 11, 5, 10, 6, -1, -1, -1, -1},
    {9, 2, 1, 9, 11, 2, 9, 4, 11, 7, 11, 4, 5, 10, 6, -1},
    {8, 4, 7, 3, 11, 5, 3, 5, 1, 5, 11, 6, -1, -1, -1, -1},
    {5, 1, 11, 5, 11, 6, 1, 0, 11, 7, 11, 4, 0, 4, 11, -1},
    {0, 5, 9, 0, 6, 5, 0, 3, 6, 11, 6, 3, 8, 4, 7, -1},
    {6, 5, 9, 6, 9, 11, 4, 7, 9, 7, 11, 9, -1, -1, -1, -1},
    {10, 4, 9, 6, 4, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 10, 6, 4, 9, 10, 0, 8, 3, -1, -1, -1, -1, -1, -1, -1},
    {10, 0, 1, 10, 6, 0, 6, 4, 0, -1, -1, -1, -1, -1, -1, -1},
    {8, 3, 1, 8, 1, 6, 8, 6, 4, 6, 1, 10, -1, -1, -1, -1},
    {1, 4, 9, 1, 2, 4, 2, 6, 4, -1, -1, -1, -1, -1, -1, -1},
    {3, 0, 8, 1, 2, 9, 2, 4, 9, 2, 6, 4, -1, -1, -1, -1},
    {0, 2, 4, 4, 2, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {8, 3, 2, 8, 2, 4, 4, 2, 6, -1, -1, -1, -1, -1, -1, -1},
    {10, 4, 9, 10, 6, 4, 11, 2, 3, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 2, 2, 8, 11, 4, 9, 10, 4, 10, 6, -1, -1, -1, -1},
    {3, 11, 2, 0, 1, 6, 0, 6, 4, 6, 1, 10, -1, -1, -1, -1},
    {6, 4, 1, 6, 1, 10, 4, 8, 1, 2, 1, 11, 8, 11, 1, -1},
    {9, 6, 4, 9, 3, 6, 9, 1, 3, 11, 6, 3, -1, -1, -1, -1},
    {8, 11, 1, 8, 1, 0, 11, 6, 1, 9, 1, 4, 6, 4, 1, -1},
    {3, 11, 6, 3, 6, 0, 0, 6, 4, -1, -1, -1, -1, -1, -1, -1},
    {6, 4, 8, 11, 6, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {7, 10, 6, 7, 8, 10, 8, 9, 10, -1, -1, -1, -1, -1, -1, -1},
    {0, 7, 3, 0, 10, 7, 0, 9, 10, 6, 7, 10, -1, -1, -1, -1},
    {10, 6, 7, 1, 10, 7, 1, 7, 8, 1, 8, 0, -1, -1, -1, -1},
    {10, 6, 7, 10, 7, 1, 1, 7, 3, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 6, 1, 6, 8, 1, 8, 9, 8, 6, 7, -1, -1, -1, -1},
    {2, 6, 9, 2, 9, 1, 6, 7, 9, 0, 9, 3, 7, 3, 9, -1},
    {7, 8, 0, 7, 0, 6, 6, 0, 2, -1, -1, -1, -1, -1, -1, -1},
    {7, 3, 2, 6, 7, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 3, 11, 10, 6, 8, 10, 8, 9, 8, 6, 7, -1, -1, -1, -1},
    {2, 0, 7, 2, 7, 11, 0, 9, 7, 6, 7, 10, 9, 10, 7, -1},
    {1, 8, 0, 1, 7, 8, 1, 10, 7, 6, 7, 10, 2, 3, 11, -1},
    {11, 2, 1, 11, 1, 7, 10, 6, 1, 6, 7, 1, -1, -1, -1, -1},
    {8, 9, 6, 8, 6, 7, 9, 1, 6, 11, 6, 3, 1, 3, 6, -1},
    {0, 9, 1, 11, 6, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {7, 8, 0, 7, 0, 6, 3, 11, 0, 11, 6, 0, -1, -1, -1, -1},
    {7, 11, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {7, 6, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {3, 0, 8, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 9, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {8, 1, 9, 8, 3, 1, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1},
    {10, 1, 2, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, 3, 0, 8, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1},
    {2, 9, 0, 2, 10, 9, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1},
    {6, 11, 7, 2, 10, 3, 10, 8, 3, 10, 9, 8, -1, -1, -1, -1},
    {7, 2, 3, 6, 2, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {7, 0, 8, 7, 6, 0, 6, 2, 0, -1, -1, -1, -1, -1, -1, -1},
    {2, 7, 6, 2, 3, 7, 0, 1, 9, -1, -1, -1, -1, -1, -1, -1},
    {1, 6, 2, 1, 8, 6, 1, 9, 8, 8, 7, 6, -1, -1, -1, -1},
    {10, 7, 6, 10, 1, 7, 1, 3, 7, -1, -1, -1, -1, -1, -1, -1},
    {10, 7, 6, 1, 7, 10, 1, 8, 7, 1, 0, 8, -1, -1, -1, -1},
    {0, 3, 7, 0, 7, 10, 0, 10, 9, 6, 10, 7, -1, -1, -1, -1},
    {7, 6, 10, 7, 10, 8, 8, 10, 9, -1, -1, -1, -1, -1, -1, -1},
    {6, 8, 4, 11, 8, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {3, 6, 11, 3, 0, 6, 0, 4, 6, -1, -1, -1, -1, -1, -1, -1},
    {8, 6, 11, 8, 4, 6, 9, 0, 1, -1, -1, -1, -1, -1, -1, -1},
    {9, 4, 6, 9, 6, 3, 9, 3, 1, 11, 3, 6, -1, -1, -1, -1},
    {6, 8, 4, 6, 11, 8, 2, 10, 1, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, 3, 0, 11, 0, 6, 11, 0, 4, 6, -1, -1, -1, -1},
    {4, 11, 8, 4, 6, 11, 0, 2, 9, 2, 10, 9, -1, -1, -1, -1},
    {10, 9, 3, 10, 3, 2, 9, 4, 3, 11, 3, 6, 4, 6, 3, -1},
    {8, 2, 3, 8, 4, 2, 4, 6, 2, -1, -1, -1, -1, -1, -1, -1},
    {0, 4, 2, 4, 6, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 9, 0, 2, 3, 4, 2, 4, 6, 4, 3, 8, -1, -1, -1, -1},
    {1, 9, 4, 1, 4, 2, 2, 4, 6, -1, -1, -1, -1, -1, -1, -1},
    {8, 1, 3, 8, 6, 1, 8, 4, 6, 6, 10, 1, -1, -1, -1, -1},
    {10, 1, 0, 10, 0, 6, 6, 0, 4, -1, -1, -1, -1, -1, -1, -1},
    {4, 6, 3, 4, 3, 8, 6, 10, 3, 0, 3, 9, 10, 9, 3, -1},
    {10, 9, 4, 6, 10, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 9, 5, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 4, 9, 5, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1},
    {5, 0, 1, 5, 4, 0, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1},
    {11, 7, 6, 8, 3, 4, 3, 5, 4, 3, 1, 5, -1, -1, -1, -1},
    {9, 5, 4, 10, 1, 2, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1},
    {6, 11, 7, 1, 2, 10, 0, 8, 3, 4, 9, 5, -1, -1, -1, -1},
    {7, 6, 11, 5, 4, 10, 4, 2, 10, 4, 0, 2, -1, -1, -1, -1},
    {3, 4, 8, 3, 5, 4, 3, 2, 5, 10, 5, 2, 11, 7, 6, -1},
    {7, 2, 3, 7, 6, 2, 5, 4, 9, -1, -1, -1, -1, -1, -1, -1},
    {9, 5, 4, 0, 8, 6, 0, 6, 2, 6, 8, 7, -1, -1, -1, -1},
    {3, 6, 2, 3, 7, 6, 1, 5, 0, 5, 4, 0, -1, -1, -1, -1},
    {6, 2, 8, 6, 8, 7, 2, 1, 8, 4, 8, 5, 1, 5, 8, -1},
    {9, 5, 4, 10, 1, 6, 1, 7, 6, 1, 3, 7, -1, -1, -1, -1},
    {1, 6, 10, 1, 7, 6, 1, 0, 7, 8, 7, 0, 9, 5, 4, -1},
    {4, 0, 10, 4, 10, 5, 0, 3, 10, 6, 10, 7, 3, 7, 10, -1},
    {7, 6, 10, 7, 10, 8, 5, 4, 10, 4, 8, 10, -1, -1, -1, -1},
    {6, 9, 5, 6, 11, 9, 11, 8, 9, -1, -1, -1, -1, -1, -1, -1},
    {3, 6, 11, 0, 6, 3, 0, 5, 6, 0, 9, 5, -1, -1, -1, -1},
    {0, 11, 8, 0, 5, 11, 0, 1, 5, 5, 6, 11, -1, -1, -1, -1},
    {6, 11, 3, 6, 3, 5, 5, 3, 1, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, 9, 5, 11, 9, 11, 8, 11, 5, 6, -1, -1, -1, -1},
    {0, 11, 3, 0, 6, 11, 0, 9, 6, 5, 6, 9, 1, 2, 10, -1},
    {11, 8, 5, 11, 5, 6, 8, 0, 5, 10, 5, 2, 0, 2, 5, -1},
    {6, 11, 3, 6, 3, 5, 2, 10, 3, 10, 5, 3, -1, -1, -1, -1},
    {5, 8, 9, 5, 2, 8, 5, 6, 2, 3, 8, 2, -1, -1, -1, -1},
    {9, 5, 6, 9, 6, 0, 0, 6, 2, -1, -1, -1, -1, -1, -1, -1},
    {1, 5, 8, 1, 8, 0, 5, 6, 8, 3, 8, 2, 6, 2, 8, -1},
    {1, 5, 6, 2, 1, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 3, 6, 1, 6, 10, 3, 8, 6, 5, 6, 9, 8, 9, 6, -1},
    {10, 1, 0, 10, 0, 6, 9, 5, 0, 5, 6, 0, -1, -1, -1, -1},
    {0, 3, 8, 5, 6, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {10, 5, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {11, 5, 10, 7, 5, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {11, 5, 10, 11, 7, 5, 8, 3, 0, -1, -1, -1, -1, -1, -1, -1},
    {5, 11, 7, 5, 10, 11, 1, 9, 0, -1, -1, -1, -1, -1, -1, -1},
    {10, 7, 5, 10, 11, 7, 9, 8, 1, 8, 3, 1, -1, -1, -1, -1},
    {11, 1, 2, 11, 7, 1, 7, 5, 1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 1, 2, 7, 1, 7, 5, 7, 2, 11, -1, -1, -1, -1},
    {9, 7, 5, 9, 2, 7, 9, 0, 2, 2, 11, 7, -1, -1, -1, -1},
    {7, 5, 2, 7, 2, 11, 5, 9, 2, 3, 2, 8, 9, 8, 2, -1},
    {2, 5, 10, 2, 3, 5, 3, 7, 5, -1, -1, -1, -1, -1, -1, -1},
    {8, 2, 0, 8, 5, 2, 8, 7, 5, 10, 2, 5, -1, -1, -1, -1},
    {9, 0, 1, 5, 10, 3, 5, 3, 7, 3, 10, 2, -1, -1, -1, -1},
    {9, 8, 2, 9, 2, 1, 8, 7, 2, 10, 2, 5, 7, 5, 2, -1},
    {1, 3, 5, 3, 7, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 7, 0, 7, 1, 1, 7, 5, -1, -1, -1, -1, -1, -1, -1},
    {9, 0, 3, 9, 3, 5, 5, 3, 7, -1, -1, -1, -1, -1, -1, -1},
    {9, 8, 7, 5, 9, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {5, 8, 4, 5, 10, 8, 10, 11, 8, -1, -1, -1, -1, -1, -1, -1},
    {5, 0, 4, 5, 11, 0, 5, 10, 11, 11, 3, 0, -1, -1, -1, -1},
    {0, 1, 9, 8, 4, 10, 8, 10, 11, 10, 4, 5, -1, -1, -1, -1},
    {10, 11, 4, 10, 4, 5, 11, 3, 4, 9, 4, 1, 3, 1, 4, -1},
    {2, 5, 1, 2, 8, 5, 2, 11, 8, 4, 5, 8, -1, -1, -1, -1},
    {0, 4, 11, 0, 11, 3, 4, 5, 11, 2, 11, 1, 5, 1, 11, -1},
    {0, 2, 5, 0, 5, 9, 2, 11, 5, 4, 5, 8, 11, 8, 5, -1},
    {9, 4, 5, 2, 11, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 5, 10, 3, 5, 2, 3, 4, 5, 3, 8, 4, -1, -1, -1, -1},
    {5, 10, 2, 5, 2, 4, 4, 2, 0, -1, -1, -1, -1, -1, -1, -1},
    {3, 10, 2, 3, 5, 10, 3, 8, 5, 4, 5, 8, 0, 1, 9, -1},
    {5, 10, 2, 5, 2, 4, 1, 9, 2, 9, 4, 2, -1, -1, -1, -1},
    {8, 4, 5, 8, 5, 3, 3, 5, 1, -1, -1, -1, -1, -1, -1, -1},
    {0, 4, 5, 1, 0, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {8, 4, 5, 8, 5, 3, 9, 0, 5, 0, 3, 5, -1, -1, -1, -1},
    {9, 4, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 11, 7, 4, 9, 11, 9, 10, 11, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 4, 9, 7, 9, 11, 7, 9, 10, 11, -1, -1, -1, -1},
    {1, 10, 11, 1, 11, 4, 1, 4, 0, 7, 4, 11, -1, -1, -1, -1},
    {8, 3, 1, 8, 1, 4, 1, 10, 4, 7, 4, 11, 10, 11, 4, -1},
    {4, 11, 7, 9, 11, 4, 9, 2, 11, 9, 1, 2, -1, -1, -1, -1},
    {9, 7, 4, 9, 11, 7, 9, 1, 11, 2, 11, 1, 0, 8, 3, -1},
    {11, 7, 4, 11, 4, 2, 2, 4, 0, -1, -1, -1, -1, -1, -1, -1},
    {11, 7, 4, 11, 4, 2, 8, 3, 4, 3, 2, 4, -1, -1, -1, -1},
    {2, 9, 10, 2, 7, 9, 2, 3, 7, 7, 4, 9, -1, -1, -1, -1},
    {9, 10, 7, 9, 7, 4, 10, 2, 7, 8, 7, 0, 2, 0, 7, -1},
    {3, 7, 10, 3, 10, 2, 7, 4, 10, 1, 10, 0, 4, 0, 10, -1},
    {1, 10, 2, 8, 7, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 9, 1, 4, 1, 7, 7, 1, 3, -1, -1, -1, -1, -1, -1, -1},
    {4, 9, 1, 4, 1, 7, 0, 8, 1, 8, 7, 1, -1, -1, -1, -1},
    {4, 0, 3, 7, 4, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 8, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 10, 8, 10, 11, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {3, 0, 9, 3, 9, 11, 11, 9, 10, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 10, 0, 10, 8, 8, 10, 11, -1, -1, -1, -1, -1, -1, -1},
    {3, 1, 10, 11, 3, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 11, 1, 11, 9, 9, 11, 8, -1, -1, -1, -1, -1, -1, -1},
    {3, 0, 9, 3, 9, 11, 1, 2, 9, 2, 11, 9, -1, -1, -1, -1},
    {0, 2, 11, 8, 0, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {3, 2, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 3, 8, 2, 8, 10, 10, 8, 9, -1, -1, -1, -1, -1, -1, -1},
    {9, 10, 2, 0, 9, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 3, 8, 2, 8, 10, 0, 1, 8, 1, 10, 8, -1, -1, -1, -1},
    {1, 10, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 3, 8, 9, 1, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 9, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 3, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}
};

// Edge vertex indices - defines which corners each edge connects
const int32 FMarchingCubes::EdgeVertexIndices[12][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},  // Bottom face edges
    {4, 5}, {5, 6}, {6, 7}, {7, 4},  // Top face edges  
    {0, 4}, {1, 5}, {2, 6}, {3, 7}   // Vertical edges
};

// Corner positions for a unit cube (relative to corner 0)
const FVector FMarchingCubes::CubeCorners[8] = {
    FVector(0, 0, 0), // 0: bottom-left-front
    FVector(1, 0, 0), // 1: bottom-right-front
    FVector(1, 1, 0), // 2: bottom-right-back
    FVector(0, 1, 0), // 3: bottom-left-back
    FVector(0, 0, 1), // 4: top-left-front
    FVector(1, 0, 1), // 5: top-right-front
    FVector(1, 1, 1), // 6: top-right-back
    FVector(0, 1, 1)  // 7: top-left-back
};

void FMarchingCubes::GenerateCube(const FCubeConfiguration& Config, float IsoLevel,
                                 TArray<FMarchingCubesVertex>& OutVertices, 
                                 TArray<FMarchingCubesTriangle>& OutTriangles)
{
    // Get cube configuration index (0-255)
    const int32 CubeIndex = GetCubeIndex(Config.DensityValues, IsoLevel);
    
    // No triangles for this configuration
    if (EdgeTable[CubeIndex] == 0)
        return;
        
    // Calculate interpolated vertices on edges
    FVector EdgeVertices[12];
    
    // Check each edge and interpolate vertex if needed
    for (int32 EdgeIndex = 0; EdgeIndex < 12; ++EdgeIndex)
    {
        if (EdgeTable[CubeIndex] & (1 << EdgeIndex))
        {
            // Get the two corners this edge connects
            const int32 Corner1 = EdgeVertexIndices[EdgeIndex][0];
            const int32 Corner2 = EdgeVertexIndices[EdgeIndex][1];
            
            // Interpolate vertex position along the edge
            EdgeVertices[EdgeIndex] = InterpolateVertex(
                Config.Positions[Corner1], 
                Config.Positions[Corner2],
                Config.DensityValues[Corner1], 
                Config.DensityValues[Corner2], 
                IsoLevel
            );
        }
    }
    
    // Generate triangles based on the triangle table
    for (int32 TriangleIndex = 0; TriangleTable[CubeIndex][TriangleIndex] != -1; TriangleIndex += 3)
    {
        // Get the three vertices for this triangle
        const int32 Edge1 = TriangleTable[CubeIndex][TriangleIndex];
        const int32 Edge2 = TriangleTable[CubeIndex][TriangleIndex + 1];
        const int32 Edge3 = TriangleTable[CubeIndex][TriangleIndex + 2];
        
        // Create vertices with positions and basic normals
        const FVector V1 = EdgeVertices[Edge1];
        const FVector V2 = EdgeVertices[Edge2];
        const FVector V3 = EdgeVertices[Edge3];
        
        // Calculate face normal (ensure it points upward from fluid surface)
        const FVector Normal = FVector::CrossProduct(V2 - V1, V3 - V1).GetSafeNormal();
        
        // Calculate UVs (simple planar projection)
        const FVector Center = (V1 + V2 + V3) / 3.0f;
        const FVector2D UV1 = FVector2D(V1.X * 0.01f, V1.Y * 0.01f);
        const FVector2D UV2 = FVector2D(V2.X * 0.01f, V2.Y * 0.01f);
        const FVector2D UV3 = FVector2D(V3.X * 0.01f, V3.Y * 0.01f);
        
        // Add vertices
        const int32 StartIndex = OutVertices.Num();
        OutVertices.Emplace(V1, Normal, UV1);
        OutVertices.Emplace(V2, Normal, UV2);
        OutVertices.Emplace(V3, Normal, UV3);
        
        // Add triangle
        OutTriangles.Emplace(StartIndex, StartIndex + 1, StartIndex + 2);
    }
}

void FMarchingCubes::GenerateGridMesh(const TArray<float>& DensityGrid,
                                     const FIntVector& GridSize,
                                     float CellSize,
                                     const FVector& GridOrigin,
                                     float IsoLevel,
                                     TArray<FMarchingCubesVertex>& OutVertices,
                                     TArray<FMarchingCubesTriangle>& OutTriangles)
{
    OutVertices.Empty();
    OutTriangles.Empty();
    
    // Process each cube in the grid
    for (int32 X = 0; X < GridSize.X - 1; ++X)
    {
        for (int32 Y = 0; Y < GridSize.Y - 1; ++Y)
        {
            for (int32 Z = 0; Z < GridSize.Z - 1; ++Z)
            {
                FCubeConfiguration Config;
                
                // Set up the cube configuration
                const FVector CubeOrigin = GridOrigin + FVector(X, Y, Z) * CellSize;
                
                // Fill in corner positions and density values
                for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
                {
                    const FVector RelativeCorner = CubeCorners[CornerIndex];
                    Config.Positions[CornerIndex] = CubeOrigin + RelativeCorner * CellSize;
                    
                    // Get density at this corner
                    const int32 CornerX = X + (int32)RelativeCorner.X;
                    const int32 CornerY = Y + (int32)RelativeCorner.Y;
                    const int32 CornerZ = Z + (int32)RelativeCorner.Z;
                    
                    Config.DensityValues[CornerIndex] = GetDensityAt(DensityGrid, GridSize, CornerX, CornerY, CornerZ);
                }
                
                // Generate mesh for this cube
                GenerateCube(Config, IsoLevel, OutVertices, OutTriangles);
            }
        }
    }
}

void FMarchingCubes::GenerateChunkMesh(UFluidChunk* FluidChunk, float IsoLevel,
                                      TArray<FMarchingCubesVertex>& OutVertices,
                                      TArray<FMarchingCubesTriangle>& OutTriangles)
{
    if (!FluidChunk)
        return;
        
    OutVertices.Empty();
    OutTriangles.Empty();
    
    const int32 ChunkSize = FluidChunk->ChunkSize;
    const float CellSize = FluidChunk->CellSize;
    const FVector ChunkOrigin = FluidChunk->ChunkWorldPosition;
    
    // Process each cube in the chunk
    for (int32 X = 0; X < ChunkSize - 1; ++X)
    {
        for (int32 Y = 0; Y < ChunkSize - 1; ++Y)
        {
            for (int32 Z = 0; Z < ChunkSize - 1; ++Z)
            {
                FCubeConfiguration Config;
                
                // Set up the cube configuration
                const FVector CubeOrigin = ChunkOrigin + FVector(X, Y, Z) * CellSize;
                
                // Fill in corner positions and density values
                for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
                {
                    const FVector RelativeCorner = CubeCorners[CornerIndex];
                    Config.Positions[CornerIndex] = CubeOrigin + RelativeCorner * CellSize;
                    
                    // Get fluid density at this corner
                    const int32 CornerX = X + (int32)RelativeCorner.X;
                    const int32 CornerY = Y + (int32)RelativeCorner.Y;
                    const int32 CornerZ = Z + (int32)RelativeCorner.Z;
                    
                    Config.DensityValues[CornerIndex] = FluidChunk->GetFluidAt(CornerX, CornerY, CornerZ);
                }
                
                // Generate mesh for this cube
                GenerateCube(Config, IsoLevel, OutVertices, OutTriangles);
            }
        }
    }
}

void FMarchingCubes::GenerateSeamlessChunkMesh(UFluidChunk* FluidChunk, UFluidChunkManager* ChunkManager, float IsoLevel,
                                             TArray<FMarchingCubesVertex>& OutVertices,
                                             TArray<FMarchingCubesTriangle>& OutTriangles)
{
    if (!FluidChunk || !ChunkManager)
        return;
        
    OutVertices.Empty();
    OutTriangles.Empty();
    
    const int32 ChunkSize = FluidChunk->ChunkSize;
    const float CellSize = FluidChunk->CellSize;
    const FVector ChunkOrigin = FluidChunk->ChunkWorldPosition;
    
    // Helper function to get density with boundary extension
    auto GetExtendedDensity = [&](const FVector& WorldPos, int32 LocalX, int32 LocalY, int32 LocalZ) -> float
    {
        // First try to get density from chunk manager
        float Density = ChunkManager->GetFluidAtWorldPosition(WorldPos);
        
        // If we're at chunk boundary and neighbor chunk is empty/unloaded, 
        // extend density from within this chunk to prevent gaps
        if (Density <= 0.0f)
        {
            // Check if we're at a chunk boundary
            bool bAtBoundary = (LocalX < 0 || LocalX >= ChunkSize || 
                               LocalY < 0 || LocalY >= ChunkSize || 
                               LocalZ < 0 || LocalZ >= ChunkSize);
            
            if (bAtBoundary)
            {
                // Find the nearest valid cell within this chunk and extend its density
                int32 ClampedX = FMath::Clamp(LocalX, 0, ChunkSize - 1);
                int32 ClampedY = FMath::Clamp(LocalY, 0, ChunkSize - 1);
                int32 ClampedZ = FMath::Clamp(LocalZ, 0, ChunkSize - 1);
                
                float NearestDensity = FluidChunk->GetFluidAt(ClampedX, ClampedY, ClampedZ);
                
                // Only extend if there's actually fluid nearby to prevent false surfaces
                if (NearestDensity > IsoLevel * 0.1f) // 10% of iso level threshold
                {
                    // Calculate distance from boundary for falloff
                    float DistanceX = LocalX < 0 ? -LocalX : (LocalX >= ChunkSize ? LocalX - ChunkSize + 1 : 0);
                    float DistanceY = LocalY < 0 ? -LocalY : (LocalY >= ChunkSize ? LocalY - ChunkSize + 1 : 0);
                    float DistanceZ = LocalZ < 0 ? -LocalZ : (LocalZ >= ChunkSize ? LocalZ - ChunkSize + 1 : 0);
                    float DistanceFromBoundary = FMath::Min3(DistanceX, DistanceY, DistanceZ);
                    
                    // Smooth falloff over 2-3 cells to prevent sharp edges
                    float Falloff = FMath::Exp(-DistanceFromBoundary * 0.5f);
                    Density = NearestDensity * Falloff;
                }
            }
        }
        
        return Density;
    };
    
    // Process each cube in the chunk, INCLUDING boundary cubes
    // Extend 1 cell beyond chunk boundaries to ensure seamless transitions
    for (int32 X = -1; X < ChunkSize; ++X)
    {
        for (int32 Y = -1; Y < ChunkSize; ++Y)
        {
            for (int32 Z = -1; Z < ChunkSize; ++Z)
            {
                FCubeConfiguration Config;
                
                // Set up the cube configuration
                const FVector CubeOrigin = ChunkOrigin + FVector(X, Y, Z) * CellSize;
                
                // Fill in corner positions and density values
                for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
                {
                    const FVector RelativeCorner = CubeCorners[CornerIndex];
                    Config.Positions[CornerIndex] = CubeOrigin + RelativeCorner * CellSize;
                    
                    // Calculate local coordinates for this corner
                    const int32 CornerX = X + (int32)RelativeCorner.X;
                    const int32 CornerY = Y + (int32)RelativeCorner.Y;
                    const int32 CornerZ = Z + (int32)RelativeCorner.Z;
                    
                    // Use extended density sampling that handles boundaries
                    Config.DensityValues[CornerIndex] = GetExtendedDensity(Config.Positions[CornerIndex], CornerX, CornerY, CornerZ);
                }
                
                // Only generate cube if it has some non-zero density to avoid empty space processing
                bool bHasFluid = false;
                for (int32 i = 0; i < 8; ++i)
                {
                    if (Config.DensityValues[i] > IsoLevel * 0.01f) // Very small threshold
                    {
                        bHasFluid = true;
                        break;
                    }
                }
                
                if (bHasFluid)
                {
                    // Generate mesh for this cube
                    GenerateCube(Config, IsoLevel, OutVertices, OutTriangles);
                }
            }
        }
    }
}

FVector FMarchingCubes::InterpolateVertex(const FVector& P1, const FVector& P2, float V1, float V2, float IsoLevel)
{
    const float Epsilon = 0.00001f;
    
    // Handle edge cases
    if (FMath::Abs(IsoLevel - V1) < Epsilon)
        return P1;
    if (FMath::Abs(IsoLevel - V2) < Epsilon)
        return P2;
    if (FMath::Abs(V1 - V2) < Epsilon)
        return P1;
        
    // Calculate interpolation factor with clamping to prevent overshooting
    float Mu = (IsoLevel - V1) / (V2 - V1);
    
    // Apply slight smoothing to reduce sharp transitions that cause gaps
    // Use smoothstep-like interpolation for better surface continuity
    Mu = FMath::Clamp(Mu, 0.0f, 1.0f);
    Mu = Mu * Mu * (3.0f - 2.0f * Mu); // Smoothstep function
    
    return P1 + Mu * (P2 - P1);
}

FVector FMarchingCubes::CalculateNormal(const TArray<float>& DensityGrid, const FIntVector& GridSize, 
                                       const FIntVector& GridPos, float CellSize)
{
    // Use finite difference to approximate gradient
    const float Dx = GetDensityAt(DensityGrid, GridSize, GridPos.X + 1, GridPos.Y, GridPos.Z) - 
                     GetDensityAt(DensityGrid, GridSize, GridPos.X - 1, GridPos.Y, GridPos.Z);
                     
    const float Dy = GetDensityAt(DensityGrid, GridSize, GridPos.X, GridPos.Y + 1, GridPos.Z) - 
                     GetDensityAt(DensityGrid, GridSize, GridPos.X, GridPos.Y - 1, GridPos.Z);
                     
    const float Dz = GetDensityAt(DensityGrid, GridSize, GridPos.X, GridPos.Y, GridPos.Z + 1) - 
                     GetDensityAt(DensityGrid, GridSize, GridPos.X, GridPos.Y, GridPos.Z - 1);
    
    return FVector(-Dx, -Dy, -Dz).GetSafeNormal();
}

FVector2D FMarchingCubes::CalculateUV(const FVector& Position, const FVector& GridOrigin, float GridExtent)
{
    const FVector RelativePos = (Position - GridOrigin) / GridExtent;
    return FVector2D(RelativePos.X, RelativePos.Y);
}

int32 FMarchingCubes::GetCubeIndex(const float DensityValues[8], float IsoLevel)
{
    int32 CubeIndex = 0;
    
    for (int32 i = 0; i < 8; ++i)
    {
        // Flip the condition: set bit when density is ABOVE iso level
        // This generates surfaces around fluid (density > iso) rather than empty space
        if (DensityValues[i] >= IsoLevel)
        {
            CubeIndex |= (1 << i);
        }
    }
    
    return CubeIndex;
}

float FMarchingCubes::GetDensityAt(const TArray<float>& DensityGrid, const FIntVector& GridSize, 
                                  int32 X, int32 Y, int32 Z)
{
    // Bounds checking
    if (X < 0 || X >= GridSize.X || Y < 0 || Y >= GridSize.Y || Z < 0 || Z >= GridSize.Z)
    {
        return 0.0f; // Outside grid is empty
    }
    
    const int32 Index = X + Y * GridSize.X + Z * GridSize.X * GridSize.Y;
    
    if (Index >= 0 && Index < DensityGrid.Num())
    {
        return DensityGrid[Index];
    }
    
    return 0.0f;
}

float FMarchingCubes::TrilinearInterpolate(const TArray<float>& DensityGrid, const FIntVector& GridSize,
                                         const FVector& Position, float CellSize, const FVector& GridOrigin)
{
    // Convert world position to grid space
    FVector LocalPos = (Position - GridOrigin) / CellSize;
    
    // Get integer grid coordinates
    int32 X0 = FMath::FloorToInt(LocalPos.X);
    int32 Y0 = FMath::FloorToInt(LocalPos.Y);
    int32 Z0 = FMath::FloorToInt(LocalPos.Z);
    
    int32 X1 = X0 + 1;
    int32 Y1 = Y0 + 1;
    int32 Z1 = Z0 + 1;
    
    // Get fractional parts
    float FracX = LocalPos.X - X0;
    float FracY = LocalPos.Y - Y0;
    float FracZ = LocalPos.Z - Z0;
    
    // Get density values at 8 corners
    float D000 = GetDensityAt(DensityGrid, GridSize, X0, Y0, Z0);
    float D100 = GetDensityAt(DensityGrid, GridSize, X1, Y0, Z0);
    float D010 = GetDensityAt(DensityGrid, GridSize, X0, Y1, Z0);
    float D110 = GetDensityAt(DensityGrid, GridSize, X1, Y1, Z0);
    float D001 = GetDensityAt(DensityGrid, GridSize, X0, Y0, Z1);
    float D101 = GetDensityAt(DensityGrid, GridSize, X1, Y0, Z1);
    float D011 = GetDensityAt(DensityGrid, GridSize, X0, Y1, Z1);
    float D111 = GetDensityAt(DensityGrid, GridSize, X1, Y1, Z1);
    
    // Trilinear interpolation
    float C00 = D000 * (1.0f - FracX) + D100 * FracX;
    float C01 = D001 * (1.0f - FracX) + D101 * FracX;
    float C10 = D010 * (1.0f - FracX) + D110 * FracX;
    float C11 = D011 * (1.0f - FracX) + D111 * FracX;
    
    float C0 = C00 * (1.0f - FracY) + C10 * FracY;
    float C1 = C01 * (1.0f - FracY) + C11 * FracY;
    
    return C0 * (1.0f - FracZ) + C1 * FracZ;
}

float FMarchingCubes::SampleDensityInterpolated(UFluidChunk* FluidChunk, UFluidChunkManager* ChunkManager,
                                              const FVector& LocalPosition)
{
    if (!FluidChunk)
        return 0.0f;
    
    const float CellSize = FluidChunk->CellSize;
    const int32 ChunkSize = FluidChunk->ChunkSize;
    
    // Get integer grid coordinates
    // Note: We don't add epsilon here as it can cause incorrect cell selection
    int32 X0 = FMath::FloorToInt(LocalPosition.X);
    int32 Y0 = FMath::FloorToInt(LocalPosition.Y);
    int32 Z0 = FMath::FloorToInt(LocalPosition.Z);
    
    int32 X1 = X0 + 1;
    int32 Y1 = Y0 + 1;
    int32 Z1 = Z0 + 1;
    
    // Get fractional parts for interpolation
    float FracX = LocalPosition.X - X0;
    float FracY = LocalPosition.Y - Y0;
    float FracZ = LocalPosition.Z - Z0;
    
    // Clamp fractions to avoid numerical issues
    FracX = FMath::Clamp(FracX, 0.0f, 1.0f);
    FracY = FMath::Clamp(FracY, 0.0f, 1.0f);
    FracZ = FMath::Clamp(FracZ, 0.0f, 1.0f);
    
    // Helper lambda to get density with proper boundary handling
    auto GetDensity = [&](int32 X, int32 Y, int32 Z) -> float
    {
        // Check if we're within this chunk
        if (X >= 0 && X < ChunkSize && Y >= 0 && Y < ChunkSize && Z >= 0 && Z < ChunkSize)
        {
            return FluidChunk->GetFluidAt(X, Y, Z);
        }
        else if (ChunkManager)
        {
            // Sample from neighboring chunk
            // Calculate the exact world position for this grid point
            FVector WorldPos = FluidChunk->ChunkWorldPosition + FVector(X * CellSize, Y * CellSize, Z * CellSize);
            
            // For boundary cells, we need to ensure we sample from the correct position
            // When X >= ChunkSize, we're sampling from the next chunk
            // When X < 0, we're sampling from the previous chunk
            const float HalfCell = CellSize * 0.5f;
            
            // Adjust position to cell center for accurate sampling
            if (X >= ChunkSize)
            {
                // Sampling from next chunk's first cells
                WorldPos.X += HalfCell;
            }
            else if (X < 0)
            {
                // Sampling from previous chunk's last cells
                // When X = -1, we want cell (ChunkSize-1) of previous chunk
                // WorldPos is already at the cell start, add half to get center
                WorldPos.X += HalfCell;
            }
            
            if (Y >= ChunkSize)
            {
                WorldPos.Y += HalfCell;
            }
            else if (Y < 0)
            {
                WorldPos.Y += HalfCell;
            }
            
            if (Z >= ChunkSize)
            {
                WorldPos.Z += HalfCell;
            }
            else if (Z < 0)
            {
                WorldPos.Z += HalfCell;
            }
                
            return ChunkManager->GetFluidAtWorldPosition(WorldPos);
        }
        return 0.0f;
    };
    
    // Get density values at 8 corners of the interpolation cube
    float D000 = GetDensity(X0, Y0, Z0);
    float D100 = GetDensity(X1, Y0, Z0);
    float D010 = GetDensity(X0, Y1, Z0);
    float D110 = GetDensity(X1, Y1, Z0);
    float D001 = GetDensity(X0, Y0, Z1);
    float D101 = GetDensity(X1, Y0, Z1);
    float D011 = GetDensity(X0, Y1, Z1);
    float D111 = GetDensity(X1, Y1, Z1);
    
    // Perform trilinear interpolation
    float C00 = D000 * (1.0f - FracX) + D100 * FracX;
    float C01 = D001 * (1.0f - FracX) + D101 * FracX;
    float C10 = D010 * (1.0f - FracX) + D110 * FracX;
    float C11 = D011 * (1.0f - FracX) + D111 * FracX;
    
    float C0 = C00 * (1.0f - FracY) + C10 * FracY;
    float C1 = C01 * (1.0f - FracY) + C11 * FracY;
    
    return C0 * (1.0f - FracZ) + C1 * FracZ;
}

void FMarchingCubes::GenerateHighResChunkMesh(UFluidChunk* FluidChunk, UFluidChunkManager* ChunkManager,
                                            float IsoLevel, int32 ResolutionMultiplier,
                                            TArray<FMarchingCubesVertex>& OutVertices,
                                            TArray<FMarchingCubesTriangle>& OutTriangles)
{
    if (!FluidChunk || ResolutionMultiplier < 1)
        return;
        
    OutVertices.Empty();
    OutTriangles.Empty();
    
    const int32 ChunkSize = FluidChunk->ChunkSize;
    const float CellSize = FluidChunk->CellSize;
    const FVector ChunkOrigin = FluidChunk->ChunkWorldPosition;
    
    // Calculate high-resolution grid dimensions
    // We need to process one less than the full size to avoid double-processing at boundaries
    const int32 HighResSize = ChunkSize * ResolutionMultiplier;
    const float HighResCellSize = CellSize / ResolutionMultiplier;
    
    // Process each high-resolution cube
    // We need to process all cubes up to the chunk boundary
    // Each cube samples at position and position+1, so the last valid cube
    // is at HighResSize-1, which will sample into the neighboring chunk
    for (int32 X = 0; X < HighResSize; ++X)
    {
        for (int32 Y = 0; Y < HighResSize; ++Y)
        {
            for (int32 Z = 0; Z < HighResSize; ++Z)
            {
                // Calculate the actual position in chunk space
                float LocalX = (float)X / ResolutionMultiplier;
                float LocalY = (float)Y / ResolutionMultiplier;
                float LocalZ = (float)Z / ResolutionMultiplier;
                
                // Skip cubes that start at or beyond the chunk boundary
                // Cubes at position ChunkSize would belong to the next chunk
                if (LocalX >= ChunkSize || LocalY >= ChunkSize || LocalZ >= ChunkSize)
                    continue;
                
                FCubeConfiguration Config;
                
                // Set up the cube configuration at high resolution
                const FVector CubeOrigin = ChunkOrigin + FVector(X, Y, Z) * HighResCellSize;
                
                // Fill in corner positions and interpolated density values
                bool bHasValidDensity = false;
                for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
                {
                    const FVector RelativeCorner = CubeCorners[CornerIndex];
                    Config.Positions[CornerIndex] = CubeOrigin + RelativeCorner * HighResCellSize;
                    
                    // Get interpolated density at this high-res corner position
                    FVector LocalPos = FVector(X, Y, Z) + RelativeCorner;
                    LocalPos /= ResolutionMultiplier; // Convert to original grid space
                    
                    // Sample with proper boundary handling - this will fetch from neighbors when needed
                    Config.DensityValues[CornerIndex] = SampleDensityInterpolated(FluidChunk, ChunkManager, LocalPos);
                    
                    if (Config.DensityValues[CornerIndex] > 0.0f)
                        bHasValidDensity = true;
                }
                
                // Only generate mesh for cubes that have some density
                if (bHasValidDensity)
                {
                    GenerateCube(Config, IsoLevel, OutVertices, OutTriangles);
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