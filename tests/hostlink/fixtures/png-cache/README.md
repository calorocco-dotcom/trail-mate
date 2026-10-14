# Image cache fixtures

These fixtures contain original, programmatically generated pixels, with no
external image content. `generate.py` reproduces them using Pillow and Python
zlib; regeneration is not required to run native tests.

- `red-16x8.jpg`: solid red RGB, JPEG quality 95, no chroma subsampling, for
  the actual tiny JPEG decoder and BGR cache regression.
- `rgba-96x96.png`: Pillow PNG encoder, adaptive row filters and compressed IDAT.
- `fixed-96x96.png`: the same pixels with unfiltered rows and fixed-Huffman zlib.

For both PNG files, pixel `(x,y)` is
`((x*5)%256, (y*7)%256, ((x+y)*13)%256, [0,128,255][(x+y)%3])`.
Native tests compute expected colors from this definition before comparing every
BMP pixel. Additional fixtures, including all legal color/depth/Adam7/filter
combinations and malformed streams, are generated in memory by the native test.
