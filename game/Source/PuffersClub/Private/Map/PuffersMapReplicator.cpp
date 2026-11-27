#include "Map/PuffersMapReplicator.h"

#include "Map/PuffersMapSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

void FPuffersMapBuild::PreReplicatedRemove(const FPuffersMapBuildArray& InArraySerializer)
{
	if (InArraySerializer.Owner)
	{
		InArraySerializer.Owner->HandleReplicated(*this, true);
	}
}

void FPuffersMapBuild::PostReplicatedAdd(const FPuffersMapBuildArray& InArraySerializer)
{
	if (InArraySerializer.Owner)
	{
		InArraySerializer.Owner->HandleReplicated(*this, false);
	}
}

void FPuffersMapBuild::PostReplicatedChange(const FPuffersMapBuildArray& InArraySerializer)
{
	if (InArraySerializer.Owner)
	{
		InArraySerializer.Owner->HandleReplicated(*this, false);
	}
}

APuffersMapReplicator::APuffersMapReplicator()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(2.f);
	SetMinNetUpdateFrequency(0.5f);
}

void APuffersMapReplicator::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APuffersMapReplicator, Builds);
}

void APuffersMapReplicator::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	Builds.Owner = this;
}

const FPuffersMapBuild* APuffersMapReplicator::FindBuild(FName BuildId) const
{
	return Builds.Items.FindByPredicate([BuildId](const FPuffersMapBuild& B) { return B.BuildId == BuildId; });
}

bool APuffersMapReplicator::SameContent(const FPuffersMapBuild& A, const FPuffersMapBuild& B)
{
	if (A.BuildId != B.BuildId || A.PlotId != B.PlotId || A.OwnerId != B.OwnerId || A.OwnerName != B.OwnerName || A.Use != B.Use
		|| A.Label != B.Label || A.State != B.State || A.BuildVersion != B.BuildVersion || A.HeightM != B.HeightM || A.Footprint.Num() != B.Footprint.Num())
	{
		return false;
	}
	for (int32 i = 0; i < A.Footprint.Num(); ++i)
	{
		if (!A.Footprint[i].Equals(B.Footprint[i], 0.05))
		{
			return false;
		}
	}
	return true;
}

bool APuffersMapReplicator::Upsert(const FPuffersMapBuild& Build)
{
	if (Build.BuildId.IsNone())
	{
		return false;
	}
	FPuffersMapBuild* Existing = Builds.Items.FindByPredicate([&Build](const FPuffersMapBuild& B) { return B.BuildId == Build.BuildId; });
	if (Existing)
	{
		if (Build.Seq > 0 && Existing->Seq > Build.Seq)
		{
			return false;
		}
		if (SameContent(*Existing, Build))
		{
			// Only the backend sequence changed: keep it, but nothing to show.
			Existing->Seq = FMath::Max(Existing->Seq, Build.Seq);
			return false;
		}
		const int32 RepId = Existing->ReplicationID;
		const int32 RepKey = Existing->ReplicationKey;
		const int32 RecentKey = Existing->MostRecentArrayReplicationKey;
		const int64 Seq = FMath::Max(Existing->Seq, Build.Seq);
		*Existing = Build;
		Existing->ReplicationID = RepId;
		Existing->ReplicationKey = RepKey;
		Existing->MostRecentArrayReplicationKey = RecentKey;
		Existing->Seq = Seq;
		Builds.MarkItemDirty(*Existing);
		NotifyLocal(*Existing, false);
		return true;
	}
	FPuffersMapBuild& Added = Builds.Items.Add_GetRef(Build);
	Builds.MarkItemDirty(Added);
	NotifyLocal(Added, false);
	return true;
}

bool APuffersMapReplicator::Remove(FName BuildId)
{
	const int32 Index = Builds.Items.IndexOfByPredicate([BuildId](const FPuffersMapBuild& B) { return B.BuildId == BuildId; });
	if (Index == INDEX_NONE)
	{
		return false;
	}
	const FPuffersMapBuild Copy = Builds.Items[Index];
	Builds.Items.RemoveAt(Index);
	Builds.MarkArrayDirty();
	NotifyLocal(Copy, true);
	return true;
}

void APuffersMapReplicator::ReplaceAll(const TArray<FPuffersMapBuild>& InBuilds)
{
	for (const FPuffersMapBuild& B : InBuilds)
	{
		Upsert(B);
	}
}

void APuffersMapReplicator::NotifyLocal(const FPuffersMapBuild& Build, bool bRemoved)
{
	// Dedicated servers have no map to update; listen servers and standalone do.
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	HandleReplicated(Build, bRemoved);
}

void APuffersMapReplicator::HandleReplicated(const FPuffersMapBuild& Build, bool bRemoved)
{
	const UWorld* World = GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	if (UPuffersMapSubsystem* Map = GI ? GI->GetSubsystem<UPuffersMapSubsystem>() : nullptr)
	{
		Map->NotifyBuildChanged(Build, bRemoved);
	}
}
