import sys, math, numpy as np
from PIL import Image, ImageDraw, ImageFilter
d = sys.argv[1]
S = 2048                                  # draw at 2x, shrink at the end (smooth edges)
yy, xx = np.mgrid[0:S, 0:S].astype(np.float32) / S

def blob(cx, cy, r, colour, strength):
    t = np.clip(1 - np.hypot(xx - cx, yy - cy) / r, 0, 1) ** 2
    return t[..., None] * np.array(colour, np.float32) * strength, t * strength

def background(ring=True):
    top, bottom = np.array([20, 18, 36], np.float32), np.array([4, 5, 9], np.float32)
    img = top + (bottom - top) * yy[..., None]
    for cx, cy, r, c, s in [(0.18, 0.15, 0.75, (94, 92, 230), 0.75),     # indigo, top left (like the app)
                            (0.92, 0.95, 0.65, (64, 200, 224), 0.45),    # teal, bottom right
                            (0.52, 0.50, 0.42, (94, 92, 230), 0.35)]:    # glow behind the hand
        add, t = blob(cx, cy, r, c, s)
        img = img * (1 - t[..., None]) + add + img * 0  # screen-ish blend
        img = np.maximum(img, add)
    im = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), 'RGB').convert('RGBA')
    if ring:   # the app's chromatic ring, as faint liquid glass
        lay = Image.new('RGBA', (S, S), (0, 0, 0, 0)); g = ImageDraw.Draw(lay)
        c, ro, ri = S / 2, S * 0.40, S * 0.29
        g.ellipse([c - ro, c - ro, c + ro, c + ro], fill=(255, 255, 255, 26))
        g.ellipse([c - ri, c - ri, c + ri, c + ri], fill=(0, 0, 0, 0))
        for k in range(12):                                             # 12 notes
            ang = math.radians(-90 + 15 + 30 * k)
            g.line([c + ri * math.cos(ang), c + ri * math.sin(ang), c + ro * math.cos(ang), c + ro * math.sin(ang)],
                   fill=(255, 255, 255, 40), width=6)
        g.ellipse([c - ro, c - ro, c + ro, c + ro], outline=(255, 255, 255, 90), width=7)   # rims
        g.ellipse([c - ri, c - ri, c + ri, c + ri], outline=(255, 255, 255, 55), width=5)
        # sheen on the top half
        sheen = Image.new('L', (S, S), 0); sg = ImageDraw.Draw(sheen)
        for i in range(60):
            sg.rectangle([0, c - ro + i * ro / 60, S, c - ro + (i + 1) * ro / 60], fill=int(40 * (1 - i / 60)))
        ringmask = Image.new('L', (S, S), 0); rg = ImageDraw.Draw(ringmask)
        rg.ellipse([c - ro, c - ro, c + ro, c + ro], fill=255); rg.ellipse([c - ri, c - ri, c + ri, c + ri], fill=0)
        white = Image.new('RGBA', (S, S), (255, 255, 255, 0)); white.putalpha(Image.fromarray(np.minimum(np.asarray(sheen), np.asarray(ringmask))))
        lay = Image.alpha_composite(lay, white)
        im = Image.alpha_composite(im, lay)
    return im

def with_hand(im, scale):
    hand = Image.open(d + '/hand.png')
    w = int(S * scale); h = int(hand.height * w / hand.width)
    hand = hand.resize((w, h), Image.LANCZOS)
    x, y = (S - w) // 2 + int(S * 0.01), (S - h) // 2 + int(S * 0.015)
    shadow = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    sh = Image.new('RGBA', hand.size, (0, 0, 0, 170)); sh.putalpha(hand.getchannel('A').point(lambda v: v * 170 // 255))
    shadow.paste(sh, (x + 10, y + 40), sh)
    im = Image.alpha_composite(im, shadow.filter(ImageFilter.GaussianBlur(40)))
    layer = Image.new('RGBA', (S, S), (0, 0, 0, 0)); layer.paste(hand, (x, y), hand)
    return Image.alpha_composite(im, layer)

def squircle_mask(size, inner, n=4.2):
    yy2, xx2 = np.mgrid[0:size, 0:size].astype(np.float32)
    c = size / 2; r = inner / 2
    v = (np.abs(xx2 - c) / r) ** n + (np.abs(yy2 - c) / r) ** n
    return np.clip((1 - v) * r / n * 1.0, 0, 1)   # soft ~1px edge

for name, ring, scale in [('A_ring', True, 0.66), ('B_plain', False, 0.70)]:
    full = with_hand(background(ring), scale)
    ios = full.convert('RGB').resize((1024, 1024), Image.LANCZOS)
    ios.save(f'{d}/icon_{name}_ios.png')
    # macOS: the artwork in a rounded square (824 of 1024) with a margin and a soft shadow
    mac = Image.new('RGBA', (1024, 1024), (0, 0, 0, 0))
    art = full.resize((824, 824), Image.LANCZOS)
    m = Image.fromarray((squircle_mask(824, 824) * 255).astype(np.uint8))
    art.putalpha(m)
    sh = Image.new('RGBA', (1024, 1024), (0, 0, 0, 0)); shm = Image.new('L', (1024, 1024), 0); shm.paste(m.point(lambda v: v * 90 // 255), (100, 112))
    sh.putalpha(shm.filter(ImageFilter.GaussianBlur(14)))
    mac = Image.alpha_composite(mac, sh); mac.paste(art, (100, 100), art)
    mac.save(f'{d}/icon_{name}_mac.png')
print('ok')
