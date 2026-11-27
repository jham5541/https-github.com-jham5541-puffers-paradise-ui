"""Generates Content/Data/Map/WorldLayout.json (counties, land, water, lot zones, places, planned highways)
from the Puffington world plan (the same geometry as /world-map/puffington-world-map.png).

    pip install shapely
    python3 Scripts/map/make_world_layout.py

Coordinates: plan kilometers (x east, y south, 22 x 16 km box) -> map meters. Downtown Central Station (10, 8) km
is Unreal's world origin (see MapConfig.json worldOriginOnMapM). Surveyed roads replace the planned ones where they exist.
"""
import json, math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from world_geo import *
from shapely.geometry import box, LineString, MultiPolygon, Polygon
from shapely.ops import unary_union
COL = {}
NAME={'central':'Central County','heights':'Heights County','southside':'Southside County','frontier':'Frontier County','coast':'Coast County'}

# ---- plan data (copied from the world map renderer) ----
UGC=[('Cabin Woods lots',box(2.8,0.4,5.2,1.6)),('Hills Estates',box(11.2,1.2,14.4,3.6)),('Heights Foothills',box(12.4,4.0,14.6,5.0)),
     ('Desert lots',box(2.0,4.6,5.4,7.6)),('Old Town lots',box(2.6,8.2,5.4,10.0)),('Ranch lots',box(0.3,7.8,1.6,10.6)),
     ('Southside Suburbs',box(5.0,11.6,7.0,14.6)),('Bayview Waterfront',box(15.1,2.0,16.6,4.0)),('Ocean Dr Waterfront',box(14.9,11.0,16.8,12.8)),
     ('Palm Key',box(17.8,8.8,21.4,11.2))]
ugc_all=unary_union([g for _,g in UGC]).intersection(land).difference(core)
ugc_area=ugc_all.area

