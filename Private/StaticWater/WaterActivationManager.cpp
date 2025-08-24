#include "StaticWater/WaterActivationManager.h"
#include "StaticWater/StaticWaterGenerator.h"
#include "StaticWater/StaticWaterRenderer.h"
#include "CellularAutomata/FluidChunkManager.h"
#include "CellularAutomata/CAFluidGrid.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

UWaterActivationManager::UWaterActivationManager()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetComponentTickInterval(0.1f); // 10 FPS by default
}

void UWaterActivationManager::BeginPlay()
{
	Super::BeginPlay();
	
	// Find components on the same actor if not set
	if (AActor* Owner = GetOwner())
	{
		if (!StaticWaterGenerator)
		{
			StaticWaterGenerator = Owner->FindComponentByClass<UStaticWaterGenerator>();
		}
		
		if (!StaticWaterRenderer)
		{
			StaticWaterRenderer = Owner->FindComponentByClass<UStaticWaterRenderer>();
		}
		
		// FluidChunkManager and FluidGrid are UObjects, not UActorComponents
		// They will be set manually via SetFluidChunkManager() and SetFluidGrid() from VoxelFluidActor
	}

	// Validate required components
	if (!StaticWaterGenerator)
	{
		UE_LOG(LogTemp, Warning, TEXT("WaterActivationManager: No StaticWaterGenerator found"));
	}
	if (!FluidChunkManager)
	{
		UE_LOG(LogTemp, Warning, TEXT("WaterActivationManager: No FluidChunkManager set - will be configured by VoxelFluidActor"));
	}

	bIsInitialized = true;
}

void UWaterActivationManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clean up all active regions
	{
		FScopeLock Lock(&RegionsMutex);
		ActiveRegions.Empty();
		ActivationQueue.Empty();
	}
	
	Super::EndPlay(EndPlayReason);
}

void UWaterActivationManager::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	if (!bIsInitialized)
		return;

	UpdateActiveRegions(DeltaTime);

#if WITH_EDITOR
	if (bShowActiveRegions || bShowActivationRadius)
	{
		DrawDebugInfo();
	}
#endif
}

void UWaterActivationManager::SetStaticWaterGenerator(UStaticWaterGenerator* InGenerator)
{
	StaticWaterGenerator = InGenerator;
}

void UWaterActivationManager::SetStaticWaterRenderer(UStaticWaterRenderer* InRenderer)
{
	StaticWaterRenderer = InRenderer;
}

void UWaterActivationManager::SetFluidChunkManager(UFluidChunkManager* InChunkManager)
{
	FluidChunkManager = InChunkManager;
}

void UWaterActivationManager::SetFluidGrid(UCAFluidGrid* InFluidGrid)
{
	FluidGrid = InFluidGrid;
}

void UWaterActivationManager::OnTerrainEdited(const FVector& EditPosition, float EditRadius, float HeightChange)
{
	if (!StaticWaterGenerator || FMath::Abs(HeightChange) < ActivationSettings.TerrainChangeThreshold)
		return;

	// Check if there's static water in the edit area
	if (StaticWaterGenerator->HasStaticWaterAtLocation(EditPosition))
	{
		const float ActivationRadius = FMath::Max(EditRadius * 2.0f, ActivationSettings.DefaultActivationRadius);
		
		if (bEnableLogging)
		{
			UE_LOG(LogTemp, Log, TEXT("WaterActivationManager: Terrain edited at %s (radius: %.1f, height change: %.1f) - activating water"), 
				*EditPosition.ToString(), EditRadius, HeightChange);
		}
		
		// Queue activation with high priority for terrain edits
		FPendingActivation Activation;
		Activation.Center = EditPosition;
		Activation.Radius = ActivationRadius;
		Activation.Priority = 100; // High priority
		Activation.QueueTime = GetWorld()->GetTimeSeconds();
		
		FScopeLock Lock(&RegionsMutex);
		ActivationQueue.Add(Activation);
	}
}

void UWaterActivationManager::OnFluidAdded(const FVector& Position, float Amount)
{
	if (Amount <= 0.0f)
		return;

	// Always activate when fluid is manually added
	const float ActivationRadius = ActivationSettings.DefaultActivationRadius;
	
	if (bEnableLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("WaterActivationManager: Fluid added at %s (amount: %.2f) - activating water"), 
			*Position.ToString(), Amount);
	}
	
	FPendingActivation Activation;
	Activation.Center = Position;
	Activation.Radius = ActivationRadius;
	Activation.Priority = 90; // High priority
	Activation.QueueTime = GetWorld()->GetTimeSeconds();
	
	FScopeLock Lock(&RegionsMutex);
	ActivationQueue.Add(Activation);
}

