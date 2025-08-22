#include "Optimization/FluidOctree.h"
#include "CellularAutomata/FluidChunk.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

UFluidOctree::UFluidOctree()
{
	WorldCenter = FVector::ZeroVector;
	WorldSize = 100000.0f;
	TotalChunks = 0;
}

void UFluidOctree::Initialize(const FVector& InWorldCenter, float InWorldSize)
{
	FScopeLock Lock(&OctreeMutex);

	WorldCenter = InWorldCenter;
	WorldSize = InWorldSize;
	
	FOctreeNodeBounds RootBounds(WorldCenter, WorldSize * 0.5f);
	RootNode = MakeShareable(new FOctreeNode(RootBounds, 0));
	
	TotalChunks = 0;
	
	UE_LOG(LogTemp, Warning, TEXT("=== OCTREE INITIALIZATION ==="));
	UE_LOG(LogTemp, Warning, TEXT("Fluid Octree initialized:"));
	UE_LOG(LogTemp, Warning, TEXT("  Center: %s"), *WorldCenter.ToString());
	UE_LOG(LogTemp, Warning, TEXT("  Size: %.0f"), WorldSize);
	UE_LOG(LogTemp, Warning, TEXT("  Root Bounds: Min=%s, Max=%s"), 
		*RootBounds.ToBox().Min.ToString(), 
		*RootBounds.ToBox().Max.ToString());
	UE_LOG(LogTemp, Warning, TEXT("  Root Node Valid: %s"), RootNode.IsValid() ? TEXT("YES") : TEXT("NO"));
	UE_LOG(LogTemp, Warning, TEXT("============================="));
}

void UFluidOctree::InsertChunk(UFluidChunk* Chunk)
{
	if (!Chunk)
	{
		UE_LOG(LogTemp, Warning, TEXT("InsertChunk: Chunk is NULL"));
		return;
	}
	
	if (!RootNode.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("InsertChunk: RootNode is not valid"));
		return;
	}
	
	FScopeLock Lock(&OctreeMutex);
	
	FFluidOctreeData Data;
	Data.ChunkCoord = Chunk->ChunkCoord;
	Data.ChunkPtr = Chunk;
	Data.TotalFluidVolume = Chunk->GetTotalFluidVolume();
	Data.ActiveCellCount = Chunk->GetActiveCellCount();
	Data.bIsActive = (Chunk->State == EChunkState::Active);
	Data.LastUpdateTime = Chunk->LastUpdateTime;
	
	bool bInserted = RootNode->Insert(Data);
	if (bInserted)
	{
		TotalChunks++;
		UE_LOG(LogTemp, Verbose, TEXT("Octree: Inserted chunk %s at position %s (Total: %d)"), 
			*Chunk->ChunkCoord.ToString(), 
			*Chunk->ChunkWorldPosition.ToString(),
			TotalChunks);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Octree: Failed to insert chunk %s at position %s"), 
			*Chunk->ChunkCoord.ToString(), 
			*Chunk->ChunkWorldPosition.ToString());
	}
}

void UFluidOctree::RemoveChunk(const FFluidChunkCoord& ChunkCoord)
{
	if (!RootNode.IsValid())
		return;
	
	FScopeLock Lock(&OctreeMutex);
	
	if (RootNode->Remove(ChunkCoord))
	{
		TotalChunks = FMath::Max(0, TotalChunks - 1);
	}
}

void UFluidOctree::UpdateChunk(UFluidChunk* Chunk)
{
	if (!Chunk || !RootNode.IsValid())
		return;
	
	FScopeLock Lock(&OctreeMutex);
	
	RootNode->Remove(Chunk->ChunkCoord);
	
	FFluidOctreeData Data;
	Data.ChunkCoord = Chunk->ChunkCoord;
	Data.ChunkPtr = Chunk;
	Data.TotalFluidVolume = Chunk->GetTotalFluidVolume();
	Data.ActiveCellCount = Chunk->GetActiveCellCount();
	Data.bIsActive = (Chunk->State == EChunkState::Active);
	Data.LastUpdateTime = Chunk->LastUpdateTime;
	
	RootNode->Insert(Data);
}

