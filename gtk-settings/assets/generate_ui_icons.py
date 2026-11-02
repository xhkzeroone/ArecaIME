"""Generate filled outlines: GTK symbolic loaders do not preserve SVG strokes.
Run from any directory with Python 3; no graphics dependencies required.
"""
from math import cos, sin, pi, hypot
from pathlib import Path


def stroke(points, closed=False):
    parts = []
    pairs = list(zip(points, points[1:] + points[:1] if closed else points[1:]))
    for (x, y), (u, v) in pairs:
        length = hypot(u - x, v - y)
        if length < .001:
            continue
        dx, dy = -(v - y) / length * .9, (u - x) / length * .9
        parts.append(f'<path d="M{x+dx:.3f} {y+dy:.3f}L{u+dx:.3f} {v+dy:.3f} {u-dx:.3f} {v-dy:.3f} {x-dx:.3f} {y-dy:.3f}Z"/>')
    parts.extend(f'<circle cx="{x:.3f}" cy="{y:.3f}" r=".9"/>' for x, y in points)
    return ''.join(parts)


def arc(x, y, r, start=0, end=2*pi, steps=32):
    return [(x+r*cos(start+(end-start)*i/steps), y+r*sin(start+(end-start)*i/steps)) for i in range(steps+1)]


def circle(x, y, r):
    return stroke(arc(x, y, r), True)


def rect(x, y, w, h, r):
    points=[]
    for cx,cy,start in ((x+w-r,y+r,-pi/2),(x+w-r,y+h-r,0),(x+r,y+h-r,pi/2),(x+r,y+r,pi)):
        points.extend(arc(cx,cy,r,start,start+pi/2,8))
    return stroke(points, True)


def line(*points):
    return stroke(list(points))


def dot(x,y):
    return f'<circle cx="{x}" cy="{y}" r="1"/>'


def smile():
    return stroke([(8+i/2,14+3*sin(pi*i/16)) for i in range(17)])


icons={
    'home': stroke([(3,10),(12,3),(21,10),(21,21),(15,21),(15,14),(9,14),(9,21),(3,21)],True),
    'keyboard': rect(2,5,20,14,3)+line((7,16),(17,16))+''.join(dot(x,y) for y in (9,12) for x in (6,10,14,18)),
    'macro': stroke([(13,3),(5,13),(11,13),(10,21),(19,10),(13,10)],True)+line((3,3),(3,6))+line((1.5,4.5),(4.5,4.5)),
    'apps': ''.join(rect(x,y,7,7,1.8) for x in (3,14) for y in (3,14)),
    'advanced': line((3,7),(9,7))+circle(11,7,2)+line((13,7),(21,7))+line((3,17),(13,17))+circle(15,17,2)+line((17,17),(21,17)),
    'palette': stroke(arc(12,12,9,.2,5.7)+[(17,8),(15,10),(15,13),(18,14),(21,14)],True)+''.join(dot(x,y) for x,y in ((6,11),(9,6),(14,5))),
    'icons': rect(3,3,18,18,6)+dot(8,9)+dot(16,9)+smile(),
    'about': circle(12,12,9)+dot(12,7)+line((12,11),(12,17)),
    'close': line((7,7),(17,17))+line((17,7),(7,17)),
    'plus': line((12,5),(12,19))+line((5,12),(19,12)),
    'delete': line((4,7),(20,7))+line((9,7),(9,4),(15,4),(15,7))+line((6,7),(7,20),(17,20),(18,7))+line((10,11),(10,16))+line((14,11),(14,16)),
    'bug': rect(7,7,10,14,5)+line((9,7),(7,3))+line((15,7),(17,3))+line((3,10),(7,10))+line((17,10),(21,10))+line((3,15),(7,15))+line((17,15),(21,15))+line((12,11),(12,17)),
    'refresh': stroke(arc(12,12,8,3.5,6.1))+line((21,5),(20,10),(15,9))+stroke(arc(12,12,8,.35,2.95))+line((3,19),(4,14),(9,15)),
    'image': rect(3,3,18,18,3)+circle(8,8,1.5)+line((3,17),(9,12),(13,15),(17,10),(21,15)),
    'check': line((5,12),(10,17),(20,7)),
}

if __name__ == '__main__':
    root=Path(__file__).resolve().parent/'ui'
    root.mkdir(exist_ok=True)
    for name,body in icons.items():
        (root/f'{name}-symbolic.svg').write_text(f'<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><g fill="#2e3436">{body}</g></svg>\n')
