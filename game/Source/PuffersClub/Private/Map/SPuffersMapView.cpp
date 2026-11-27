#include "Map/SPuffersMapView.h"

#include "Map/PuffersMapSubsystem.h"
#include "Map/PuffersMapCameraRig.h"
#include "Map/PuffersMapData.h"

#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"

namespace
{
	FORCEINLINE float EaseOut(float T)
	{
		T = FMath::Clamp(T, 0.f, 1.f);
		return 1.f - FMath::Pow(1.f - T, 3.f);
	}

	FORCEINLINE FLinearColor WithAlpha(FLinearColor C, float A)
	{
		C.A *= A;
		return C;
	}

	FORCEINLINE FColor ToColor(const FLinearColor& C)
	{
		return C.ToFColor(true);
	}

	// Splits a screen polyline into dash pieces.
	void Dashes(const TArray<FVector2D>& Pts, float On, float Off, float Offset, TArray<TArray<FVector2D>>& Out)
	{
		Out.Reset();
		const float Period = On + Off;
		if (Pts.Num() < 2 || Period <= 0.f)
		{
			return;
		}
		float Phase = FMath::Fmod(Offset, Period);
		if (Phase < 0.f)
		{
			Phase += Period;
		}
		bool bOn = Phase < On;
		float Left = bOn ? On - Phase : Period - Phase;
		TArray<FVector2D> Cur;
		if (bOn)
		{
			Cur.Add(Pts[0]);
		}
		for (int32 i = 1; i < Pts.Num(); ++i)
		{
			FVector2D A = Pts[i - 1];
			const FVector2D B = Pts[i];
			float SegLen = (float)FVector2D::Distance(A, B);
			while (SegLen > Left)
			{
				const FVector2D M = A + (B - A).GetSafeNormal() * Left;
				if (bOn)
				{
					Cur.Add(M);
					Out.Add(MoveTemp(Cur));
					Cur.Reset();
				}
				else
				{
					Cur.Add(M);
				}
				SegLen -= Left;
				A = M;
				bOn = !bOn;
				Left = bOn ? On : Off;
			}
			Left -= SegLen;
			if (bOn)
			{
				Cur.Add(B);
			}
		}
		if (bOn && Cur.Num() >= 2)
		{
			Out.Add(MoveTemp(Cur));
		}
	}

	const FName FontBold(TEXT("Bold"));
	const FName FontRegular(TEXT("Regular"));
	const FName FontLight(TEXT("Light"));
}

// ------------------------------------------------------------------------------------------------ Projector

bool SPuffersMapView::FProjector::ToScreen(const FVector2D& P, FVector2D& Out, float ZMeters) const
{
	if (!b3D)
	{
		Out = ScreenCenter + (P - Center) * Zoom;
		return true;
	}
	APlayerController* Controller = PC.Get();
	UPuffersMapSubsystem* M = Map.Get();
	if (!Controller || !M)
	{
		return false;
	}
	FVector2D Px;
	if (!Controller->ProjectWorldLocationToScreen(M->MapToWorld(P, ZMeters * 100.f), Px, true))
	{
		return false;
	}
	Out = Px / DpiScale;
	return true;
}

FVector2D SPuffersMapView::FProjector::ToMap(const FVector2D& Screen) const
{
	if (!b3D)
	{
		return Center + (Screen - ScreenCenter) / Zoom;
	}
	APlayerController* Controller = PC.Get();
	UPuffersMapSubsystem* M = Map.Get();
	FVector WorldPos, WorldDir;
	if (!Controller || !M || !Controller->DeprojectScreenPositionToWorld((float)(Screen.X * DpiScale), (float)(Screen.Y * DpiScale), WorldPos, WorldDir))
	{
		return Center;
	}
	const double PlaneZ = FocusZ * 100.0;
	if (FMath::Abs(WorldDir.Z) < 1e-4)
	{
		return Center;
	}
	const double T = (PlaneZ - WorldPos.Z) / WorldDir.Z;
	return M->WorldToMap(WorldPos + WorldDir * FMath::Max(T, 0.0));
}

// ------------------------------------------------------------------------------------------------ Setup

void SPuffersMapView::Construct(const FArguments& InArgs)
{
	Map = InArgs._MapSubsystem;
	Mode = InArgs._Mode;
	SetClipping(EWidgetClipping::ClipToBounds);
	SetCanTick(true);
	if (UPuffersMapSubsystem* M = Map.Get())
	{
		const FPuffersMapConfig& C = M->GetConfig();
		Zoom = TargetZoom = (Mode == EMode::Minimap) ? C.MinimapZoom : C.DefaultZoom;
		float Heading;
		if (!M->GetPlayerMapPose(Center, Heading))
		{
			Center = C.WorldSizeM * 0.5;
			bFollow = false;
		}
		if (Mode == EMode::FullScreen)
		{
			// Opening animation starts a little zoomed in and settles out.
			Zoom = TargetZoom * 1.6;
		}
	}
	Toggles.bLegend = (Mode == EMode::FullScreen);
	OpenedAt = 0.0;
	Time = 0.0;
}

FVector2D SPuffersMapView::ComputeDesiredSize(float) const
{
	return Mode == EMode::Minimap ? FVector2D(300, 200) : FVector2D(800, 600);
}

void SPuffersMapView::PlayClose(FSimpleDelegate OnDone)
{
	ClosingAt = Time;
	OnClosed = OnDone;
}

void SPuffersMapView::ShowToast(const FString& Text)
{
	Toast = Text;
	ToastAt = Time;
}

void SPuffersMapView::FocusOn(const FVector2D& MapPoint, float InZoom)
{
	bFollow = false;
	Center = MapPoint;
	TargetZoom = InZoom;
}

float SPuffersMapView::OpenAlpha() const
{
	if (Mode != EMode::FullScreen)
	{
		return 1.f;
	}
	float A = EaseOut((float)((Time - OpenedAt) / 0.35));
	if (ClosingAt >= 0.0)
	{
		A *= 1.f - EaseOut((float)((Time - ClosingAt) / 0.2));
	}
	return A;
}

void SPuffersMapView::ClampView()
{
	const UPuffersMapSubsystem* M = Map.Get();
	if (!M)
	{
		return;
	}
	const FPuffersMapConfig& C = M->GetConfig();
	TargetZoom = FMath::Clamp(TargetZoom, (double)C.MinZoom, (double)C.MaxZoom);
	const FVector2D Margin(2000.0, 2000.0);
	Center.X = FMath::Clamp(Center.X, -Margin.X, C.WorldSizeM.X + Margin.X);
	Center.Y = FMath::Clamp(Center.Y, -Margin.Y, C.WorldSizeM.Y + Margin.Y);
}

SPuffersMapView::FProjector SPuffersMapView::MakeProjector(const FGeometry& Geometry) const
{
	FProjector P;
	const FVector2D Size = Geometry.GetLocalSize();
	P.ScreenCenter = Size * 0.5;
	P.Center = Center;
	P.Zoom = FMath::Max(Zoom, 1e-4);
	P.DpiScale = FMath::Max((double)Geometry.Scale, 0.01);
	P.Map = Map;
	if (UPuffersMapSubsystem* M = Map.Get())
	{
		P.PC = M->GetLocalPlayerController();
		if (Mode == EMode::FullScreen && M->GetViewMode() == EPuffersMapViewMode::World3D && M->GetCameraRig())
		{
			P.b3D = true;
			P.FocusZ = (float)(M->GetCameraRig()->GetFocus().Z / 100.0);
		}
	}
	return P;
}

// ------------------------------------------------------------------------------------------------ Tick + input

