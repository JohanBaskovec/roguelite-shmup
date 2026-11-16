"""Neon Breach trailer: cuts the engine recordings, adds the 80s treatment, mixes the sound.

python3 compose.py preview 10 50 130 ...   -> PNG frames in preview/
python3 compose.py render                  -> raw RGB frames on stdout (piped into gst-launch), mix.wav
"""
import sys, os, math
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops
import scipy.io.wavfile as wavfile

W, H, FPS = 1920, 1080, 30
TOTAL = 22.0
NF = int(TOTAL * FPS)
SR = 48000
ROOT = os.environ.get('TRAILER_DIR', '/tmp/trailer')
FONT_HEAVY = '/usr/share/fonts/truetype/lato/Lato-Black.ttf'
FONT_MONO = '/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf'
rng = np.random.default_rng(84)

# ---- Gameplay segments -------------------------------------------------------
# name, 60 fps frames used (after the 30-frame margin), zoom from / to, stamp text, stamp colour
SEGS = [
    ('A', 90, 1.45, 1.25, 'ROCKETS!', 'chrome'),
    ('B', 90, 1.30, 1.50, 'A SWORD?!', 'chrome'),
    ('C', 60, 1.50, 1.35, 'LASERS!', 'neon_pink'),
    ('D', 60, 1.35, 1.55, 'TOXIC!', 'neon_green'),
    ('E', 60, 1.30, 1.45, 'BLACK HOLES!', 'chrome'),
    ('F', 60, 1.55, 1.30, 'ECHOES!', 'neon_cyan'),
    ('G', 90, 1.40, 1.20, 'THE OVERSEER', 'neon_pink'),
    ('H', 150, 1.00, 1.18, 'ALL. AT. ONCE.', 'chrome'),
]
DROP = 4.0
LOGO = 15.0