L=[] # num, county, kind, name, x, y, note, mine
def add(c,k,n,x,y,note,mine=False): L.append(dict(c=c,k=k,n=n,x=x,y=y,note=note,mine=mine))
# Central
add('central','start','Player start: Downtown Central Station',10.0,8.0,'Where every player spawns. Main subway hub in the middle of the loop.')
add('central','prison','Downtown Detention Center',9.5,6.6,'Central County main prison.',True)
add('central','shop','Bail Bonds Office',9.95,6.55,'Post cash bail with another player vouching as bondsman.',True)
add('central','shop','Civic Complex & Job Center',9.6,9.1,'City hall (H03). Apply for police, prison C.O. and shop jobs.')
add('central','shop','Mega Mall',8.55,7.0,'Mac works security here. Room packs become the store interiors.')
add('central','shop','Car Dealership',8.5,9.3,'Buy cars. Showroom from the Car_Dealer pack.')
add('central','lounge','The Pink Haze, 123 Kush Ave',11.3,7.6,'Strip club from the pilot. Red and purple neon.')
add('central','lounge','Boho Club',11.55,9.0,'Nightclub on The Strip.')
add('central','lounge','Cigar Lounge',11.5,6.6,"Big Al's premium space.")
add('central','disp','Strip Dispensary',11.0,8.6,'Rentable dispensary on The Strip.',True)
add('central','shop',"Terp's Radio Station",10.6,6.7,'Reactive radio keyed to missions.')
add('central','shop','Detective & Security Office',10.7,9.4,"Mac's investigations office.")
add('central','shop','Rent-a-Business Row',9.2,8.1,'Already-built city businesses to rent and run.',True)
add('central','shop','Puffers Retail Flagship',11.0,7.05,'Brand flagship store (later pack).')
# Heights
add('heights','home','Heights High-Rise Apartments',9.0,5.25,'Rented 1bd/2bd/3bd apartments. Lobby, hallways, elevators.')
add('heights','home','Premium Penthouses',9.7,4.85,'Top-floor rentals with rooftop pools.')
add('heights','shop','Puffington University',11.3,5.1,'Campus with cherry-tree quad (X12).')
add('heights','shop','Heights Gym',8.4,4.6,'Gym from the GymEAP pack.')
add('heights','disp','Heights Dispensary',10.4,4.4,'Rentable dispensary.',True)
add('heights','home','The Hills mansions',12.7,2.4,'Mansion lots and the Modern House prebuilt.')
add('heights','home','Cabin Woods',4.0,1.0,'Forest cabins and Swedish House. Cheapest homes.')
add('heights','shop','North Quarry',7.0,3.0,'Quarry lots (Rocky Sand pack).')
add('heights','prison','Hills Correctional Facility',1.3,0.8,'Heights County main prison, on a ridge.',True)
add('heights','lounge','Lakeside Lounge',5.6,3.45,'Cannabis lounge on Hills Reservoir.',True)
# Southside
add('southside','lounge','The Puffers Club',8.7,10.9,'The crew lounge and main social hub.')
add('southside','shop',"Master Sowa's Restaurant",9.25,10.65,'Between the bodega and liquor store.')
add('southside','shop','Bodega & Liquor Store',9.6,10.95,'Either side of Sowa\'s, as in the bible.')
add('southside','shop','Corner Store',10.1,11.5,'Munchie gets chased out here in the pilot.')
add('southside','home',"Haze's Apartment, 1235 Kush Ave",9.15,11.4,"Haze's place. The pilot opens here.")
add('southside','disp','Southside Dispensary',8.35,11.5,'Rentable dispensary.',True)
add('southside','shop','Pawn Counter',9.75,11.85,'Sell any inventory item.',True)
add('southside','home','Kush Ave Housing Projects',8.3,12.4,'Low-rent towers. Cheapest city apartments.',True)
add('southside','shop',"Lato's Warehouse",11.0,10.7,'Stock house in the grow corridor.')
add('southside','shop',"Jokes Up's Laboratory",12.5,11.2,'Abandoned warehouse turned lab.',True)
add('southside','shop','Wholesale Market',11.7,11.6,'Shop owners restock supply here.',True)
add('southside','shop','Industrial Wastes',12.6,13.1,'Scrapyards and ruins.')
add('southside','shop','South Quarry',4.6,13.9,'Quarry lots in the south.')
add('southside','prison','Southside Correctional',10.3,13.8,'Southside County main prison.',True)
# Frontier
add('frontier','prison','Puffington State Penitentiary',1.2,6.2,'Frontier main prison. BL01 King of the Yard.')
add('frontier','shop','Gas Station (H01)',6.3,7.6,'H01 First Day Out stop on the road from the Pen.',True)
add('frontier','shop','Old Town',4.0,9.1,'Western frontier town (OldWest pack).')
add('frontier','shop','The Bazaar',3.0,4.9,'Desert market (later pack).')
add('frontier','home','Desert starter homes',4.4,6.6,'Starter prebuilt House model on desert lots.',True)
add('frontier','disp','Roadhouse Dispensary',5.9,9.9,'Highway dispensary.',True)
add('frontier','lounge','Desert Cliffs Lounge',2.0,8.0,'Open-air lounge on the cliffs.',True)
# Coast
add('coast','shop','Harbor & Docks',14.7,5.15,'Cranes and ships (ShipAndSea).')
add('coast','transit','Harbor Ferry Pier',15.25,5.95,'Ferry to Harbor Island Prison.',True)
add('coast','prison','Harbor Island Prison',17.9,5.7,'Coast County main prison, offshore.',True)
add('coast','home','Yacht Marina',16.25,7.35,'Moorings for player yachts.')
add('coast','lounge','Tropical Garden Lounge',15.85,8.15,'Beach lounge for nightlife missions.')
add('coast','shop','Beach Event Grounds',16.25,9.25,'Stage for beach events.')
add('coast','lounge','Club Tropix Beach Resort',16.45,11.0,'Waiting on FBX export from SketchUp.')
add('coast','home','Sukkar Villa & waterfront estates',15.6,12.1,'Luxury beachfront homes.')
add('coast','home','Palm Key private island',19.6,10.0,'Mega-mansion island lots, reached by bridge.',True)
add('coast','disp','Beach Dispensary',16.6,10.1,'Rentable dispensary on Ocean Dr.',True)
add('coast','home','Bayview Waterfront',15.8,3.0,'Waterfront house lots on Puff Bay.',True)
for i,m in enumerate(L): m['num']=i+1