TArray<UFluidChunk*> UFluidOctree::QueryChunksInBounds(const FBox& Bounds)
{
	TArray<UFluidChunk*> Result;
	
	if (!RootNode.IsValid())
		return Result;
	
	FScopeLock Lock(&OctreeMutex);
	
	FVector Center = Bounds.GetCenter();
	FVector Extent = Bounds.GetExtent();
	float HalfSize = FMath::Max3(Extent.X, Extent.Y, Extent.Z);
	
	FOctreeNodeBounds QueryBounds(Center, HalfSize);
	
	TArray<FFluidOctreeData> OctreeData;
	RootNode->Query(QueryBounds, OctreeData);
	
	Result.Reserve(OctreeData.Num());
	for (const FFluidOctreeData& Data : OctreeData)
	{
		if (Data.ChunkPtr.IsValid())
		{
			UFluidChunk* Chunk = Data.ChunkPtr.Get();
			if (Chunk && Bounds.Intersect(Chunk->GetWorldBounds()))
			{
				Result.Add(Chunk);
			}
		}
	}
	
	return Result;
}

TArray<UFluidChunk*> UFluidOctree::QueryChunksInRadius(const FVector& Center, float Radius)
{
	TArray<UFluidChunk*> Result;
	
	if (!RootNode.IsValid())
		return Result;
	
	FScopeLock Lock(&OctreeMutex);
	
	TArray<FFluidOctreeData> OctreeData;
	RootNode->QuerySphere(Center, Radius, OctreeData);
	
	Result.Reserve(OctreeData.Num());
	for (const FFluidOctreeData& Data : OctreeData)
	{
		if (Data.ChunkPtr.IsValid())
		{
			Result.Add(Data.ChunkPtr.Get());
		}
	}
	
	return Result;
}

TArray<UFluidChunk*> UFluidOctree::GetNearbyActiveChunks(const FVector& Position, float SearchRadius)
{
	TArray<UFluidChunk*> Result;
	
	if (!RootNode.IsValid())
		return Result;
	
	FScopeLock Lock(&OctreeMutex);
	
	TArray<FFluidOctreeData> OctreeData;
	RootNode->QuerySphere(Position, SearchRadius, OctreeData);
	
	Result.Reserve(OctreeData.Num());
	for (const FFluidOctreeData& Data : OctreeData)
	{
		if (Data.ChunkPtr.IsValid() && Data.bIsActive)
		{
			Result.Add(Data.ChunkPtr.Get());
		}
	}
	
	Result.Sort([Position](const UFluidChunk& A, const UFluidChunk& B)
	{
		float DistA = FVector::DistSquared(A.ChunkWorldPosition, Position);
		float DistB = FVector::DistSquared(B.ChunkWorldPosition, Position);
		return DistA < DistB;
	});
	
	return Result;
}

UFluidChunk* UFluidOctree::FindNearestChunk(const FVector& Position)
{
	if (!RootNode.IsValid())
		return nullptr;
	
	FScopeLock Lock(&OctreeMutex);
	
	float SearchRadius = 1000.0f;
	const float MaxSearchRadius = WorldSize;
	
	while (SearchRadius <= MaxSearchRadius)
	{
		TArray<FFluidOctreeData> OctreeData;
		RootNode->QuerySphere(Position, SearchRadius, OctreeData);
		
		if (OctreeData.Num() > 0)
		{
			UFluidChunk* NearestChunk = nullptr;
			float NearestDistSq = FLT_MAX;
			
			for (const FFluidOctreeData& Data : OctreeData)
			{
				if (Data.ChunkPtr.IsValid())
				{
					float DistSq = FVector::DistSquared(Data.ChunkPtr->ChunkWorldPosition, Position);
					if (DistSq < NearestDistSq)
					{
						NearestDistSq = DistSq;
						NearestChunk = Data.ChunkPtr.Get();
					}
				}
			}
			
			if (NearestChunk)
				return NearestChunk;
		}
		
		SearchRadius *= 2.0f;
	}
	
	return nullptr;
}

