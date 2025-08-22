#include "Optimization/FluidLODManager.h"
#include "CellularAutomata/FluidChunk.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "Async/ParallelFor.h"

UFluidLODManager::UFluidLODManager()
{
	LODCounts.SetNum(5); // LOD0-3 + Culled
}

void UFluidLODManager::Initialize(const FFluidLODSettings& InSettings)
{
	Settings = InSettings;
	ChunkLODStates.Empty();
	CurrentFrame = 0;
	
	for (int32& Count : LODCounts)
	{
		Count = 0;
	}
}

void UFluidLODManager::UpdateLODStates(const TArray<UFluidChunk*>& Chunks, const TArray<FVector>& ViewerPositions, float DeltaTime)
{
	FScopeLock Lock(&LODMutex);
	
	CurrentFrame++;
	
	// Process chunks in parallel for better performance
	ParallelFor(Chunks.Num(), [&](int32 Index)
	{
		UFluidChunk* Chunk = Chunks[Index];
		if (!Chunk)
			return;
		
		FChunkLODState* LODState = ChunkLODStates.Find(Chunk->ChunkCoord);
		if (!LODState)
		{
			FChunkLODState NewState;
			NewState.ChunkCoord = Chunk->ChunkCoord;
			ChunkLODStates.Add(Chunk->ChunkCoord, NewState);
			LODState = ChunkLODStates.Find(Chunk->ChunkCoord);
		}
		
		// Calculate distance to nearest viewer
		float MinDistance = FLT_MAX;
		FVector ChunkCenter = Chunk->ChunkWorldPosition;
		
		for (const FVector& ViewerPos : ViewerPositions)
		{
			float Distance = FVector::Dist(ChunkCenter, ViewerPos);
			MinDistance = FMath::Min(MinDistance, Distance);
		}
		
		LODState->DistanceToViewer = MinDistance;
		
		// Determine target LOD based on distance
		LODState->TargetLOD = Settings.GetLODForDistance(MinDistance);
		
		// Check frustum culling
		if (Settings.bUseFrustumCulling && FrustumPlanes.Num() == 6)
		{
			LODState->bInFrustum = IsChunkInFrustum(Chunk);
			if (!LODState->bInFrustum)
			{
				LODState->TargetLOD = EFluidLODLevel::LOD_Culled;
			}
		}
		
		// Calculate importance factor based on fluid activity
		LODState->ImportanceFactor = CalculateChunkImportance(Chunk, ViewerPositions.Num() > 0 ? ViewerPositions[0] : FVector::ZeroVector);
		
		// Adjust LOD based on importance (active chunks get higher detail)
		if (LODState->ImportanceFactor > 1.5f && LODState->TargetLOD > EFluidLODLevel::LOD0)
		{
			LODState->TargetLOD = static_cast<EFluidLODLevel>(FMath::Max(0, (int32)LODState->TargetLOD - 1));
		}
		
		// Update visibility
		LODState->bIsVisible = (LODState->TargetLOD != EFluidLODLevel::LOD_Culled) && 
		                      LODState->bInFrustum && 
		                      !LODState->bOccluded;
		
		// Update transition
		if (Settings.bSmoothLODTransitions)
		{
			LODState->UpdateTransition(DeltaTime, Settings.LODTransitionSpeed);
		}
		else
		{
			LODState->CurrentLOD = LODState->TargetLOD;
		}
		
		// Apply LOD to chunk
		Chunk->SetLODLevel((int32)LODState->CurrentLOD);
		
		// Update frame counter
		LODState->FramesSinceLastUpdate++;
		if (LODState->ShouldUpdateThisFrame(CurrentFrame))
		{
			LODState->FramesSinceLastUpdate = 0;
			LODState->LastUpdateTime = FPlatformTime::Seconds();
		}
	});
	
	UpdateLODCounts();
}

