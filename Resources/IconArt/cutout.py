import sys, numpy as np
from PIL import Image
from scipy import ndimage as nd
d = sys.argv[1]
a = np.asarray(Image.open(d + '/source.jpg').convert('RGB')).astype(np.float32)
dark = a.max(2) < 70
lab, n = nd.label(dark)
border = set(np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]]))) - {0}
bg = np.isin(lab, list(border))
art = ~bg
art = nd.binary_fill_holes(nd.binary_opening(art, iterations=2))
# distance from the coloured shape -> soft alpha for a re-drawn outline of ~11 px
dist = nd.distance_transform_edt(~art)
outline_w = 11.0
alpha = np.clip(outline_w - dist + 0.5, 0, 1)
rgb = a.copy()
rgb[~art] = [8, 8, 12]                     # the outline colour (almost black)
out = np.dstack([rgb, alpha * 255]).astype(np.uint8)
ys, xs = np.where(alpha > 0)
pad = 4
out = out[ys.min()-pad:ys.max()+pad, xs.min()-pad:xs.max()+pad]
Image.fromarray(out, 'RGBA').save(d + '/hand.png')
print('hand', out.shape)