void SPuffersMapView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	const float Dt = FMath::Min(InDeltaTime, 0.1f);
	Time += Dt;
	LastSize = AllottedGeometry.GetLocalSize();
	LastDpi = AllottedGeometry.Scale;
	UPuffersMapSubsystem* M = Map.Get();
	if (!M)
	{
		return;
	}
	if (ClosingAt >= 0.0 && Time - ClosingAt > 0.22)
	{
		FSimpleDelegate Done = OnClosed;
		OnClosed.Unbind();
		ClosingAt = 1e12;
		Done.ExecuteIfBound();
		return;
	}

	// Zoom eases toward its target (exponential, frame-rate independent), anchored under the cursor.
	TargetZoom = FMath::Clamp(TargetZoom * FMath::Pow(2.0, (double)StickZoom * Dt * 1.6), (double)M->GetConfig().MinZoom, (double)M->GetConfig().MaxZoom);
	const double K = 1.0 - FMath::Exp(-Dt * 12.0);
	const double NewZoom = FMath::Exp(FMath::Lerp(FMath::Loge(Zoom), FMath::Loge(TargetZoom), K));
	if (bHasZoomAnchor && !bFollow)
	{
		// Keep the anchored map point under the same screen point.
		Center = ZoomAnchorMap - (ZoomAnchorScreen - LastSize * 0.5) / NewZoom;
		if (FMath::Abs(NewZoom - TargetZoom) / TargetZoom < 0.002)
		{
			bHasZoomAnchor = false;
		}
	}
	Zoom = NewZoom;

	// Pan: drag inertia, keys and stick.
	const FVector2D Input = KeyPan + StickPan;
	if (!Input.IsNearlyZero())
	{
		bFollow = false;
		Center += Input * (900.0 / Zoom) * Dt;
	}
	if (!bDragging && !PanVelocity.IsNearlyZero(0.5))
	{
		Center += PanVelocity * Dt;
		PanVelocity *= FMath::Exp(-Dt * 5.0);
	}

	FVector2D PlayerPos;
	float Heading;
	if (bFollow && M->GetPlayerMapPose(PlayerPos, Heading))
	{
		const double F = Mode == EMode::Minimap ? 1.0 : 1.0 - FMath::Exp(-Dt * 6.0);
		Center = FMath::Lerp(Center, PlayerPos, F);
	}
	ClampView();

	// Drive the 3D camera rig from the same view state.
	if (Mode == EMode::FullScreen && M->GetViewMode() == EPuffersMapViewMode::World3D)
	{
		if (APuffersMapCameraRig* Rig = M->GetCameraRig())
		{
			const double VisibleWidthM = LastSize.X / Zoom;
			Rig->SetTarget(M->MapToWorld(Center, (float)Rig->GetFocus().Z), VisibleWidthM * 100.0);
		}
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

FReply SPuffersMapView::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (Mode == EMode::Minimap)
	{
		return FReply::Unhandled();
	}
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDragging = true;
		bDragMoved = false;
		DragLast = Local;
		PanVelocity = FVector2D::ZeroVector;
		return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	}
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		UPuffersMapSubsystem* M = Map.Get();
		if (M)
		{
			const FProjector P = MakeProjector(MyGeometry);
			const FVector2D At = P.ToMap(Local);
			FVector2D WpScreen;
			if (M->HasWaypoint() && P.ToScreen(M->GetWaypoint(), WpScreen) && FVector2D::Distance(WpScreen, Local) < 18.0)
			{
				M->ClearWaypoint();
				ShowToast(TEXT("Waypoint cleared"));
			}
			else
			{
				M->SetWaypoint(At);
				ShowToast(FString::Printf(TEXT("Waypoint: %s"), *M->DescribeLocation(At)));
			}
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SPuffersMapView::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bDragging && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply SPuffersMapView::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	bHovering = true;
	HoverScreen = Local;
	if (bDragging)
	{
		const FVector2D Delta = Local - DragLast;
		DragLast = Local;
		if (Delta.SizeSquared() > 0.0)
		{
			bDragMoved = true;
			bFollow = false;
			bHasZoomAnchor = false;
			Center -= Delta / Zoom;
			const double Dt = FMath::Max(FSlateApplication::Get().GetDeltaTime(), 1e-3);
			PanVelocity = FMath::Lerp(PanVelocity, -Delta / Zoom / Dt, 0.5);
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SPuffersMapView::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (Mode == EMode::Minimap)
	{
		return FReply::Unhandled();
	}
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FProjector P = MakeProjector(MyGeometry);
	ZoomAnchorScreen = Local;
	ZoomAnchorMap = P.ToMap(Local);
	bHasZoomAnchor = !P.b3D;
	bFollow = false;
	TargetZoom *= FMath::Pow(1.35, (double)MouseEvent.GetWheelDelta());
	ClampView();
	return FReply::Handled();
}

FReply SPuffersMapView::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (Mode == EMode::Minimap)
	{
		return FReply::Unhandled();
	}
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FProjector P = MakeProjector(MyGeometry);
	ZoomAnchorScreen = Local;
	ZoomAnchorMap = P.ToMap(Local);
	bHasZoomAnchor = !P.b3D;
	bFollow = false;
	TargetZoom *= 2.0;
	ClampView();
	return FReply::Handled();
}

void SPuffersMapView::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	bHovering = false;
	SLeafWidget::OnMouseLeave(MouseEvent);
}

FReply SPuffersMapView::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	UPuffersMapSubsystem* M = Map.Get();
	if (!M || Mode == EMode::Minimap)
	{
		return FReply::Unhandled();
	}
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::M || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_Special_Right)
	{
		M->CloseMap();
		return FReply::Handled();
	}
	if (Key == EKeys::F || Key == EKeys::Gamepad_RightThumbstick)
	{
		bFollow = true;
		bHasZoomAnchor = false;
		ShowToast(TEXT("Following you"));
		return FReply::Handled();
	}
	if (Key == EKeys::T || Key == EKeys::Gamepad_FaceButton_Top)
	{
		const bool bTo3D = M->GetViewMode() == EPuffersMapViewMode::Map2D;
		if (M->SetViewMode(bTo3D ? EPuffersMapViewMode::World3D : EPuffersMapViewMode::Map2D))
		{
			ShowToast(bTo3D ? TEXT("3D view") : TEXT("2D map"));
		}
		return FReply::Handled();
	}
	if (Key == EKeys::L)
	{
		Toggles.bLegend = !Toggles.bLegend;
		return FReply::Handled();
	}
	if (Key == EKeys::V && M->HasSatellite())
	{
		Toggles.bSatellite = !Toggles.bSatellite;
		ShowToast(Toggles.bSatellite ? TEXT("Satellite on") : TEXT("Satellite off"));
		return FReply::Handled();
	}
	if (Key == EKeys::R)
	{
		Toggles.bRoadsIn3D = !Toggles.bRoadsIn3D;
		return FReply::Handled();
	}
	if (Key == EKeys::Equals || Key == EKeys::Add || Key == EKeys::Gamepad_RightTrigger)
	{
		bHasZoomAnchor = false;
		TargetZoom *= 1.5;
		ClampView();
		return FReply::Handled();
	}
	if (Key == EKeys::Hyphen || Key == EKeys::Subtract || Key == EKeys::Gamepad_LeftTrigger)
	{
		bHasZoomAnchor = false;
		TargetZoom /= 1.5;
		ClampView();
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Bottom)
	{
		// Gamepad: A sets a waypoint at the center reticle.
		M->SetWaypoint(Center);
		ShowToast(FString::Printf(TEXT("Waypoint: %s"), *M->DescribeLocation(Center)));
		return FReply::Handled();
	}
	const double Step = 140.0 / Zoom;
	if (Key == EKeys::W || Key == EKeys::Up) { Center.Y -= Step; bFollow = false; return FReply::Handled(); }
	if (Key == EKeys::S || Key == EKeys::Down) { Center.Y += Step; bFollow = false; return FReply::Handled(); }
	if (Key == EKeys::A || Key == EKeys::Left) { Center.X -= Step; bFollow = false; return FReply::Handled(); }
	if (Key == EKeys::D || Key == EKeys::Right) { Center.X += Step; bFollow = false; return FReply::Handled(); }
	return FReply::Unhandled();
}

FReply SPuffersMapView::OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent)
{
	if (Mode == EMode::Minimap)
	{
		return FReply::Unhandled();
	}
	const FKey Key = InAnalogInputEvent.GetKey();
	const float V = FMath::Abs(InAnalogInputEvent.GetAnalogValue()) > 0.18f ? InAnalogInputEvent.GetAnalogValue() : 0.f;
	if (Key == EKeys::Gamepad_LeftX) { StickPan.X = V; return FReply::Handled(); }
	if (Key == EKeys::Gamepad_LeftY) { StickPan.Y = -V; return FReply::Handled(); }
	if (Key == EKeys::Gamepad_RightY) { StickZoom = V; return FReply::Handled(); }
	return FReply::Unhandled();
}

// ------------------------------------------------------------------------------------------------ Batch helpers

void SPuffersMapView::AddTri(FBatch& B, const FSlateRenderTransform& T, const FVector2D& A, const FVector2D& Bp, const FVector2D& C, FColor Col)
{
	const SlateIndex Base = (SlateIndex)B.Verts.Num();
	B.Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, FVector2f(A), FVector2f(0.5f, 0.5f), Col));
	B.Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, FVector2f(Bp), FVector2f(0.5f, 0.5f), Col));
	B.Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, FVector2f(C), FVector2f(0.5f, 0.5f), Col));
	B.Indices.Append({ Base, Base + 1, Base + 2 });
}

void SPuffersMapView::AddQuad(FBatch& B, const FSlateRenderTransform& T, const FVector2D& A, const FVector2D& Bp, const FVector2D& C, const FVector2D& D, FColor Col)
{
	const SlateIndex Base = (SlateIndex)B.Verts.Num();
	B.Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, FVector2f(A), FVector2f(0.5f, 0.5f), Col));
	B.Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, FVector2f(Bp), FVector2f(0.5f, 0.5f), Col));
	B.Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, FVector2f(C), FVector2f(0.5f, 0.5f), Col));
	B.Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, FVector2f(D), FVector2f(0.5f, 0.5f), Col));
	B.Indices.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
}

void SPuffersMapView::AddPolyline(FBatch& B, const FSlateRenderTransform& T, const TArray<FVector2D>& Pts, float Width, FLinearColor Col,
	float DashOn, float DashOff, float DashOffset)
{
	if (Pts.Num() < 2 || Col.A <= 0.003f || Width <= 0.f)
	{
		return;
	}
	if (DashOn > 0.f && DashOff > 0.f)
	{
		TArray<TArray<FVector2D>> Pieces;
		Dashes(Pts, DashOn, DashOff, DashOffset, Pieces);
		for (const TArray<FVector2D>& Piece : Pieces)
		{
			AddPolyline(B, T, Piece, Width, Col);
		}
		return;
	}
	const FColor Solid = ToColor(Col);
	const FColor Clear = ToColor(WithAlpha(Col, 0.f));
	// Thin lines get their alpha scaled instead of going sub-pixel, which reads smoother.
	const float Half = FMath::Max(Width, 1.f) * 0.5f;
	const float Feather = 0.9f;
	for (int32 i = 1; i < Pts.Num(); ++i)
	{
		const FVector2D A = Pts[i - 1];
		const FVector2D C = Pts[i];
		const FVector2D Dir = (C - A).GetSafeNormal();
		if (Dir.IsNearlyZero())
		{
			continue;
		}
		const FVector2D N(-Dir.Y, Dir.X);
		const FVector2D In = N * Half;
		const FVector2D Outer = N * (Half + Feather);
		AddQuad(B, T, A - In, C - In, C + In, A + In, Solid);
		// Feathered edges (inner vertex solid, outer clear).
		const SlateIndex Base = (SlateIndex)B.Verts.Num();
		B.Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, FVector2f(A + In), FVector2f(0.5f, 0.5f), Solid));
		B.Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, FVector2f(C + In), FVector2f(0.5f, 0.5f), Solid));
		B.Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, FVector2f(C + Outer), FVector2f(0.5f, 0.5f), Clear));
		B.Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, FVector2f(A + Outer), FVector2f(0.5f, 0.5f), Clear));
		B.Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, FVector2f(A - In), FVector2f(0.5f, 0.5f), Solid));
		B.Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, FVector2f(C - In), FVector2f(0.5f, 0.5f), Solid));
		B.Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, FVector2f(C - Outer), FVector2f(0.5f, 0.5f), Clear));
		B.Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, FVector2f(A - Outer), FVector2f(0.5f, 0.5f), Clear));
		B.Indices.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3, Base + 4, Base + 5, Base + 6, Base + 4, Base + 6, Base + 7 });
		// Round joins on thick lines so corners don't show gaps.
		if (Half > 1.6f && i < Pts.Num() - 1)
		{
			AddCircle(B, T, C, Half, Col, Half > 6.f ? 12 : 6);
		}
	}
}

