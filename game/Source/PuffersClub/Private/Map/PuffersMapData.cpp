#include "Map/PuffersMapData.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Algo/Reverse.h"

DEFINE_LOG_CATEGORY(LogPuffersMap);

// ------------------------------------------------------------------------------------------------ Geometry

namespace PuffersMapGeo
{
	static double SegDistSq(const FVector2D& P, const FVector2D& A, const FVector2D& B, FVector2D* OutClosest)
	{
		const FVector2D AB = B - A;
		const double Len2 = AB.SizeSquared();
		double T = Len2 > UE_SMALL_NUMBER ? FVector2D::DotProduct(P - A, AB) / Len2 : 0.0;
		T = FMath::Clamp(T, 0.0, 1.0);
		const FVector2D C = A + AB * T;
		if (OutClosest)
		{
			*OutClosest = C;
		}
		return FVector2D::DistSquared(P, C);
	}

	static void SimplifyRange(const TArray<FVector>& Pts, int32 First, int32 Last, double TolSq, TArray<bool>& Keep)
	{
		if (Last <= First + 1)
		{
			return;
		}
		const FVector2D A(Pts[First]);
		const FVector2D B(Pts[Last]);
		double MaxD = -1.0;
		int32 MaxI = INDEX_NONE;
		for (int32 i = First + 1; i < Last; ++i)
		{
			const double D = SegDistSq(FVector2D(Pts[i]), A, B, nullptr);
			if (D > MaxD)
			{
				MaxD = D;
				MaxI = i;
			}
		}
		if (MaxI != INDEX_NONE && MaxD > TolSq)
		{
			Keep[MaxI] = true;
			SimplifyRange(Pts, First, MaxI, TolSq, Keep);
			SimplifyRange(Pts, MaxI, Last, TolSq, Keep);
		}
	}

	TArray<FVector> Simplify(const TArray<FVector>& Points, double Tolerance)
	{
		if (Points.Num() <= 2 || Tolerance <= 0.0)
		{
			return Points;
		}
		TArray<bool> Keep;
		Keep.Init(false, Points.Num());
		Keep[0] = true;
		Keep.Last() = true;
		SimplifyRange(Points, 0, Points.Num() - 1, Tolerance * Tolerance, Keep);
		TArray<FVector> Out;
		for (int32 i = 0; i < Points.Num(); ++i)
		{
			if (Keep[i])
			{
				Out.Add(Points[i]);
			}
		}
		return Out;
	}

	double SignedArea(const TArray<FVector2D>& Ring)
	{
		double A = 0.0;
		for (int32 i = 0, j = Ring.Num() - 1; i < Ring.Num(); j = i++)
		{
			A += (Ring[j].X * Ring[i].Y) - (Ring[i].X * Ring[j].Y);
		}
		return A * 0.5;
	}

	static bool PointInTri(const FVector2D& P, const FVector2D& A, const FVector2D& B, const FVector2D& C)
	{
		const double D1 = FVector2D::CrossProduct(B - A, P - A);
		const double D2 = FVector2D::CrossProduct(C - B, P - B);
		const double D3 = FVector2D::CrossProduct(A - C, P - C);
		const bool bNeg = (D1 < 0) || (D2 < 0) || (D3 < 0);
		const bool bPos = (D1 > 0) || (D2 > 0) || (D3 > 0);
		return !(bNeg && bPos);
	}

	bool Triangulate(const TArray<FVector2D>& Ring, TArray<int32>& OutTriangles)
	{
		OutTriangles.Reset();
		const int32 N = Ring.Num();
		if (N < 3)
		{
			return false;
		}
		TArray<int32> V;
		V.Reserve(N);
		const bool bCCW = SignedArea(Ring) > 0.0;
		for (int32 i = 0; i < N; ++i)
		{
			V.Add(bCCW ? i : N - 1 - i);
		}
		int32 Guard = 0;
		while (V.Num() > 3 && Guard < N * N)
		{
			++Guard;
			bool bClipped = false;
			for (int32 i = 0; i < V.Num(); ++i)
			{
				const int32 I0 = V[(i + V.Num() - 1) % V.Num()];
				const int32 I1 = V[i];
				const int32 I2 = V[(i + 1) % V.Num()];
				const FVector2D& A = Ring[I0];
				const FVector2D& B = Ring[I1];
				const FVector2D& C = Ring[I2];
				if (FVector2D::CrossProduct(B - A, C - B) <= 0.0)
				{
					continue; // reflex
				}
				bool bEmpty = true;
				for (int32 k = 0; k < V.Num() && bEmpty; ++k)
				{
					const int32 Ik = V[k];
					if (Ik == I0 || Ik == I1 || Ik == I2)
					{
						continue;
					}
					bEmpty = !PointInTri(Ring[Ik], A, B, C);
				}
				if (!bEmpty)
				{
					continue;
				}
				OutTriangles.Append({ I0, I1, I2 });
				V.RemoveAt(i);
				bClipped = true;
				break;
			}
			if (!bClipped)
			{
				break;
			}
		}
		if (V.Num() == 3)
		{
			OutTriangles.Append({ V[0], V[1], V[2] });
			return true;
		}
		// Degenerate or self-intersecting: fan the rest so something still draws.
		for (int32 i = 1; i + 1 < V.Num(); ++i)
		{
			OutTriangles.Append({ V[0], V[i], V[i + 1] });
		}
		return false;
	}

