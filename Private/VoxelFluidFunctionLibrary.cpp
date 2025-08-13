#include "VoxelFluidFunctionLibrary.h"
#include "Actors/VoxelFluidActor.h"
#include "CellularAutomata/FluidChunkManager.h"
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
		// Set simulation bounds based on requested grid size
		const float CellSize = FluidActor->CellSize;
		FluidActor->SimulationBoundsExtent = FVector(GridSizeX * CellSize * 0.5f, GridSizeY * CellSize * 0.5f, GridSizeZ * CellSize * 0.5f);
		FluidActor->InitializeFluidSystem();
		
		UE_LOG(LogTemp, Log, TEXT("Spawned Voxel Fluid System at %s with effective size %dx%dx%d cells"), 
			   *Location.ToString(), GridSizeX, GridSizeY, GridSizeZ);
	}

	return FluidActor;
}

void UVoxelFluidFunctionLibrary::AddRainToFluidSystem(AVoxelFluidActor* FluidActor, float Intensity, float Radius)
{
	if (!FluidActor || !FluidActor->ChunkManager)
		return;

	const FVector ActorLocation = FluidActor->GetActorLocation();
	
	// Add rain using the chunk manager
	// This will automatically handle chunk loading and fluid distribution
	const int32 NumDrops = FMath::CeilToInt(Radius * Radius * Intensity * 0.01f);
	for (int32 i = 0; i < NumDrops; ++i)
	{
		const float Angle = FMath::FRand() * 2.0f * PI;
		const float Distance = FMath::Sqrt(FMath::FRand()) * Radius;
		const FVector DropLocation = ActorLocation + FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 1000.0f);
		
		FluidActor->AddFluidAtLocation(DropLocation, Intensity * 0.1f);
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
	if (!FluidActor || !FluidActor->ChunkManager)
		return;

	// Create splash using chunk manager
	const int32 NumSplashPoints = FMath::CeilToInt(SplashRadius * 0.1f);
	for (int32 i = 0; i < NumSplashPoints; ++i)
	{
		const FVector RandomOffset = FVector(
			FMath::RandRange(-SplashRadius, SplashRadius),
			FMath::RandRange(-SplashRadius, SplashRadius),
			FMath::RandRange(0.0f, SplashRadius * 0.5f)
		);
		
		const FVector SplashPoint = ImpactLocation + RandomOffset;
		const float Distance = RandomOffset.Size();
		const float Falloff = 1.0f - FMath::Clamp(Distance / SplashRadius, 0.0f, 1.0f);
		
		FluidActor->AddFluidAtLocation(SplashPoint, SplashAmount * Falloff);
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
	if (!FluidActor || !FluidActor->ChunkManager)
		return 0.0f;

	// Query fluid depth from chunk manager
	// TODO: Implement GetFluidDepthAtWorldPosition in ChunkManager
	// For now return 0.0f as placeholder
	return 0.0f;
}

bool UVoxelFluidFunctionLibrary::IsLocationSubmerged(AVoxelFluidActor* FluidActor, const FVector& WorldLocation, float MinDepth)
{
	return GetFluidDepthAtLocation(FluidActor, WorldLocation) >= MinDepth;
}

void UVoxelFluidFunctionLibrary::TestFluidOnTerrain(AVoxelFluidActor* FluidActor, int32 NumTestPoints)
{
	if (!FluidActor || !FluidActor->ChunkManager)
		return;

	FluidActor->RefreshTerrainData();
	
	const FVector ActorLocation = FluidActor->GetActorLocation();
	const FVector BoundsExtent = FluidActor->SimulationBoundsExtent;
	
	for (int32 i = 0; i < NumTestPoints; ++i)
	{
		const FVector TestLocation = ActorLocation + FVector(
			FMath::RandRange(-BoundsExtent.X * 0.5f, BoundsExtent.X * 0.5f),
			FMath::RandRange(-BoundsExtent.Y * 0.5f, BoundsExtent.Y * 0.5f),
			FMath::RandRange(0.0f, (float)(BoundsExtent.Z * 0.75f))
		);
		
		FluidActor->AddFluidAtLocation(TestLocation, 0.8f);
		
		UE_LOG(LogTemp, Log, TEXT("Added test fluid at world position %s"), *TestLocation.ToString());
	}
	
	FluidActor->StartSimulation();
}