JAILS={'central':[(8.4,6.4),(11.6,6.3),(8.3,9.7),(10.3,9.6),(11.7,9.6)],
 'heights':[(2.2,0.9),(6.2,1.3),(10.8,0.8),(13.6,3.9),(8.0,3.4)],
 'frontier':[(1.0,3.0),(3.2,3.9),(6.6,6.4),(1.2,11.5),(3.4,12.6)],
 'southside':[(6.4,12.0),(7.9,14.2),(11.0,12.7),(13.6,12.2),(13.0,14.2)],
 'coast':[(15.6,1.2),(15.6,4.4),(15.1,7.4),(16.0,9.6),(19.0,9.3)]}

# roads (spline control points)
ROADS=[('Frontier Hwy',[(7.6,8.1),(6.3,7.7),(4.6,7.0),(2.8,6.5),(1.2,6.2)],True),
 ('Hills Pass',[(10.6,5.6),(11.6,4.4),(12.4,3.2),(12.8,2.2)],True),
 ('Cabin Rd',[(8.6,5.6),(7.4,4.0),(5.8,2.0),(4.0,1.0),(1.3,0.8)],False),
 ('Coast Hwy',[(12.4,6.6),(13.6,6.2),(14.7,5.2),(15.6,4.0),(15.8,2.4),(15.6,1.2)],True),
 ('Ocean Dr',[(12.4,8.6),(14.0,8.4),(15.6,8.2),(16.3,9.3),(16.5,11.0),(15.6,12.1)],True),
 ('Southside Ave',[(10.0,10.4),(10.1,11.8),(10.3,13.8)],False),
 ('Quarry Rd',[(8.2,10.4),(7.0,12.0),(5.8,13.2),(4.6,13.9)],False),
 ('Old Town Rd',[(7.6,9.4),(6.0,9.8),(4.0,9.1)],False),
 ('Desert Rd',[(4.6,7.0),(3.8,5.6),(3.0,4.9),(1.0,3.0)],False),
 ('Wastes Rd',[(12.4,9.8),(12.6,11.2),(12.6,13.1),(13.0,14.2)],False),
 ('Key Bridge',[(16.8,10.1),(18.2,10.0)],True),
]
FERRY=[(15.25,5.95),(16.4,5.9),(17.4,5.75)]
DISTRICTS=[(8.6,7.7,'Mega Mall'),(10.0,7.45,'Downtown'),(11.4,8.1,'The Strip'),(10.0,4.15,'The Heights'),(12.05,4.75,"University"),
 (8.85,12.0,'Southside'),(12.0,12.05,'Industrial'),(12.7,1.45,'The Hills'),(4.0,1.95,'Cabin Woods'),(3.6,6.0,'Desert Outskirts'),
 (4.0,9.75,'Old Town'),(14.5,6.55,'Harbor'),(17.45,10.05,'Puffington Beach'),(19.6,11.5,'Palm Key'),(12.6,13.6,'The Wastes'),
 (4.8,14.45,'South Quarry'),(7.0,2.55,'North Quarry'),(17.9,6.6,'Harbor Island'),(15.8,3.65,'Bayview')]
WATER_LABELS=[(19.5,6.9,'Puffington Sound'),(18.6,14.3,'Ocean'),(14.4,1.05,'Puff Bay'),(7.75,13.2,'Puffington River'),(4.5,15.6,'South Channel'),(5.6,2.4,'Hills Reservoir')]
COUNTY_LABELS=[(6.9,0.5,'heights'),(2.6,13.6,'frontier'),(10.0,14.55,'southside'),(19.0,3.2,'coast'),(10.0,6.25,'central')]

def m(x, y): return [round(x*1000, 1), round(y*1000, 1)]
def rings(g):
    gs = g.geoms if hasattr(g, 'geoms') else [g]
    return [p for p in gs if p.geom_type == 'Polygon' and not p.is_empty and p.area > 1e-4]