void UWaterActivationManager::OnExplosion(const FVector& Position, float Radius)
{
	// Check if explosion affects static water
	if (!StaticWaterGenerator || !StaticWaterGenerator->HasStaticWaterAtLocation(Position))
		return;

	const float ActivationRadius = FMath::Max(Radius * 1.5f, ActivationSettings.DefaultActivationRadius);
	
	if (bEnableLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("WaterActivationManager: Explosion at %s (radius: %.1f) - activating water"), 
			*Position.ToString(), Radius);
	}
	
	FPendingActivation Activation;
	Activation.Center = Position;
	Activation.Radius = ActivationRadius;
	Activation.Priority = 95; // Very high priority
	Activation.QueueTime = GetWorld()->GetTimeSeconds();
	
	FScopeLock Lock(&RegionsMutex);
	ActivationQueue.Add(Activation);
}

bool UWaterActivationManager::ActivateWaterInRegion(const FVector& Center, float Radius)
{
	if (!StaticWaterGenerator || !FluidChunkManager)
		return false;

	// Check if region already exists
	FScopeLock Lock(&RegionsMutex);
	
	if (FWaterActivationRegion* ExistingRegion = FindRegionAtPosition(Center))
	{
		// Extend existing region if needed
		const float ExistingRadius = FMath::Sqrt(ExistingRegion->Bounds.GetSize().SizeSquared2D() * 0.25f);
		if (Radius > ExistingRadius)
		{
			const FVector RegionSize = FVector(Radius * 2.0f, Radius * 2.0f, 2000.0f);
			ExistingRegion->Bounds = FBox::BuildAABB(Center, RegionSize * 0.5f);
			ExistingRegion->ActivationRadius = Radius;
		}
		return true;
	}

	// Create new region
	if (FWaterActivationRegion* NewRegion = CreateActivationRegion(Center, Radius))
	{
		ActivateSimulation(*NewRegion);
		
		OnWaterRegionActivated.Broadcast(Center, Radius);
		
		if (bEnableLogging)
		{
			UE_LOG(LogTemp, Log, TEXT("WaterActivationManager: Activated water region at %s (radius: %.1f)"), 
				*Center.ToString(), Radius);
		}
		
		return true;
	}

	return false;
}

bool UWaterActivationManager::DeactivateWaterInRegion(const FVector& Center, float Radius)
{
	FScopeLock Lock(&RegionsMutex);
	
	for (int32 i = 0; i < ActiveRegions.Num(); ++i)
	{
		FWaterActivationRegion& Region = ActiveRegions[i];
		if (Region.ContainsPoint(Center) || FVector::Dist(Region.Bounds.GetCenter(), Center) <= Radius)
		{
			DeactivateSimulation(Region);
			OnWaterRegionDeactivated.Broadcast(Region.Bounds.GetCenter(), Region.ActivationRadius);
			
			if (bEnableLogging)
			{
				UE_LOG(LogTemp, Log, TEXT("WaterActivationManager: Deactivated water region at %s"), 
					*Region.Bounds.GetCenter().ToString());
			}
			
			ActiveRegions.RemoveAt(i);
			return true;
		}
	}
	
	return false;
}

void UWaterActivationManager::ForceDeactivateAllRegions()
{
	FScopeLock Lock(&RegionsMutex);
	
	for (FWaterActivationRegion& Region : ActiveRegions)
	{
		DeactivateSimulation(Region);
		OnWaterRegionDeactivated.Broadcast(Region.Bounds.GetCenter(), Region.ActivationRadius);
	}
	
	ActiveRegions.Empty();
	ActivationQueue.Empty();
	
	UE_LOG(LogTemp, Log, TEXT("WaterActivationManager: Force deactivated all regions"));
}

bool UWaterActivationManager::IsRegionActive(const FVector& Position) const
{
	FScopeLock Lock(&RegionsMutex);
	return FindRegionAtPosition(Position) != nullptr;
}

int32 UWaterActivationManager::GetActiveRegionCount() const
{
	FScopeLock Lock(&RegionsMutex);
	return ActiveRegions.Num();
}

TArray<FVector> UWaterActivationManager::GetActiveRegionCenters() const
{
	TArray<FVector> Centers;
	
	FScopeLock Lock(&RegionsMutex);
	for (const FWaterActivationRegion& Region : ActiveRegions)
	{
		Centers.Add(Region.Bounds.GetCenter());
	}
	
	return Centers;
}

