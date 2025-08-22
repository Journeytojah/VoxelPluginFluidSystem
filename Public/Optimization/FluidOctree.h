#pragma once

#include "CoreMinimal.h"
#include "CellularAutomata/FluidChunk.h"
#include "FluidOctree.generated.h"

USTRUCT(BlueprintType)
struct VOXELFLUIDSYSTEM_API FOctreeNodeBounds
{
	GENERATED_BODY()

	UPROPERTY()
	FVector Center;

	UPROPERTY()
	float HalfSize;

	FOctreeNodeBounds()
	{
		Center = FVector::ZeroVector;
		HalfSize = 1000.0f;
	}

	FOctreeNodeBounds(const FVector& InCenter, float InHalfSize)
		: Center(InCenter), HalfSize(InHalfSize)
	{
	}

	bool Contains(const FVector& Point) const
	{
		return FMath::Abs(Point.X - Center.X) <= HalfSize &&
			   FMath::Abs(Point.Y - Center.Y) <= HalfSize &&
			   FMath::Abs(Point.Z - Center.Z) <= HalfSize;
	}

	bool Intersects(const FOctreeNodeBounds& Other) const
	{
		float Distance = FVector::Dist(Center, Other.Center);
		return Distance <= (HalfSize + Other.HalfSize);
	}

	bool IntersectsSphere(const FVector& SphereCenter, float SphereRadius) const
	{
		float Distance = FVector::Dist(Center, SphereCenter);
		return Distance <= (HalfSize * FMath::Sqrt(3.0f) + SphereRadius);
	}

	FBox ToBox() const
	{
		FVector Extent(HalfSize);
		return FBox(Center - Extent, Center + Extent);
	}

	int32 GetChildIndex(const FVector& Point) const
	{
		int32 Index = 0;
		if (Point.X > Center.X) Index |= 1;
		if (Point.Y > Center.Y) Index |= 2;
		if (Point.Z > Center.Z) Index |= 4;
		return Index;
	}

	FOctreeNodeBounds GetChildBounds(int32 ChildIndex) const
	{
		float ChildHalfSize = HalfSize * 0.5f;
		FVector Offset(
			(ChildIndex & 1) ? ChildHalfSize : -ChildHalfSize,
			(ChildIndex & 2) ? ChildHalfSize : -ChildHalfSize,
			(ChildIndex & 4) ? ChildHalfSize : -ChildHalfSize
		);
		return FOctreeNodeBounds(Center + Offset, ChildHalfSize);
	}
};

USTRUCT()
struct FFluidOctreeData
{
	GENERATED_BODY()

	UPROPERTY()
	FFluidChunkCoord ChunkCoord;

	UPROPERTY()
	float TotalFluidVolume = 0.0f;

	UPROPERTY()
	int32 ActiveCellCount = 0;

	UPROPERTY()
	bool bIsActive = false;

	UPROPERTY()
	float LastUpdateTime = 0.0f;

	TWeakObjectPtr<UFluidChunk> ChunkPtr;
};

class FOctreeNode
{
public:
	FOctreeNodeBounds Bounds;
	TArray<TSharedPtr<FOctreeNode>> Children;
	TArray<FFluidOctreeData> Data;
	int32 Depth;
	bool bIsLeaf;

	static constexpr int32 MAX_DEPTH = 8;
	static constexpr int32 MAX_DATA_PER_NODE = 2; // Reduced from 8 to 2 for more subdivisions

	FOctreeNode(const FOctreeNodeBounds& InBounds, int32 InDepth)
		: Bounds(InBounds), Depth(InDepth), bIsLeaf(true)
	{
		Children.Reserve(8);
	}

	void Subdivide()
	{
		if (!bIsLeaf || Depth >= MAX_DEPTH)
			return;

		bIsLeaf = false;
		Children.Empty(8);

		for (int32 i = 0; i < 8; i++)
		{
			FOctreeNodeBounds ChildBounds = Bounds.GetChildBounds(i);
			Children.Add(MakeShareable(new FOctreeNode(ChildBounds, Depth + 1)));
		}

		TArray<FFluidOctreeData> OldData = MoveTemp(Data);
		Data.Empty();

		for (const FFluidOctreeData& Item : OldData)
		{
			if (Item.ChunkPtr.IsValid())
			{
				FVector ChunkCenter = Item.ChunkPtr->ChunkWorldPosition;
				int32 ChildIndex = Bounds.GetChildIndex(ChunkCenter);
				if (Children.IsValidIndex(ChildIndex))
				{
					Children[ChildIndex]->Insert(Item);
				}
			}
		}
	}

	bool Insert(const FFluidOctreeData& NewData)
	{
		if (!NewData.ChunkPtr.IsValid())
			return false;

		FVector ChunkPos = NewData.ChunkPtr->ChunkWorldPosition;
		if (!Bounds.Contains(ChunkPos))
			return false;

		if (bIsLeaf)
		{
			Data.Add(NewData);

			if (Data.Num() > MAX_DATA_PER_NODE && Depth < MAX_DEPTH)
			{
				Subdivide();
			}
			return true;
		}
		else
		{
			int32 ChildIndex = Bounds.GetChildIndex(ChunkPos);
			if (Children.IsValidIndex(ChildIndex) && Children[ChildIndex].IsValid())
			{
				return Children[ChildIndex]->Insert(NewData);
			}
		}

		return false;
	}

	bool Remove(const FFluidChunkCoord& ChunkCoord)
	{
		if (bIsLeaf)
		{
			for (int32 i = Data.Num() - 1; i >= 0; i--)
			{
				if (Data[i].ChunkCoord == ChunkCoord)
				{
					Data.RemoveAt(i);
					return true;
				}
			}
		}
		else
		{
			for (auto& Child : Children)
			{
				if (Child.IsValid() && Child->Remove(ChunkCoord))
				{
					return true;
				}
			}
		}
		return false;
	}