void SPuffersMapView::AddPolygon(FBatch& B, const FSlateRenderTransform& T, const TArray<FVector2D>& Pts, const TArray<int32>& Tris, FLinearColor Col)
{
	if (Col.A <= 0.003f || Pts.Num() < 3)
	{
		return;
	}
	const FColor C = ToColor(Col);
	const SlateIndex Base = (SlateIndex)B.Verts.Num();
	for (const FVector2D& P : Pts)
	{
		B.Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, FVector2f(P), FVector2f(0.5f, 0.5f), C));
	}
	for (int32 I : Tris)
	{
		B.Indices.Add(Base + (SlateIndex)I);
	}
}

void SPuffersMapView::AddCircle(FBatch& B, const FSlateRenderTransform& T, const FVector2D& C, float Radius, FLinearColor Col, int32 Segments)
{
	if (Col.A <= 0.003f || Radius <= 0.f)
	{
		return;
	}
	const FColor Fill = ToColor(Col);
	const FColor Clear = ToColor(WithAlpha(Col, 0.f));
	const SlateIndex Base = (SlateIndex)B.Verts.Num();
	B.Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, FVector2f(C), FVector2f(0.5f, 0.5f), Fill));
	for (int32 i = 0; i < Segments; ++i)
	{
		const float A = (float)i / Segments * UE_TWO_PI;
		const FVector2D Dir(FMath::Cos(A), FMath::Sin(A));
		B.Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, FVector2f(C + Dir * Radius), FVector2f(0.5f, 0.5f), Fill));
		B.Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, FVector2f(C + Dir * (Radius + 0.9f)), FVector2f(0.5f, 0.5f), Clear));
	}
	for (int32 i = 0; i < Segments; ++i)
	{
		const SlateIndex In0 = Base + 1 + i * 2;
		const SlateIndex Out0 = In0 + 1;
		const SlateIndex In1 = Base + 1 + ((i + 1) % Segments) * 2;
		const SlateIndex Out1 = In1 + 1;
		B.Indices.Append({ Base, In0, In1, In0, Out0, Out1, In0, Out1, In1 });
	}
}

void SPuffersMapView::AddRing(FBatch& B, const FSlateRenderTransform& T, const FVector2D& C, float Radius, float Width, FLinearColor Col, int32 Segments)
{
	TArray<FVector2D> Pts;
	Pts.Reserve(Segments + 1);
	for (int32 i = 0; i <= Segments; ++i)
	{
		const float A = (float)i / Segments * UE_TWO_PI;
		Pts.Add(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius);
	}
	AddPolyline(B, T, Pts, Width, Col);
}

void SPuffersMapView::Flush(FBatch& B, FSlateWindowElementList& Out, int32 Layer) const
{
	if (B.Indices.Num() == 0)
	{
		B.Verts.Reset();
		return;
	}
	static const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*White);
	FSlateDrawElement::MakeCustomVerts(Out, Layer, Handle, B.Verts, B.Indices, nullptr, 0, 0);
	B.Verts.Reset();
	B.Indices.Reset();
}

// ------------------------------------------------------------------------------------------------ Text + panels

FVector2D SPuffersMapView::MeasureText(const FString& Text, int32 Size, const FName& Typeface)
{
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	return Measure->Measure(Text, FCoreStyle::GetDefaultFontStyle(Typeface, Size));
}

void SPuffersMapView::DrawText(const FGeometry& G, FSlateWindowElementList& Out, int32 Layer, const FString& Text, const FVector2D& Pos,
	int32 Size, FLinearColor Color, bool bCentered, bool bShadow, const FName& Typeface) const
{
	if (Color.A <= 0.01f || Text.IsEmpty())
	{
		return;
	}
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(Typeface, Size);
	const FVector2D TextSize = MeasureText(Text, Size, Typeface);
	const FVector2D At = bCentered ? Pos - TextSize * 0.5 : Pos;
	if (bShadow)
	{
		const FLinearColor Shadow(0.f, 0.f, 0.f, 0.8f * Color.A);
		FSlateDrawElement::MakeText(Out, Layer, G.ToPaintGeometry(TextSize, FSlateLayoutTransform(At + FVector2D(1.2, 1.4))), Text, Font, ESlateDrawEffect::None, Shadow);
	}
	FSlateDrawElement::MakeText(Out, Layer + 1, G.ToPaintGeometry(TextSize, FSlateLayoutTransform(At)), Text, Font, ESlateDrawEffect::None, Color);
}

void SPuffersMapView::DrawPanel(const FGeometry& G, FSlateWindowElementList& Out, int32 Layer, const FVector2D& Pos, const FVector2D& Size, FLinearColor Color) const
{
	static FSlateRoundedBoxBrush Brush(FLinearColor::White, 10.f);
	FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)), &Brush, ESlateDrawEffect::None, Color);
}

// ------------------------------------------------------------------------------------------------ Paint

int32 SPuffersMapView::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	UPuffersMapSubsystem* M = Map.Get();
	if (!M)
	{
		return LayerId;
	}
	const float Alpha = OpenAlpha() * InWidgetStyle.GetColorAndOpacityTint().A;
	const FProjector P = MakeProjector(G);
	int32 L = LayerId;

	if (!P.b3D)
	{
		PaintBase(G, P, Out, L++, Alpha);
		if (Toggles.bSatellite && M->HasSatellite())
		{
			PaintSatellite(G, P, Out, L++, Alpha);
		}
		if (Toggles.bAreas)
		{
			PaintAreas(G, P, Out, L++, Alpha);
		}
	}
	else
	{
		// Slight darkening so overlays read against the photoreal world.
		FBatch Dim;
		const FVector2D S = G.GetLocalSize();
		AddQuad(Dim, G.GetAccumulatedRenderTransform(), FVector2D(0, 0), FVector2D(S.X, 0), S, FVector2D(0, S.Y), ToColor(FLinearColor(0, 0, 0, 0.18f * Alpha)));
		Flush(Dim, Out, L++);
	}
	if (Toggles.bRoads && (!P.b3D || Toggles.bRoadsIn3D))
	{
		PaintRoads(G, P, Out, L, Alpha);
		L += 3;
	}
	if (Toggles.bBuilds)
	{
		PaintBuilds(G, P, Out, L, Alpha);
		L += 3;
	}
	PaintRoute(G, P, Out, L++, Alpha);
	if (Toggles.bLabels)
	{
		L = PaintLabels(G, P, Out, L, Alpha);
	}
	if (Toggles.bMarkers)
	{
		PaintMarkers(G, P, Out, L, Alpha);
		L += 3;
	}
	PaintPlayer(G, P, Out, L, Alpha);
	L += 2;
	return PaintChrome(G, P, Out, L, Alpha);
}

void SPuffersMapView::PaintBase(const FGeometry& G, const FProjector& P, FSlateWindowElementList& Out, int32 Layer, float Alpha) const
{
	const FPuffersMapStyle& St = Map->GetStyle();
	const FSlateRenderTransform& T = G.GetAccumulatedRenderTransform();
	const FVector2D S = G.GetLocalSize();
	FBatch B;
	// Sea with a soft radial glow toward the center of the world.
	AddQuad(B, T, FVector2D(0, 0), FVector2D(S.X, 0), S, FVector2D(0, S.Y), ToColor(WithAlpha(St.Sea, Alpha)));
	FVector2D WorldCenter;
	P.ToScreen(Map->GetConfig().WorldSizeM * 0.5, WorldCenter);
	const float GlowR = (float)(Map->GetConfig().WorldSizeM.Size() * 0.55 * P.Zoom);
	const FColor Glow = ToColor(WithAlpha(St.SeaGlow, Alpha));
	const FColor GlowClear = ToColor(WithAlpha(St.SeaGlow, 0.f));
	const int32 Seg = 40;
	const SlateIndex Base = (SlateIndex)B.Verts.Num();
	B.Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, FVector2f(WorldCenter), FVector2f(0.5f, 0.5f), Glow));
	for (int32 i = 0; i <= Seg; ++i)
	{
		const float A = (float)i / Seg * UE_TWO_PI;
		B.Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, FVector2f(WorldCenter + FVector2D(FMath::Cos(A), FMath::Sin(A)) * GlowR), FVector2f(0.5f, 0.5f), GlowClear));
	}
	for (int32 i = 0; i < Seg; ++i)
	{
		B.Indices.Append({ Base, Base + 1 + i, Base + 2 + i });
	}
	Flush(B, Out, Layer);
}

