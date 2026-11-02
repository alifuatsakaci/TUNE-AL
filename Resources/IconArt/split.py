import sys, numpy as np
from PIL import Image, ImageFilter
from scipy import ndimage as nd
d = sys.argv[1]
im = np.asarray(Image.open(d + '/hand.png')).astype(np.float32)
rgb, a = im[..., :3], im[..., 3]
mx, mn = rgb.max(2), rgb.min(2)
silver = (mx - mn < 45) & (mx > 80) & (a > 200)
lab, n = nd.label(silver)
sizes = nd.sum(silver, lab, range(1, n + 1))
core = nd.binary_fill_holes(nd.binary_closing(lab == (np.argmax(sizes) + 1), iterations=3))
dist = nd.distance_transform_edt(~core)

# ---- the fork: silver body + a clean black outline of 10 px (no shadow, no skin)
f = im.copy()
ring = (dist > 0) & (dist <= 11.5)
f[ring, :3] = [8, 8, 12]
f[..., 3] = np.clip(11.5 - dist, 0, 1) * 255
Image.fromarray(f.astype(np.uint8), 'RGBA').save(d + '/fork.png')

# its soft shadow on the palm, drawn separately so it can move with the fork
sh = np.zeros_like(im); sh[..., 3] = (dist <= 11.5) * 110
Image.fromarray(sh.astype(np.uint8), 'RGBA').filter(ImageFilter.GaussianBlur(9)).save(d + '/fork_shadow.png')

# ---- the hand without the fork: cut out the fork AND its shadow, then fill smoothly
hole = (dist <= 26) & (a > 0)
skin = (rgb[..., 0] - rgb[..., 2] > 60) & (a > 250) & ~hole & (mx > 150)   # clean, lit skin only
known = ~hole
h = rgb.copy()
h[hole] = rgb[skin].mean(0)
for _ in range(1500):        # "harmonic" fill: each hole pixel becomes the average of its neighbours
    avg = (np.roll(h, 1, 0) + np.roll(h, -1, 0) + np.roll(h, 1, 1) + np.roll(h, -1, 1)) / 4
    h[hole] = avg[hole]
    h[~skin & known] = rgb[~skin & known]          # keep outline, lines, shading as they are
# the dark outline/lines next to the hole must not bleed in: refill using skin-only neighbours
hand = im.copy(); hand[..., :3] = h
Image.fromarray(np.clip(hand, 0, 255).astype(np.uint8), 'RGBA').save(d + '/hand_nofork.png')

H, W = a.shape
ys, xs = np.where(core)
root = ys.min()
for y in range(ys.min(), ys.max()):
    row = core[y, xs.min():xs.max() + 1].astype(int)
    if (np.diff(row) == 1).sum() + row[0] >= 2:
        root = y
print('size', W, H)
print('fork x %.4f-%.4f y %.4f-%.4f  tineRoot y %.4f  centreX %.4f' % (xs.min() / W, xs.max() / W, ys.min() / H, ys.max() / H, root / H, xs.mean() / W))
