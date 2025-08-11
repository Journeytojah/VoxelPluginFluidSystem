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

DECLARE_DWORD_COUNTER_STAT(TEXT("Active Fluid Cells"), STAT_VoxelFluid_ActiveCells, STATGROUP_VoxelFluid);
DECLARE_DWORD_COUNTER_STAT(TEXT("Total Grid Cells"), STAT_VoxelFluid_TotalCells, STATGROUP_VoxelFluid);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Total Fluid Volume"), STAT_VoxelFluid_TotalVolume, STATGROUP_VoxelFluid);