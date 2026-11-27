#include "Map/PuffersMapBuildService.h"

#include "Map/PuffersMapData.h"
#include "Map/PuffersMapReplicator.h"
#include "Map/PuffersMapSubsystem.h"
#include "Career/PuffersCareerSubsystem.h"

#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TimerManager.h"

namespace
{
	FString JsonToString(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj, Writer);
		return Out;
	}

	uint32 Hash32(const FString& S, uint32 Salt)
	{
		// FNV-1a: stable across platforms and runs (GetTypeHash on FName is not).
		uint32 H = 2166136261u ^ Salt;
		for (TCHAR C : S)
		{
			H ^= (uint32)FChar::ToLower(C);
			H *= 16777619u;
		}
		return H;
	}

	FString LocalSavePath()
	{
		return FPaths::ProjectSavedDir() / TEXT("PuffersMap") / TEXT("Builds.json");
	}

	int64 NowUnix()
	{
		return FDateTime::UtcNow().ToUnixTimestamp();
	}
}

// ------------------------------------------------------------------------------------------------ Lifecycle

bool UPuffersMapBuildService::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UPuffersMapBuildService::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPuffersMapBuildService, STATGROUP_Tickables);
}

bool UPuffersMapBuildService::HasAuthority() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

UPuffersMapSubsystem* UPuffersMapBuildService::GetMap() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UPuffersMapSubsystem>() : nullptr;
}

void UPuffersMapBuildService::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!HasAuthority())
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.Name = TEXT("PuffersMapReplicator");
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Replicator = InWorld.SpawnActor<APuffersMapReplicator>(APuffersMapReplicator::StaticClass(), FTransform::Identity, Params);

	PostLoginHandle = FGameModeEvents::GameModePostLoginEvent.AddUObject(this, &UPuffersMapBuildService::OnPostLogin);
	for (FConstPlayerControllerIterator It = InWorld.GetPlayerControllerIterator(); It; ++It)
	{
		AttachNetComponent(It->Get());
	}

	if (UsesBackend())
	{
		NextPoll = 0.0;
		UE_LOG(LogPuffersMap, Display, TEXT("Map builds sync with %s"), *BackendUrl());
	}
	else
	{
		LoadLocal();
	}
}

void UPuffersMapBuildService::Deinitialize()
{
	if (PostLoginHandle.IsValid())
	{
		FGameModeEvents::GameModePostLoginEvent.Remove(PostLoginHandle);
	}
	if (bLocalDirty)
	{
		SaveLocal();
	}
	Super::Deinitialize();
}

void UPuffersMapBuildService::OnPostLogin(AGameModeBase* GameMode, APlayerController* PC)
{
	if (GameMode && GameMode->GetWorld() == GetWorld())
	{
		AttachNetComponent(PC);
	}
}

void UPuffersMapBuildService::AttachNetComponent(APlayerController* PC)
{
	if (!PC || PC->FindComponentByClass<UPuffersMapNetComponent>())
	{
		return;
	}
	UPuffersMapNetComponent* Comp = NewObject<UPuffersMapNetComponent>(PC, TEXT("PuffersMapNet"));
	Comp->RegisterComponent();
	Comp->SetIsReplicated(true);
}

void UPuffersMapBuildService::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!HasAuthority())
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (UsesBackend())
	{
		if (!bPushInFlight && PendingPush.Num() > 0 && Now >= NextPush)
		{
			PushBackend();
		}
		if (!bPollInFlight && Now >= NextPoll)
		{
			PollBackend();
		}
	}
	else if (bLocalDirty && Now >= NextLocalSave)
	{
		SaveLocal();
	}
}

// ------------------------------------------------------------------------------------------------ Recording

