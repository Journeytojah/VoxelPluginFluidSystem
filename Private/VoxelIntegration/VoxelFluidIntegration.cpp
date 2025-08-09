#include "VoxelIntegration/VoxelFluidIntegration.h"
#include "VoxelIntegration/VoxelTerrainSampler.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Components/SceneComponent.h"

UVoxelFluidIntegration::UVoxelFluidIntegration()
{
	PrimaryComponentTick.bCanEverTick = true;
	
	GridResolutionX = 128;
	GridResolutionY = 128;
	GridResolutionZ = 32;
	CellWorldSize = 100.0f;
	bAutoUpdateTerrain = true;
	TerrainUpdateInterval = 1.0f;
	bDebugDrawCells = false;
	MinFluidToRender = 0.01f;
}

void UVoxelFluidIntegration::BeginPlay()
{
	Super::BeginPlay();
	
	if (!FluidGrid)
	{
		FluidGrid = NewObject<UCAFluidGrid>(this, UCAFluidGrid::StaticClass());
	}
	
	if (FluidGrid)
	{
		FluidGrid->InitializeGrid(GridResolutionX, GridResolutionY, GridResolutionZ, CellWorldSize);
	}
	
	// GridWorldOrigin will be set when FluidGrid is initialized
	
	if (IsVoxelWorldValid() && bAutoUpdateTerrain)
	{
		UpdateTerrainHeights();
	}
}

void UVoxelFluidIntegration::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	if (!FluidGrid)
		return;
	
	FluidGrid->UpdateSimulation(DeltaTime);
	
	if (bAutoUpdateTerrain)
	{
		TerrainUpdateTimer += DeltaTime;
		if (TerrainUpdateTimer >= TerrainUpdateInterval)
		{
			TerrainUpdateTimer = 0.0f;
			UpdateTerrainHeights();
		}
	}
	
	if (bDebugDrawCells)
	{
		DrawDebugFluid();
	}
}

void UVoxelFluidIntegration::InitializeFluidSystem(AActor* InVoxelWorld)
{
	VoxelWorld = InVoxelWorld;
	
	if (!FluidGrid)
	{
		FluidGrid = NewObject<UCAFluidGrid>(this, UCAFluidGrid::StaticClass());
	}
	
	if (FluidGrid)
	{
		FluidGrid->InitializeGrid(GridResolutionX, GridResolutionY, GridResolutionZ, CellWorldSize);
	}
	
	if (IsVoxelWorldValid())
	{
		SyncWithVoxelTerrain();
	}
}

void UVoxelFluidIntegration::SyncWithVoxelTerrain()
{
	if (!IsVoxelWorldValid() || !FluidGrid)
		return;
	
	UpdateTerrainHeights();
}

void UVoxelFluidIntegration::UpdateTerrainHeights()
{
	if (!FluidGrid)
		return;
	
	const FVector CurrentGridOrigin = FluidGrid->GridOrigin;
	
	for (int32 x = 0; x < GridResolutionX; ++x)
	{
		for (int32 y = 0; y < GridResolutionY; ++y)
		{
			const FVector WorldPos = CurrentGridOrigin + FVector(x * CellWorldSize, y * CellWorldSize, 0);
			const float TerrainHeight = SampleVoxelHeight(WorldPos.X, WorldPos.Y);
			
			FluidGrid->SetTerrainHeight(x, y, TerrainHeight);
		}
	}
}

float UVoxelFluidIntegration::SampleVoxelHeight(float WorldX, float WorldY)
{
	FVector SampleLocation(WorldX, WorldY, 0);
	
	if (IsVoxelWorldValid())
	{
		return UVoxelTerrainSampler::SampleTerrainHeightAtLocation(VoxelWorld, SampleLocation);
	}
	else
	{
		return UVoxelTerrainSampler::SampleTerrainHeightAtLocation(GetWorld(), SampleLocation);
	}
}

void UVoxelFluidIntegration::AddFluidAtWorldPosition(const FVector& WorldPosition, float Amount)
{
	if (!FluidGrid)
		return;
	
	int32 CellX, CellY, CellZ;
	if (FluidGrid->GetCellFromWorldPosition(WorldPosition - FluidGrid->GridOrigin, CellX, CellY, CellZ))
	{
		FluidGrid->AddFluid(CellX, CellY, CellZ, Amount);
	}
}

void UVoxelFluidIntegration::RemoveFluidAtWorldPosition(const FVector& WorldPosition, float Amount)
{
	if (!FluidGrid)
		return;
	
	int32 CellX, CellY, CellZ;
	if (FluidGrid->GetCellFromWorldPosition(WorldPosition - FluidGrid->GridOrigin, CellX, CellY, CellZ))
	{
		FluidGrid->RemoveFluid(CellX, CellY, CellZ, Amount);
	}
}

void UVoxelFluidIntegration::DrawDebugFluid()
{
	if (!FluidGrid || !GetWorld())
		return;
	
	for (int32 x = 0; x < GridResolutionX; ++x)
	{
		for (int32 y = 0; y < GridResolutionY; ++y)
		{
			for (int32 z = 0; z < GridResolutionZ; ++z)
			{
				const float FluidLevel = FluidGrid->GetFluidAt(x, y, z);
				
				if (FluidLevel > MinFluidToRender)
				{
					const FVector CellWorldPos = FluidGrid->GetWorldPositionFromCell(x, y, z);
					const float BoxSize = CellWorldSize * 0.9f * FluidLevel;
					
					const FColor FluidColor = FColor::MakeRedToGreenColorFromScalar(1.0f - FluidLevel);
					
					DrawDebugBox(GetWorld(), CellWorldPos, FVector(BoxSize * 0.5f), FluidColor, false, -1.0f, 0, 2.0f);
				}
			}
		}
	}
}

bool UVoxelFluidIntegration::IsVoxelWorldValid() const
{
	return VoxelWorld != nullptr;
}