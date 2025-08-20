#pragma once

#include "Stats/Stats.h"

DECLARE_STATS_GROUP(TEXT("VoxelFluid"), STATGROUP_VoxelFluid, STATCAT_Advanced);

// =====================================================
//  PRIMARY METRICS - Always Show These
// =====================================================

// Performance Timing (Critical for optimization)
DECLARE_CYCLE_STAT(TEXT("Total Update"), STAT_VoxelFluid_UpdateSimulation, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("Chunk Update"), STAT_VoxelFluid_ChunkManagerUpdate, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("Mesh Gen"), STAT_VoxelFluid_MarchingCubes, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("Border Sync"), STAT_VoxelFluid_BorderSync, STATGROUP_VoxelFluid);

// Resource Usage (Critical metrics)
DECLARE_DWORD_COUNTER_STAT(TEXT("Active/Total Chunks"), STAT_VoxelFluid_ActiveChunks, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Total Chunks"), STAT_VoxelFluid_LoadedChunks, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Active Cells"), STAT_VoxelFluid_ActiveCells, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Fluid Volume"), STAT_VoxelFluid_TotalVolume, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Chunk Update MS"), STAT_VoxelFluid_AvgChunkUpdateTime, STATGROUP_VoxelFluid);

// Memory (Critical for high-res optimization)
DECLARE_DWORD_COUNTER_STAT(TEXT("Memory MB"), STAT_VoxelFluid_TotalMemoryMB, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Cache KB"), STAT_VoxelFluid_CacheMemoryKB, STATGROUP_VoxelFluid);

// =====================================================
//  SECONDARY METRICS - Detailed Analysis
// =====================================================

// Simulation Components
DECLARE_CYCLE_STAT(TEXT("Gravity"), STAT_VoxelFluid_ApplyGravity, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("Flow"), STAT_VoxelFluid_ApplyFlowRules, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("Pressure"), STAT_VoxelFluid_ApplyPressure, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("Velocity"), STAT_VoxelFluid_UpdateVelocities, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("Streaming"), STAT_VoxelFluid_ChunkStreaming, STATGROUP_VoxelFluid);

// Rendering
DECLARE_CYCLE_STAT(TEXT("Visualization"), STAT_VoxelFluid_Visualization, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Rendered"), STAT_VoxelFluid_RenderedChunks, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Cached Mesh"), STAT_VoxelFluid_CachedMeshes, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Gen Mesh"), STAT_VoxelFluid_GeneratedMeshes, STATGROUP_VoxelFluid);

// Chunk States
DECLARE_DWORD_COUNTER_STAT(TEXT("Inactive"), STAT_VoxelFluid_InactiveChunks, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Border"), STAT_VoxelFluid_BorderOnlyChunks, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Load Q"), STAT_VoxelFluid_ChunkLoadQueueSize, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Unload Q"), STAT_VoxelFluid_ChunkUnloadQueueSize, STATGROUP_VoxelFluid);

// LOD
DECLARE_DWORD_COUNTER_STAT(TEXT("LOD0"), STAT_VoxelFluid_LOD0Meshes, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("LOD1"), STAT_VoxelFluid_LOD1Meshes, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("LOD2"), STAT_VoxelFluid_LOD2Meshes, STATGROUP_VoxelFluid);

// =====================================================
//  DEBUG METRICS - Usually Hidden
// =====================================================

// Integration
DECLARE_CYCLE_STAT(TEXT("VoxelIntegration"), STAT_VoxelFluid_VoxelIntegration, STATGROUP_VoxelFluid);

// Detailed Cell Info
DECLARE_DWORD_COUNTER_STAT(TEXT("Total Cells"), STAT_VoxelFluid_TotalCells, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Significant"), STAT_VoxelFluid_SignificantCells, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Avg Level"), STAT_VoxelFluid_AvgFluidLevel, STATGROUP_VoxelFluid);

// Player Position
DECLARE_FLOAT_COUNTER_STAT(TEXT("Player X"), STAT_VoxelFluid_PlayerPosX, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Player Y"), STAT_VoxelFluid_PlayerPosY, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Player Z"), STAT_VoxelFluid_PlayerPosZ, STATGROUP_VoxelFluid);

// Settings
DECLARE_FLOAT_COUNTER_STAT(TEXT("Active Dist"), STAT_VoxelFluid_ActiveDistance, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Load Dist"), STAT_VoxelFluid_LoadDistance, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Cross Flow"), STAT_VoxelFluid_CrossChunkFlow, STATGROUP_VoxelFluid);

// Cache
DECLARE_DWORD_COUNTER_STAT(TEXT("Cache Count"), STAT_VoxelFluid_CacheEntries, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Saved"), STAT_VoxelFluid_ChunksSaved, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Loaded"), STAT_VoxelFluid_ChunksLoaded, STATGROUP_VoxelFluid);

// Sources
DECLARE_DWORD_COUNTER_STAT(TEXT("Sources"), STAT_VoxelFluid_ActiveSources, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Flow Rate"), STAT_VoxelFluid_TotalSourceFlow, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Evaporation"), STAT_VoxelFluid_EvaporationRate, STATGROUP_VoxelFluid);

// Memory Details
DECLARE_DWORD_COUNTER_STAT(TEXT("Mesh Mem MB"), STAT_VoxelFluid_MeshMemoryMB, STATGROUP_VoxelFluid);

// Sparse Grid Stats
DECLARE_CYCLE_STAT(TEXT("Convert To Sparse"), STAT_VoxelFluid_ConvertToSparse, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("Convert To Dense"), STAT_VoxelFluid_ConvertToDense, STATGROUP_VoxelFluid);