bool UPuffersMapBuildService::Sanitize(FPuffersMapBuild& Build, FString& OutWhy)
{
	if (Build.BuildId.IsNone())
	{
		OutWhy = TEXT("Build needs an id.");
		return false;
	}
	if (Build.Footprint.Num() < 3 || Build.Footprint.Num() > 64)
	{
		OutWhy = TEXT("Footprint needs 3 to 64 points.");
		return false;
	}
	for (const FVector2D& P : Build.Footprint)
	{
		if (!FMath::IsFinite(P.X) || !FMath::IsFinite(P.Y) || FMath::Abs(P.X) > 100000.0 || FMath::Abs(P.Y) > 100000.0)
		{
			OutWhy = TEXT("Footprint is outside the world.");
			return false;
		}
	}
	// The biggest plot is a 64-cell ranch (64 x 400 m²); allow some slack for odd shapes.
	if (FMath::Abs(PuffersMapGeo::SignedArea(Build.Footprint)) > 40000.0)
	{
		OutWhy = TEXT("Footprint is bigger than any plot.");
		return false;
	}
	Build.Label = Build.Label.Left(64);
	Build.OwnerName = Build.OwnerName.Left(40);
	Build.HeightM = FMath::Clamp(Build.HeightM, 0.f, 400.f);
	return true;
}

bool UPuffersMapBuildService::RecordBuild(FPuffersMapBuild Build, FString& OutWhy)
{
	if (!HasAuthority() || !Replicator)
	{
		OutWhy = TEXT("Builds are recorded on the server.");
		return false;
	}
	if (!Sanitize(Build, OutWhy))
	{
		return false;
	}
	Build.UpdatedAt = NowUnix();
	if (const FPuffersMapBuild* Existing = Replicator->FindBuild(Build.BuildId))
	{
		Build.Seq = Existing->Seq;
	}
	if (!Replicator->Upsert(Build))
	{
		return true; // nothing changed
	}
	if (UsesBackend())
	{
		PendingPush.Add(Build.BuildId, Build);
	}
	else
	{
		bLocalDirty = true;
		NextLocalSave = FPlatformTime::Seconds() + 2.0;
	}
	UE_LOG(LogPuffersMap, Log, TEXT("Build %s (%s, %s) recorded for %s"), *Build.BuildId.ToString(), *Build.Use.ToString(),
		*PuffersMapNames::BuildStateName(Build.State), *Build.OwnerName);
	return true;
}

bool UPuffersMapBuildService::RecordBuildFromActor(AActor* Building, FName BuildId, FName PlotId, FName OwnerId, const FString& OwnerName, FName Use,
	EPuffersMapBuildState State, const FString& Label, FString& OutWhy)
{
	if (!Building)
	{
		OutWhy = TEXT("No building actor.");
		return false;
	}
	FPuffersMapBuild Build;
	Build.BuildId = BuildId.IsNone() ? Building->GetFName() : BuildId;
	Build.PlotId = PlotId;
	Build.OwnerId = OwnerId;
	Build.OwnerName = OwnerName;
	Build.Use = Use;
	Build.State = State;
	Build.Label = Label;
	Build.Footprint = FootprintFromActor(GetMap(), Building);
	FVector Origin, Extent;
	Building->GetActorBounds(true, Origin, Extent);
	Build.HeightM = (float)(Extent.Z * 2.0 / 100.0);
	return RecordBuild(Build, OutWhy);
}

bool UPuffersMapBuildService::RemoveBuild(FName BuildId)
{
	if (!HasAuthority() || !Replicator)
	{
		return false;
	}
	const FPuffersMapBuild* Existing = Replicator->FindBuild(BuildId);
	if (!Existing)
	{
		return false;
	}
	FPuffersMapBuild Copy = *Existing;
	Copy.State = EPuffersMapBuildState::Demolished;
	FString Why;
	return RecordBuild(Copy, Why);
}

// ------------------------------------------------------------------------------------------------ Footprints

TArray<FVector2D> UPuffersMapBuildService::FootprintFromActor(const UPuffersMapSubsystem* Map, const AActor* Actor)
{
	TArray<FVector2D> Out;
	if (!Map || !Actor)
	{
		return Out;
	}
	const FBox Local = Actor->CalculateComponentsBoundingBoxInLocalSpace(true);
	if (Local.IsValid)
	{
		const FTransform& T = Actor->GetActorTransform();
		const FVector Corners[4] = {
			FVector(Local.Min.X, Local.Min.Y, 0), FVector(Local.Max.X, Local.Min.Y, 0),
			FVector(Local.Max.X, Local.Max.Y, 0), FVector(Local.Min.X, Local.Max.Y, 0) };
		for (const FVector& C : Corners)
		{
			Out.Add(Map->WorldToMap(T.TransformPosition(C)));
		}
		return Out;
	}
	FVector Origin, Extent;
	Actor->GetActorBounds(false, Origin, Extent);
	const FVector2D C = Map->WorldToMap(Origin);
	return PuffersMapGeo::BoxFootprint(C, FVector2D(Extent.X, Extent.Y) / 100.0, 0.0);
}