void SPuffersMapView::PaintSatellite(const FGeometry& G, const FProjector& P, FSlateWindowElementList& Out, int32 Layer, float Alpha) const
{
	UPuffersMapSubsystem* M = Map.Get();
	const TArray<FPuffersMapTileRef>& Tiles = M->GetSatelliteTiles();
	const FVector2D S = G.GetLocalSize();
	const FBox2D View(P.ToMap(FVector2D::ZeroVector), P.ToMap(S));
	const FPuffersMapConfig& C = M->GetConfig();
	const double TexelM = C.SatelliteTileM / FMath::Max(C.SatelliteTilePx, 1);
	const double MetersPerPx = 1.0 / P.Zoom;
	const int32 Want = FMath::Clamp(FMath::FloorToInt(FMath::Log2(FMath::Max(MetersPerPx / TexelM, 1.0))), 0, M->GetSatelliteMaxLevel());
	// Coarse levels first as an underlay, then the wanted level on top.
	for (int32 Level = M->GetSatelliteMaxLevel(); Level >= Want; --Level)
	{
		for (int32 i = 0; i < Tiles.Num(); ++i)
		{
			const FPuffersMapTileRef& Tile = Tiles[i];
			if (Tile.Level != Level || !Tile.Bounds.Intersect(View))
			{
				continue;
			}
			// Only request the wanted level and the coarsest (few tiles); others draw if already cached.
			UTexture2D* Tex = (Level == Want || Level == M->GetSatelliteMaxLevel()) ? M->RequestTile(i) : nullptr;
			if (!Tex)
			{
				continue;
			}
			TSharedPtr<FSlateBrush>& Brush = TileBrushes.FindOrAdd(i);
			if (!Brush.IsValid())
			{
				Brush = MakeShared<FSlateBrush>();
			}
			Brush->SetResourceObject(Tex);
			Brush->ImageSize = FVector2D(Tex->GetSizeX(), Tex->GetSizeY());
			Brush->DrawAs = ESlateBrushDrawType::Image;
			FVector2D A, B;
			P.ToScreen(Tile.Bounds.Min, A);
			P.ToScreen(Tile.Bounds.Max, B);
			FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(B - A, FSlateLayoutTransform(A)), Brush.Get(), ESlateDrawEffect::None, FLinearColor(1, 1, 1, Alpha));
		}
	}
}

void SPuffersMapView::PaintAreas(const FGeometry& G, const FProjector& P, FSlateWindowElementList& Out, int32 Layer, float Alpha) const
{
	const UPuffersMapSubsystem* M = Map.Get();
	const FPuffersMapDataSet& D = M->GetData();
	const FPuffersMapStyle& St = M->GetStyle();
	const FSlateRenderTransform& T = G.GetAccumulatedRenderTransform();
	const FVector2D S = G.GetLocalSize();
	const FBox2D View(P.ToMap(FVector2D::ZeroVector), P.ToMap(S));
	const bool bSat = Toggles.bSatellite && M->HasSatellite();
	const double Z = P.Zoom;

	TArray<int32> Visible;
	D.QueryAreas(View, Visible);
	FBatch Fill;
	FBatch Lines;
	TArray<FVector2D> Screen;
	for (int32 Index : Visible)
	{
		const FPuffersMapArea& A = D.Areas[Index];
		FLinearColor Col = FLinearColor::Transparent;
		FLinearColor Edge = FLinearColor::Transparent;
		float EdgeW = 0.f;
		float DashOn = 0.f, DashOff = 0.f;
		switch (A.Kind)
		{
		case EPuffersMapAreaKind::Land: Col = St.Land; break;
		case EPuffersMapAreaKind::County:
			Col = St.CountyLand.Contains(A.StyleKey) ? St.CountyLand[A.StyleKey] : St.Land;
			Edge = WithAlpha(St.CountyColors.Contains(A.StyleKey) ? St.CountyColors[A.StyleKey] : St.Accent, 0.55f);
			EdgeW = Z < 0.05 ? 1.5f : 2.f;
			break;
		case EPuffersMapAreaKind::Water: Col = St.Water; break;
		case EPuffersMapAreaKind::Park: Col = St.Park; break;
		case EPuffersMapAreaKind::LotZone:
			Col = St.LotZone;
			Edge = WithAlpha(FLinearColor::White, 0.55f);
			EdgeW = 1.5f;
			DashOn = 6.f;
			DashOff = 5.f;
			break;
		case EPuffersMapAreaKind::RoadSurface: if (Z > 0.5) { Col = St.RoadSurface; } break;
		case EPuffersMapAreaKind::Sidewalk: if (Z > 1.0) { Col = St.Sidewalk; } break;
		case EPuffersMapAreaKind::Plaza: if (Z > 0.6) { Col = St.Sidewalk; } break;
		case EPuffersMapAreaKind::Building:
			if (Z > 0.35)
			{
				Col = St.BuildingFill;
				Edge = St.BuildingEdge;
				EdgeW = 1.f;
			}
			break;
		default: break;
		}
		// Satellite already shows the real ground: keep only a light county tint and the edges.
		if (bSat && (A.Kind == EPuffersMapAreaKind::Land || A.Kind == EPuffersMapAreaKind::County || A.Kind == EPuffersMapAreaKind::Water
			|| A.Kind == EPuffersMapAreaKind::Park || A.Kind == EPuffersMapAreaKind::Building || A.Kind == EPuffersMapAreaKind::RoadSurface || A.Kind == EPuffersMapAreaKind::Sidewalk))
		{
			Col.A *= A.Kind == EPuffersMapAreaKind::County ? 0.18f : 0.f;
		}
		if (Col.A <= 0.f && Edge.A <= 0.f)
		{
			continue;
		}
		// Sub-pixel areas aren't worth drawing.
		const FBox2D& Bx = D.GetAreaBounds(Index);
		if ((Bx.Max - Bx.Min).GetMax() * Z < 1.5)
		{
			continue;
		}
		Screen.Reset(A.Polygon.Num());
		for (const FVector2D& Pt : A.Polygon)
		{
			FVector2D Sp;
			P.ToScreen(Pt, Sp);
			Screen.Add(Sp);
		}
		AddPolygon(Fill, T, Screen, A.Triangles, WithAlpha(Col, Alpha));
		if (EdgeW > 0.f && Edge.A > 0.f)
		{
			Screen.Add(Screen[0]);
			AddPolyline(Lines, T, Screen, EdgeW, WithAlpha(Edge, Alpha), DashOn, DashOff);
		}
		// Keep batches bounded (SlateIndex is 32-bit, but huge batches stall the renderer).
		if (Fill.Verts.Num() > 60000)
		{
			Flush(Fill, Out, Layer);
		}
	}
	Flush(Fill, Out, Layer);
	Flush(Lines, Out, Layer);
}

void SPuffersMapView::PaintRoads(const FGeometry& G, const FProjector& P, FSlateWindowElementList& Out, int32 Layer, float Alpha) const
{
	const UPuffersMapSubsystem* M = Map.Get();
	const FPuffersMapDataSet& D = M->GetData();
	const FPuffersMapStyle& St = M->GetStyle();
	const FSlateRenderTransform& T = G.GetAccumulatedRenderTransform();
	const FVector2D S = G.GetLocalSize();
	const double Z = P.Zoom;
	FBox2D View(P.ToMap(FVector2D::ZeroVector), P.ToMap(S));
	if (P.b3D)
	{
		View = View.ExpandBy(View.GetSize().GetMax());
	}
	const int32 Lod = FPuffersMapDataSet::LodForZoom(Z);

	TArray<int32> Visible;
	D.QueryRoads(View, Visible);
	// Draw order: paths/trails under streets under highways; tunnels under everything; bridges on top.
	Visible.Sort([&D](int32 A, int32 B)
	{
		const FPuffersMapRoad& RA = D.Roads[A];
		const FPuffersMapRoad& RB = D.Roads[B];
		auto Rank = [](const FPuffersMapRoad& R)
		{
			static const int32 ClassRank[] = { 6, 5, 4, 3, 1, 2, 2, 7, 6 };
			int32 Base = ClassRank[FMath::Clamp((int32)R.Class, 0, 8)];
			if (R.IsTunnel()) Base -= 20;
			if (R.IsBridge() || (R.Flags & EPuffersRoadFlags::Overpass)) Base += 20;
			return Base;
		};
		return Rank(RA) < Rank(RB);
	});

	// Reveal sweep from the player when the full map opens.
	FVector2D RevealOrigin = S * 0.5;
	float RevealPx = 1e9f;
	if (Mode == EMode::FullScreen && !P.b3D)
	{
		FVector2D PlayerPos;
		float Heading;
		if (M->GetPlayerMapPose(PlayerPos, Heading))
		{
			P.ToScreen(PlayerPos, RevealOrigin);
		}
		const float T01 = EaseOut((float)((Time - OpenedAt) / 0.9));
		RevealPx = T01 >= 1.f ? 1e9f : T01 * (float)S.Size() * 1.2f;
	}

	FBatch Casing, Fill, Under, Shadow;
	TArray<FVector2D> Screen;
	for (int32 Index : Visible)
	{
		const FPuffersMapRoad& R = D.Roads[Index];
		const FPuffersMapRoadStyle& RS = St.Road(R.Class);
		if (Z < RS.MinZoom && R.Class != EPuffersRoadClass::Ferry)
		{
			continue;
		}
		const TArray<FVector>& Pts = D.GetRoadPoints(Index, Lod);
		Screen.Reset(Pts.Num());
		bool bAny = false;
		for (const FVector& Pt : Pts)
		{
			FVector2D Sp;
			if (P.ToScreen(FVector2D(Pt), Sp, (float)Pt.Z))
			{
				Screen.Add(Sp);
				bAny = true;
			}
		}
		if (!bAny || Screen.Num() < 2)
		{
			continue;
		}
		float RoadAlpha = Alpha;
		if (RevealPx < 1e8f)
		{
			const FVector2D Mid = Screen[Screen.Num() / 2];
			const float Dist = (float)FVector2D::Distance(Mid, RevealOrigin);
			RoadAlpha *= FMath::Clamp((RevealPx - Dist) / 120.f + 1.f, 0.f, 1.f);
			if (RoadAlpha <= 0.f)
			{
				continue;
			}
		}
		// Fade in just above the class's minimum zoom instead of popping.
		if (RS.MinZoom > 0.f)
		{
			RoadAlpha *= FMath::Clamp((float)((Z - RS.MinZoom) / (RS.MinZoom * 0.5)), 0.f, 1.f);
		}
		const float W = FMath::Clamp((float)(R.WidthM * Z), RS.MinPx, RS.MaxPx);
		if (R.Class == EPuffersRoadClass::Ferry)
		{
			AddPolyline(Fill, T, Screen, 2.f, WithAlpha(RS.Fill, RoadAlpha), 8.f, 6.f, (float)(Time * 10.0));
			continue;
		}
		if (R.Class == EPuffersRoadClass::Planned)
		{
			AddPolyline(Casing, T, Screen, W + 2.f, WithAlpha(RS.Casing, RoadAlpha));
			AddPolyline(Fill, T, Screen, W, WithAlpha(RS.Fill, RoadAlpha * 0.85f), 14.f, 6.f);
			continue;
		}
		if (R.IsTunnel())
		{
			// Tunnels: translucent dashed casing so you can see where they run underground.
			AddPolyline(Under, T, Screen, W + 2.f, WithAlpha(RS.Casing, RoadAlpha * St.TunnelAlpha));
			AddPolyline(Under, T, Screen, W, WithAlpha(RS.Fill, RoadAlpha * St.TunnelAlpha), 10.f, 7.f);
			continue;
		}
		if (R.Class == EPuffersRoadClass::Rail)
		{
			AddPolyline(Casing, T, Screen, W + 1.5f, WithAlpha(RS.Casing, RoadAlpha));
			AddPolyline(Fill, T, Screen, FMath::Max(W * 0.5f, 1.f), WithAlpha(RS.Fill, RoadAlpha), 6.f, 6.f);
			continue;
		}
		const bool bRaised = R.IsBridge() || (R.Flags & EPuffersRoadFlags::Overpass) != 0;
		if (bRaised && Z > 0.03)
		{
			// Drop shadow under bridges and overpasses.
			TArray<FVector2D> Sh = Screen;
			for (FVector2D& V : Sh)
			{
				V += FVector2D(2.5, 3.5);
			}
			AddPolyline(Shadow, T, Sh, W + 4.f, FLinearColor(0, 0, 0, 0.35f * RoadAlpha));
		}
		if (RS.Casing.A > 0.f)
		{
			AddPolyline(bRaised ? Fill : Casing, T, Screen, W + (bRaised ? 3.5f : 2.f), WithAlpha(bRaised ? St.BridgeCasing : RS.Casing, RoadAlpha));
		}
		AddPolyline(Fill, T, Screen, W, WithAlpha(RS.Fill, RoadAlpha));
		if (Fill.Verts.Num() > 80000 || Casing.Verts.Num() > 80000)
		{
			Flush(Under, Out, Layer);
			Flush(Shadow, Out, Layer);
			Flush(Casing, Out, Layer + 1);
			Flush(Fill, Out, Layer + 2);
		}
	}
	Flush(Under, Out, Layer);
	Flush(Shadow, Out, Layer);
	Flush(Casing, Out, Layer + 1);
	Flush(Fill, Out, Layer + 2);
}