	bool PointInPolygon(const FVector2D& P, const TArray<FVector2D>& Ring)
	{
		bool bIn = false;
		for (int32 i = 0, j = Ring.Num() - 1; i < Ring.Num(); j = i++)
		{
			const FVector2D& A = Ring[i];
			const FVector2D& B = Ring[j];
			if (((A.Y > P.Y) != (B.Y > P.Y)) && (P.X < (B.X - A.X) * (P.Y - A.Y) / (B.Y - A.Y) + A.X))
			{
				bIn = !bIn;
			}
		}
		return bIn;
	}

	double PolylineLength(const TArray<FVector>& Points)
	{
		double L = 0.0;
		for (int32 i = 1; i < Points.Num(); ++i)
		{
			L += FVector2D::Distance(FVector2D(Points[i - 1]), FVector2D(Points[i]));
		}
		return L;
	}

	double DistanceToPolyline(const FVector2D& P, const TArray<FVector>& Points, FVector2D* OutClosest)
	{
		double Best = TNumericLimits<double>::Max();
		for (int32 i = 1; i < Points.Num(); ++i)
		{
			FVector2D C;
			const double D = SegDistSq(P, FVector2D(Points[i - 1]), FVector2D(Points[i]), &C);
			if (D < Best)
			{
				Best = D;
				if (OutClosest)
				{
					*OutClosest = C;
				}
			}
		}
		if (Points.Num() == 1)
		{
			Best = FVector2D::DistSquared(P, FVector2D(Points[0]));
			if (OutClosest)
			{
				*OutClosest = FVector2D(Points[0]);
			}
		}
		return FMath::Sqrt(Best);
	}

	TArray<FVector2D> BoxFootprint(const FVector2D& Center, const FVector2D& HalfExtents, double YawDeg)
	{
		const double R = FMath::DegreesToRadians(YawDeg);
		const double C = FMath::Cos(R);
		const double S = FMath::Sin(R);
		auto Rot = [&](double X, double Y) { return Center + FVector2D(X * C - Y * S, X * S + Y * C); };
		return { Rot(-HalfExtents.X, -HalfExtents.Y), Rot(HalfExtents.X, -HalfExtents.Y), Rot(HalfExtents.X, HalfExtents.Y), Rot(-HalfExtents.X, HalfExtents.Y) };
	}

	TArray<FVector2D> ConvexHull(TArray<FVector2D> P)
	{
		if (P.Num() < 3)
		{
			return P;
		}
		P.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X || (A.X == B.X && A.Y < B.Y); });
		TArray<FVector2D> H;
		H.SetNum(P.Num() * 2);
		int32 K = 0;
		for (int32 i = 0; i < P.Num(); ++i)
		{
			while (K >= 2 && FVector2D::CrossProduct(H[K - 1] - H[K - 2], P[i] - H[K - 2]) <= 0)
			{
				--K;
			}
			H[K++] = P[i];
		}
		for (int32 i = P.Num() - 2, T = K + 1; i >= 0; --i)
		{
			while (K >= T && FVector2D::CrossProduct(H[K - 1] - H[K - 2], P[i] - H[K - 2]) <= 0)
			{
				--K;
			}
			H[K++] = P[i];
		}
		H.SetNum(FMath::Max(K - 1, 0));
		return H;
	}
}

// ------------------------------------------------------------------------------------------------ Names

namespace PuffersMapNames
{
	template<typename E>
	static FString EnumName(E Value)
	{
		const UEnum* Enum = StaticEnum<E>();
		return Enum ? Enum->GetNameStringByValue((int64)Value) : FString();
	}

	template<typename E>
	static E ParseEnum(const FString& Name, E Fallback)
	{
		const UEnum* Enum = StaticEnum<E>();
		if (!Enum)
		{
			return Fallback;
		}
		for (int32 i = 0; i < Enum->NumEnums(); ++i)
		{
			if (Enum->GetNameStringByIndex(i).Equals(Name, ESearchCase::IgnoreCase))
			{
				return (E)Enum->GetValueByIndex(i);
			}
		}
		return Fallback;
	}

