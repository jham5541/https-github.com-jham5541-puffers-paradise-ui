// The map widget. One class draws the full-screen map, the minimap and any embedded map (UPuffersMapWidget).
//
// Drawing is layered and data-driven (see FLayerToggles and MapConfig.json "style"). Every layer culls with the
// data set's spatial grid and picks a road level of detail from the zoom, so the full 22 x 16 km world stays smooth.
//
// To add a layer: add a Paint<Name> method, a toggle, and call it from OnPaint in draw order. Nothing else needs to change.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/RenderingCommon.h"
#include "Map/PuffersMapTypes.h"

class UPuffersMapSubsystem;
class FSlateWindowElementList;
struct FSlateBrush;

class PUFFERSCLUB_API SPuffersMapView : public SLeafWidget
{
public:
	enum class EMode : uint8
	{
		FullScreen,
		Minimap,
		Embedded
	};

	struct FLayerToggles
	{
		bool bSatellite = true;
		bool bAreas = true;
		bool bRoads = true;
		bool bBuilds = true;
		bool bLabels = true;
		bool bMarkers = true;
		bool bLegend = true;
		// Roads drawn over the real world in the 3D view.
		bool bRoadsIn3D = false;
	};

	SLATE_BEGIN_ARGS(SPuffersMapView)
		: _MapSubsystem(nullptr)
		, _Mode(EMode::Embedded)
	{}
		SLATE_ARGUMENT(UPuffersMapSubsystem*, MapSubsystem)
		SLATE_ARGUMENT(EMode, Mode)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// SWidget
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual bool SupportsKeyboardFocus() const override { return Mode != EMode::Minimap; }
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent) override;

	void PlayClose(FSimpleDelegate OnDone);
	void ShowToast(const FString& Text);
	void FocusOn(const FVector2D& MapPoint, float Zoom);
	FLayerToggles& Layers() { return Toggles; }

