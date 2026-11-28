# World map

The in-game map of Puffington: every road, path, tunnel and bridge, the counties and places, and every player's builds, live.

| Press | In the map |
|---|---|
| **M** | open / close (Esc also closes) |
| drag, wheel, double-click | pan, zoom at the cursor, zoom in |
| right-click | set a waypoint (right-click it again to clear). The route follows real roads. |
| **F** | follow me again |
| **2D / 3D button** (bottom center), **T** | animates between the flat map and 3D (see below) |
| **L** / **V** / **R** | legend / satellite tiles / roads over the 3D view |
| Gamepad | left stick pan, right stick zoom, A waypoint, Y 2D/3D, B close |

The minimap sits bottom-left (300 x 200) with the street and county you're in and the route distance. `puffers.Map.Minimap 0` hides it.

A three.js browser version of the map (same 2D/3D button, dense City Sample style stand-in streets, traffic, simulated live builds) is in
`Tools/MapPreview/` (`python3 Tools/MapPreview/build.py`, then open `index.html`) and online at https://claude.ai/artifact/JqnD3kVAhZNvL2ixcVhiQc.
Drop any `Generated/<Level>.mappack.json` on it to replace the stand-in with surveyed roads and buildings.

## How it fits together

```
Content/Data/Map/
  MapConfig.json            world transform, zoom, survey rules, style, backend URL
  WorldLayout.json          counties, land, water, lot zones, places, planned highways  (Scripts/map/make_world_layout.py)
  Generated/<Level>.mappack.json   surveyed roads, paths, tunnels, bridges, road surfaces  (puffers.Map.Survey)
  Satellite/<Level>/        top-down capture tiles + manifest.json                       (puffers.Map.Survey satellite)

UPuffersMapSubsystem (game instance)   loads all of it, world<->map transforms, markers, waypoint + route, screens, 3D gate
SPuffersMapView (Slate)                one widget for full screen, minimap and UMG (UPuffersMapWidget)
APuffersMapCameraRig                   3D view camera
UPuffersMapBuildService (world, server) records builds, spawns APuffersMapReplicator, syncs with the backend
APuffersMapReplicator                  fast-array of builds, replicated to every client
UPuffersMapNetComponent                added to every PlayerController; carries a player's build reports to the server
APuffersMapSurveyor                    road / bridge / tunnel extraction and satellite capture
APuffersMapRoadTagVolume               name roads, force bridge / tunnel / one-way / exclude
```

Map space is meters, x east, y south, origin at the top-left of the 22 x 16 km world box. Unreal's world origin sits at
Downtown Central Station (10 km, 8 km), set by `worldOriginOnMapM` in MapConfig.json. Surveyed packs store Unreal
centimeters, so moving the origin never needs a re-survey.

## Mapping every road (City Sample, outskirts, every new environment)

Open the level in PIE (or a development build) and run in the console:

```
puffers.Map.Survey                  roads only (fast)
puffers.Map.Survey satellite        roads + top-down satellite tiles (needs a GPU that can render the level)
puffers.Map.Survey bounds=core      only the 4 x 4 km Puffington Core
puffers.Map.Survey nosweep          only what's loaded right now
```

What it reads:

* **ZoneGraph** (City Sample's MassTraffic data): every vehicle lane, pedestrian lane and crosswalk, plus each zone's
  outline as the real road / sidewalk surface. Lane direction is kept so routes respect one-ways.
* **Landscape splines** (outskirts roads and trails).
* **Spline actors** whose name, class or tag matches a `survey.actors` rule (Road, Street, Trail, Rail, Bridge, Tunnel, `MapRoad`...).
  Tag any spline actor `MapRoad` to put it on the map, or `MapIgnore` to keep it off.
* **Meshes** whose asset name matches a `survey.meshes` rule become sidewalks, plazas, parks and building footprints.

Bridges and tunnels are detected by line traces along every road (every 10 m): ground far below on both sides = raised
(a **bridge** if over water or nothing, an **overpass** if over other ground); solid cover overhead and over both edges
= **tunnel**. Runs shorter than 25 m are ignored. `APuffersMapRoadTagVolume` boxes always win: drop one over a road to
name it ("Kush Ave"), change its class, or force bridge / tunnel / one-way / exclude.

World Partition levels are swept cell by cell (400 m, or 500 m with satellite): the surveyor becomes the view target,
waits for streaming, reads what loaded and runs the traces while that geometry is in memory. The log lists every ZoneGraph
tag the level uses; if a tag isn't classified the way you want, add a rule to `survey.zoneGraphTags` and re-run.

Each run writes `Content/Data/Map/Generated/<Level>.mappack.json` and replaces that level's previous pack. Adding an
environment = open it, run the survey, commit the pack. Planned highways from the world plan disappear wherever surveyed
roads exist.

## Player builds on everyone's map

* **Career plots** (handoff Rev 12): when a player adds or changes a building on a plot (`AddPlotBuilding` now publishes
  `PlotBuildingChanged`), their `UPuffersMapNetComponent` sends every building on their plots to the server. It also
  resyncs every 30 s and on any plot / raid / estate event, and tells the server about buildings that are gone.
* **Any other build system** (APuffersBuildZone pieces, Blueprints): on the server call
  `UPuffersMapBuildService::RecordBuildFromActor(BuildingActor, BuildId, PlotId, OwnerId, OwnerName, Use, State, Label)`,
  or from the owning client `UPuffersMapNetComponent::ServerReportBuilds`.
* The server fills in the owner from the player state, rejects edits to other players' builds, oversized footprints and
  bad ids, then upserts into `APuffersMapReplicator`. Only changes replicate (fast array), so thousands of builds are cheap.
* Clients get a ripple animation on anything new or changed in the last 8 seconds. Under construction = marching-ants
  outline, for sale (lapsed plot fee) = pulsing gold, raided = red.

Where a plot sits: the lot actor tagged or named with the listing's `BuildZoneLotId` if it exists; until real lot actors
exist, a stable spot on the 20 m grid inside that county's lot zone from WorldLayout.json (hashed from the plot id, so
every client and server agree).