void SPuffersMapView::PaintBuilds(const FGeometry& G, const FProjector& P, FSlateWindowElementList& Out, int32 Layer, float Alpha) const
{
	const UPuffersMapSubsystem* M = Map.Get();
	const FPuffersMapStyle& St = M->GetStyle();
	const FSlateRenderTransform& T = G.GetAccumulatedRenderTransform();
	const TArray<FPuffersMapBuild> Builds = M->GetBuilds();
	FBatch Fill, Edge, Fx;
	TArray<FVector2D> Screen;
	TArray<int32> Tris;
	const FVector2D S = G.GetLocalSize();
	for (const FPuffersMapBuild& Bd : Builds)
	{
		if (Bd.State == EPuffersMapBuildState::Demolished || Bd.Footprint.Num() < 3)
		{
			continue;
		}
		Screen.Reset();
		bool bOk = true;
		for (const FVector2D& Pt : Bd.Footprint)
		{
			FVector2D Sp;
			bOk &= P.ToScreen(Pt, Sp);
			Screen.Add(Sp);
		}
		if (!bOk)
		{
			continue;
		}
		FBox2D Bx(Screen);
		if (!Bx.Intersect(FBox2D(FVector2D(-60, -60), S + FVector2D(60, 60))))
		{
			continue;
		}
		FVector2D CenterS;
		P.ToScreen(Bd.GetCenter(), CenterS);
		const FLinearColor UseCol = St.UseColors.Contains(Bd.Use) ? St.UseColors[Bd.Use] : St.Accent;
		const bool bTiny = Bx.GetSize().GetMax() < 6.0;
		if (bTiny)
		{
			// Too small to show a footprint: a dot so every build is still visible zoomed out.
			AddCircle(Fill, T, CenterS, 3.2f, WithAlpha(UseCol, Alpha));
			AddRing(Edge, T, CenterS, 3.6f, 1.f, FLinearColor(0, 0, 0, 0.8f * Alpha), 12);
		}
		else
		{
			PuffersMapGeo::Triangulate(Screen, Tris);
			const float FillA = Bd.State == EPuffersMapBuildState::UnderConstruction ? 0.35f : 0.85f;
			AddPolygon(Fill, T, Screen, Tris, WithAlpha(UseCol, FillA * Alpha));
			Screen.Add(Screen[0]);
			switch (Bd.State)
			{
			case EPuffersMapBuildState::UnderConstruction:
				// Marching ants while it's being built.
				AddPolyline(Edge, T, Screen, 2.f, WithAlpha(UseCol, Alpha), 6.f, 4.f, (float)(-Time * 24.0));
				break;
			case EPuffersMapBuildState::ForSale:
			{
				const float Pulse = 0.55f + 0.45f * FMath::Sin((float)Time * 4.f);
				AddPolyline(Edge, T, Screen, 2.5f, WithAlpha(FLinearColor(1.f, 0.85f, 0.2f), Pulse * Alpha));
				break;
			}
			case EPuffersMapBuildState::Raided:
				AddPolyline(Edge, T, Screen, 2.5f, WithAlpha(FLinearColor(1.f, 0.3f, 0.28f), Alpha));
				break;
			default:
				AddPolyline(Edge, T, Screen, 1.5f, WithAlpha(UseCol * 1.25f, Alpha));
				break;
			}
		}
		// Ripple for anything that appeared or changed in the last 8 seconds (live from other players).
		const double Age = M->GetBuildAge(Bd.BuildId);
		if (Age < 8.0)
		{
			for (int32 k = 0; k < 3; ++k)
			{
				const float Phase = FMath::Fmod((float)Age * 0.9f + k * 0.33f, 1.f);
				const float Fade = (1.f - Phase) * (1.f - (float)Age / 8.f);
				AddRing(Fx, T, CenterS, 6.f + Phase * 46.f, 2.f, WithAlpha(UseCol, Fade * Alpha));
			}
		}
	}
	Flush(Fill, Out, Layer);
	Flush(Edge, Out, Layer + 1);
	Flush(Fx, Out, Layer + 2);

	// Labels for builds once zoomed in.
	if (P.Zoom > 0.6 || P.b3D)
	{
		for (const FPuffersMapBuild& Bd : Builds)
		{
			if (Bd.State == EPuffersMapBuildState::Demolished)
			{
				continue;
			}
			FVector2D C;
			if (!P.ToScreen(Bd.GetCenter(), C) || C.X < -50 || C.Y < -50 || C.X > S.X + 50 || C.Y > S.Y + 50)
			{
				continue;
			}
			FString Text = Bd.Label.IsEmpty() ? FString::Printf(TEXT("%s's %s"), *Bd.OwnerName, *Bd.Use.ToString()) : Bd.Label;
			if (Bd.State == EPuffersMapBuildState::ForSale) Text += TEXT("  FOR SALE");
			if (Bd.State == EPuffersMapBuildState::UnderConstruction) Text += TEXT("  (building)");
			DrawText(G, Out, Layer + 3, Text, C + FVector2D(0, -14), 9, WithAlpha(St.Label, Alpha), true, true);
		}
	}
}

void SPuffersMapView::PaintRoute(const FGeometry& G, const FProjector& P, FSlateWindowElementList& Out, int32 Layer, float Alpha) const
{
	const UPuffersMapSubsystem* M = Map.Get();
	if (!M->HasWaypoint() || M->GetRoute().Num() < 2)
	{
		return;
	}
	const FPuffersMapStyle& St = M->GetStyle();
	const FSlateRenderTransform& T = G.GetAccumulatedRenderTransform();
	TArray<FVector2D> Screen;
	for (const FVector2D& Pt : M->GetRoute())
	{
		FVector2D Sp;
		if (P.ToScreen(Pt, Sp))
		{
			Screen.Add(Sp);
		}
	}
	FBatch B;
	const float W = Mode == EMode::Minimap ? 4.f : 5.f;
	AddPolyline(B, T, Screen, W + 6.f, WithAlpha(St.Route, 0.22f * Alpha));
	AddPolyline(B, T, Screen, W, WithAlpha(St.Route, Alpha));
	// Bright pulses flowing toward the destination.
	AddPolyline(B, T, Screen, W * 0.55f, WithAlpha(FLinearColor::White, 0.85f * Alpha), 10.f, 26.f, (float)(-Time * 60.0));
	Flush(B, Out, Layer);
}