	FString RoadClassName(EPuffersRoadClass Class) { return EnumName(Class); }
	EPuffersRoadClass ParseRoadClass(const FString& Name) { return ParseEnum(Name, EPuffersRoadClass::Street); }
	FString AreaKindName(EPuffersMapAreaKind Kind) { return EnumName(Kind); }
	EPuffersMapAreaKind ParseAreaKind(const FString& Name) { return ParseEnum(Name, EPuffersMapAreaKind::Land); }
	FString LabelKindName(EPuffersMapLabelKind Kind) { return EnumName(Kind); }
	EPuffersMapLabelKind ParseLabelKind(const FString& Name) { return ParseEnum(Name, EPuffersMapLabelKind::Place); }
	FString BuildStateName(EPuffersMapBuildState State) { return EnumName(State); }
	EPuffersMapBuildState ParseBuildState(const FString& Name) { return ParseEnum(Name, EPuffersMapBuildState::Completed); }

	uint8 ParseRoadFlags(const TArray<FString>& Names)
	{
		uint8 F = 0;
		for (const FString& N : Names)
		{
			if (N.Equals(TEXT("Bridge"), ESearchCase::IgnoreCase)) F |= EPuffersRoadFlags::Bridge;
			else if (N.Equals(TEXT("Tunnel"), ESearchCase::IgnoreCase)) F |= EPuffersRoadFlags::Tunnel;
			else if (N.Equals(TEXT("OneWay"), ESearchCase::IgnoreCase)) F |= EPuffersRoadFlags::OneWay;
			else if (N.Equals(TEXT("Overpass"), ESearchCase::IgnoreCase)) F |= EPuffersRoadFlags::Overpass;
		}
		return F;
	}

	TArray<FString> RoadFlagNames(uint8 Flags)
	{
		TArray<FString> Out;
		if (Flags & EPuffersRoadFlags::Bridge) Out.Add(TEXT("Bridge"));
		if (Flags & EPuffersRoadFlags::Tunnel) Out.Add(TEXT("Tunnel"));
		if (Flags & EPuffersRoadFlags::OneWay) Out.Add(TEXT("OneWay"));
		if (Flags & EPuffersRoadFlags::Overpass) Out.Add(TEXT("Overpass"));
		return Out;
	}
}

// ------------------------------------------------------------------------------------------------ Pack JSON

namespace
{
	TArray<double> ReadNumbers(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field)
	{
		TArray<double> Out;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Obj->TryGetArrayField(Field, Arr))
		{
			Out.Reserve(Arr->Num());
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				Out.Add(V->AsNumber());
			}
		}
		return Out;
	}

	TArray<FString> ReadStrings(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field)
	{
		TArray<FString> Out;
		Obj->TryGetStringArrayField(Field, Out);
		return Out;
	}

	TSharedPtr<FJsonValue> Num(double V)
	{
		// 0.1 precision keeps packs small (0.1 cm in world space, 0.1 m in map space is fine for drawing).
		return MakeShared<FJsonValueNumber>(FMath::RoundToDouble(V * 10.0) / 10.0);
	}
}

