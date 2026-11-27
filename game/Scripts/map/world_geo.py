from shapely.geometry import Polygon, Point, box
from shapely.ops import unary_union
from shapely.affinity import scale
W,H=22,16
main0=Polygon([(0,0),(15.6,0),(16.3,1.2),(15.7,2.4),(16.9,3.4),(16.5,4.6),(15.3,5.0),(14.3,5.4),(14.1,6.3),(15.1,6.9),(16.7,7.0),(17.3,8.2),(16.9,9.6),(17.5,10.8),(16.6,12.0),(15.4,13.2),(14.2,14.4),(12.0,14.6),(10.1,15.1),(8.1,14.8),(6.6,15.4),(4.6,15.0),(2.6,15.5),(0,15.2)])
bay=scale(Point(14.6,0.2).buffer(1.6,resolution=16),1.3,1.0)
main=main0.difference(bay)
harbor_island=Point(17.9,5.7).buffer(0.55,resolution=8)
palm_key=scale(Point(19.6,10.0).buffer(1.0,resolution=12),1.5,0.9)
lake=scale(Point(5.6,2.4).buffer(0.9,resolution=12),1.4,0.8)
lake2=scale(Point(2.2,10.2).buffer(0.5),1.3,0.9)
# river polyline
river=[(9.6,0),(9.5,1.4),(9.0,2.8),(8.4,4.0),(7.6,5.2),(7.3,6.6),(7.5,8.0),(7.2,9.6),(7.0,11.0),(7.4,12.6),(7.3,15.0)]
from shapely.geometry import LineString
riv=LineString(river).buffer(0.13)
land=unary_union([main,harbor_island,palm_key]).difference(lake).difference(lake2).difference(riv)
core=box(8,6,12,10)
C={
 'heights':Polygon([(0,0),(15,0),(15,4.2),(12,6),(8,6),(5.5,3.6),(2.6,3.6),(2.6,1.8),(0,1.8)]),
 'frontier':Polygon([(0,1.8),(2.6,1.8),(2.6,3.6),(5.5,3.6),(8,6),(8,10.4),(5.2,11.2),(3.6,16),(0,16)]),
 'southside':Polygon([(3.6,16),(5.2,11.2),(8,10.4),(8,10),(12,10),(14.4,9.6),(15.6,16)]),
 'coast':Polygon([(15,0),(22,0),(22,16),(15.6,16),(14.4,9.6),(12,10),(12,6),(15,4.2)]),
 'central':core}
CL={k:v.intersection(land) for k,v in C.items()}
if __name__=='__main__':
  print('land',round(land.area,1),'water',round(W*H-land.area,1),'pct',round(land.area/(W*H)*100,1))
  for k,v in CL.items(): print(k,round(v.area,1))