	void Query(const FOctreeNodeBounds& QueryBounds, TArray<FFluidOctreeData>& OutResults) const
	{
		if (!Bounds.Intersects(QueryBounds))
			return;

		if (bIsLeaf)
		{
			for (const FFluidOctreeData& Item : Data)
			{
				if (Item.ChunkPtr.IsValid())
				{
					FVector ChunkPos = Item.ChunkPtr->ChunkWorldPosition;
					if (QueryBounds.Contains(ChunkPos))
					{
						OutResults.Add(Item);
					}
				}
			}
		}
		else
		{
			for (const auto& Child : Children)
			{
				if (Child.IsValid())
				{
					Child->Query(QueryBounds, OutResults);
				}
			}
		}
	}

	void QuerySphere(const FVector& Center, float Radius, TArray<FFluidOctreeData>& OutResults) const
	{
		if (!Bounds.IntersectsSphere(Center, Radius))
			return;

		if (bIsLeaf)
		{
			for (const FFluidOctreeData& Item : Data)
			{
				if (Item.ChunkPtr.IsValid())
				{
					FVector ChunkPos = Item.ChunkPtr->ChunkWorldPosition;
					if (FVector::Dist(ChunkPos, Center) <= Radius)
					{
						OutResults.Add(Item);
					}
				}
			}
		}
		else
		{
			for (const auto& Child : Children)
			{
				if (Child.IsValid())
				{
					Child->QuerySphere(Center, Radius, OutResults);
				}
			}
		}
	}

	void GetAllData(TArray<FFluidOctreeData>& OutResults) const
	{
		if (bIsLeaf)
		{
			OutResults.Append(Data);
		}
		else
		{
			for (const auto& Child : Children)
			{
				if (Child.IsValid())
				{
					Child->GetAllData(OutResults);
				}
			}
		}
	}

	int32 GetTotalNodeCount() const
	{
		int32 Count = 1;
		if (!bIsLeaf)
		{
			for (const auto& Child : Children)
			{
				if (Child.IsValid())
				{
					Count += Child->GetTotalNodeCount();
				}
			}
		}
		return Count;
	}

	int32 GetTotalDataCount() const
	{
		if (bIsLeaf)
		{
			return Data.Num();
		}
		else
		{
			int32 Count = 0;
			for (const auto& Child : Children)
			{
				if (Child.IsValid())
				{
					Count += Child->GetTotalDataCount();
				}
			}
			return Count;
		}
	}

	void Clear()
	{
		Data.Empty();
		Children.Empty();
		bIsLeaf = true;
	}

	void OptimizeNode()
	{
		if (bIsLeaf)
			return;

		int32 TotalChildData = 0;
		for (const auto& Child : Children)
		{
			if (Child.IsValid())
			{
				Child->OptimizeNode();
				TotalChildData += Child->GetTotalDataCount();
			}
		}

		if (TotalChildData <= MAX_DATA_PER_NODE / 2)
		{
			TArray<FFluidOctreeData> AllChildData;
			GetAllData(AllChildData);
			
			Data = MoveTemp(AllChildData);
			Children.Empty();
			bIsLeaf = true;
		}
	}
};

UCLASS(BlueprintType)
class VOXELFLUIDSYSTEM_API UFluidOctree : public UObject
{
	GENERATED_BODY()

public:
	UFluidOctree();

	void Initialize(const FVector& InWorldCenter, float InWorldSize);

	UFUNCTION(BlueprintCallable, Category = "Fluid Octree")
	void InsertChunk(UFluidChunk* Chunk);

	UFUNCTION(BlueprintCallable, Category = "Fluid Octree")
	void RemoveChunk(const FFluidChunkCoord& ChunkCoord);

	UFUNCTION(BlueprintCallable, Category = "Fluid Octree")
	void UpdateChunk(UFluidChunk* Chunk);

	UFUNCTION(BlueprintCallable, Category = "Fluid Octree")
	TArray<UFluidChunk*> QueryChunksInBounds(const FBox& Bounds);

	UFUNCTION(BlueprintCallable, Category = "Fluid Octree")
	TArray<UFluidChunk*> QueryChunksInRadius(const FVector& Center, float Radius);

	UFUNCTION(BlueprintCallable, Category = "Fluid Octree")
	TArray<UFluidChunk*> GetNearbyActiveChunks(const FVector& Position, float SearchRadius);

	UFUNCTION(BlueprintCallable, Category = "Fluid Octree")
	UFluidChunk* FindNearestChunk(const FVector& Position);

	UFUNCTION(BlueprintCallable, Category = "Fluid Octree")
	void OptimizeTree();

	UFUNCTION(BlueprintCallable, Category = "Fluid Octree")
	void Clear();

	UFUNCTION(BlueprintCallable, Category = "Fluid Octree")
	int32 GetNodeCount() const;

	UFUNCTION(BlueprintCallable, Category = "Fluid Octree")
	int32 GetChunkCount() const;

	UFUNCTION(BlueprintCallable, Category = "Fluid Octree")
	FString GetDebugStats() const;

	void DrawDebugOctree(UWorld* World, const FVector& ViewerPosition, float MaxDrawDistance) const;

protected:
	TSharedPtr<FOctreeNode> RootNode;

	UPROPERTY()
	FVector WorldCenter;

	UPROPERTY()
	float WorldSize;

	UPROPERTY()
	int32 TotalChunks;

	mutable FCriticalSection OctreeMutex;

private:
	void DrawDebugNode(UWorld* World, const FOctreeNode* Node, const FVector& ViewerPosition, float MaxDrawDistance) const;
};