bool FPuffersMapPack::FromJson(const FString& Json, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("Not valid JSON.");
		return false;
	}
	FString Format;
	if (!Root->TryGetStringField(TEXT("format"), Format) || Format != TEXT("puffers-map-pack"))
	{
		OutError = TEXT("Missing \"format\": \"puffers-map-pack\".");
		return false;
	}
	Root->TryGetStringField(TEXT("source"), Source);
	Root->TryGetStringField(TEXT("generatedAt"), GeneratedAt);
	Root->TryGetStringField(TEXT("space"), Space);
	const bool bWorld = Space.Equals(TEXT("world"), ESearchCase::IgnoreCase);

	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Root->TryGetArrayField(TEXT("roads"), Arr))
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			if (!O.IsValid())
			{
				continue;
			}
			FPuffersMapRoad R;
			FString S;
			if (O->TryGetStringField(TEXT("id"), S)) R.Id = FName(*S);
			O->TryGetStringField(TEXT("name"), R.Name);
			if (O->TryGetStringField(TEXT("class"), S)) R.Class = PuffersMapNames::ParseRoadClass(S);
			R.Flags = PuffersMapNames::ParseRoadFlags(ReadStrings(O, TEXT("flags")));
			double W = R.WidthM;
			if (O->TryGetNumberField(TEXT("width"), W)) R.WidthM = (float)W;
			const TArray<double> P = ReadNumbers(O, TEXT("pts"));
			// Map-space roads may be authored as x,y pairs ("dim": 2).
			int32 Dim = 3;
			O->TryGetNumberField(TEXT("dim"), Dim);
			Dim = FMath::Clamp(Dim, 2, 3);
			for (int32 i = 0; i + Dim - 1 < P.Num(); i += Dim)
			{
				R.Points.Add(FVector(P[i], P[i + 1], Dim == 3 ? P[i + 2] : 0.0));
			}
			R.Source = FName(*Source);
			if (R.Points.Num() >= 2)
			{
				Roads.Add(MoveTemp(R));
			}
		}
	}
	if (Root->TryGetArrayField(TEXT("areas"), Arr))
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			if (!O.IsValid())
			{
				continue;
			}
			FPuffersMapArea A;
			FString S;
			if (O->TryGetStringField(TEXT("id"), S)) A.Id = FName(*S);
			O->TryGetStringField(TEXT("name"), A.Name);
			if (O->TryGetStringField(TEXT("kind"), S)) A.Kind = PuffersMapNames::ParseAreaKind(S);
			if (O->TryGetStringField(TEXT("style"), S)) A.StyleKey = FName(*S);
			double H = 0.0;
			if (O->TryGetNumberField(TEXT("height"), H)) A.HeightM = (float)H;
			const TArray<double> P = ReadNumbers(O, TEXT("pts"));
			for (int32 i = 0; i + 1 < P.Num(); i += 2)
			{
				A.Polygon.Add(FVector2D(P[i], P[i + 1]));
			}
			for (double T : ReadNumbers(O, TEXT("tris")))
			{
				A.Triangles.Add((int32)T);
			}
			A.Source = FName(*Source);
			if (A.Polygon.Num() >= 3)
			{
				Areas.Add(MoveTemp(A));
			}
		}
	}
	if (Root->TryGetArrayField(TEXT("labels"), Arr))
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			if (!O.IsValid())
			{
				continue;
			}
			FPuffersMapLabel L;
			FString S;
			O->TryGetStringField(TEXT("text"), L.Text);
			if (O->TryGetStringField(TEXT("kind"), S)) L.Kind = PuffersMapNames::ParseLabelKind(S);
			double X = 0, Y = 0;
			O->TryGetNumberField(TEXT("x"), X);
			O->TryGetNumberField(TEXT("y"), Y);
			L.Position = FVector2D(X, Y);
			if (O->TryGetStringField(TEXT("icon"), S)) L.Icon = FName(*S);
			if (O->TryGetStringField(TEXT("style"), S)) L.StyleKey = FName(*S);
			O->TryGetStringField(TEXT("note"), L.Note);
			double Z = 0;
			if (O->TryGetNumberField(TEXT("minZoom"), Z)) L.MinZoom = (float)Z;
			if (O->TryGetNumberField(TEXT("maxZoom"), Z)) L.MaxZoom = (float)Z;
			if (!L.Text.IsEmpty())
			{
				Labels.Add(MoveTemp(L));
			}
		}
	}
	const TSharedPtr<FJsonObject>* StatsObj = nullptr;
	if (Root->TryGetObjectField(TEXT("stats"), StatsObj))
	{
		for (const auto& Pair : (*StatsObj)->Values)
		{
			Stats.Add(Pair.Key, (int32)Pair.Value->AsNumber());
		}
	}
	(void)bWorld;
	return true;
}