EFluidLODLevel UFluidLODManager::GetChunkLOD(const FFluidChunkCoord& ChunkCoord) const
{
	const FChunkLODState* LODState = ChunkLODStates.Find(ChunkCoord);
	return LODState ? LODState->CurrentLOD : EFluidLODLevel::LOD0;
}

bool UFluidLODManager::ShouldUpdateChunk(UFluidChunk* Chunk, int32 CurrentFrameOverride) const
{
	if (!Chunk)
		return false;
	
	const FChunkLODState* LODState = ChunkLODStates.Find(Chunk->ChunkCoord);
	if (!LODState)
		return true; // Update chunks without LOD state
	
	int32 FrameToCheck = CurrentFrameOverride >= 0 ? CurrentFrameOverride : CurrentFrame;
	return LODState->ShouldUpdateThisFrame(FrameToCheck);
}

void UFluidLODManager::SetLODSettings(const FFluidLODSettings& NewSettings)
{
	Settings = NewSettings;
}

FString UFluidLODManager::GetLODStats() const
{
	FString Stats = TEXT("Fluid LOD Statistics:\n");
	Stats += FString::Printf(TEXT("Total Chunks: %d\n"), ChunkLODStates.Num());
	Stats += FString::Printf(TEXT("LOD0 (Full): %d\n"), LODCounts[0]);
	Stats += FString::Printf(TEXT("LOD1 (Medium): %d\n"), LODCounts[1]);
	Stats += FString::Printf(TEXT("LOD2 (Low): %d\n"), LODCounts[2]);
	Stats += FString::Printf(TEXT("LOD3 (Very Low): %d\n"), LODCounts[3]);
	Stats += FString::Printf(TEXT("Culled: %d\n"), LODCounts[4]);
	
	int32 VisibleCount = 0;
	for (const auto& Pair : ChunkLODStates)
	{
		if (Pair.Value.bIsVisible)
			VisibleCount++;
	}
	Stats += FString::Printf(TEXT("Visible Chunks: %d\n"), VisibleCount);
	
	return Stats;
}

int32 UFluidLODManager::GetChunksAtLOD(EFluidLODLevel LOD) const
{
	int32 LODIndex = (int32)LOD;
	return LODCounts.IsValidIndex(LODIndex) ? LODCounts[LODIndex] : 0;
}

void UFluidLODManager::ForceUpdateLOD(UFluidChunk* Chunk, EFluidLODLevel NewLOD)
{
	if (!Chunk)
		return;
	
	FScopeLock Lock(&LODMutex);
	
	FChunkLODState* LODState = ChunkLODStates.Find(Chunk->ChunkCoord);
	if (LODState)
	{
		LODState->CurrentLOD = NewLOD;
		LODState->TargetLOD = NewLOD;
		LODState->TransitionAlpha = 0.0f;
		Chunk->SetLODLevel((int32)NewLOD);
	}
}