bool UPuffersMapBuildService::FindLotFootprint(FName LotId, TArray<FVector2D>& Out) const
{
	if (LotId.IsNone())
	{
		return false;
	}
	if (const TArray<FVector2D>* Cached = LotCache.Find(LotId))
	{
		Out = *Cached;
		return Out.Num() >= 3;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(LotId) || It->GetFName() == LotId)
		{
			Out = FootprintFromActor(GetMap(), *It);
			LotCache.Add(LotId, Out);
			return Out.Num() >= 3;
		}
	}
	LotCache.Add(LotId, {});
	return false;
}

TArray<FVector2D> UPuffersMapBuildService::ResolvePlotFootprint(FName PlotId, FName LotId, FName CountyId, int32 Cells, int32 BuildingIndex, int32 BuildingCount) const
{
	TArray<FVector2D> Lot;
	if (!FindLotFootprint(LotId, Lot))
	{
		// Stable placement inside the county's lot zones on the 20 m grid.
		const UPuffersMapSubsystem* Map = GetMap();
		if (!Map)
		{
			return {};
		}
		const FPuffersMapDataSet& D = Map->GetData();
		TArray<int32> Zones;
		for (int32 i = 0; i < D.Areas.Num(); ++i)
		{
			if (D.Areas[i].Kind != EPuffersMapAreaKind::LotZone)
			{
				continue;
			}
			const FBox2D& B = D.GetAreaBounds(i);
			const FName County = Map->GetCountyAt(B.GetCenter());
			if (CountyId.IsNone() || County.ToString().Equals(CountyId.ToString(), ESearchCase::IgnoreCase) || D.Areas[i].StyleKey.ToString().Equals(CountyId.ToString(), ESearchCase::IgnoreCase))
			{
				Zones.Add(i);
			}
		}
		if (Zones.Num() == 0)
		{
			for (int32 i = 0; i < D.Areas.Num(); ++i)
			{
				if (D.Areas[i].Kind == EPuffersMapAreaKind::LotZone)
				{
					Zones.Add(i);
				}
			}
		}
		if (Zones.Num() == 0)
		{
			return {};
		}
		const FString Key = PlotId.ToString();
		const int32 Zone = Zones[Hash32(Key, 1) % Zones.Num()];
		const FBox2D B = D.GetAreaBounds(Zone);
		const int32 W = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt((float)FMath::Max(Cells, 1))));
		const int32 H = FMath::Max(1, FMath::CeilToInt((float)FMath::Max(Cells, 1) / W));
		const double Cell = 20.0;
		const int32 Cols = FMath::Max(1, FMath::FloorToInt((B.Max.X - B.Min.X) / Cell) - W);
		const int32 Rows = FMath::Max(1, FMath::FloorToInt((B.Max.Y - B.Min.Y) / Cell) - H);
		for (uint32 Try = 0; Try < 24; ++Try)
		{
			const int32 Cx = (int32)(Hash32(Key, 100 + Try * 2) % (uint32)Cols);
			const int32 Cy = (int32)(Hash32(Key, 101 + Try * 2) % (uint32)Rows);
			const FVector2D Min(FMath::FloorToDouble(B.Min.X / Cell) * Cell + Cx * Cell, FMath::FloorToDouble(B.Min.Y / Cell) * Cell + Cy * Cell);
			const FVector2D Max = Min + FVector2D(W * Cell, H * Cell);
			if (PuffersMapGeo::PointInPolygon((Min + Max) * 0.5, D.Areas[Zone].Polygon))
			{
				Lot = { Min, FVector2D(Max.X, Min.Y), Max, FVector2D(Min.X, Max.Y) };
				break;
			}
		}
		if (Lot.Num() == 0)
		{
			return {};
		}
	}
	if (BuildingCount <= 1 || Lot.Num() != 4)
	{
		return Lot;
	}
	// Several buildings share the lot: split it along its long side, with a small gap.
	const FVector2D A = Lot[0], Bv = Lot[1], C = Lot[2], Dv = Lot[3];
	const bool bAlongAB = FVector2D::Distance(A, Bv) >= FVector2D::Distance(Bv, C);
	const double T0 = (double)BuildingIndex / BuildingCount + 0.02;
	const double T1 = (double)(BuildingIndex + 1) / BuildingCount - 0.02;
	if (bAlongAB)
	{
		return { FMath::Lerp(A, Bv, T0), FMath::Lerp(A, Bv, T1), FMath::Lerp(Dv, C, T1), FMath::Lerp(Dv, C, T0) };
	}
	return { FMath::Lerp(A, Dv, T0), FMath::Lerp(Bv, C, T0), FMath::Lerp(Bv, C, T1), FMath::Lerp(A, Dv, T1) };
}