float UWaterActivationManager::GetRegionActivationTime(const FVector& Position) const
{
	FScopeLock Lock(&RegionsMutex);
	
	if (const FWaterActivationRegion* Region = FindRegionAtPosition(Position))
	{
		return GetWorld()->GetTimeSeconds() - Region->ActivationTime;
	}
	
	return -1.0f;
}

void UWaterActivationManager::UpdateActiveRegions(float DeltaTime)
{
	RegionUpdateTimer += DeltaTime;
	DeactivationCheckTimer += DeltaTime;
	OptimizationTimer += DeltaTime;
	
	// Process activation queue
	if (RegionUpdateTimer >= ActivationSettings.UpdateFrequency)
	{
		RegionUpdateTimer = 0.0f;
		ProcessActivationQueue();
	}
	
	// Check for deactivation
	if (DeactivationCheckTimer >= ActivationSettings.SettleCheckInterval)
	{
		DeactivationCheckTimer = 0.0f;
		CheckForDeactivation(DeltaTime);
	}
	
	// Optimize regions periodically
	if (OptimizationTimer >= 30.0f)
	{
		OptimizationTimer = 0.0f;
		OptimizeRegions();
	}
	
	// Reset per-frame counters
	ActivationsThisFrame = 0;
	DeactivationsThisFrame = 0;
}

void UWaterActivationManager::CheckForDeactivation(float DeltaTime)
{
	FScopeLock Lock(&RegionsMutex);
	
	TArray<int32> RegionsToDeactivate;
	const float CurrentTime = GetWorld()->GetTimeSeconds();
	
	for (int32 i = 0; i < ActiveRegions.Num(); ++i)
	{
		FWaterActivationRegion& Region = ActiveRegions[i];
		
		// Check if region has been active long enough to consider deactivation
		if (CurrentTime - Region.ActivationTime < ActivationSettings.DeactivationDelay)
			continue;
		
		// Check if fluid has settled
		if (IsFluidSettled(Region))
		{
			RegionsToDeactivate.Add(i);
		}
	}
	
	// Deactivate regions (in reverse order to maintain indices)
	for (int32 i = RegionsToDeactivate.Num() - 1; i >= 0; --i)
	{
		const int32 RegionIndex = RegionsToDeactivate[i];
		FWaterActivationRegion& Region = ActiveRegions[RegionIndex];
		
		DeactivateSimulation(Region);
		OnWaterRegionDeactivated.Broadcast(Region.Bounds.GetCenter(), Region.ActivationRadius);
		
		if (bEnableLogging)
		{
			UE_LOG(LogTemp, Log, TEXT("WaterActivationManager: Auto-deactivated settled region at %s"), 
				*Region.Bounds.GetCenter().ToString());
		}
		
		ActiveRegions.RemoveAt(RegionIndex);
		++DeactivationsThisFrame;
	}
}

void UWaterActivationManager::ProcessActivationQueue()
{
	FScopeLock Lock(&RegionsMutex);
	
	// Sort queue by priority and age
	ActivationQueue.Sort([](const FPendingActivation& A, const FPendingActivation& B)
	{
		if (A.Priority != B.Priority)
			return A.Priority > B.Priority;
		return A.QueueTime < B.QueueTime; // Older first
	});
	
	// Process activations up to frame limit
	while (!ActivationQueue.IsEmpty() && ActivationsThisFrame < ActivationSettings.MaxActivationsPerFrame)
	{
		FPendingActivation Activation = ActivationQueue[0];
		ActivationQueue.RemoveAt(0);
		
		// Check if we're at the active region limit
		if (ActiveRegions.Num() >= ActivationSettings.MaxActiveRegions)
		{
			// Try to remove the oldest, least important region
			int32 OldestRegionIndex = -1;
			float OldestTime = MAX_flt;
			
			for (int32 i = 0; i < ActiveRegions.Num(); ++i)
			{
				if (ActiveRegions[i].ActivationTime < OldestTime)
				{
					OldestTime = ActiveRegions[i].ActivationTime;
					OldestRegionIndex = i;
				}
			}
			
			if (OldestRegionIndex >= 0)
			{
				FWaterActivationRegion& OldRegion = ActiveRegions[OldestRegionIndex];
				DeactivateSimulation(OldRegion);
				OnWaterRegionDeactivated.Broadcast(OldRegion.Bounds.GetCenter(), OldRegion.ActivationRadius);
				ActiveRegions.RemoveAt(OldestRegionIndex);
			}
			else
			{
				break; // Can't make room
			}
		}
		
		// Activate the region
		if (ActivateWaterInRegion(Activation.Center, Activation.Radius))
		{
			++ActivationsThisFrame;
		}
	}
}