FString FPuffersMapPack::ToJson() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("format"), TEXT("puffers-map-pack"));
	Root->SetNumberField(TEXT("version"), 1);
	Root->SetStringField(TEXT("space"), Space);
	Root->SetStringField(TEXT("source"), Source);
	Root->SetStringField(TEXT("generatedAt"), GeneratedAt);

	TArray<TSharedPtr<FJsonValue>> RoadArr;
	for (const FPuffersMapRoad& R : Roads)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"), R.Id.ToString());
		if (!R.Name.IsEmpty()) O->SetStringField(TEXT("name"), R.Name);
		O->SetStringField(TEXT("class"), PuffersMapNames::RoadClassName(R.Class));
		const TArray<FString> Flags = PuffersMapNames::RoadFlagNames(R.Flags);
		if (Flags.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> FA;
			for (const FString& F : Flags) FA.Add(MakeShared<FJsonValueString>(F));
			O->SetArrayField(TEXT("flags"), FA);
		}
		O->SetNumberField(TEXT("width"), FMath::RoundToDouble(R.WidthM * 10.0) / 10.0);
		TArray<TSharedPtr<FJsonValue>> P;
		P.Reserve(R.Points.Num() * 3);
		for (const FVector& V : R.Points)
		{
			P.Add(Num(V.X)); P.Add(Num(V.Y)); P.Add(Num(V.Z));
		}
		O->SetArrayField(TEXT("pts"), P);
		RoadArr.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("roads"), RoadArr);

	TArray<TSharedPtr<FJsonValue>> AreaArr;
	for (const FPuffersMapArea& A : Areas)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"), A.Id.ToString());
		if (!A.Name.IsEmpty()) O->SetStringField(TEXT("name"), A.Name);
		O->SetStringField(TEXT("kind"), PuffersMapNames::AreaKindName(A.Kind));
		if (!A.StyleKey.IsNone()) O->SetStringField(TEXT("style"), A.StyleKey.ToString());
		if (A.HeightM != 0.f) O->SetNumberField(TEXT("height"), A.HeightM);
		TArray<TSharedPtr<FJsonValue>> P;
		for (const FVector2D& V : A.Polygon)
		{
			P.Add(Num(V.X)); P.Add(Num(V.Y));
		}
		O->SetArrayField(TEXT("pts"), P);
		AreaArr.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("areas"), AreaArr);

	TArray<TSharedPtr<FJsonValue>> LabelArr;
	for (const FPuffersMapLabel& L : Labels)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("text"), L.Text);
		O->SetStringField(TEXT("kind"), PuffersMapNames::LabelKindName(L.Kind));
		O->SetNumberField(TEXT("x"), L.Position.X);
		O->SetNumberField(TEXT("y"), L.Position.Y);
		if (!L.Icon.IsNone()) O->SetStringField(TEXT("icon"), L.Icon.ToString());
		if (!L.StyleKey.IsNone()) O->SetStringField(TEXT("style"), L.StyleKey.ToString());
		if (!L.Note.IsEmpty()) O->SetStringField(TEXT("note"), L.Note);
		if (L.MinZoom > 0.f) O->SetNumberField(TEXT("minZoom"), L.MinZoom);
		LabelArr.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("labels"), LabelArr);

	TSharedRef<FJsonObject> StatsObj = MakeShared<FJsonObject>();
	for (const auto& Pair : Stats)
	{
		StatsObj->SetNumberField(Pair.Key, Pair.Value);
	}
	Root->SetObjectField(TEXT("stats"), StatsObj);

	FString Out;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Root, Writer);
	return Out;
}

// ------------------------------------------------------------------------------------------------ Data set

void FPuffersMapDataSet::Reset()
{
	Roads.Reset();
	Areas.Reset();
	Labels.Reset();
	Sources.Reset();
	RoadBounds.Reset();
	AreaBounds.Reset();
	for (int32 i = 0; i < NumLods; ++i)
	{
		RoadLods[i].Reset();
	}
	RoadGrid.Reset();
	AreaGrid.Reset();
	Nodes.Reset();
	NodeLookup.Reset();
	Bounds = FBox2D(ForceInit);
}

void FPuffersMapDataSet::AddPack(const FPuffersMapPack& Pack)
{
	const FName SourceName(*Pack.Source);
	if (Sources.Contains(Pack.Source))
	{
		Roads.RemoveAll([SourceName](const FPuffersMapRoad& R) { return R.Source == SourceName; });
		Areas.RemoveAll([SourceName](const FPuffersMapArea& A) { return A.Source == SourceName; });
	}
	else
	{
		Sources.Add(Pack.Source);
	}

	const bool bWorld = Pack.Space.Equals(TEXT("world"), ESearchCase::IgnoreCase) && WorldToMap;
	for (FPuffersMapRoad R : Pack.Roads)
	{
		if (bWorld)
		{
			for (FVector& P : R.Points)
			{
				const FVector2D M = WorldToMap(FVector2D(P.X, P.Y));
				P = FVector(M.X, M.Y, P.Z / 100.0);
			}
		}
		R.Source = SourceName;
		Roads.Add(MoveTemp(R));
	}
	for (FPuffersMapArea A : Pack.Areas)
	{
		if (bWorld)
		{
			for (FVector2D& P : A.Polygon)
			{
				P = WorldToMap(P);
			}
			A.HeightM /= 100.f;
		}
		A.Source = SourceName;
		Areas.Add(MoveTemp(A));
	}
	for (FPuffersMapLabel L : Pack.Labels)
	{
		if (bWorld)
		{
			L.Position = WorldToMap(L.Position);
		}
		Labels.Add(MoveTemp(L));
	}
}