void UFluidLODManager::UpdateFrustumCulling(const FMatrix& ViewProjectionMatrix)
{
	FrustumPlanes.SetNum(6);
	
	// Extract frustum planes from view-projection matrix
	// Left plane
	FrustumPlanes[0] = FPlane(
		ViewProjectionMatrix.M[0][3] + ViewProjectionMatrix.M[0][0],
		ViewProjectionMatrix.M[1][3] + ViewProjectionMatrix.M[1][0],
		ViewProjectionMatrix.M[2][3] + ViewProjectionMatrix.M[2][0],
		ViewProjectionMatrix.M[3][3] + ViewProjectionMatrix.M[3][0]
	);
	
	// Right plane
	FrustumPlanes[1] = FPlane(
		ViewProjectionMatrix.M[0][3] - ViewProjectionMatrix.M[0][0],
		ViewProjectionMatrix.M[1][3] - ViewProjectionMatrix.M[1][0],
		ViewProjectionMatrix.M[2][3] - ViewProjectionMatrix.M[2][0],
		ViewProjectionMatrix.M[3][3] - ViewProjectionMatrix.M[3][0]
	);
	
	// Bottom plane
	FrustumPlanes[2] = FPlane(
		ViewProjectionMatrix.M[0][3] + ViewProjectionMatrix.M[0][1],
		ViewProjectionMatrix.M[1][3] + ViewProjectionMatrix.M[1][1],
		ViewProjectionMatrix.M[2][3] + ViewProjectionMatrix.M[2][1],
		ViewProjectionMatrix.M[3][3] + ViewProjectionMatrix.M[3][1]
	);
	
	// Top plane
	FrustumPlanes[3] = FPlane(
		ViewProjectionMatrix.M[0][3] - ViewProjectionMatrix.M[0][1],
		ViewProjectionMatrix.M[1][3] - ViewProjectionMatrix.M[1][1],
		ViewProjectionMatrix.M[2][3] - ViewProjectionMatrix.M[2][1],
		ViewProjectionMatrix.M[3][3] - ViewProjectionMatrix.M[3][1]
	);
	
	// Near plane
	FrustumPlanes[4] = FPlane(
		ViewProjectionMatrix.M[0][2],
		ViewProjectionMatrix.M[1][2],
		ViewProjectionMatrix.M[2][2],
		ViewProjectionMatrix.M[3][2]
	);
	
	// Far plane
	FrustumPlanes[5] = FPlane(
		ViewProjectionMatrix.M[0][3] - ViewProjectionMatrix.M[0][2],
		ViewProjectionMatrix.M[1][3] - ViewProjectionMatrix.M[1][2],
		ViewProjectionMatrix.M[2][3] - ViewProjectionMatrix.M[2][2],
		ViewProjectionMatrix.M[3][3] - ViewProjectionMatrix.M[3][2]
	);
	
	// Normalize planes
	for (FPlane& Plane : FrustumPlanes)
	{
		float Length = FMath::Sqrt(Plane.X * Plane.X + Plane.Y * Plane.Y + Plane.Z * Plane.Z);
		if (Length > 0.0f)
		{
			Plane /= Length;
		}
	}
}

void UFluidLODManager::PerformOcclusionQueries(UWorld* World, const FVector& ViewPosition)
{
	if (!World || !Settings.bUseOcclusionCulling)
		return;
	
	// Simple occlusion test using line traces
	for (auto& Pair : ChunkLODStates)
	{
		FChunkLODState& LODState = Pair.Value;
		
		// Only test chunks that are in frustum and not already culled
		if (!LODState.bInFrustum || LODState.TargetLOD == EFluidLODLevel::LOD_Culled)
		{
			LODState.bOccluded = false;
			continue;
		}
		
		// Perform line trace from viewer to chunk center
		FHitResult HitResult;
		FVector ChunkCenter = FVector::ZeroVector; // Should get from chunk
		
		FCollisionQueryParams QueryParams;
		QueryParams.bTraceComplex = false;
		QueryParams.AddIgnoredActor(nullptr); // Add ignored actors if needed
		
		bool bHit = World->LineTraceSingleByChannel(
			HitResult,
			ViewPosition,
			ChunkCenter,
			ECC_Visibility,
			QueryParams
		);
		
		LODState.bOccluded = bHit && (HitResult.Distance < LODState.DistanceToViewer - 100.0f);
	}
}

float UFluidLODManager::CalculateChunkImportance(UFluidChunk* Chunk, const FVector& ViewPosition) const
{
	if (!Chunk)
		return 1.0f;
	
	float Importance = 1.0f;
	
	// Factor in fluid activity
	float Activity = Chunk->GetActiveCellCount() / (float)(Chunk->ChunkSize * Chunk->ChunkSize * Chunk->ChunkSize);
	Importance *= (1.0f + Activity * 2.0f);
	
	// Factor in fluid volume
	float Volume = Chunk->GetTotalFluidVolume();
	if (Volume > 0.1f)
	{
		Importance *= (1.0f + FMath::Min(Volume * 0.1f, 2.0f));
	}
	
	// Factor in whether chunk is settling or active
	if (!Chunk->bFullySettled)
	{
		Importance *= 1.5f;
	}
	
	// Factor in view angle (chunks in center of view are more important)
	// This would require camera forward vector, simplified here
	
	return FMath::Clamp(Importance, 0.1f, 3.0f);
}

