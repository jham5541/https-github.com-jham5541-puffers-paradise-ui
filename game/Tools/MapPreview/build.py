"""Builds the three.js map preview (index.html) from template.html and Content/Data/Map/WorldLayout.json.

    python3 Tools/MapPreview/build.py        (run from the game folder)

Open index.html in a browser. Drop a Content/Data/Map/Generated/<Level>.mappack.json on it to see surveyed roads.
"""
import json, os
here = os.path.dirname(os.path.abspath(__file__))
layout = json.load(open(os.path.join(here, '..', '..', 'Content', 'Data', 'Map', 'WorldLayout.json')))
page = open(os.path.join(here, 'template.html')).read().replace('/*__LAYOUT__*/', json.dumps(layout, separators=(',', ':')).replace('</', '<\\/'))
open(os.path.join(here, 'index.html'), 'w').write(page)
print('wrote', os.path.join(here, 'index.html'), len(page), 'bytes')