void FPuffersMapDataSet::InsertGrid(TMap<FIntPoint, TArray<int32>>& Grid, const FBox2D& Box, int32 Index)
{
	const FIntPoint Min(FMath::FloorToInt(Box.Min.X / GridCellM), FMath::FloorToInt(Box.Min.Y / GridCellM));
	const FIntPoint Max(FMath::FloorToInt(Box.Max.X / GridCellM), FMath::FloorToInt(Box.Max.Y / GridCellM));
	// Huge areas (land, counties) go in one oversized bucket instead of thousands of cells.
	if ((int64)(Max.X - Min.X + 1) * (Max.Y - Min.Y + 1) > 400)
	{
		Grid.FindOrAdd(FIntPoint(MAX_int32, MAX_int32)).Add(Index);
		return;
	}
	for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
	{
		for (int32 X = Min.X; X <= Max.X; ++X)
		{
			Grid.FindOrAdd(FIntPoint(X, Y)).Add(Index);
		}
	}
}

void FPuffersMapDataSet::QueryGrid(const TMap<FIntPoint, TArray<int32>>& Grid, const FBox2D& Box, TArray<int32>& Out, int32 Count)
{
	Out.Reset();
	TBitArray<> Seen(false, Count);
	auto AddAll = [&](const TArray<int32>& Cell)
	{
		for (int32 I : Cell)
		{
			if (I < Count && !Seen[I])
			{
				Seen[I] = true;
				Out.Add(I);
			}
		}
	};
	if (const TArray<int32>* Big = Grid.Find(FIntPoint(MAX_int32, MAX_int32)))
	{
		AddAll(*Big);
	}
	const FIntPoint Min(FMath::FloorToInt(Box.Min.X / GridCellM), FMath::FloorToInt(Box.Min.Y / GridCellM));
	const FIntPoint Max(FMath::FloorToInt(Box.Max.X / GridCellM), FMath::FloorToInt(Box.Max.Y / GridCellM));
	if ((int64)(Max.X - Min.X + 1) * (Max.Y - Min.Y + 1) > Grid.Num())
	{
		// View covers more cells than exist: walk the map instead.
		for (const auto& Pair : Grid)
		{
			if (Pair.Key.X == MAX_int32 || (Pair.Key.X >= Min.X && Pair.Key.X <= Max.X && Pair.Key.Y >= Min.Y && Pair.Key.Y <= Max.Y))
			{
				AddAll(Pair.Value);
			}
		}
		return;
	}
	for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
	{
		for (int32 X = Min.X; X <= Max.X; ++X)
		{
			if (const TArray<int32>* Cell = Grid.Find(FIntPoint(X, Y)))
			{
				AddAll(*Cell);
			}
		}
	}
}

void FPuffersMapDataSet::Finalize()
{
	Bounds = FBox2D(ForceInit);
	RoadBounds.SetNum(Roads.Num());
	for (int32 L = 0; L < NumLods; ++L)
	{
		RoadLods[L].SetNum(Roads.Num());
	}
	RoadGrid.Reset();
	for (int32 i = 0; i < Roads.Num(); ++i)
	{
		FBox2D B(ForceInit);
		for (const FVector& P : Roads[i].Points)
		{
			B += FVector2D(P);
		}
		B = B.ExpandBy(Roads[i].WidthM * 0.5);
		RoadBounds[i] = B;
		Bounds += B;
		RoadLods[0][i] = Roads[i].Points;
		for (int32 L = 1; L < NumLods; ++L)
		{
			RoadLods[L][i] = PuffersMapGeo::Simplify(Roads[i].Points, LodTolerance[L]);
		}
		InsertGrid(RoadGrid, B, i);
	}

	AreaBounds.SetNum(Areas.Num());
	AreaGrid.Reset();
	for (int32 i = 0; i < Areas.Num(); ++i)
	{
		FPuffersMapArea& A = Areas[i];
		// Drop a closing duplicate point.
		if (A.Polygon.Num() > 3 && A.Polygon[0].Equals(A.Polygon.Last(), 0.01))
		{
			A.Polygon.Pop();
		}
		bool bValidTris = A.Triangles.Num() >= 3 && A.Triangles.Num() % 3 == 0;
		for (int32 T : A.Triangles)
		{
			bValidTris &= A.Polygon.IsValidIndex(T);
		}
		if (!bValidTris)
		{
			PuffersMapGeo::Triangulate(A.Polygon, A.Triangles);
		}
		FBox2D B(ForceInit);
		for (const FVector2D& P : A.Polygon)
		{
			B += P;
		}
		AreaBounds[i] = B;
		Bounds += B;
		InsertGrid(AreaGrid, B, i);
	}
	BuildGraph();
}

