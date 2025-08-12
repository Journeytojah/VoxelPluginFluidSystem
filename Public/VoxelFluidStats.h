#pragma once

#include "Stats/Stats.h"

DECLARE_STATS_GROUP(TEXT("VoxelFluid"), STATGROUP_VoxelFluid, STATCAT_Advanced);

// === Performance Timing Stats ===
DECLARE_CYCLE_STAT(TEXT("Update Simulation"), STAT_VoxelFluid_UpdateSimulation, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("Apply Gravity"), STAT_VoxelFluid_ApplyGravity, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("Apply Flow Rules"), STAT_VoxelFluid_ApplyFlowRules, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("Apply Pressure"), STAT_VoxelFluid_ApplyPressure, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("Update Velocities"), STAT_VoxelFluid_UpdateVelocities, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("Visualization"), STAT_VoxelFluid_Visualization, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("Marching Cubes Generation"), STAT_VoxelFluid_MarchingCubes, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("VoxelIntegration"), STAT_VoxelFluid_VoxelIntegration, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("ChunkManager Update"), STAT_VoxelFluid_ChunkManagerUpdate, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("Chunk Streaming"), STAT_VoxelFluid_ChunkStreaming, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("Border Sync"), STAT_VoxelFluid_BorderSync, STATGROUP_VoxelFluid);

// === Fluid Cell Statistics ===
DECLARE_DWORD_COUNTER_STAT(TEXT("Active Fluid Cells"), STAT_VoxelFluid_ActiveCells, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Total Grid Cells"), STAT_VoxelFluid_TotalCells, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Total Fluid Volume"), STAT_VoxelFluid_TotalVolume, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Cells With Fluid > 0.1"), STAT_VoxelFluid_SignificantCells, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Avg Fluid Level"), STAT_VoxelFluid_AvgFluidLevel, STATGROUP_VoxelFluid);

// === Chunk System Statistics ===
DECLARE_DWORD_COUNTER_STAT(TEXT("Loaded Chunks"), STAT_VoxelFluid_LoadedChunks, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Active Chunks"), STAT_VoxelFluid_ActiveChunks, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Inactive Chunks"), STAT_VoxelFluid_InactiveChunks, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("BorderOnly Chunks"), STAT_VoxelFluid_BorderOnlyChunks, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Chunk Load Queue"), STAT_VoxelFluid_ChunkLoadQueueSize, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Chunk Unload Queue"), STAT_VoxelFluid_ChunkUnloadQueueSize, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Avg Chunk Update Time (ms)"), STAT_VoxelFluid_AvgChunkUpdateTime, STATGROUP_VoxelFluid);

// === Rendering Statistics ===
DECLARE_DWORD_COUNTER_STAT(TEXT("Rendered Chunks"), STAT_VoxelFluid_RenderedChunks, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Cached Meshes Used"), STAT_VoxelFluid_CachedMeshes, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Meshes Generated"), STAT_VoxelFluid_GeneratedMeshes, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("LOD 0 Meshes"), STAT_VoxelFluid_LOD0Meshes, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("LOD 1 Meshes"), STAT_VoxelFluid_LOD1Meshes, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("LOD 2 Meshes"), STAT_VoxelFluid_LOD2Meshes, STATGROUP_VoxelFluid);

// === Player & World Information ===
DECLARE_FLOAT_COUNTER_STAT(TEXT("Player X"), STAT_VoxelFluid_PlayerPosX, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Player Y"), STAT_VoxelFluid_PlayerPosY, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Player Z"), STAT_VoxelFluid_PlayerPosZ, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Active Distance"), STAT_VoxelFluid_ActiveDistance, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Load Distance"), STAT_VoxelFluid_LoadDistance, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Cross-Chunk Flow"), STAT_VoxelFluid_CrossChunkFlow, STATGROUP_VoxelFluid);