FWaterActivationRegion* UWaterActivationManager::FindRegionAtPosition(const FVector& Position)
{
	for (FWaterActivationRegion& Region : ActiveRegions)
	{
		if (Region.ContainsPoint(Position))
		{
			return &Region;
		}
	}
	return nullptr;
}

const FWaterActivationRegion* UWaterActivationManager::FindRegionAtPosition(const FVector& Position) const
{
	for (const FWaterActivationRegion& Region : ActiveRegions)
	{
		if (Region.ContainsPoint(Position))
		{
			return &Region;
		}
	}
	return nullptr;
}

FWaterActivationRegion* UWaterActivationManager::CreateActivationRegion(const FVector& Center, float Radius)
{
	FWaterActivationRegion NewRegion;
	
	// Set bounds as a box around the center
	const FVector RegionSize = FVector(Radius * 2.0f, Radius * 2.0f, 2000.0f); // Fixed height for now
	NewRegion.Bounds = FBox::BuildAABB(Center, RegionSize * 0.5f);
	NewRegion.ActivationRadius = Radius;
	NewRegion.bIsActive = false;
	NewRegion.ActivationTime = GetWorld()->GetTimeSeconds();
	NewRegion.Priority = 0;
	
	int32 Index = ActiveRegions.Add(NewRegion);
	return &ActiveRegions[Index];
}

void UWaterActivationManager::RemoveActivationRegion(int32 RegionIndex)
{
	if (ActiveRegions.IsValidIndex(RegionIndex))
	{
		ActiveRegions.RemoveAt(RegionIndex);
	}
}

bool UWaterActivationManager::ShouldMergeRegions(const FWaterActivationRegion& RegionA, const FWaterActivationRegion& RegionB) const
{
	// Check if regions overlap or are very close
	const float MergeThreshold = ActivationSettings.DefaultActivationRadius * 0.5f;
	return FVector::Dist(RegionA.Bounds.GetCenter(), RegionB.Bounds.GetCenter()) <= MergeThreshold;
}

void UWaterActivationManager::MergeRegions(FWaterActivationRegion& TargetRegion, const FWaterActivationRegion& SourceRegion)
{
	// Expand target region to encompass both
	TargetRegion.Bounds += SourceRegion.Bounds;
	
	// Use the more recent activation time
	TargetRegion.ActivationTime = FMath::Max(TargetRegion.ActivationTime, SourceRegion.ActivationTime);
	
	// Use higher priority
	TargetRegion.Priority = FMath::Max(TargetRegion.Priority, SourceRegion.Priority);
	
	// Merge static water data
	TargetRegion.StaticWaterPositions.Append(SourceRegion.StaticWaterPositions);
	TargetRegion.StaticWaterAmounts.Append(SourceRegion.StaticWaterAmounts);
}

void UWaterActivationManager::ActivateSimulation(FWaterActivationRegion& Region)
{
	if (Region.bIsActive)
		return;
		
	// Transfer static water to simulation
	TransferStaticToSimulation(Region);
	
	// Hide static water renderer in this region
	if (StaticWaterRenderer)
	{
		StaticWaterRenderer->RebuildChunksInRadius(Region.Bounds.GetCenter(), Region.ActivationRadius);
	}
	
	Region.bIsActive = true;
	LastActivationTime = GetWorld()->GetTimeSeconds();
}

void UWaterActivationManager::DeactivateSimulation(FWaterActivationRegion& Region)
{
	if (!Region.bIsActive)
		return;
		
	// Transfer simulation back to static water if preserving volume
	if (ActivationSettings.bPreserveFluidVolume)
	{
		TransferSimulationToStatic(Region);
	}
	
	// Re-enable static water renderer in this region
	if (StaticWaterRenderer)
	{
		StaticWaterRenderer->RebuildChunksInRadius(Region.Bounds.GetCenter(), Region.ActivationRadius);
	}
	
	Region.bIsActive = false;
	Region.Clear();
}