int32 FPuffersMapDataSet::LodForZoom(double PixelsPerMeter)
{
	// Keep simplification error under ~1 px.
	const double MetersPerPixel = 1.0 / FMath::Max(PixelsPerMeter, 1e-4);
	int32 Lod = 0;
	for (int32 L = 1; L < NumLods; ++L)
	{
		if (LodTolerance[L] <= MetersPerPixel * 1.25)
		{
			Lod = L;
		}
	}
	return Lod;
}

const TArray<FVector>& FPuffersMapDataSet::GetRoadPoints(int32 RoadIndex, int32 Lod) const
{
	const int32 L = FMath::Clamp(Lod, 0, NumLods - 1);
	return RoadLods[L].IsValidIndex(RoadIndex) ? RoadLods[L][RoadIndex] : Roads[RoadIndex].Points;
}

void FPuffersMapDataSet::QueryRoads(const FBox2D& Box, TArray<int32>& Out) const
{
	QueryGrid(RoadGrid, Box, Out, Roads.Num());
	Out.RemoveAll([&](int32 I) { return !RoadBounds[I].Intersect(Box); });
}

void FPuffersMapDataSet::QueryAreas(const FBox2D& Box, TArray<int32>& Out) const
{
	QueryGrid(AreaGrid, Box, Out, Areas.Num());
	Out.RemoveAll([&](int32 I) { return !AreaBounds[I].Intersect(Box); });
	// Stable draw order: big first so small areas sit on top.
	Out.Sort([&](int32 A, int32 B)
	{
		if (Areas[A].Kind != Areas[B].Kind)
		{
			return (uint8)Areas[A].Kind < (uint8)Areas[B].Kind;
		}
		return A < B;
	});
}

int32 FPuffersMapDataSet::FindNearestRoad(const FVector2D& P, double MaxDistM, bool bNamedOnly, double* OutDist) const
{
	TArray<int32> Candidates;
	QueryRoads(FBox2D(P - FVector2D(MaxDistM), P + FVector2D(MaxDistM)), Candidates);
	int32 Best = INDEX_NONE;
	double BestD = MaxDistM;
	for (int32 I : Candidates)
	{
		if (bNamedOnly && Roads[I].Name.IsEmpty())
		{
			continue;
		}
		const double D = PuffersMapGeo::DistanceToPolyline(P, Roads[I].Points) - Roads[I].WidthM * 0.5;
		if (D < BestD)
		{
			BestD = D;
			Best = I;
		}
	}
	if (OutDist)
	{
		*OutDist = BestD;
	}
	return Best;
}

int32 FPuffersMapDataSet::FindAreaAt(const FVector2D& P, EPuffersMapAreaKind Kind) const
{
	TArray<int32> Candidates;
	QueryAreas(FBox2D(P, P), Candidates);
	int32 Best = INDEX_NONE;
	double BestArea = TNumericLimits<double>::Max();
	for (int32 I : Candidates)
	{
		if (Areas[I].Kind != Kind || !AreaBounds[I].IsInside(P) || !PuffersMapGeo::PointInPolygon(P, Areas[I].Polygon))
		{
			continue;
		}
		const double A = FMath::Abs(PuffersMapGeo::SignedArea(Areas[I].Polygon));
		if (A < BestArea)
		{
			BestArea = A;
			Best = I;
		}
	}
	return Best;
}

// ------------------------------------------------------------------------------------------------ Route graph

static bool IsDrivable(EPuffersRoadClass C)
{
	return C == EPuffersRoadClass::Highway || C == EPuffersRoadClass::Arterial || C == EPuffersRoadClass::Street
		|| C == EPuffersRoadClass::Alley || C == EPuffersRoadClass::Trail || C == EPuffersRoadClass::Planned;
}

static float SpeedFor(EPuffersRoadClass C)
{
	switch (C)
	{
	case EPuffersRoadClass::Highway: return 30.f;
	case EPuffersRoadClass::Arterial: return 18.f;
	case EPuffersRoadClass::Planned: return 22.f;
	case EPuffersRoadClass::Street: return 13.f;
	case EPuffersRoadClass::Trail: return 8.f;
	default: return 7.f;
	}
}

int32 FPuffersMapDataSet::NodeFor(const FVector2D& P)
{
	// 2 m snap: ZoneGraph lanes that connect share an end point within a few centimeters.
	const FIntPoint Key(FMath::RoundToInt(P.X / 2.0), FMath::RoundToInt(P.Y / 2.0));
	if (const int32* Found = NodeLookup.Find(Key))
	{
		return *Found;
	}
	const int32 Index = Nodes.AddDefaulted();
	Nodes[Index].Pos = P;
	NodeLookup.Add(Key, Index);
	return Index;
}