int32 SPuffersMapView::PaintLabels(const FGeometry& G, const FProjector& P, FSlateWindowElementList& Out, int32 Layer, float Alpha) const
{
	const UPuffersMapSubsystem* M = Map.Get();
	const FPuffersMapDataSet& D = M->GetData();
	const FPuffersMapStyle& St = M->GetStyle();
	const FSlateRenderTransform& T = G.GetAccumulatedRenderTransform();
	const FVector2D S = G.GetLocalSize();
	const double Z = P.Zoom;
	const bool bMini = Mode == EMode::Minimap;
	TArray<FBox2D> Placed;
	auto Free = [&Placed](const FBox2D& Box)
	{
		for (const FBox2D& Other : Placed)
		{
			if (Other.Intersect(Box))
			{
				return false;
			}
		}
		return true;
	};
	FBatch Icons;

	// Icons and labels sorted so bigger features claim space first.
	TArray<int32> Order;
	for (int32 i = 0; i < D.Labels.Num(); ++i)
	{
		Order.Add(i);
	}
	Order.Sort([&D](int32 A, int32 B) { return (uint8)D.Labels[A].Kind < (uint8)D.Labels[B].Kind; });

	for (int32 Index : Order)
	{
		const FPuffersMapLabel& Lb = D.Labels[Index];
		FVector2D Sp;
		if (!P.ToScreen(Lb.Position, Sp) || Sp.X < -200 || Sp.Y < -60 || Sp.X > S.X + 200 || Sp.Y > S.Y + 60)
		{
			continue;
		}
		if (Z < Lb.MinZoom || Z > Lb.MaxZoom)
		{
			continue;
		}
		float LabelAlpha = Alpha;
		int32 Size = 11;
		FLinearColor Col = St.Label;
		FName Face = FontBold;
		FString Text = Lb.Text;
		switch (Lb.Kind)
		{
		case EPuffersMapLabelKind::County:
			if (bMini) continue;
			// County names fill the screen when zoomed out and fade as you zoom in.
			LabelAlpha *= FMath::Clamp((float)((0.12 - Z) / 0.06), 0.f, 1.f);
			Size = 20;
			Col = St.CountyColors.Contains(Lb.StyleKey) ? St.CountyColors[Lb.StyleKey] : St.Label;
			Text = Text.ToUpper();
			break;
		case EPuffersMapLabelKind::District:
			LabelAlpha *= FMath::Clamp((float)((Z - 0.05) / 0.04), 0.f, 1.f) * FMath::Clamp((float)((2.0 - Z) / 0.5), 0.f, 1.f);
			Size = bMini ? 9 : 12;
			Col = WithAlpha(St.Label, 0.8f);
			Face = FontRegular;
			Text = Text.ToUpper();
			break;
		case EPuffersMapLabelKind::Water:
			LabelAlpha *= FMath::Clamp((float)((Z - 0.03) / 0.03), 0.f, 1.f);
			Size = 11;
			Col = FLinearColor(0.55f, 0.75f, 0.9f, 0.85f);
			Face = FontLight;
			break;
		case EPuffersMapLabelKind::Street:
			LabelAlpha *= FMath::Clamp((float)((Z - 0.5) / 0.3), 0.f, 1.f);
			Size = 9;
			Face = FontRegular;
			break;
		case EPuffersMapLabelKind::Place:
		case EPuffersMapLabelKind::Landmark:
		{
			// Icon always (from fairly far out), text once there's room.
			const bool bStart = Lb.Icon == TEXT("start");
			const float IconAlpha = Alpha * FMath::Clamp((float)((Z - 0.035) / 0.02), bStart ? 1.f : 0.f, 1.f);
			const FLinearColor IconCol = St.IconColors.Contains(Lb.Icon) ? St.IconColors[Lb.Icon] : St.Accent;
			const float R = bMini ? 4.5f : (Z > 0.3 ? 8.f : 6.f);
			if (IconAlpha > 0.01f)
			{
				const FLinearColor Ink(0.04f, 0.06f, 0.08f, IconAlpha);
				if (Lb.Icon == TEXT("prison") || Lb.Icon == TEXT("jail"))
				{
					const float Rr = Lb.Icon == TEXT("jail") ? R * 0.7f : R;
					AddQuad(Icons, T, Sp + FVector2D(-Rr - 1.5, -Rr - 1.5), Sp + FVector2D(Rr + 1.5, -Rr - 1.5), Sp + FVector2D(Rr + 1.5, Rr + 1.5), Sp + FVector2D(-Rr - 1.5, Rr + 1.5), ToColor(Ink));
					AddQuad(Icons, T, Sp + FVector2D(-Rr, -Rr), Sp + FVector2D(Rr, -Rr), Sp + FVector2D(Rr, Rr), Sp + FVector2D(-Rr, Rr), ToColor(WithAlpha(IconCol, IconAlpha)));
				}
				else if (Lb.Icon == TEXT("disp"))
				{
					const float Rr = R * 1.15f;
					AddQuad(Icons, T, Sp + FVector2D(0, -Rr - 2), Sp + FVector2D(Rr + 2, 0), Sp + FVector2D(0, Rr + 2), Sp + FVector2D(-Rr - 2, 0), ToColor(Ink));
					AddQuad(Icons, T, Sp + FVector2D(0, -Rr), Sp + FVector2D(Rr, 0), Sp + FVector2D(0, Rr), Sp + FVector2D(-Rr, 0), ToColor(WithAlpha(IconCol, IconAlpha)));
				}
				else if (Lb.Icon == TEXT("home"))
				{
					AddCircle(Icons, T, Sp, R + 1.5f, Ink, 12);
					AddTri(Icons, T, Sp + FVector2D(-R, 0), Sp + FVector2D(0, -R), Sp + FVector2D(R, 0), ToColor(WithAlpha(IconCol, IconAlpha)));
					AddQuad(Icons, T, Sp + FVector2D(-R * 0.7, 0), Sp + FVector2D(R * 0.7, 0), Sp + FVector2D(R * 0.7, R * 0.8), Sp + FVector2D(-R * 0.7, R * 0.8), ToColor(WithAlpha(IconCol, IconAlpha)));
				}
				else if (bStart)
				{
					// Pulsing star for the player start.
					const float Pulse = 1.f + 0.12f * FMath::Sin((float)Time * 3.f);
					TArray<FVector2D> Star;
					for (int32 k = 0; k < 10; ++k)
					{
						const float Rk = (k % 2 == 0 ? R * 1.6f : R * 0.7f) * Pulse;
						const float Ak = k * UE_PI / 5.f - UE_HALF_PI;
						Star.Add(Sp + FVector2D(FMath::Cos(Ak), FMath::Sin(Ak)) * Rk);
					}
					TArray<int32> StarTris;
					PuffersMapGeo::Triangulate(Star, StarTris);
					AddCircle(Icons, T, Sp, R * 1.1f, Ink, 12);
					AddPolygon(Icons, T, Star, StarTris, WithAlpha(IconCol, IconAlpha));
				}
				else
				{
					AddCircle(Icons, T, Sp, R + 1.5f, Ink, 14);
					AddCircle(Icons, T, Sp, R, WithAlpha(IconCol, IconAlpha), 14);
				}
			}
			LabelAlpha *= FMath::Clamp((float)((Z - (bStart ? 0.06 : 0.22)) / 0.08), 0.f, 1.f);
			Size = bMini ? 8 : 10;
			Sp += FVector2D(0, R + 9);
			break;
		}
		default:
			break;
		}
		if (LabelAlpha <= 0.02f || (bMini && Lb.Kind != EPuffersMapLabelKind::District && Lb.Kind != EPuffersMapLabelKind::Place))
		{
			continue;
		}
		const FVector2D TS = MeasureText(Text, Size, Face);
		const FBox2D Box(Sp - TS * 0.5 - FVector2D(3, 1), Sp + TS * 0.5 + FVector2D(3, 1));
		if (!Free(Box))
		{
			continue;
		}
		Placed.Add(Box);
		DrawText(G, Out, Layer + 1, Text, Sp, Size, WithAlpha(Col, LabelAlpha), true, true, Face);
	}

	// Street names on named roads when zoomed in (horizontal, at the middle of each visible road).
	if (Z > 0.45 && !P.b3D)
	{
		const FBox2D View(P.ToMap(FVector2D::ZeroVector), P.ToMap(S));
		TArray<int32> Visible;
		D.QueryRoads(View, Visible);
		TSet<FString> Done;
		for (int32 Index : Visible)
		{
			const FPuffersMapRoad& R = D.Roads[Index];
			if (R.Name.IsEmpty() || Done.Contains(R.Name) || R.Class == EPuffersRoadClass::Path)
			{
				continue;
			}
			const TArray<FVector>& Pts = R.Points;
			FVector2D Sp;
			P.ToScreen(FVector2D(Pts[Pts.Num() / 2]), Sp);
			if (Sp.X < 0 || Sp.Y < 0 || Sp.X > S.X || Sp.Y > S.Y)
			{
				continue;
			}
			const int32 Size = R.Class == EPuffersRoadClass::Highway ? 11 : 9;
			const FVector2D TS = MeasureText(R.Name, Size, FontBold);
			const FBox2D Box(Sp - TS * 0.5 - FVector2D(4, 2), Sp + TS * 0.5 + FVector2D(4, 2));
			if (!Free(Box))
			{
				continue;
			}
			Placed.Add(Box);
			Done.Add(R.Name);
			const FLinearColor Col = R.Class == EPuffersRoadClass::Highway ? FLinearColor(0.1f, 0.08f, 0.02f) : FLinearColor(0.08f, 0.1f, 0.12f);
			DrawPanel(G, Out, Layer + 1, Box.Min, Box.GetSize(), WithAlpha(St.Road(R.Class).Fill, 0.92f * Alpha));
			DrawText(G, Out, Layer + 2, R.Name, Sp, Size, WithAlpha(Col, Alpha), true, false);
		}
	}
	Flush(Icons, Out, Layer);
	return Layer + 3;
}