// ------------------------------------------------------------------------------------------------ JSON

TSharedRef<FJsonObject> UPuffersMapBuildService::BuildToJson(const FPuffersMapBuild& B)
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("buildId"), B.BuildId.ToString());
	O->SetStringField(TEXT("plotId"), B.PlotId.ToString());
	O->SetStringField(TEXT("ownerId"), B.OwnerId.ToString());
	O->SetStringField(TEXT("ownerName"), B.OwnerName);
	O->SetStringField(TEXT("use"), B.Use.ToString());
	O->SetStringField(TEXT("label"), B.Label);
	O->SetStringField(TEXT("state"), PuffersMapNames::BuildStateName(B.State));
	TArray<TSharedPtr<FJsonValue>> Pts;
	for (const FVector2D& P : B.Footprint)
	{
		Pts.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(P.X * 10.0) / 10.0));
		Pts.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(P.Y * 10.0) / 10.0));
	}
	O->SetArrayField(TEXT("footprint"), Pts);
	O->SetNumberField(TEXT("heightM"), B.HeightM);
	O->SetNumberField(TEXT("version"), B.BuildVersion);
	O->SetNumberField(TEXT("seq"), (double)B.Seq);
	O->SetNumberField(TEXT("updatedAt"), (double)B.UpdatedAt);
	return O;
}

bool UPuffersMapBuildService::BuildFromJson(const TSharedPtr<FJsonObject>& O, FPuffersMapBuild& Out)
{
	if (!O.IsValid())
	{
		return false;
	}
	FString S;
	if (!O->TryGetStringField(TEXT("buildId"), S) || S.IsEmpty())
	{
		return false;
	}
	Out = FPuffersMapBuild();
	Out.BuildId = FName(*S);
	if (O->TryGetStringField(TEXT("plotId"), S)) Out.PlotId = FName(*S);
	if (O->TryGetStringField(TEXT("ownerId"), S)) Out.OwnerId = FName(*S);
	O->TryGetStringField(TEXT("ownerName"), Out.OwnerName);
	if (O->TryGetStringField(TEXT("use"), S)) Out.Use = FName(*S);
	O->TryGetStringField(TEXT("label"), Out.Label);
	if (O->TryGetStringField(TEXT("state"), S)) Out.State = PuffersMapNames::ParseBuildState(S);
	const TArray<TSharedPtr<FJsonValue>>* Pts = nullptr;
	if (O->TryGetArrayField(TEXT("footprint"), Pts))
	{
		for (int32 i = 0; i + 1 < Pts->Num(); i += 2)
		{
			Out.Footprint.Add(FVector2D((*Pts)[i]->AsNumber(), (*Pts)[i + 1]->AsNumber()));
		}
	}
	double D = 0;
	if (O->TryGetNumberField(TEXT("heightM"), D)) Out.HeightM = (float)D;
	if (O->TryGetNumberField(TEXT("version"), D)) Out.BuildVersion = (int32)D;
	if (O->TryGetNumberField(TEXT("seq"), D)) Out.Seq = (int64)D;
	if (O->TryGetNumberField(TEXT("updatedAt"), D)) Out.UpdatedAt = (int64)D;
	return true;
}