void FPuffersMapDataSet::BuildGraph()
{
	Nodes.Reset();
	NodeLookup.Reset();
	for (int32 i = 0; i < Roads.Num(); ++i)
	{
		const FPuffersMapRoad& R = Roads[i];
		if (!IsDrivable(R.Class) || R.Points.Num() < 2)
		{
			continue;
		}
		const int32 A = NodeFor(FVector2D(R.Points[0]));
		const int32 B = NodeFor(FVector2D(R.Points.Last()));
		if (A == B)
		{
			continue;
		}
		const float Len = (float)PuffersMapGeo::PolylineLength(R.Points);
		const float Cost = Len / SpeedFor(R.Class);
		Nodes[A].Edges.Add({ B, i, false, Cost, Len });
		if ((R.Flags & EPuffersRoadFlags::OneWay) == 0)
		{
			Nodes[B].Edges.Add({ A, i, true, Cost, Len });
		}
	}
}

int32 FPuffersMapDataSet::NearestNode(const FVector2D& P, double MaxDistM) const
{
	int32 Best = INDEX_NONE;
	double BestD = MaxDistM * MaxDistM;
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		if (Nodes[i].Edges.Num() == 0)
		{
			continue;
		}
		const double D = FVector2D::DistSquared(P, Nodes[i].Pos);
		if (D < BestD)
		{
			BestD = D;
			Best = i;
		}
	}
	return Best;
}

bool FPuffersMapDataSet::FindRoute(const FVector2D& From, const FVector2D& To, TArray<FVector2D>& OutPath, double& OutLengthM, double SnapM) const
{
	OutPath.Reset();
	OutLengthM = 0.0;
	const int32 Start = NearestNode(From, SnapM);
	const int32 Goal = NearestNode(To, SnapM);
	if (Start == INDEX_NONE || Goal == INDEX_NONE)
	{
		return false;
	}

	struct FOpen
	{
		int32 Node;
		float F;
	};
	auto Pred = [](const FOpen& A, const FOpen& B) { return A.F < B.F; };
	const float MaxSpeed = 30.f;
	TArray<float> G;
	G.Init(TNumericLimits<float>::Max(), Nodes.Num());
	TArray<int32> CameFromEdgeNode;
	CameFromEdgeNode.Init(INDEX_NONE, Nodes.Num());
	TArray<int32> CameFromEdge;
	CameFromEdge.Init(INDEX_NONE, Nodes.Num());
	TArray<FOpen> Open;
	G[Start] = 0.f;
	Open.HeapPush({ Start, (float)(FVector2D::Distance(Nodes[Start].Pos, Nodes[Goal].Pos) / MaxSpeed) }, Pred);
	bool bFound = false;
	int32 Expanded = 0;
	while (Open.Num() > 0 && Expanded < 500000)
	{
		FOpen Cur;
		Open.HeapPop(Cur, Pred, EAllowShrinking::No);
		if (Cur.Node == Goal)
		{
			bFound = true;
			break;
		}
		++Expanded;
		const FNode& N = Nodes[Cur.Node];
		for (int32 e = 0; e < N.Edges.Num(); ++e)
		{
			const FEdge& E = N.Edges[e];
			const float NG = G[Cur.Node] + E.Cost;
			if (NG < G[E.To])
			{
				G[E.To] = NG;
				CameFromEdgeNode[E.To] = Cur.Node;
				CameFromEdge[E.To] = e;
				Open.HeapPush({ E.To, NG + (float)(FVector2D::Distance(Nodes[E.To].Pos, Nodes[Goal].Pos) / MaxSpeed) }, Pred);
			}
		}
	}
	if (!bFound)
	{
		return false;
	}

	// Walk back, then stitch road geometry front to back.
	TArray<const FEdge*> Edges;
	for (int32 N = Goal; N != Start; N = CameFromEdgeNode[N])
	{
		const int32 Prev = CameFromEdgeNode[N];
		if (Prev == INDEX_NONE)
		{
			return false;
		}
		Edges.Add(&Nodes[Prev].Edges[CameFromEdge[N]]);
	}
	Algo::Reverse(Edges);
	OutPath.Add(From);
	for (const FEdge* E : Edges)
	{
		const TArray<FVector>& Pts = Roads[E->Road].Points;
		for (int32 k = 0; k < Pts.Num(); ++k)
		{
			const FVector& P = E->bReverse ? Pts[Pts.Num() - 1 - k] : Pts[k];
			OutPath.Add(FVector2D(P));
		}
		OutLengthM += E->Length;
	}
	OutPath.Add(To);
	return true;
}
