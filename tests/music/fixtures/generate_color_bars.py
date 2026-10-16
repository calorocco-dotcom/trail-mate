"""Generate original JPEG/PNG channel-order regression fixtures with Pillow."""
from pathlib import Path
from PIL import Image

image = Image.new('RGB', (64, 32))
colors = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)]
for index, color in enumerate(colors):
    image.paste(color, (index * 16, 0, (index + 1) * 16, 32))
directory = Path(__file__).resolve().parent
image.save(directory / 'color-bars.jpg', quality=95, subsampling=0)
image.save(directory / 'color-bars.png')
