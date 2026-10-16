"""Generate original, deterministic artwork for APIC decoder tests (requires Pillow)."""
from pathlib import Path
from PIL import Image

folder = Path(__file__).with_name("fixtures")
folder.mkdir(exist_ok=True)
image = Image.new("RGB", (240, 240))
for y in range(240):
    for x in range(240):
        color = (214-y//4, 219-y//5, 193-y//6)
        if (x-173)**2+(y-62)**2 < 29**2:
            color = (231,174,96)
        if y > 113+abs(x-84)//3:
            color = (119,151,142)
        if y > 153-abs(x-168)//4:
            color = (62,111,108)
        if y > 193+abs(x-55)//5:
            color = (35,78,80)
        if y > 190 and abs(x-(109+(y-190)//3)) < 2:
            color = (222,204,162)
        image.putpixel((x,y),color)
image.save(folder/"cover.jpg",quality=90,progressive=False)
image.save(folder/"cover.png")
image.resize((320,160)).save(folder/"landscape.jpg",quality=90)
image.save(folder/"progressive.jpg",quality=90,progressive=True)