void UFluidOctree::OptimizeTree()
{
	if (!RootNode.IsValid())
		return;
	
	FScopeLock Lock(&OctreeMutex);
	
	RootNode->OptimizeNode();
	
	TotalChunks = RootNode->GetTotalDataCount();
}

void UFluidOctree::Clear()
{
	FScopeLock Lock(&OctreeMutex);
	
	if (RootNode.IsValid())
	{
		RootNode->Clear();
	}
	
	TotalChunks = 0;
}

int32 UFluidOctree::GetNodeCount() const
{
	if (!RootNode.IsValid())
		return 0;
	
	FScopeLock Lock(&OctreeMutex);
	return RootNode->GetTotalNodeCount();
}

int32 UFluidOctree::GetChunkCount() const
{
	return TotalChunks;
}

FString UFluidOctree::GetDebugStats() const
{
	if (!RootNode.IsValid())
		return TEXT("Octree not initialized");
	
	FScopeLock Lock(&OctreeMutex);
	
	int32 NodeCount = RootNode->GetTotalNodeCount();
	int32 DataCount = RootNode->GetTotalDataCount();
	
	return FString::Printf(TEXT("Octree Stats: Nodes=%d, Chunks=%d, Avg Chunks/Node=%.2f"),
		NodeCount, DataCount, NodeCount > 0 ? (float)DataCount / NodeCount : 0.0f);
}

void UFluidOctree::DrawDebugOctree(UWorld* World, const FVector& ViewerPosition, float MaxDrawDistance) const
{
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("DrawDebugOctree: World is NULL"));
		return;
	}
	
	if (!RootNode.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("DrawDebugOctree: RootNode is not valid"));
		return;
	}
	
	FScopeLock Lock(&OctreeMutex);
	
	int32 NodeCount = GetNodeCount();
	int32 ChunkCount = GetChunkCount();
	
	UE_LOG(LogTemp, Verbose, TEXT("DrawDebugOctree: Drawing %d nodes, %d chunks at viewer pos %s"), 
		NodeCount, ChunkCount, *ViewerPosition.ToString());
	
	// Draw debug indicators at viewer position so we know where we are (persistent for 2 seconds)
	DrawDebugSphere(World, ViewerPosition, 50.0f, 8, FColor::Blue, false, 2.0f, 0, 3.0f);
	DrawDebugString(World, ViewerPosition + FVector(0, 0, 100), TEXT("VIEWER"), nullptr, FColor::White, 2.0f, true, 2.0f);
	
	// Draw a debug message at viewer position
	FString DebugText = FString::Printf(TEXT("Octree: %d nodes, %d chunks"), NodeCount, ChunkCount);
	DrawDebugString(World, ViewerPosition + FVector(0, 0, 200), 
		DebugText, nullptr, FColor::Yellow, 2.0f, true, 1.5f);
	
	// Draw root node bounds in bright magenta to make it very visible (persistent for 2 seconds)
	FVector RootCenter = RootNode->Bounds.Center;
	FVector RootExtent = FVector(RootNode->Bounds.HalfSize);
	DrawDebugBox(World, RootCenter, RootExtent, FColor::Magenta, false, 2.0f, 0, 5.0f);
	
	// Draw connection line from viewer to root center
	DrawDebugLine(World, ViewerPosition, RootCenter, FColor::Cyan, false, 2.0f, 0, 2.0f);
	DrawDebugString(World, RootCenter + FVector(0, 0, 100), TEXT("OCTREE ROOT"), nullptr, FColor::Magenta, 2.0f, true, 2.0f);
	
	UE_LOG(LogTemp, Warning, TEXT("Drawing root box at %s with extent %s"), *RootCenter.ToString(), *RootExtent.ToString());
	UE_LOG(LogTemp, Warning, TEXT("Distance from viewer to root: %.0f"), FVector::Dist(ViewerPosition, RootCenter));
	
	DrawDebugNode(World, RootNode.Get(), ViewerPosition, MaxDrawDistance);
	
	UE_LOG(LogTemp, Verbose, TEXT("DrawDebugOctree: Completed drawing octree"));
}

