#include "VoxelFluidFunctionLibrary.h"
#include "Actors/VoxelFluidActor.h"
#include "CellularAutomata/CAFluidGrid.h"
#include "VoxelIntegration/VoxelFluidIntegration.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

AVoxelFluidActor* UVoxelFluidFunctionLibrary::SpawnFluidSystem(UObject* WorldContextObject, const FVector& Location, 
																int32 GridSizeX, int32 GridSizeY, int32 GridSizeZ)
{
	if (!WorldContextObject)
		return nullptr;

	UWorld* World = WorldContextObject->GetWorld();
	if (!World)
		return nullptr;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelFluidActor* FluidActor = World->SpawnActor<AVoxelFluidActor>(AVoxelFluidActor::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
	
	if (FluidActor)
	{
		FluidActor->GridSizeX = GridSizeX;
		FluidActor->GridSizeY = GridSizeY;
		FluidActor->GridSizeZ = GridSizeZ;
		FluidActor->InitializeFluidSystem();
		
		UE_LOG(LogTemp, Log, TEXT("Spawned Voxel Fluid System at %s with grid size %dx%dx%d"), 
			   *Location.ToString(), GridSizeX, GridSizeY, GridSizeZ);
	}

	return FluidActor;
}

void UVoxelFluidFunctionLibrary::AddRainToFluidSystem(AVoxelFluidActor* FluidActor, float Intensity, float Radius)
{
	if (!FluidActor || !FluidActor->FluidGrid)
		return;

	const FVector ActorLocation = FluidActor->GetActorLocation();
	const float CellSize = FluidActor->CellSize;
	
	int32 CellRadius = FMath::CeilToInt(Radius / CellSize);
	int32 CenterX = FluidActor->GridSizeX / 2;
	int32 CenterY = FluidActor->GridSizeY / 2;
	
	for (int32 x = FMath::Max(0, CenterX - CellRadius); x < FMath::Min(FluidActor->GridSizeX, CenterX + CellRadius); ++x)
	{
		for (int32 y = FMath::Max(0, CenterY - CellRadius); y < FMath::Min(FluidActor->GridSizeY, CenterY + CellRadius); ++y)
		{
			float Distance = FVector2D(x - CenterX, y - CenterY).Size() * CellSize;
			if (Distance <= Radius)
			{
				if (FMath::FRand() < Intensity)
				{
					for (int32 z = FluidActor->GridSizeZ - 1; z >= 0; --z)
					{
						if (FluidActor->FluidGrid->GetFluidAt(x, y, z) < 0.1f)
						{
							FluidActor->FluidGrid->AddFluid(x, y, z, Intensity * 0.1f);
							break;
						}
					}
				}
			}
		}
	}
}

void UVoxelFluidFunctionLibrary::CreateFluidSource(AVoxelFluidActor* FluidActor, const FVector& SourceLocation, float FlowRate)
{
	if (!FluidActor)
		return;

	FluidActor->AddFluidSource(SourceLocation, FlowRate);
}

void UVoxelFluidFunctionLibrary::CreateFluidSplash(AVoxelFluidActor* FluidActor, const FVector& ImpactLocation, float SplashRadius, float SplashAmount)
{
	if (!FluidActor || !FluidActor->FluidGrid)
		return;

	const FVector LocalPos = ImpactLocation - FluidActor->GetActorLocation();
	int32 CenterX, CenterY, CenterZ;
	
	if (!FluidActor->FluidGrid->GetCellFromWorldPosition(LocalPos, CenterX, CenterY, CenterZ))
		return;

	int32 CellRadius = FMath::CeilToInt(SplashRadius / FluidActor->CellSize);
	
	for (int32 x = FMath::Max(0, CenterX - CellRadius); x < FMath::Min(FluidActor->GridSizeX, CenterX + CellRadius); ++x)
	{
		for (int32 y = FMath::Max(0, CenterY - CellRadius); y < FMath::Min(FluidActor->GridSizeY, CenterY + CellRadius); ++y)
		{
			for (int32 z = FMath::Max(0, CenterZ - 1); z < FMath::Min(FluidActor->GridSizeZ, CenterZ + 3); ++z)
			{
				float Distance = FVector(x - CenterX, y - CenterY, z - CenterZ).Size() * FluidActor->CellSize;
				if (Distance <= SplashRadius)
				{
					float Falloff = 1.0f - (Distance / SplashRadius);
					FluidActor->FluidGrid->AddFluid(x, y, z, SplashAmount * Falloff);
				}
			}
		}
	}
}

void UVoxelFluidFunctionLibrary::SyncAllFluidActorsWithTerrain(UObject* WorldContextObject)
{
	if (!WorldContextObject)
		return;

	UWorld* World = WorldContextObject->GetWorld();
	if (!World)
		return;

	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(World, AVoxelFluidActor::StaticClass(), FoundActors);
	
	for (AActor* Actor : FoundActors)
	{
		AVoxelFluidActor* FluidActor = Cast<AVoxelFluidActor>(Actor);
		if (FluidActor)
		{
			FluidActor->RefreshTerrainData();
		}
	}
	
	UE_LOG(LogTemp, Log, TEXT("Synced %d fluid actors with terrain"), FoundActors.Num());
}

float UVoxelFluidFunctionLibrary::GetFluidDepthAtLocation(AVoxelFluidActor* FluidActor, const FVector& WorldLocation)
{
	if (!FluidActor || !FluidActor->FluidGrid)
		return 0.0f;

	const FVector LocalPos = WorldLocation - FluidActor->GetActorLocation();
	int32 CellX, CellY, CellZ;
	
	if (!FluidActor->FluidGrid->GetCellFromWorldPosition(LocalPos, CellX, CellY, CellZ))
		return 0.0f;

	float TotalDepth = 0.0f;
	for (int32 z = CellZ; z < FluidActor->GridSizeZ; ++z)
	{
		TotalDepth += FluidActor->FluidGrid->GetFluidAt(CellX, CellY, z) * FluidActor->CellSize;
	}
	
	return TotalDepth;
}

bool UVoxelFluidFunctionLibrary::IsLocationSubmerged(AVoxelFluidActor* FluidActor, const FVector& WorldLocation, float MinDepth)
{
	return GetFluidDepthAtLocation(FluidActor, WorldLocation) >= MinDepth;
}

void UVoxelFluidFunctionLibrary::TestFluidOnTerrain(AVoxelFluidActor* FluidActor, int32 NumTestPoints)
{
	if (!FluidActor || !FluidActor->FluidGrid)
		return;

	FluidActor->RefreshTerrainData();
	
	for (int32 i = 0; i < NumTestPoints; ++i)
	{
		int32 TestX = FMath::RandRange(FluidActor->GridSizeX / 4, FluidActor->GridSizeX * 3 / 4);
		int32 TestY = FMath::RandRange(FluidActor->GridSizeY / 4, FluidActor->GridSizeY * 3 / 4);
		
		for (int32 z = FluidActor->GridSizeZ - 1; z >= 0; --z)
		{
			const int32 CellIdx = TestX + TestY * FluidActor->GridSizeX + z * FluidActor->GridSizeX * FluidActor->GridSizeY;
			if (CellIdx >= 0 && CellIdx < FluidActor->FluidGrid->Cells.Num())
			{
				if (!FluidActor->FluidGrid->Cells[CellIdx].bIsSolid)
				{
					FluidActor->FluidGrid->AddFluid(TestX, TestY, z, 0.8f);
					
					UE_LOG(LogTemp, Log, TEXT("Added test fluid at grid position (%d, %d, %d)"), TestX, TestY, z);
					break;
				}
			}
		}
	}
	
	FluidActor->StartSimulation();
}