TArray<UFluidChunk*> UFluidLODManager::GetVisibleChunks() const
{
	TArray<UFluidChunk*> VisibleChunks;
	
	// Note: This requires storing chunk pointers in LOD states
	// For now, returning empty array
	
	return VisibleChunks;
}

void UFluidLODManager::OptimizeLODDistribution(int32 MaxActiveChunks)
{
	// Adaptive LOD adjustment to maintain performance
	int32 TotalActive = LODCounts[0] + LODCounts[1] + LODCounts[2] + LODCounts[3];
	
	if (TotalActive > MaxActiveChunks)
	{
		// Need to reduce detail
		float ReductionRatio = (float)MaxActiveChunks / TotalActive;
		
		// Adjust LOD distances to reduce active chunks
		Settings.LOD0Distance *= ReductionRatio;
		Settings.LOD1Distance *= ReductionRatio;
		Settings.LOD2Distance *= ReductionRatio;
		Settings.LOD3Distance *= ReductionRatio;
		
		UE_LOG(LogTemp, Warning, TEXT("LOD distances reduced by %.1f%% to maintain performance"),
			(1.0f - ReductionRatio) * 100.0f);
	}
	else if (TotalActive < MaxActiveChunks * 0.7f)
	{
		// Can increase detail
		float IncreaseRatio = FMath::Min(1.2f, (float)MaxActiveChunks / FMath::Max(1, TotalActive));
		
		Settings.LOD0Distance *= IncreaseRatio;
		Settings.LOD1Distance *= IncreaseRatio;
		Settings.LOD2Distance *= IncreaseRatio;
		Settings.LOD3Distance *= IncreaseRatio;
	}
}

bool UFluidLODManager::IsChunkInFrustum(UFluidChunk* Chunk) const
{
	if (!Chunk || FrustumPlanes.Num() != 6)
		return true; // Assume visible if we can't test
	
	FBox ChunkBounds = Chunk->GetWorldBounds();
	
	// Test chunk bounds against each frustum plane
	for (const FPlane& Plane : FrustumPlanes)
	{
		// Get the corner of the box that is furthest in the negative direction of the plane normal
		FVector NegativeVertex(
			Plane.X > 0 ? ChunkBounds.Min.X : ChunkBounds.Max.X,
			Plane.Y > 0 ? ChunkBounds.Min.Y : ChunkBounds.Max.Y,
			Plane.Z > 0 ? ChunkBounds.Min.Z : ChunkBounds.Max.Z
		);
		
		// If this corner is outside the plane, the entire box is outside
		if (Plane.PlaneDot(NegativeVertex) < 0.0f)
		{
			return false;
		}
	}
	
	return true;
}

float UFluidLODManager::GetMinDistanceToViewers(const FVector& ChunkPosition, const TArray<FVector>& ViewerPositions) const
{
	float MinDistance = FLT_MAX;
	
	for (const FVector& ViewerPos : ViewerPositions)
	{
		float Distance = FVector::Dist(ChunkPosition, ViewerPos);
		MinDistance = FMath::Min(MinDistance, Distance);
	}
	
	return MinDistance;
}

void UFluidLODManager::UpdateLODCounts()
{
	for (int32& Count : LODCounts)
	{
		Count = 0;
	}
	
	for (const auto& Pair : ChunkLODStates)
	{
		int32 LODIndex = (int32)Pair.Value.CurrentLOD;
		if (LODCounts.IsValidIndex(LODIndex))
		{
			LODCounts[LODIndex]++;
		}
	}
}