def pts(poly, tol=0.004):
    c = list(poly.simplify(tol).exterior.coords)[:-1]
    return [v for xy in c for v in m(*xy)]
def catmull(points, n=12):
    out = []
    for i in range(len(points)-1):
        p0 = points[i-1] if i > 0 else points[i]; p1 = points[i]; p2 = points[i+1]; p3 = points[i+2] if i+2 < len(points) else points[i+1]
        for k in range(n):
            t = k/n; t2 = t*t; t3 = t2*t
            out.append(tuple(0.5*((2*p1[j]) + (-p0[j]+p2[j])*t + (2*p0[j]-5*p1[j]+4*p2[j]-p3[j])*t2 + (-p0[j]+3*p1[j]-3*p2[j]+p3[j])*t3) for j in range(2)))
    out.append(points[-1])
    return out

areas, roads, labels = [], [], []
land_outer = unary_union([main, harbor_island, palm_key])
for i, p in enumerate(rings(land_outer)):
    areas.append(dict(id=f'land_{i}', kind='Land', pts=pts(p)))
landish = land_outer
for k, g in C.items():
    for i, p in enumerate(rings(g.intersection(landish))):
        areas.append(dict(id=f'county_{k}_{i}', name=NAME[k], kind='County', style=k, pts=pts(p)))
water = unary_union([lake, lake2, riv])
for i, p in enumerate(rings(water)):
    areas.append(dict(id=f'water_{i}', kind='Water', pts=pts(p, 0.002)))
for name, g in UGC:
    for i, p in enumerate(rings(g.intersection(land).difference(core))):
        areas.append(dict(id=f'lots_{name.replace(" ", "_")}_{i}', name=name, kind='LotZone', pts=pts(p, 0.001)))

for name, cp, major in ROADS:
    sp = catmull(cp)
    roads.append(dict(id='plan_'+name.replace(' ', '_'), name=name, cls='Planned' if major else 'Planned',
                      flags=['Bridge'] if 'Bridge' in name else [], width=14 if major else 9,
                      pts=[v for x, y in sp for v in m(x, y)]))
roads.append(dict(id='plan_ferry', name='Harbor Ferry', cls='Ferry', flags=[], width=4, pts=[v for x, y in FERRY for v in m(x, y)]))

for x, y, k in COUNTY_LABELS:
    labels.append(dict(text=NAME[k], kind='County', x=x*1000, y=y*1000, style=k))
for x, y, t in DISTRICTS:
    labels.append(dict(text=t, kind='District', x=x*1000, y=y*1000))
for x, y, t in WATER_LABELS:
    labels.append(dict(text=t, kind='Water', x=x*1000, y=y*1000))
for p in L:
    name = p['n'].replace('Player start: ', '')
    labels.append(dict(text=name, kind='Place', x=round(p['x']*1000, 1), y=round(p['y']*1000, 1), icon=p['k'], style=p['c'], note=p['note']))
for c, arr in JAILS.items():
    for i, (x, y) in enumerate(arr):
        labels.append(dict(text=f'{NAME[c].replace(" County", "")} Jail {i+1}', kind='Landmark', x=x*1000, y=y*1000, icon='jail', style=c,
                           note='County jail (AI police). Busted players go to the nearest main prison.', minZoom=0.08))

pack = {
    'format': 'puffers-map-pack', 'version': 1, 'space': 'map', 'source': 'WorldLayout',
    'generatedAt': 'make_world_layout.py',
    'roads': [dict(id=r['id'], name=r['name'], **{'class': r['cls']}, flags=r['flags'], width=r['width'], dim=2, pts=r['pts']) for r in roads],
    'areas': areas, 'labels': labels,
}
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Content', 'Data', 'Map', 'WorldLayout.json')
with open(out, 'w') as f:
    json.dump(pack, f, separators=(',', ':'))
print(f'wrote {os.path.normpath(out)}: {len(roads)} roads, {len(areas)} areas, {len(labels)} labels, {os.path.getsize(out)//1024} KB')
