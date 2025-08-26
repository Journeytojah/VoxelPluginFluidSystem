#pragma once

#include "Stats/Stats.h"

DECLARE_STATS_GROUP(TEXT("VoxelFluid"), STATGROUP_VoxelFluid, STATCAT_Advanced);

// =====================================================
//  TOP 20 CRITICAL STATS - Performance & Stuttering Debug
// =====================================================

// === CRITICAL PERFORMANCE TIMING (Stuttering Diagnosis) ===
DECLARE_CYCLE_STAT(TEXT("[1] Total Update"), STAT_VoxelFluid_UpdateSimulation, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("[2] Chunk Manager"), STAT_VoxelFluid_ChunkManagerUpdate, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("[3] State Changes"), STAT_VoxelFluid_ChunkStateChange, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("[4] Changes/Frame"), STAT_VoxelFluid_StateChangesPerFrame, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("[5] Frame MS"), STAT_VoxelFluid_TotalFrameMS, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("[6] FPS Impact"), STAT_VoxelFluid_FPSImpact, STATGROUP_VoxelFluid);

// === RESOURCE TRACKING ===
DECLARE_DWORD_COUNTER_STAT(TEXT("[7] Active Chunks"), STAT_VoxelFluid_ActiveChunks, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("[8] Total Chunks"), STAT_VoxelFluid_LoadedChunks, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("[9] Active Cells"), STAT_VoxelFluid_ActiveCells, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("[10] Memory MB"), STAT_VoxelFluid_TotalMemoryMB, STATGROUP_VoxelFluid);

// === HYBRID SYSTEM BALANCE ===
DECLARE_DWORD_COUNTER_STAT(TEXT("[11] Sim Chunks"), STAT_VoxelFluid_SimulationChunks, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("[12] Static Chunks"), STAT_VoxelFluid_HybridStaticChunks, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("[13] Static Render"), STAT_VoxelFluid_StaticRenderChunks, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("[14] Sim/Static Ratio"), STAT_VoxelFluid_SimStaticRatio, STATGROUP_VoxelFluid);

// === CRITICAL TIMING BOTTLENECKS ===
DECLARE_CYCLE_STAT(TEXT("[15] Terrain Sampling"), STAT_VoxelFluid_TerrainSampling, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("[16] Mesh Gen"), STAT_VoxelFluid_MarchingCubes, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("[17] Static Apply"), STAT_VoxelFluid_StaticWaterApply, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("[18] Chunk Streaming"), STAT_VoxelFluid_ChunkStreaming, STATGROUP_VoxelFluid);

// === SYSTEM HEALTH ===
DECLARE_DWORD_COUNTER_STAT(TEXT("[19] Rendered"), STAT_VoxelFluid_RenderedChunks, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("[20] Active Regions"), STAT_VoxelFluid_ActiveRegions, STATGROUP_VoxelFluid);

// =====================================================
//  HIDDEN STATS (Declared but not displayed)
// =====================================================
// These stats are declared for code compatibility but won't show in stat voxelfluid
// unless specifically requested. They use "_" prefix to sort them below the main 20.

// Still used in code - keep declarations for compilation
DECLARE_FLOAT_COUNTER_STAT(TEXT("_Total Volume"), STAT_VoxelFluid_TotalVolume, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("_Inactive Chunks"), STAT_VoxelFluid_InactiveChunks, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("_Border Chunks"), STAT_VoxelFluid_BorderOnlyChunks, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("_Load Queue"), STAT_VoxelFluid_ChunkLoadQueueSize, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("_Unload Queue"), STAT_VoxelFluid_ChunkUnloadQueueSize, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("_Chunk Update Time"), STAT_VoxelFluid_AvgChunkUpdateTime, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("_Static Water Cells"), STAT_VoxelFluid_StaticWaterCells, STATGROUP_VoxelFluid);

// Simulation detail stats
DECLARE_CYCLE_STAT(TEXT("_Apply Gravity"), STAT_VoxelFluid_ApplyGravity, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("_Apply Flow Rules"), STAT_VoxelFluid_ApplyFlowRules, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("_Apply Pressure"), STAT_VoxelFluid_ApplyPressure, STATGROUP_VoxelFluid);

// Visualization detail stats
DECLARE_CYCLE_STAT(TEXT("_Visualization"), STAT_VoxelFluid_Visualization, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("_Cached Meshes"), STAT_VoxelFluid_CachedMeshes, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("_Generated Meshes"), STAT_VoxelFluid_GeneratedMeshes, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("_LOD0 Meshes"), STAT_VoxelFluid_LOD0Meshes, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("_LOD1 Meshes"), STAT_VoxelFluid_LOD1Meshes, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("_LOD2 Meshes"), STAT_VoxelFluid_LOD2Meshes, STATGROUP_VoxelFluid);

// Static water detail stats
DECLARE_DWORD_COUNTER_STAT(TEXT("_Static LOD0 Chunks"), STAT_VoxelFluid_StaticLOD0Chunks, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("_Static LOD1+ Chunks"), STAT_VoxelFluid_StaticLOD1PlusChunks, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("_Static Ring Inner Radius"), STAT_VoxelFluid_StaticRingInnerRadius, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("_Static Ring Outer Radius"), STAT_VoxelFluid_StaticRingOuterRadius, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("_Transition Chunks"), STAT_VoxelFluid_TransitionChunks, STATGROUP_VoxelFluid);

// Performance detail stats
DECLARE_FLOAT_COUNTER_STAT(TEXT("_Sim MS/Chunk"), STAT_VoxelFluid_SimMSPerChunk, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("_Static MS/Chunk"), STAT_VoxelFluid_StaticMSPerChunk, STATGROUP_VoxelFluid);

// Integration stats
DECLARE_CYCLE_STAT(TEXT("_Voxel Integration"), STAT_VoxelFluid_VoxelIntegration, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("_Terrain Refresh"), STAT_VoxelFluid_TerrainRefresh, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("_Terrain Queries"), STAT_VoxelFluid_TerrainQueries, STATGROUP_VoxelFluid);

// Chunk system detail stats
DECLARE_CYCLE_STAT(TEXT("_Chunk Unload"), STAT_VoxelFluid_ChunkUnload, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("_Border Sync"), STAT_VoxelFluid_BorderSync, STATGROUP_VoxelFluid);

// Source detail stats  
DECLARE_CYCLE_STAT(TEXT("_Fluid Source Update"), STAT_VoxelFluid_FluidSourceUpdate, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("_Dynamic Refill"), STAT_VoxelFluid_DynamicRefill, STATGROUP_VoxelFluid);

// Sparse grid conversion stats
DECLARE_CYCLE_STAT(TEXT("_Convert To Sparse"), STAT_VoxelFluid_ConvertToSparse, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("_Convert To Dense"), STAT_VoxelFluid_ConvertToDense, STATGROUP_VoxelFluid);