// ------------------------------------------------------------------------------------------------ Backend

FString UPuffersMapBuildService::BackendUrl() const
{
	const UPuffersMapSubsystem* Map = GetMap();
	FString Url = Map ? Map->GetConfig().BackendUrl : FString();
	Url.RemoveFromEnd(TEXT("/"));
	return Url;
}

bool UPuffersMapBuildService::UsesBackend() const
{
	const UWorld* World = GetWorld();
	if (!World || BackendUrl().IsEmpty())
	{
		return false;
	}
	const ENetMode Mode = World->GetNetMode();
	return Mode == NM_DedicatedServer || Mode == NM_ListenServer;
}

void UPuffersMapBuildService::PushBackend()
{
	TArray<FPuffersMapBuild> Batch;
	PendingPush.GenerateValueArray(Batch);
	PendingPush.Reset();
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FPuffersMapBuild& B : Batch)
	{
		Arr.Add(MakeShared<FJsonValueObject>(BuildToJson(B)));
	}
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(TEXT("builds"), Arr);

	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(BackendUrl() + TEXT("/v1/map/builds"));
	Req->SetVerb(TEXT("POST"));
	Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Req->SetHeader(TEXT("x-puffers-server-key"), FPlatformMisc::GetEnvironmentVariable(TEXT("PUFFERS_SERVER_KEY")));
	Req->SetContentAsString(JsonToString(Body));
	bPushInFlight = true;
	TWeakObjectPtr<UPuffersMapBuildService> WeakThis(this);
	Req->OnProcessRequestComplete().BindLambda([WeakThis, Batch](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		UPuffersMapBuildService* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		Self->bPushInFlight = false;
		if (!bOk || !Resp.IsValid() || Resp->GetResponseCode() >= 300)
		{
			// Keep them and retry in 10 s (newer edits to the same build win).
			for (const FPuffersMapBuild& B : Batch)
			{
				if (!Self->PendingPush.Contains(B.BuildId))
				{
					Self->PendingPush.Add(B.BuildId, B);
				}
			}
			Self->NextPush = FPlatformTime::Seconds() + 10.0;
			UE_LOG(LogPuffersMap, Warning, TEXT("Map build push failed (%d). Retrying."), Resp.IsValid() ? Resp->GetResponseCode() : 0);
			return;
		}
		// The backend answers with the stored rows and their sequence numbers.
		TSharedPtr<FJsonObject> Root;
		const TArray<TSharedPtr<FJsonValue>>* Stored = nullptr;
		if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Resp->GetContentAsString()), Root) && Root.IsValid()
			&& Root->TryGetArrayField(TEXT("builds"), Stored) && Self->Replicator)
		{
			for (const TSharedPtr<FJsonValue>& V : *Stored)
			{
				FPuffersMapBuild B;
				if (BuildFromJson(V->AsObject(), B))
				{
					Self->Replicator->Upsert(B);
				}
			}
		}
	});
	Req->ProcessRequest();
}

void UPuffersMapBuildService::PollBackend()
{
	const UPuffersMapSubsystem* Map = GetMap();
	NextPoll = FPlatformTime::Seconds() + (Map ? FMath::Max(Map->GetConfig().BackendPollSeconds, 1.f) : 5.f);
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(FString::Printf(TEXT("%s/v1/map/builds?since=%lld&limit=2000"), *BackendUrl(), Cursor));
	Req->SetVerb(TEXT("GET"));
	Req->SetHeader(TEXT("x-puffers-server-key"), FPlatformMisc::GetEnvironmentVariable(TEXT("PUFFERS_SERVER_KEY")));
	bPollInFlight = true;
	TWeakObjectPtr<UPuffersMapBuildService> WeakThis(this);
	Req->OnProcessRequestComplete().BindLambda([WeakThis](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		UPuffersMapBuildService* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		Self->bPollInFlight = false;
		if (!bOk || !Resp.IsValid() || Resp->GetResponseCode() >= 300)
		{
			return;
		}
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Resp->GetContentAsString()), Root) || !Root.IsValid())
		{
			return;
		}
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Root->TryGetArrayField(TEXT("builds"), Arr) && Self->Replicator)
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				FPuffersMapBuild B;
				if (BuildFromJson(V->AsObject(), B))
				{
					Self->Cursor = FMath::Max(Self->Cursor, B.Seq);
					Self->Replicator->Upsert(B);
				}
			}
		}
		double Next = 0;
		if (Root->TryGetNumberField(TEXT("cursor"), Next))
		{
			Self->Cursor = FMath::Max(Self->Cursor, (int64)Next);
		}
		bool bMore = false;
		if (Root->TryGetBoolField(TEXT("more"), bMore) && bMore)
		{
			Self->NextPoll = 0.0; // keep paging on the next tick
		}
	});
	Req->ProcessRequest();
}