void SPuffersMapView::PaintMarkers(const FGeometry& G, const FProjector& P, FSlateWindowElementList& Out, int32 Layer, float Alpha) const
{
	const UPuffersMapSubsystem* M = Map.Get();
	const FPuffersMapStyle& St = M->GetStyle();
	const FSlateRenderTransform& T = G.GetAccumulatedRenderTransform();
	const FVector2D S = G.GetLocalSize();
	FBatch B;
	auto Pin = [&](const FVector2D& At, const FLinearColor& Col, bool bTracked, float Drop)
	{
		FVector2D Sp;
		if (!P.ToScreen(At, Sp))
		{
			return;
		}
		// Off-screen tracked markers clamp to the edge with an arrow.
		const FVector2D Inset(14, 14);
		const FVector2D Clamped(FMath::Clamp(Sp.X, Inset.X, S.X - Inset.X), FMath::Clamp(Sp.Y, Inset.Y, S.Y - Inset.Y));
		if (!Clamped.Equals(Sp, 0.5))
		{
			if (!bTracked)
			{
				return;
			}
			const FVector2D Dir = (Sp - S * 0.5).GetSafeNormal();
			const FVector2D N(-Dir.Y, Dir.X);
			AddTri(B, T, Clamped + Dir * 10.0, Clamped - Dir * 5.0 + N * 7.0, Clamped - Dir * 5.0 - N * 7.0, ToColor(WithAlpha(Col, Alpha)));
			return;
		}
		Sp.Y -= Drop;
		if (bTracked)
		{
			const float Phase = FMath::Fmod((float)Time * 0.8f, 1.f);
			AddRing(B, T, Sp + FVector2D(0, Drop), 6.f + Phase * 22.f, 2.f, WithAlpha(Col, (1.f - Phase) * Alpha));
		}
		AddTri(B, T, Sp + FVector2D(-6, -4), Sp + FVector2D(6, -4), Sp + FVector2D(0, 8), ToColor(FLinearColor(0.04f, 0.06f, 0.08f, Alpha)));
		AddCircle(B, T, Sp + FVector2D(0, -9), 8.5f, FLinearColor(0.04f, 0.06f, 0.08f, Alpha), 16);
		AddCircle(B, T, Sp + FVector2D(0, -9), 6.5f, WithAlpha(Col, Alpha), 16);
		AddCircle(B, T, Sp + FVector2D(0, -9), 2.5f, FLinearColor(0.04f, 0.06f, 0.08f, Alpha), 10);
	};
	for (const FPuffersMapMarker& Mk : M->GetMarkers())
	{
		Pin(Mk.Position, Mk.Color, Mk.bTracked, 0.f);
	}
	if (M->HasWaypoint())
	{
		// Waypoint pin bounces in for the first half second after it is set.
		const float Bounce = FMath::Abs(FMath::Sin((float)Time * 3.f)) * 3.f;
		Pin(M->GetWaypoint(), St.Route, true, Bounce);
	}
	Flush(B, Out, Layer);
}

void SPuffersMapView::PaintPlayer(const FGeometry& G, const FProjector& P, FSlateWindowElementList& Out, int32 Layer, float Alpha) const
{
	const UPuffersMapSubsystem* M = Map.Get();
	FVector2D Pos;
	float Heading;
	if (!M->GetPlayerMapPose(Pos, Heading))
	{
		return;
	}
	FVector2D Sp;
	if (!P.ToScreen(Pos, Sp))
	{
		return;
	}
	const FSlateRenderTransform& T = G.GetAccumulatedRenderTransform();
	const FPuffersMapStyle& St = M->GetStyle();
	FBatch B;
	const float Rad = FMath::DegreesToRadians(Heading);
	const FVector2D Dir(FMath::Cos(Rad), FMath::Sin(Rad));
	const FVector2D N(-Dir.Y, Dir.X);
	// Heading cone (view direction), fading out.
	{
		const float Len = 46.f;
		const FColor In = ToColor(FLinearColor(1.f, 1.f, 1.f, 0.28f * Alpha));
		const FColor OutC = ToColor(FLinearColor(1.f, 1.f, 1.f, 0.f));
		const SlateIndex Base = (SlateIndex)B.Verts.Num();
		B.Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, FVector2f(Sp), FVector2f(0.5f, 0.5f), In));
		B.Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, FVector2f(Sp + Dir * Len + N * Len * 0.55), FVector2f(0.5f, 0.5f), OutC));
		B.Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, FVector2f(Sp + Dir * Len - N * Len * 0.55), FVector2f(0.5f, 0.5f), OutC));
		B.Indices.Append({ Base, Base + 1, Base + 2 });
	}
	// Pulse ring every 1.6 s.
	const float Phase = FMath::Fmod((float)Time / 1.6f, 1.f);
	AddRing(B, T, Sp, 8.f + Phase * 26.f, 2.f, WithAlpha(St.Accent, (1.f - Phase) * 0.8f * Alpha));
	// Arrow.
	const FColor Ink = ToColor(FLinearColor(0.04f, 0.06f, 0.08f, Alpha));
	AddTri(B, T, Sp + Dir * 13.0, Sp - Dir * 9.0 + N * 10.0, Sp - Dir * 4.0, Ink);
	AddTri(B, T, Sp + Dir * 13.0, Sp - Dir * 4.0, Sp - Dir * 9.0 - N * 10.0, Ink);
	AddTri(B, T, Sp + Dir * 10.0, Sp - Dir * 6.5 + N * 7.5, Sp - Dir * 2.5, ToColor(WithAlpha(St.Accent, Alpha)));
	AddTri(B, T, Sp + Dir * 10.0, Sp - Dir * 2.5, Sp - Dir * 6.5 - N * 7.5, ToColor(WithAlpha(St.Accent, Alpha)));
	Flush(B, Out, Layer);
}