void UWaterActivationManager::TransferStaticToSimulation(const FWaterActivationRegion& Region)
{
	if (!StaticWaterGenerator || !FluidChunkManager)
		return;
		
	// Sample static water in the region and add to simulation
	const FBox& Bounds = Region.Bounds;
	const float SampleSpacing = 100.0f; // 1m spacing
	
	const int32 SamplesX = FMath::CeilToInt(Bounds.GetSize().X / SampleSpacing);
	const int32 SamplesY = FMath::CeilToInt(Bounds.GetSize().Y / SampleSpacing);
	
	for (int32 X = 0; X < SamplesX; ++X)
	{
		for (int32 Y = 0; Y < SamplesY; ++Y)
		{
			const FVector SamplePos = FVector(
				Bounds.Min.X + X * SampleSpacing,
				Bounds.Min.Y + Y * SampleSpacing,
				0.0f
			);
			
			const float WaterDepth = StaticWaterGenerator->GetWaterDepthAtLocation(SamplePos);
			if (WaterDepth > 0.0f)
			{
				// Add fluid to simulation at this position
				FluidChunkManager->AddFluidAtWorldPosition(SamplePos, WaterDepth);
			}
		}
	}
}

void UWaterActivationManager::TransferSimulationToStatic(FWaterActivationRegion& Region)
{
	// TODO: Sample fluid from simulation and update static water generator
	// This would involve reading fluid levels from active chunks and updating static regions
	
	if (bEnableLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("WaterActivationManager: Transferring simulation back to static (not fully implemented)"));
	}
}

bool UWaterActivationManager::IsFluidSettled(const FWaterActivationRegion& Region) const
{
	if (!FluidChunkManager)
		return true; // Assume settled if no chunk manager
		
	const float AverageVelocity = GetAverageFluidVelocity(Region);
	return AverageVelocity < ActivationSettings.FluidSettleThreshold;
}

float UWaterActivationManager::GetAverageFluidVelocity(const FWaterActivationRegion& Region) const
{
	if (!FluidChunkManager)
		return 0.0f;
		
	// TODO: Sample fluid velocity from chunks in the region
	// For now, return a dummy value
	return 0.001f;
}

bool UWaterActivationManager::HasFluidInRegion(const FWaterActivationRegion& Region) const
{
	if (!FluidChunkManager)
		return false;
		
	// TODO: Check if there's any fluid in chunks that intersect this region
	// For now, assume there is fluid
	return true;
}

void UWaterActivationManager::OptimizeRegions()
{
	FScopeLock Lock(&RegionsMutex);
	
	// Remove empty regions
	RemoveEmptyRegions();
	
	// Merge overlapping regions
	for (int32 i = 0; i < ActiveRegions.Num(); ++i)
	{
		for (int32 j = i + 1; j < ActiveRegions.Num(); ++j)
		{
			if (ShouldMergeRegions(ActiveRegions[i], ActiveRegions[j]))
			{
				MergeRegions(ActiveRegions[i], ActiveRegions[j]);
				ActiveRegions.RemoveAt(j);
				--j; // Adjust index after removal
			}
		}
	}
}

void UWaterActivationManager::RemoveEmptyRegions()
{
	for (int32 i = ActiveRegions.Num() - 1; i >= 0; --i)
	{
		const FWaterActivationRegion& Region = ActiveRegions[i];
		if (!HasFluidInRegion(Region))
		{
			// Region is empty, deactivate it
			DeactivateSimulation(const_cast<FWaterActivationRegion&>(Region));
			ActiveRegions.RemoveAt(i);
		}
	}
}

void UWaterActivationManager::DrawDebugInfo() const
{
#if WITH_EDITOR
	UWorld* World = GetWorld();
	if (!World)
		return;
		
	FScopeLock Lock(&RegionsMutex);
	
	// Draw active regions
	if (bShowActiveRegions)
	{
		for (const FWaterActivationRegion& Region : ActiveRegions)
		{
			const FColor RegionColor = Region.bIsActive ? FColor::Green : FColor::Yellow;
			
			// Draw region bounds
			DrawDebugBox(World, Region.Bounds.GetCenter(), Region.Bounds.GetExtent(), 
				RegionColor, false, -1.0f, 0, 5.0f);
			
			// Draw activation time text
			const float ActivationTime = GetWorld()->GetTimeSeconds() - Region.ActivationTime;
			const FString TimeText = FString::Printf(TEXT("Active: %.1fs"), ActivationTime);
			DrawDebugString(World, Region.Bounds.GetCenter() + FVector(0, 0, 200), 
				TimeText, nullptr, RegionColor, 0.0f);
		}
	}
	
	// Draw activation radius for each region
	if (bShowActivationRadius)
	{
		for (const FWaterActivationRegion& Region : ActiveRegions)
		{
			DrawDebugCircle(World, Region.Bounds.GetCenter(), Region.ActivationRadius, 
				32, FColor::Cyan, false, -1.0f, 0, 10.0f, FVector::ForwardVector, FVector::RightVector, false);
		}
	}
#endif
}