private:
	// A batch of colored triangles drawn with one MakeCustomVerts call.
	struct FBatch
	{
		TArray<FSlateVertex> Verts;
		TArray<SlateIndex> Indices;
	};

	// Map <-> screen for one paint. 2D is affine; 3D projects through the camera rig.
	struct FProjector
	{
		bool b3D = false;
		FVector2D ScreenCenter = FVector2D::ZeroVector;
		FVector2D Center = FVector2D::ZeroVector;
		double Zoom = 1.0;
		double DpiScale = 1.0;
		float FocusZ = 0.f;
		TWeakObjectPtr<APlayerController> PC;
		TWeakObjectPtr<UPuffersMapSubsystem> Map;

		bool ToScreen(const FVector2D& P, FVector2D& Out, float ZMeters) const;
		bool ToScreen(const FVector2D& P, FVector2D& Out) const { return ToScreen(P, Out, FocusZ); }
		FVector2D ToMap(const FVector2D& Screen) const;
		double PxPerM() const { return Zoom; }
	};

	TWeakObjectPtr<UPuffersMapSubsystem> Map;
	EMode Mode = EMode::Embedded;
	FLayerToggles Toggles;

	FVector2D Center = FVector2D::ZeroVector;
	double Zoom = 0.25;
	double TargetZoom = 0.25;
	FVector2D PanVelocity = FVector2D::ZeroVector;
	FVector2D KeyPan = FVector2D::ZeroVector;
	FVector2D StickPan = FVector2D::ZeroVector;
	float StickZoom = 0.f;
	bool bFollow = true;
	bool bHasZoomAnchor = false;
	FVector2D ZoomAnchorMap = FVector2D::ZeroVector;
	FVector2D ZoomAnchorScreen = FVector2D::ZeroVector;

	double Time = 0.0;
	double OpenedAt = 0.0;
	double ClosingAt = -1.0;
	FSimpleDelegate OnClosed;

	bool bDragging = false;
	bool bDragMoved = false;
	FVector2D DragLast = FVector2D::ZeroVector;
	bool bHovering = false;
	FVector2D HoverScreen = FVector2D::ZeroVector;

	FString Toast;
	double ToastAt = -100.0;

	FVector2D LastSize = FVector2D(1920, 1080);
	double LastDpi = 1.0;

	mutable TMap<int32, TSharedPtr<FSlateBrush>> TileBrushes;

	FProjector MakeProjector(const FGeometry& Geometry) const;
	float OpenAlpha() const;
	void ClampView();

	// Batch helpers.
	static void AddTri(FBatch& B, const FSlateRenderTransform& T, const FVector2D& A, const FVector2D& Bp, const FVector2D& C, FColor Col);
	static void AddQuad(FBatch& B, const FSlateRenderTransform& T, const FVector2D& A, const FVector2D& Bp, const FVector2D& C, const FVector2D& D, FColor Col);
	// Thick feathered polyline. DashOn/DashOff in px (0 = solid). DashOffset scrolls the pattern.
	static void AddPolyline(FBatch& B, const FSlateRenderTransform& T, const TArray<FVector2D>& Pts, float Width, FLinearColor Col,
		float DashOn = 0.f, float DashOff = 0.f, float DashOffset = 0.f);
	static void AddPolygon(FBatch& B, const FSlateRenderTransform& T, const TArray<FVector2D>& Pts, const TArray<int32>& Tris, FLinearColor Col);
	static void AddCircle(FBatch& B, const FSlateRenderTransform& T, const FVector2D& C, float Radius, FLinearColor Col, int32 Segments = 24);
	static void AddRing(FBatch& B, const FSlateRenderTransform& T, const FVector2D& C, float Radius, float Width, FLinearColor Col, int32 Segments = 32);
	void Flush(FBatch& B, FSlateWindowElementList& Out, int32 Layer) const;

	// Layers, in draw order.
	void PaintBase(const FGeometry& G, const FProjector& P, FSlateWindowElementList& Out, int32 Layer, float Alpha) const;
	void PaintSatellite(const FGeometry& G, const FProjector& P, FSlateWindowElementList& Out, int32 Layer, float Alpha) const;
	void PaintAreas(const FGeometry& G, const FProjector& P, FSlateWindowElementList& Out, int32 Layer, float Alpha) const;
	void PaintRoads(const FGeometry& G, const FProjector& P, FSlateWindowElementList& Out, int32 Layer, float Alpha) const;
	void PaintBuilds(const FGeometry& G, const FProjector& P, FSlateWindowElementList& Out, int32 Layer, float Alpha) const;
	void PaintRoute(const FGeometry& G, const FProjector& P, FSlateWindowElementList& Out, int32 Layer, float Alpha) const;
	int32 PaintLabels(const FGeometry& G, const FProjector& P, FSlateWindowElementList& Out, int32 Layer, float Alpha) const;
	void PaintMarkers(const FGeometry& G, const FProjector& P, FSlateWindowElementList& Out, int32 Layer, float Alpha) const;
	void PaintPlayer(const FGeometry& G, const FProjector& P, FSlateWindowElementList& Out, int32 Layer, float Alpha) const;
	int32 PaintChrome(const FGeometry& G, const FProjector& P, FSlateWindowElementList& Out, int32 Layer, float Alpha) const;

	void DrawText(const FGeometry& G, FSlateWindowElementList& Out, int32 Layer, const FString& Text, const FVector2D& Pos,
		int32 Size, FLinearColor Color, bool bCentered, bool bShadow, const FName& Typeface = TEXT("Bold")) const;
	static FVector2D MeasureText(const FString& Text, int32 Size, const FName& Typeface = TEXT("Bold"));
	void DrawPanel(const FGeometry& G, FSlateWindowElementList& Out, int32 Layer, const FVector2D& Pos, const FVector2D& Size, FLinearColor Color) const;
};