**Online persistence.** A dedicated or listen server with `online.backendUrl` (or `PUFFERS_BACKEND_URL`) set pushes
builds to the backend and polls `GET /v1/map/builds?since=<seq>` every 5 s, so builds made on any server appear on all of
them. Set the same secret in `PUFFERS_SERVER_KEY` (game server) and `MAP_SERVER_KEY` (backend). Offline / standalone
builds persist in `Saved/PuffersMap/Builds.json`.

Backend: `backend/src/routes/map.ts`, table `map_builds` in `backend/db/schema.sql`. The backend checks the key, the
footprint size and that only the owner can change a live build.

## 3D view

The **2D / 3D** button (or **T**, or gamepad **Y**) tilts the map back over 0.9 s: north recedes, surveyed buildings and
player builds rise out of the ground as lit blocks, and roads, labels and the route follow the perspective. This
stylized 3D works on every machine, the Intel Mac included. The button's thumb slides with the tilt.

Where the GPU can render the city, the photoreal camera starts at the same moment, blends in under the tilting map, and
the map then fades away to the real world at the same 58° angle. If the frame rate can't keep up, it falls back to the
stylized 3D, not to flat 2D.

### Photoreal

The photoreal step flies a camera up from the player to look down at the real world at ~58°, north up. The wheel sets the height,
drag pans, and the map's labels, markers, builds and route are drawn on top (roads too with **R**). Because the camera
becomes the view target, World Partition streams the city around it and HLODs fill the distance, so it's the actual
game world, not a picture of it.

It needs a GPU that can render the full city. On integrated GPUs (the Intel Mac's Iris) it stays on the 2D map, and any
machine that can't hold about 12 fps for 3 s drops back to 2D. `puffers.Map.3D 0/1/2` = off / auto / force.

**Satellite tiles** are the photoreal option for the 2D map: `puffers.Map.Survey satellite` renders straight-down
orthographic tiles (500 m, 1024 px) and builds a zoom pyramid. Run it once per level on a strong machine (M3 Mac or an
RTX PC), commit `Content/Data/Map/Satellite/`, and every machine, including the Intel Mac, shows the real city from above
under the vector roads (**V** toggles). Satellite assumes `worldYawDeg` 0.

## Making it yours

* Colors, road widths and when each road class appears: `style` in MapConfig.json, then `puffers.Map.Reload`.
* New layer: add a `Paint<Name>` method to `SPuffersMapView`, a toggle in `FLayerToggles`, and call it from `OnPaint`.
* New marker types (jobs, missions, friends): `UPuffersMapSubsystem::SetMarker` with an icon, color and `bTracked`.
* Embed the map anywhere (phone app, property browser): the **Puffers Map** UMG widget.

## Console

| Command | Does |
|---|---|
| `puffers.Map.Toggle` | open / close the map |
| `puffers.Map.Reload` | reload Content/Data/Map |
| `puffers.Map.Survey [...]` | survey this level (see above) |
| `puffers.Map.Waypoint x y` | waypoint at map meters |
| `puffers.Map.Where` | where you are |
| `puffers.Map.TestBuild [Use]` | fake build 30 m ahead (server / standalone) to watch it appear on every client |

Tests: `Puffers.Map.*` (geometry, pack JSON, the shipped world layout, routing, bridge/tunnel splitting, build checks).

## Copying to the drive

The project on the drive needs these in `Source/PuffersClub/PuffersClub.Build.cs` private dependencies (the repo's
Build.cs has them): `UMG`, `HTTP`, `RHI`, `RenderCore`, `ImageCore`, `ZoneGraph`, `Landscape`, plus `Json`. ZoneGraph is
already enabled in City Sample. `Config/DefaultGame.ini` stages `Content/Data` so packaged builds can read the JSON.
None of this changes the platforms: the code is plain C++ and builds universal (arm64 + x86_64).