int32 SPuffersMapView::PaintChrome(const FGeometry& G, const FProjector& P, FSlateWindowElementList& Out, int32 Layer, float Alpha) const
{
	const UPuffersMapSubsystem* M = Map.Get();
	const FPuffersMapStyle& St = M->GetStyle();
	const FSlateRenderTransform& T = G.GetAccumulatedRenderTransform();
	const FVector2D S = G.GetLocalSize();
	FBatch B;

	if (Mode == EMode::Minimap)
	{
		// Frame, north tick and the county you're in.
		TArray<FVector2D> Frame = { FVector2D(1, 1), FVector2D(S.X - 1, 1), FVector2D(S.X - 1, S.Y - 1), FVector2D(1, S.Y - 1), FVector2D(1, 1) };
		AddPolyline(B, T, Frame, 2.f, FLinearColor(0.f, 0.f, 0.f, 0.85f * Alpha));
		FVector2D Pos;
		float Heading;
		FString Where;
		if (M->GetPlayerMapPose(Pos, Heading))
		{
			Where = M->DescribeLocation(Pos);
		}
		Flush(B, Out, Layer);
		DrawText(G, Out, Layer + 1, TEXT("N"), FVector2D(S.X * 0.5, 10), 10, WithAlpha(St.Accent, Alpha), true, true);
		if (!Where.IsEmpty())
		{
			DrawPanel(G, Out, Layer + 1, FVector2D(0, S.Y - 20), FVector2D(S.X, 20), FLinearColor(0, 0, 0, 0.55f * Alpha));
			DrawText(G, Out, Layer + 2, Where.Left(48), FVector2D(8, S.Y - 18), 9, WithAlpha(St.Label, Alpha), false, false, FontRegular);
		}
		if (M->HasWaypoint())
		{
			const FString Dist = M->GetRouteLengthM() > 1000.0 ? FString::Printf(TEXT("%.1f km"), M->GetRouteLengthM() / 1000.0) : FString::Printf(TEXT("%d m"), (int32)M->GetRouteLengthM());
			DrawText(G, Out, Layer + 2, Dist, FVector2D(S.X - 8 - MeasureText(Dist, 10).X, 6), 10, WithAlpha(St.Route, Alpha), false, true);
		}
		return Layer + 4;
	}
	if (Mode != EMode::FullScreen)
	{
		return Layer;
	}

	// Top-left title + location under the cursor (or the player).
	FVector2D Probe;
	float Heading;
	const bool bProbe = bHovering ? (Probe = P.ToMap(HoverScreen), true) : M->GetPlayerMapPose(Probe, Heading);
	DrawPanel(G, Out, Layer, FVector2D(24, 24), FVector2D(430, 66), WithAlpha(St.Panel, Alpha));
	DrawText(G, Out, Layer + 1, TEXT("PUFFINGTON"), FVector2D(40, 32), 18, WithAlpha(St.Accent, Alpha), false, false);
	if (bProbe)
	{
		DrawText(G, Out, Layer + 1, M->DescribeLocation(Probe).Left(60), FVector2D(40, 62), 10, WithAlpha(St.Label, 0.9f * Alpha), false, false, FontRegular);
	}

	// Live build counter with a blinking dot.
	const int32 NumBuilds = M->GetBuilds().Num();
	const FString Live = FString::Printf(TEXT("LIVE  %d player build%s"), NumBuilds, NumBuilds == 1 ? TEXT("") : TEXT("s"));
	const FVector2D LiveSize = MeasureText(Live, 10);
	const FVector2D LivePos(S.X - LiveSize.X - 60, 30);
	DrawPanel(G, Out, Layer, LivePos - FVector2D(30, 8), LiveSize + FVector2D(46, 18), WithAlpha(St.Panel, Alpha));
	const float Blink = 0.4f + 0.6f * (0.5f + 0.5f * FMath::Sin((float)Time * 5.f));
	AddCircle(B, T, LivePos + FVector2D(-14, LiveSize.Y * 0.5), 4.5f, FLinearColor(1.f, 0.3f, 0.3f, Blink * Alpha), 12);
	DrawText(G, Out, Layer + 1, Live, LivePos, 10, WithAlpha(St.Label, Alpha), false, false);

	// Compass.
	const FVector2D Cmp(S.X - 52, 100);
	AddCircle(B, T, Cmp, 20.f, WithAlpha(St.Panel, Alpha), 24);
	AddTri(B, T, Cmp + FVector2D(0, -15), Cmp + FVector2D(6, 0), Cmp + FVector2D(-6, 0), ToColor(WithAlpha(St.Accent, Alpha)));
	AddTri(B, T, Cmp + FVector2D(0, 15), Cmp + FVector2D(-6, 0), Cmp + FVector2D(6, 0), ToColor(WithAlpha(St.Label, 0.5f * Alpha)));

	// Scale bar: a round distance that's 80-200 px long.
	if (!P.b3D)
	{
		static const double Steps[] = { 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
		double Meters = Steps[0];
		for (double Sx : Steps)
		{
			if (Sx * P.Zoom <= 200.0)
			{
				Meters = Sx;
			}
		}
		const float Px = (float)(Meters * P.Zoom);
		const FVector2D Sb(S.X - 40 - Px, S.Y - 48);
		DrawPanel(G, Out, Layer, Sb - FVector2D(12, 26), FVector2D(Px + 24, 40), WithAlpha(St.Panel, Alpha));
		AddPolyline(B, T, { Sb, Sb + FVector2D(Px, 0) }, 3.f, WithAlpha(St.Label, Alpha));
		AddPolyline(B, T, { Sb + FVector2D(0, -5), Sb + FVector2D(0, 5) }, 2.f, WithAlpha(St.Label, Alpha));
		AddPolyline(B, T, { Sb + FVector2D(Px, -5), Sb + FVector2D(Px, 5) }, 2.f, WithAlpha(St.Label, Alpha));
		const FString Label = Meters >= 1000 ? FString::Printf(TEXT("%d km"), (int32)(Meters / 1000)) : FString::Printf(TEXT("%d m"), (int32)Meters);
		DrawText(G, Out, Layer + 1, Label, Sb + FVector2D(Px * 0.5, -14), 10, WithAlpha(St.Label, Alpha), true, false);
	}

	// Gamepad reticle.
	if (!StickPan.IsNearlyZero() || StickZoom != 0.f)
	{
		AddRing(B, T, S * 0.5, 10.f, 1.5f, WithAlpha(St.Label, 0.8f * Alpha), 20);
	}

	// Legend.
	if (Toggles.bLegend)
	{
		struct FRow { FString Text; FLinearColor Col; int32 Kind; };
		TArray<FRow> Rows = {
			{ TEXT("Highway"), St.Road(EPuffersRoadClass::Highway).Fill, 0 },
			{ TEXT("Main road"), St.Road(EPuffersRoadClass::Arterial).Fill, 0 },
			{ TEXT("Street"), St.Road(EPuffersRoadClass::Street).Fill, 0 },
			{ TEXT("Path / sidewalk"), St.Road(EPuffersRoadClass::Path).Fill, 0 },
			{ TEXT("Trail"), St.Road(EPuffersRoadClass::Trail).Fill, 0 },
			{ TEXT("Bridge"), St.BridgeCasing, 2 },
			{ TEXT("Tunnel"), WithAlpha(St.Road(EPuffersRoadClass::Street).Fill, St.TunnelAlpha), 3 },
			{ TEXT("Lot zone (buy and build)"), FLinearColor(1, 1, 1, 0.6f), 3 },
			{ TEXT("Player home"), St.UseColors.FindRef(TEXT("Home")), 1 },
			{ TEXT("Player shop"), St.UseColors.FindRef(TEXT("Shop")), 1 },
			{ TEXT("Player venue"), St.UseColors.FindRef(TEXT("Venue")), 1 },
		};
		const float RowH = 20.f;
		const FVector2D Lp(24, 104);
		const FVector2D Ls(230, Rows.Num() * RowH + 40);
		DrawPanel(G, Out, Layer, Lp, Ls, WithAlpha(St.Panel, Alpha));
		DrawText(G, Out, Layer + 1, TEXT("LEGEND"), Lp + FVector2D(14, 10), 10, WithAlpha(St.Accent, Alpha), false, false);
		for (int32 i = 0; i < Rows.Num(); ++i)
		{
			const FVector2D Row = Lp + FVector2D(14, 34 + i * RowH);
			if (Rows[i].Kind == 1)
			{
				AddQuad(B, T, Row + FVector2D(0, 3), Row + FVector2D(24, 3), Row + FVector2D(24, 15), Row + FVector2D(0, 15), ToColor(WithAlpha(Rows[i].Col, Alpha)));
			}
			else if (Rows[i].Kind == 3)
			{
				AddPolyline(B, T, { Row + FVector2D(0, 9), Row + FVector2D(24, 9) }, 3.f, WithAlpha(Rows[i].Col, Alpha), 5.f, 3.f);
			}
			else
			{
				if (Rows[i].Kind == 2)
				{
					AddPolyline(B, T, { Row + FVector2D(0, 9), Row + FVector2D(24, 9) }, 8.f, WithAlpha(Rows[i].Col, Alpha));
				}
				AddPolyline(B, T, { Row + FVector2D(0, 9), Row + FVector2D(24, 9) }, 4.f, WithAlpha(Rows[i].Kind == 2 ? St.Road(EPuffersRoadClass::Street).Fill : Rows[i].Col, Alpha));
			}
			DrawText(G, Out, Layer + 1, Rows[i].Text, Row + FVector2D(34, 1), 10, WithAlpha(St.Label, Alpha), false, false, FontRegular);
		}
	}

	// Hint bar.
	const FString Hint = P.b3D
		? TEXT("Drag pan   Wheel height   Right-click waypoint   R roads   T 2D map   F follow   M close")
		: FString::Printf(TEXT("Drag pan   Wheel zoom   Right-click waypoint   F follow   T 3D view   L legend%s   M close"), M->HasSatellite() ? TEXT("   V satellite") : TEXT(""));
	const FVector2D HintSize = MeasureText(Hint, 10, FontRegular);
	const FVector2D HintPos((S.X - HintSize.X) * 0.5, S.Y - 42);
	DrawPanel(G, Out, Layer, HintPos - FVector2D(16, 8), HintSize + FVector2D(32, 16), WithAlpha(St.Panel, Alpha));
	DrawText(G, Out, Layer + 1, Hint, HintPos, 10, WithAlpha(St.Label, 0.85f * Alpha), false, false, FontRegular);

	// Hover card for player builds and places.
	if (bHovering && !bDragging)
	{
		FString Title, Body;
		for (const FPuffersMapBuild& Bd : M->GetBuilds())
		{
			FVector2D C;
			if (Bd.Footprint.Num() >= 3 && P.ToScreen(Bd.GetCenter(), C) && (PuffersMapGeo::PointInPolygon(P.ToMap(HoverScreen), Bd.Footprint) || FVector2D::Distance(C, HoverScreen) < 8.0))
			{
				Title = Bd.Label.IsEmpty() ? FString::Printf(TEXT("%s's %s"), *Bd.OwnerName, *Bd.Use.ToString()) : Bd.Label;
				Body = FString::Printf(TEXT("Owner: %s   Status: %s   Plot: %s"), *Bd.OwnerName, *PuffersMapNames::BuildStateName(Bd.State), *Bd.PlotId.ToString());
				break;
			}
		}
		if (Title.IsEmpty())
		{
			for (const FPuffersMapLabel& Lb : M->GetData().Labels)
			{
				FVector2D C;
				if (!Lb.Icon.IsNone() && P.ToScreen(Lb.Position, C) && FVector2D::Distance(C, HoverScreen) < 10.0)
				{
					Title = Lb.Text;
					Body = Lb.Note;
					break;
				}
			}
		}
		if (!Title.IsEmpty())
		{
			const FVector2D TS = MeasureText(Title, 11);
			const FVector2D BS = Body.IsEmpty() ? FVector2D::ZeroVector : MeasureText(Body.Left(90), 9, FontRegular);
			const FVector2D Size(FMath::Max(TS.X, BS.X) + 24, TS.Y + BS.Y + 20);
			FVector2D At = HoverScreen + FVector2D(18, 12);
			At.X = FMath::Min(At.X, S.X - Size.X - 8);
			At.Y = FMath::Min(At.Y, S.Y - Size.Y - 8);
			DrawPanel(G, Out, Layer + 3, At, Size, WithAlpha(St.Panel, Alpha));
			DrawText(G, Out, Layer + 4, Title, At + FVector2D(12, 8), 11, WithAlpha(St.Accent, Alpha), false, false);
			DrawText(G, Out, Layer + 4, Body.Left(90), At + FVector2D(12, 12 + TS.Y), 9, WithAlpha(St.Label, Alpha), false, false, FontRegular);
		}
	}

	// Toast.
	const float ToastAge = (float)(Time - ToastAt);
	if (!Toast.IsEmpty() && ToastAge < 3.f)
	{
		const float A = FMath::Clamp(ToastAge / 0.15f, 0.f, 1.f) * FMath::Clamp((3.f - ToastAge) / 0.4f, 0.f, 1.f) * Alpha;
		const FVector2D TS = MeasureText(Toast, 11);
		const FVector2D At((S.X - TS.X) * 0.5, 34 - (1.f - A) * 10.f);
		DrawPanel(G, Out, Layer + 3, At - FVector2D(16, 8), TS + FVector2D(32, 16), WithAlpha(St.Panel, A));
		DrawText(G, Out, Layer + 4, Toast, At, 11, WithAlpha(St.Label, A), false, false);
	}

	Flush(B, Out, Layer + 2);
	return Layer + 6;
}