void UFluidOctree::DrawDebugNode(UWorld* World, const FOctreeNode* Node, const FVector& ViewerPosition, float MaxDrawDistance) const
{
	if (!Node)
		return;
	
	float DistToViewer = FVector::Dist(Node->Bounds.Center, ViewerPosition);
	if (DistToViewer > MaxDrawDistance)
		return;
	
	// Choose color based on node state
	FColor Color;
	float Thickness = 1.0f;
	
	if (Node->bIsLeaf)
	{
		if (Node->Data.Num() > 0)
		{
			// Leaf with data - show in red/orange based on data count
			int32 DataCount = Node->Data.Num();
			if (DataCount > 4)
			{
				Color = FColor::Red; // Many chunks
				Thickness = 3.0f;
			}
			else if (DataCount > 2)
			{
				Color = FColor::Orange; // Some chunks
				Thickness = 2.0f;
			}
			else
			{
				Color = FColor::Yellow; // Few chunks
				Thickness = 1.5f;
			}
			
			// Draw chunk count text at node center
			FString ChunkText = FString::Printf(TEXT("%d"), DataCount);
			DrawDebugString(World, Node->Bounds.Center, ChunkText, nullptr, Color, 2.0f, true, 1.0f);
		}
		else
		{
			// Empty leaf - show in green with thin lines
			Color = FColor::Green;
			Thickness = 0.5f;
		}
	}
	else
	{
		// Non-leaf node - color by depth for better visualization
		switch (Node->Depth)
		{
		case 0:
			Color = FColor::Magenta; // Root
			Thickness = 3.0f;
			break;
		case 1:
			Color = FColor::Cyan; // First subdivision
			Thickness = 2.5f;
			break;
		case 2:
			Color = FColor::Blue; // Second subdivision
			Thickness = 2.0f;
			break;
		case 3:
			Color = FColor::Purple; // Third subdivision
			Thickness = 1.8f;
			break;
		default:
			Color = FColor::White; // Deeper subdivisions
			Thickness = 1.5f;
			break;
		}
		
		// Add depth text to non-leaf nodes
		FString DepthText = FString::Printf(TEXT("D%d"), Node->Depth);
		DrawDebugString(World, Node->Bounds.Center + FVector(0, 0, 50), DepthText, nullptr, Color, 2.0f, true, 0.8f);
	}
	
	// Adjust alpha based on distance
	float Alpha = FMath::Clamp(1.0f - (DistToViewer / MaxDrawDistance), 0.3f, 1.0f);
	Color.A = (uint8)(Alpha * 255);
	
	// Draw the box with persistent lines (lifetime = 2 seconds)
	DrawDebugBox(World, Node->Bounds.Center, FVector(Node->Bounds.HalfSize), Color, false, 2.0f, 0, Thickness);
	
	// Draw corners for better visibility
	FVector Extent = FVector(Node->Bounds.HalfSize);
	for (int32 i = 0; i < 8; i++)
	{
		FVector Corner = Node->Bounds.Center;
		Corner.X += (i & 1) ? Extent.X : -Extent.X;
		Corner.Y += (i & 2) ? Extent.Y : -Extent.Y;
		Corner.Z += (i & 4) ? Extent.Z : -Extent.Z;
		
		DrawDebugPoint(World, Corner, 5.0f, Color, false, 2.0f);
	}
	
	// Recursively draw children
	if (!Node->bIsLeaf)
	{
		for (const auto& Child : Node->Children)
		{
			if (Child.IsValid())
			{
				DrawDebugNode(World, Child.Get(), ViewerPosition, MaxDrawDistance);
			}
		}
	}
}