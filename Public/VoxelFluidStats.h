#pragma once

#include "Stats/Stats.h"

DECLARE_STATS_GROUP(TEXT("VoxelFluid"), STATGROUP_VoxelFluid, STATCAT_Advanced);

DECLARE_CYCLE_STAT(TEXT("VoxelFluid UpdateSimulation"), STAT_VoxelFluid_UpdateSimulation, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("VoxelFluid ApplyGravity"), STAT_VoxelFluid_ApplyGravity, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("VoxelFluid ApplyFlowRules"), STAT_VoxelFluid_ApplyFlowRules, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("VoxelFluid ApplyPressure"), STAT_VoxelFluid_ApplyPressure, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("VoxelFluid UpdateVelocities"), STAT_VoxelFluid_UpdateVelocities, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("VoxelFluid Visualization"), STAT_VoxelFluid_Visualization, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("VoxelFluid VoxelIntegration"), STAT_VoxelFluid_VoxelIntegration, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("VoxelFluid ChunkManager Update"), STAT_VoxelFluid_ChunkManagerUpdate, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("VoxelFluid ChunkStreaming"), STAT_VoxelFluid_ChunkStreaming, STATGROUP_VoxelFluid);
DECLARE_CYCLE_STAT(TEXT("VoxelFluid BorderSync"), STAT_VoxelFluid_BorderSync, STATGROUP_VoxelFluid);

DECLARE_DWORD_COUNTER_STAT(TEXT("Active Fluid Cells"), STAT_VoxelFluid_ActiveCells, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Total Grid Cells"), STAT_VoxelFluid_TotalCells, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Total Fluid Volume"), STAT_VoxelFluid_TotalVolume, STATGROUP_VoxelFluid);

DECLARE_DWORD_COUNTER_STAT(TEXT("Loaded Chunks"), STAT_VoxelFluid_LoadedChunks, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Active Chunks"), STAT_VoxelFluid_ActiveChunks, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Inactive Chunks"), STAT_VoxelFluid_InactiveChunks, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("BorderOnly Chunks"), STAT_VoxelFluid_BorderOnlyChunks, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Chunk Load Queue Size"), STAT_VoxelFluid_ChunkLoadQueueSize, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Chunk Unload Queue Size"), STAT_VoxelFluid_ChunkUnloadQueueSize, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Average Chunk Update Time"), STAT_VoxelFluid_AvgChunkUpdateTime, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Rendered Fluid Cells"), STAT_VoxelFluid_RenderedCells, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Rendered Chunks"), STAT_VoxelFluid_RenderedChunks, STATGROUP_VoxelFluid);

// Additional debug stats
DECLARE_FLOAT_COUNTER_STAT(TEXT("Player Position X"), STAT_VoxelFluid_PlayerPosX, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Player Position Y"), STAT_VoxelFluid_PlayerPosY, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Player Position Z"), STAT_VoxelFluid_PlayerPosZ, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Active Distance Range"), STAT_VoxelFluid_ActiveDistance, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Load Distance Range"), STAT_VoxelFluid_LoadDistance, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Cross-Chunk Flow Enabled"), STAT_VoxelFluid_CrossChunkFlow, STATGROUP_VoxelFluid);