class Seg:
    def __init__(self, name, n60, z0, z1, text, style, t0):
        self.name, self.n60, self.z0, self.z1, self.text, self.style, self.t0 = name, n60, z0, z1, text, style, t0
        self.frames = np.memmap(f'{ROOT}/{name}.rgb', dtype=np.uint8, mode='r').reshape(-1, H, W, 3)
        rows = np.loadtxt(f'{ROOT}/{name}.txt')
        self.pos = rows[:, 2:4]
        # smoothed camera target: the ship, averaged over +-0.4 s
        k = 25
        pad = np.pad(self.pos, ((k, k), (0, 0)), mode='edge')
        ker = np.ones(2 * k + 1) / (2 * k + 1)
        self.cam = np.stack([np.convolve(pad[:, c], ker, mode='valid') for c in range(2)], 1)
        self.dur = n60 / 60.0

    def frame(self, t):
        """Camera-framed, motion-blurred frame at time t into the segment."""
        u = min(max(t / self.dur, 0.0), 1.0)
        i = 30 + int(t * 60)
        i = min(i, self.frames.shape[0] - 2)
        a = self.frames[i].astype(np.uint16) + self.frames[i + 1]
        img = Image.fromarray((a // 2).astype(np.uint8))
        z = self.z0 + (self.z1 - self.z0) * (u * u * (3 - 2 * u))
        cx, cy = self.cam[i]
        cx = 0.55 * cx + 0.45 * W / 2  # towards the middle: the ship is never glued to the edge
        cy = 0.55 * cy + 0.45 * (H / 2 + 40)
        cw, ch = W / z, H / z
        x0 = min(max(cx - cw / 2, 0), W - cw)
        y0 = min(max(cy - ch / 2, min(112.0, H - ch)), H - ch)  # the game's HUD out of the shot when zoomed
        if z > 1.001:
            img = img.resize((W, H), Image.BICUBIC, box=(x0, y0, x0 + cw, y0 + ch))
        return img


segs = []
t = DROP
for s in SEGS:
    sg = Seg(*s, t)
    segs.append(sg)
    t += sg.dur
assert abs(t - LOGO) < 1e-6, t

# ---- Text ------------------------------------------------------------------------
_font_cache = {}


def font(path, size):
    key = (path, size)
    if key not in _font_cache:
        _font_cache[key] = ImageFont.truetype(path, size)
    return _font_cache[key]


def text_mask(text, size, path=FONT_HEAVY, skew=0.22, spacing=0.04):
    f = font(path, size)
    # draw letter by letter for the spacing
    widths = [f.getbbox(c)[2] - f.getbbox(c)[0] if c != ' ' else size * 0.3 for c in text]
    adv = [f.getlength(c) + size * spacing for c in text]
    tw = int(sum(adv) + size)
    th = int(size * 1.5)
    m = Image.new('L', (tw, th), 0)
    d = ImageDraw.Draw(m)
    x = size * 0.3
    for c, a in zip(text, adv):
        d.text((x, size * 0.15), c, font=f, fill=255)
        x += a
    m = m.crop(m.getbbox())
    pad = int(size * 0.35)
    big = Image.new('L', (m.width + 2 * pad + int(m.height * skew), m.height + 2 * pad), 0)
    big.paste(m, (pad + int(m.height * skew), pad))
    # italic: shear
    big = big.transform(big.size, Image.AFFINE, (1, skew, -skew * big.height * 0.5, 0, 1, 0), Image.BICUBIC)
    return big.crop(big.getbbox() and (0, 0, big.width, big.height))


def gradient_chrome(h, w, horizon=0.55):
    y = np.linspace(0, 1, h)[:, None]
    top = np.array([25, 15, 80]); mid = np.array([140, 215, 255]); hi = np.array([255, 255, 255])
    lo1 = np.array([70, 25, 5]); lo2 = np.array([255, 120, 40]); lo3 = np.array([255, 235, 170])
    col = np.zeros((h, 1, 3))
    a = np.clip(y / horizon, 0, 1)
    upper = np.where(a < 0.7, top + (mid - top) * (a / 0.7), mid + (hi - mid) * ((a - 0.7) / 0.3))
    b = np.clip((y - horizon) / (1 - horizon), 0, 1)
    lower = np.where(b < 0.5, lo1 + (lo2 - lo1) * (b / 0.5), lo2 + (lo3 - lo2) * ((b - 0.5) / 0.5))
    col = np.where(y < horizon, upper, lower)
    return np.repeat(col[:, None, :], w, 1)


def chrome_text(text, size, glow=(255, 40, 170), skew=0.22):
    m = text_mask(text, size, skew=skew)
    ma = np.asarray(m, dtype=np.float32) / 255
    bb = m.getbbox()
    h, w = ma.shape
    grad = np.zeros((h, w, 3))
    y0, y1 = bb[1], bb[3]
    grad[y0:y1] = gradient_chrome(y1 - y0, w)
    stroke = np.asarray(m.filter(ImageFilter.MaxFilter(max(3, size // 18 * 2 + 1))), dtype=np.float32) / 255
    halo = np.asarray(m.filter(ImageFilter.MaxFilter(max(3, size // 9 * 2 + 1))).filter(ImageFilter.GaussianBlur(size / 7)), dtype=np.float32) / 255
    rgb = np.zeros((h, w, 3)); alpha = np.zeros((h, w))
    # glow, then dark stroke, then a thin white rim, then the chrome
    rgb += np.array(glow) * halo[..., None]; alpha = np.maximum(alpha, halo * 0.9)
    rim = np.clip(stroke - ma, 0, 1)
    rgb = rgb * (1 - stroke[..., None]) + np.array([255, 240, 255]) * stroke[..., None]
    alpha = np.maximum(alpha, stroke)
    rgb = rgb * (1 - ma[..., None]) + grad * ma[..., None]
    alpha = np.maximum(alpha, ma)
    out = np.dstack([np.clip(rgb, 0, 255), alpha * 255]).astype(np.uint8)
    return Image.fromarray(out, 'RGBA')


def neon_text(text, size, col, skew=0.18, path=FONT_HEAVY):
    m = text_mask(text, size, path=path, skew=skew)
    ma = np.asarray(m, dtype=np.float32) / 255
    g1 = np.asarray(m.filter(ImageFilter.GaussianBlur(size / 10)), dtype=np.float32) / 255
    g2 = np.asarray(m.filter(ImageFilter.GaussianBlur(size / 4)), dtype=np.float32) / 255
    col = np.array(col, dtype=np.float32)
    rgb = col * np.clip(g1 * 1.6 + g2 * 1.2, 0, 1)[..., None]
    core = np.asarray(m.filter(ImageFilter.MinFilter(3)), dtype=np.float32) / 255
    rgb = rgb * (1 - core[..., None]) + (255 * 0.85 + col * 0.15) * core[..., None]
    alpha = np.clip(g1 * 1.6 + g2 * 1.2 + ma, 0, 1)
    return Image.fromarray(np.dstack([np.clip(rgb, 0, 255), alpha * 255]).astype(np.uint8), 'RGBA')


STYLE_COL = {'neon_pink': (255, 50, 190), 'neon_green': (90, 255, 120), 'neon_cyan': (60, 230, 255)}
_text_cache = {}


def styled(text, style, size):
    key = (text, style, size)
    if key not in _text_cache:
        _text_cache[key] = chrome_text(text, size) if style == 'chrome' else neon_text(text, size, STYLE_COL[style])
    return _text_cache[key]


def paste_center(img, t_img, cx, cy, scale=1.0, alpha=1.0, rot=0.0):
    if scale != 1.0:
        t_img = t_img.resize((max(1, int(t_img.width * scale)), max(1, int(t_img.height * scale))), Image.BICUBIC)
    if rot:
        t_img = t_img.rotate(rot, Image.BICUBIC, expand=True)
    if alpha < 1.0:
        a = t_img.getchannel('A').point(lambda v: int(v * alpha))
        t_img = t_img.copy(); t_img.putalpha(a)
    img.alpha_composite(t_img, (int(cx - t_img.width / 2), int(cy - t_img.height / 2)))


# ---- Synthwave backdrop ------------------------------------------------------------
def sunset(t, brightness=1.0, sun=True):
    hz = int(H * 0.60)
    y = np.linspace(0, 1, hz)[:, None]
    sky = np.zeros((hz, W, 3), np.float32)
    c0, c1, c2, c3 = np.array([8, 2, 25]), np.array([45, 8, 80]), np.array([190, 30, 140]), np.array([255, 120, 60])
    col = np.where(y < 0.5, c0 + (c1 - c0) * (y / 0.5), np.where(y < 0.85, c1 + (c2 - c1) * ((y - 0.5) / 0.35), c2 + (c3 - c2) * ((y - 0.85) / 0.15)))
    sky[:] = col[:, None, :]
    img = np.zeros((H, W, 3), np.float32)
    img[:hz] = sky
    # stars
    st = np.random.default_rng(5)
    for _ in range(160):
        sx, sy = st.integers(0, W), st.integers(0, int(hz * 0.6))
        tw = 0.5 + 0.5 * math.sin(t * st.uniform(2, 6) + st.uniform(0, 6))
        img[sy, sx] = np.maximum(img[sy, sx], 255 * tw)
    if sun:
        r = 300
        cx, cy = W // 2, hz - 60
        yy, xx = np.mgrid[cy - r:cy + 1, cx - r:cx + r]
        inside = (xx - cx) ** 2 + (yy - cy) ** 2 < r * r
        g = (yy - (cy - r)) / r
        scol = np.stack([255 + 0 * g, 230 - 150 * g, 80 - 20 * g], -1)
        # horizontal cut-outs, thicker towards the bottom, scrolling down
        band = ((yy - cy + t * 25) % 34) < (2 + 16 * np.clip((yy - (cy - r * 0.55)) / (r * 0.55), 0, 1))
        band &= yy > cy - r * 0.55
        m = inside & ~band
        y0 = max(cy - r, 0)
        reg = img[y0:cy + 1, cx - r:cx + r]
        reg[m[-(cy + 1 - y0):]] = scol[-(cy + 1 - y0):][m[-(cy + 1 - y0):]]
    # floor: perspective grid scrolling towards the camera
    fl = H - hz
    yy = np.arange(fl)[:, None].astype(np.float32)
    floor = np.zeros((fl, W, 3), np.float32)
    floor[:] = (np.array([10, 0, 26]) + np.array([34, 0, 46]) * (yy / fl)[..., None])
    lay = Image.new('L', (W, fl), 0)
    d = ImageDraw.Draw(lay)
    d0, spread = 10.0, 230.0
    for k in range(-40, 41):  # rails towards the vanishing point
        x_top = W / 2 + k * spread * d0 / fl
        x_bot = W / 2 + k * spread * (fl + d0) / fl
        d.line([(x_top, 0), (x_bot, fl)], fill=200, width=3)
    ph = (t * 2.2) % 1.0
    for n in range(1, 60):  # cross lines rushing in
        z = n - ph
        if z < 0.35:
            continue
        y = fl * 0.6 / z
        if y > fl + 4:
            continue
        wdt = max(1, int(round(5 / z)))
        d.line([(0, y), (W, y)], fill=int(min(255, 120 + 160 / z)), width=wdt)
    la = np.asarray(lay, dtype=np.float32) / 255
    glow = np.asarray(lay.filter(ImageFilter.GaussianBlur(6)), dtype=np.float32) / 255
    fade = np.clip(yy / (fl * 0.25), 0.15, 1.0)
    floor += np.array([255, 50, 210]) * (np.clip(la * 0.9 + glow * 1.3, 0, 1) * fade)[..., None]
    floor[:4] = np.array([255, 170, 245])
    img[hz:] = floor
    return np.clip(img * brightness, 0, 255).astype(np.uint8)


# ---- VHS treatment -----------------------------------------------------------------
yy, xx = np.mgrid[0:H, 0:W]
VIGNETTE = (1 - 0.45 * (((xx - W / 2) / (W / 2)) ** 2 + ((yy - H / 2) / (H / 2)) ** 2) ** 1.3).astype(np.float32)
VIGNETTE = np.clip(VIGNETTE, 0.3, 1)[..., None]
SCAN = np.ones((H, 1, 1), np.float32)
SCAN[2::3] = 0.80
del yy, xx


def vhs(img, t, aberr=2.5, glitch=0.0, bloom=0.55):
    """img: PIL RGB. Returns uint8 array."""
    # bloom from a small copy
    small = img.resize((W // 4, H // 4), Image.BILINEAR)
    sa = np.asarray(small, dtype=np.float32)
    sa = np.clip(sa - 120, 0, 255) * 1.4
    bl = Image.fromarray(sa.astype(np.uint8)).filter(ImageFilter.GaussianBlur(9)).resize((W, H), Image.BILINEAR)
    a = np.asarray(img, dtype=np.float32) + np.asarray(bl, dtype=np.float32) * bloom
    # grade: purple shadows, a touch more contrast
    a = a * 1.06 + np.array([10, 0, 22], np.float32)
    # chromatic aberration
    d = int(round(aberr))
    if d:
        a[:, d:, 0] = a[:, :-d, 0]
        a[:, :-d, 2] = a[:, d:, 2]
    # glitch: tracking bands sliding sideways
    if glitch > 0:
        nb = 1 + int(glitch * 4)
        for _ in range(nb):
            y0 = rng.integers(0, H - 80); hh = rng.integers(8, int(30 + 140 * glitch))
            sh = int(rng.integers(-1, 2) * (20 + 140 * glitch))
            a[y0:y0 + hh] = np.roll(a[y0:y0 + hh], sh, axis=1)
            a[y0:y0 + 3] += 90 * glitch
    a *= SCAN
    a *= VIGNETTE
    a += rng.normal(0, 7, (H // 2, W // 2, 1)).repeat(2, 0).repeat(2, 1)
    # a faint tracking line rolling up the picture
    ty = int(H - (t * 140) % (H + 200))
    if 0 <= ty < H - 6:
        a[ty:ty + 6] = a[ty:ty + 6] * 0.85 + 30
    return np.clip(a, 0, 255).astype(np.uint8)


def osd(img, text, x, y, size=44, col=(235, 235, 235), alpha=1.0):
    d = ImageDraw.Draw(img)
    f = font(FONT_MONO, size)
    c = tuple(int(v * alpha) for v in col)
    d.text((x + 3, y + 3), text, font=f, fill=(0, 0, 0))
    d.text((x, y), text, font=f, fill=c)


# ---- The timeline --------------------------------------------------------------------
def ease_out(u):
    return 1 - (1 - min(max(u, 0), 1)) ** 3


def typed(s, u):
    n = int(len(s) * min(max(u, 0), 1) + 0.999)
    return s[:n]


def intro_frame(t):
    bg = Image.fromarray(sunset(t * 0.6, brightness=min(1, 0.15 + t * 0.35), sun=t > 0.4)).convert('RGBA')
    glitch = 0.0
    if t < 0.45:  # tape starting: noise
        n = rng.integers(0, 255, (H // 4, W // 4), dtype=np.uint8)
        bg = Image.fromarray(np.dstack([n, n, n])).resize((W, H), Image.NEAREST).convert('RGBA')
        glitch = 1.0
    if 0.3 < t < 2.2 and (t * 2) % 1 < 0.7:
        osd(bg, 'PLAY ▶', 90, 70, 52)
    if t < 3.5:
        osd(bg, 'SP  0:00:%02d' % int(t), W - 440, H - 120, 40, alpha=0.8)
    l1 = 'IN A WORLD WITHOUT WEAPONS...'
    l2 = 'ONE SHIP. ONLY UPGRADES.'
    if 0.5 <= t < 3.55:
        s = typed(l1, (t - 0.5) / 0.9)
        img = neon_text(s, 74, (60, 230, 255), skew=0.12) if s else None
        if img:
            img_full = neon_text(l1, 74, (60, 230, 255), skew=0.12)
            bg.alpha_composite(img, (int(W / 2 - img_full.width / 2), 150))
    if 1.9 <= t < 3.55:
        s2 = typed(l2, (t - 1.9) / 0.7)
        if s2:
            img2 = chrome_text(s2, 96)
            full2 = styled(l2, 'chrome', 96)
            bg.alpha_composite(img2, (int(W / 2 - full2.width / 2), 270))
    # quick flashes of what is coming, on the snare roll
    if t >= 3.55:
        k = int((t - 3.55) / 0.15)
        sg = [segs[7], segs[0], segs[6]][min(k, 2)]
        bg = sg.frame(sg.dur * 0.5).convert('RGBA')
        glitch = 0.6
    return bg.convert('RGB'), glitch


def game_frame(t):
    for i, sg in enumerate(segs):
        if sg.t0 <= t < sg.t0 + sg.dur or (i == len(segs) - 1 and t < LOGO):
            break
    lt = t - sg.t0
    img = sg.frame(lt).convert('RGBA')
    # stamp: slams in, holds, fades
    if lt < 0.75:
        u = lt / 0.12
        sc = 1.0 + 0.9 * (1 - ease_out(u))
        al = min(1.0, u * 1.5) * (1.0 if lt < 0.55 else max(0.0, 1 - (lt - 0.55) / 0.2))
        txt = styled(sg.text, sg.style, 150)
        jit = (rng.uniform(-6, 6), rng.uniform(-6, 6)) if lt < 0.25 else (0, 0)
        paste_center(img, txt, W / 2 + jit[0], H * 0.78 + jit[1], scale=sc, alpha=al, rot=4 if sg.style == 'chrome' else -3)
    cut = lt  # time since the cut
    flash = max(0.0, 1 - cut / 0.07)
    if flash > 0:
        img = Image.blend(img, Image.new('RGBA', (W, H), (255, 220, 255, 255)), 0.4 * flash)
    glitch = max(0.0, 0.8 - cut * 8)
    aberr = 2.5 + 12 * max(0.0, 1 - cut / 0.2)
    return img.convert('RGB'), glitch, aberr


def logo_frame(t):
    lt = t - LOGO
    bg = Image.fromarray(sunset(t)).convert('RGBA')
    # title slams in at the downbeat, with a white flash
    logo = styled('NEON BREACH', 'chrome', 230)
    sc = 1.0 + 1.6 * (1 - ease_out(lt / 0.18))
    shake = (rng.uniform(-10, 10), rng.uniform(-10, 10)) if lt < 0.4 else (0, 0)
    paste_center(bg, logo, W / 2 + shake[0], 300 + shake[1], scale=sc, alpha=min(1, lt / 0.06))
    # a glint running across the logo
    if 0.6 < lt < 1.4:
        gx = W * 0.2 + (lt - 0.6) / 0.8 * W * 0.6
        d = ImageDraw.Draw(bg)
        for k, (ln, wd) in enumerate(((70, 5), (40, 3))):
            ang = k * math.pi / 2
            d.line([(gx - ln * math.cos(ang), 250 - ln * math.sin(ang)), (gx + ln * math.cos(ang), 250 + ln * math.sin(ang))], fill=(255, 255, 255, 255), width=wd)
    if lt > 1.5:
        tag = styled('MAXIMUM BREACH. MINIMUM CHILL.', 'neon_cyan', 62)
        paste_center(bg, tag, W / 2, 520, alpha=min(1, (lt - 1.5) / 0.25))
    if lt > 3.0:
        sub = styled('COMING SOON TO A CRT NEAR YOU', 'neon_pink', 54)
        paste_center(bg, sub, W / 2, 640, alpha=min(1, (lt - 3.0) / 0.25), scale=1.0 + 0.3 * (1 - ease_out((lt - 3.0) / 0.2)))
    if lt > 3.8:
        d = ImageDraw.Draw(bg)
        al = int(255 * min(1, (lt - 3.8) / 0.3))
        x0, y0 = 90, H - 230
        d.rectangle([x0, y0, x0 + 300, y0 + 150], outline=(255, 255, 255, al), width=5)
        f1, f2 = font(FONT_HEAVY, 92), font(FONT_MONO, 24)
        d.text((x0 + 22, y0 - 4), 'R', font=f1, fill=(255, 255, 255, al))
        d.text((x0 + 120, y0 + 22), 'RATED', font=f2, fill=(255, 255, 255, al))
        d.text((x0 + 120, y0 + 52), 'RAD', font=font(FONT_HEAVY, 52), fill=(255, 80, 200, al))
        d.text((x0 + 18, y0 + 114), 'EXTREME NEON VIOLENCE', font=font(FONT_MONO, 19), fill=(255, 255, 255, al))
    flash = max(0.0, 1 - lt / 0.25)
    if flash > 0:
        bg = Image.blend(bg, Image.new('RGBA', (W, H), (255, 255, 255, 255)), flash)
    img = bg.convert('RGB')
    glitch = max(0.0, 0.7 - lt * 3)
    # end: the tape stops, the picture folds into a line
    end = TOTAL - 0.45
    if t > end:
        u = (t - end) / 0.45
        a = np.asarray(img).copy()
        hh = max(2, int(H * (1 - u) ** 2))
        sq = np.asarray(img.resize((W, hh), Image.BILINEAR))
        a[:] = 0
        y0 = (H - hh) // 2
        a[y0:y0 + hh] = sq
        if u > 0.7:
            a[:] = 0
            a[H // 2 - 2:H // 2 + 2, int(W * (u - 0.7) * 1.6):int(W * (1 - (u - 0.7) * 1.6))] = 255
        img = Image.fromarray(a)
        glitch = 0.3
    return img, glitch


def render(i):
    t = i / FPS
    aberr = 2.5
    if t < DROP:
        img, glitch = intro_frame(t)
        if t >= 3.55:
            aberr = 9
    elif t < LOGO:
        img, glitch, aberr = game_frame(t)
    else:
        img, glitch = logo_frame(t)
        aberr = 2.5 + 10 * max(0, 1 - (t - LOGO) / 0.3)
    return vhs(img, t, aberr=aberr, glitch=glitch, bloom=0.35 if (DROP + 9.5 < t < LOGO) else 0.55)


def mix_audio():
    sr, music = wavfile.read(f'{ROOT}/work/music.wav')
    out = music.astype(np.float32).copy()
    n = out.shape[0]
    for sg in segs:
        _, g = wavfile.read(f'{ROOT}/{sg.name}.wav')
        a0 = 30 * 800
        ln = int(sg.dur * SR)
        clip = g[a0:a0 + ln].astype(np.float32).copy()
        f = 240
        clip[:f] *= np.linspace(0, 1, f)[:, None]
        clip[-f:] *= np.linspace(1, 0, f)[:, None]
        p = int(sg.t0 * SR)
        out[p:p + len(clip)] += clip * 0.6
    # the flashes before the drop: a burst of the chaos shot
    _, g = wavfile.read(f'{ROOT}/H.wav')
    clip = g[60 * 800:60 * 800 + int(0.45 * SR)].astype(np.float32) * np.linspace(0.2, 0.7, int(0.45 * SR))[:, None]
    p = int(3.55 * SR)
    out[p:p + len(clip)] += clip
    out = np.tanh(out * 1.05)
    out /= max(1.0, np.abs(out).max() / 0.95)
    wavfile.write(f'{ROOT}/work/mix.wav', SR, (out * 32767).astype(np.int16))


if __name__ == '__main__':
    if sys.argv[1] == 'preview':
        os.makedirs(f'{ROOT}/work/preview', exist_ok=True)
        for a in sys.argv[2:]:
            i = int(a)
            Image.fromarray(render(i)).resize((960, 540), Image.BILINEAR).save(f'{ROOT}/work/preview/p{i:03d}.png')
    elif sys.argv[1] == 'mix':
        mix_audio()
    elif sys.argv[1] == 'render':
        outf = sys.stdout.buffer
        for i in range(NF):
            outf.write(render(i).tobytes())
            if i % 30 == 0:
                print(f'frame {i}/{NF}', file=sys.stderr, flush=True)