// ------------------------------------------------------------------------------------------------ Local save (offline)

void UPuffersMapBuildService::LoadLocal()
{
	FString Json;
	if (!Replicator || !FFileHelper::LoadFileToString(Json, *LocalSavePath()))
	{
		return;
	}
	TSharedPtr<FJsonObject> Root;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid() && Root->TryGetArrayField(TEXT("builds"), Arr))
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			FPuffersMapBuild B;
			if (BuildFromJson(V->AsObject(), B))
			{
				Replicator->Upsert(B);
			}
		}
	}
}

void UPuffersMapBuildService::SaveLocal()
{
	bLocalDirty = false;
	if (!Replicator)
	{
		return;
	}
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FPuffersMapBuild& B : Replicator->GetBuilds())
	{
		Arr.Add(MakeShared<FJsonValueObject>(BuildToJson(B)));
	}
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("builds"), Arr);
	FFileHelper::SaveStringToFile(JsonToString(Root), *LocalSavePath());
}

// ------------------------------------------------------------------------------------------------ Net component

UPuffersMapNetComponent::UPuffersMapNetComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

bool UPuffersMapNetComponent::IsOwningClient() const
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	return PC && PC->IsLocalController();
}

void UPuffersMapNetComponent::BeginPlay()
{
	Super::BeginPlay();
	if (IsOwningClient())
	{
		BindCareer();
		// Initial sync once the career save has loaded.
		GetWorld()->GetTimerManager().SetTimer(SyncTimer, this, &UPuffersMapNetComponent::ReportCareerPlots, 30.f, true, 2.f);
	}
}

void UPuffersMapNetComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SyncTimer);
	}
	if (bBound)
	{
		const UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
		if (UPuffersCareerSubsystem* Career = GI ? GI->GetSubsystem<UPuffersCareerSubsystem>() : nullptr)
		{
			Career->OnCareerEvent.RemoveDynamic(this, &UPuffersMapNetComponent::HandleCareerEvent);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void UPuffersMapNetComponent::BindCareer()
{
	const UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	if (UPuffersCareerSubsystem* Career = GI ? GI->GetSubsystem<UPuffersCareerSubsystem>() : nullptr)
	{
		Career->OnCareerEvent.AddUniqueDynamic(this, &UPuffersMapNetComponent::HandleCareerEvent);
		bBound = true;
	}
}

void UPuffersMapNetComponent::HandleCareerEvent(FName EventName, FName Context)
{
	// Any plot-related change (build, sale, lapse, cure) resyncs the player's plots.
	const FString E = EventName.ToString();
	if (E.Contains(TEXT("Plot")) || E.Contains(TEXT("Raid")) || E.Contains(TEXT("Estate")))
	{
		ReportCareerPlots();
	}
}

void UPuffersMapNetComponent::ReportCareerPlots()
{
	const UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UPuffersCareerSubsystem* Career = GI ? GI->GetSubsystem<UPuffersCareerSubsystem>() : nullptr;
	const UPuffersCareerSaveGame* Save = Career ? Career->GetSave() : nullptr;
	const UPuffersMapBuildService* Service = GetWorld() ? GetWorld()->GetSubsystem<UPuffersMapBuildService>() : nullptr;
	if (!Save || !Service)
	{
		return;
	}
	TArray<FPuffersMapBuild> Builds;
	TSet<FName> Now;
	for (const FPuffersPlotOwnership& Plot : Save->Housing.Plots)
	{
		if (Plot.OwnerId != Save->Owner.CareerOwnerId || Plot.State == EPuffersPlotState::Sold)
		{
			continue;
		}
		const FPuffersPlotListing* Listing = Career->GetPlotListing(Plot.PlotId);
		const int32 Count = Plot.Buildings.Num();
		for (int32 i = 0; i < Count; ++i)
		{
			const FPuffersPlotBuilding& Bd = Plot.Buildings[i];
			FPuffersMapBuild B;
			B.BuildId = FName(*FString::Printf(TEXT("%s/%s"), *Plot.PlotId.ToString(), *Bd.BuildingId.ToString()));
			B.PlotId = Plot.PlotId;
			B.Use = Bd.Use;
			B.BuildVersion = Bd.BuildVersion;
			B.State = Plot.State == EPuffersPlotState::ListedAsBuilt ? EPuffersMapBuildState::ForSale
				: (Bd.bCompleted ? EPuffersMapBuildState::Completed : EPuffersMapBuildState::UnderConstruction);
			B.Footprint = Service->ResolvePlotFootprint(Plot.PlotId, Listing ? Listing->BuildZoneLotId : NAME_None,
				Listing ? Listing->CountyId : NAME_None, Listing ? Listing->Cells : 2, i, Count);
			if (B.Footprint.Num() >= 3)
			{
				Builds.Add(B);
				Now.Add(B.BuildId);
			}
		}
	}
	for (const FName& Gone : LastReported)
	{
		if (!Now.Contains(Gone))
		{
			ServerRemoveBuild(Gone);
		}
	}
	LastReported = Now;
	if (Builds.Num() > 0)
	{
		ServerReportBuilds(Builds);
	}
}

void UPuffersMapNetComponent::ServerReportBuilds_Implementation(const TArray<FPuffersMapBuild>& Builds)
{
	UPuffersMapBuildService* Service = GetWorld() ? GetWorld()->GetSubsystem<UPuffersMapBuildService>() : nullptr;
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!Service || !PC || !Service->GetReplicator())
	{
		return;
	}
	const APlayerState* PS = PC->PlayerState;
	FString OwnerKey = TEXT("LocalCareer");
	if (PS && PS->GetUniqueId().IsValid())
	{
		OwnerKey = PS->GetUniqueId().ToString();
	}
	const FString OwnerName = PS ? PS->GetPlayerName() : TEXT("Player");
	int32 Accepted = 0;
	for (FPuffersMapBuild B : Builds)
	{
		if (Accepted >= 64)
		{
			break;
		}
		B.OwnerId = FName(*OwnerKey);
		B.OwnerName = OwnerName;
		B.Seq = 0;
		// Plot buildings must use "<PlotId>/<BuildingId>" ids.
		if (!B.PlotId.IsNone() && !B.BuildId.ToString().StartsWith(B.PlotId.ToString() + TEXT("/")))
		{
			continue;
		}
		// Nobody can overwrite someone else's build.
		if (const FPuffersMapBuild* Existing = Service->GetReplicator()->FindBuild(B.BuildId))
		{
			if (Existing->OwnerId != B.OwnerId && Existing->State != EPuffersMapBuildState::Demolished)
			{
				continue;
			}
		}
		FString Why;
		if (Service->RecordBuild(B, Why))
		{
			++Accepted;
		}
		else
		{
			UE_LOG(LogPuffersMap, Warning, TEXT("Rejected build %s from %s: %s"), *B.BuildId.ToString(), *OwnerName, *Why);
		}
	}
}

void UPuffersMapNetComponent::ServerRemoveBuild_Implementation(FName BuildId)
{
	UPuffersMapBuildService* Service = GetWorld() ? GetWorld()->GetSubsystem<UPuffersMapBuildService>() : nullptr;
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!Service || !PC || !Service->GetReplicator())
	{
		return;
	}
	const FPuffersMapBuild* Existing = Service->GetReplicator()->FindBuild(BuildId);
	const APlayerState* PS = PC->PlayerState;
	const FString OwnerKey = (PS && PS->GetUniqueId().IsValid()) ? PS->GetUniqueId().ToString() : TEXT("LocalCareer");
	if (Existing && Existing->OwnerId == FName(*OwnerKey))
	{
		Service->RemoveBuild(BuildId);
	}
}
