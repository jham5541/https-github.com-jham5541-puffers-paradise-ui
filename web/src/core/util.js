import * as THREE from 'three';

// ---------- deterministic RNG ----------
export function RNG(seed = 1) {
  let a = seed >>> 0;
  const next = () => {
    a |= 0; a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
  next.range = (lo, hi) => lo + next() * (hi - lo);
  next.int = (lo, hi) => Math.floor(next.range(lo, hi + 1));
  next.pick = (arr) => arr[Math.floor(next() * arr.length)];
  next.chance = (p) => next() < p;
  return next;
}

export const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
export const lerp = (a, b, t) => a + (b - a) * t;
export const damp = (a, b, lambda, dt) => lerp(a, b, 1 - Math.exp(-lambda * dt));
export const angleLerp = (a, b, t) => {
  let d = ((b - a + Math.PI) % (Math.PI * 2) + Math.PI * 2) % (Math.PI * 2) - Math.PI;
  return a + d * t;
};

// ---------- canvas texture helper ----------
export function makeCanvasTexture(w, h, draw, opts = {}) {
  const c = document.createElement('canvas');
  c.width = w; c.height = h;
  draw(c.getContext('2d'), w, h);
  const tex = new THREE.CanvasTexture(c);
  tex.colorSpace = opts.linear ? THREE.NoColorSpace : THREE.SRGBColorSpace;
  tex.wrapS = tex.wrapT = opts.repeat ? THREE.RepeatWrapping : THREE.ClampToEdgeWrapping;
  tex.anisotropy = opts.anisotropy ?? 8;
  if (opts.repeat) tex.repeat.set(opts.repeat[0], opts.repeat[1]);
  return tex;
}

// ---------- 2D AABB spatial hash for static colliders ----------
// Collider: { x0, z0, x1, z1, h } (h = height; h<0.4 is step-over)
export class SpatialHash {
  constructor(cell = 20) { this.cell = cell; this.map = new Map(); this.all = []; }
  _key(cx, cz) { return cx * 100000 + cz; }
  add(c) {
    this.all.push(c);
    const c0x = Math.floor(c.x0 / this.cell), c1x = Math.floor(c.x1 / this.cell);
    const c0z = Math.floor(c.z0 / this.cell), c1z = Math.floor(c.z1 / this.cell);
    for (let cx = c0x; cx <= c1x; cx++) for (let cz = c0z; cz <= c1z; cz++) {
      const k = this._key(cx, cz);
      (this.map.get(k) || this.map.set(k, []).get(k)).push(c);
    }
  }
  query(x, z, r, out = []) {
    out.length = 0;
    const c0x = Math.floor((x - r) / this.cell), c1x = Math.floor((x + r) / this.cell);
    const c0z = Math.floor((z - r) / this.cell), c1z = Math.floor((z + r) / this.cell);
    for (let cx = c0x; cx <= c1x; cx++) for (let cz = c0z; cz <= c1z; cz++) {
      const arr = this.map.get(this._key(cx, cz));
      if (arr) for (const c of arr) if (out.indexOf(c) === -1) out.push(c);
    }
    return out;
  }
}

const _q = [];
// Push a circle (x,z,r) out of static colliders. Returns {x,z, hit}.
export function collideCircle(hash, x, z, r, minH = 0.4) {
  let hit = false;
  hash.query(x, z, r + 1, _q);
  for (const c of _q) {
    if (c.h !== undefined && c.h < minH) continue;
    const nx = clamp(x, c.x0, c.x1), nz = clamp(z, c.z0, c.z1);
    let dx = x - nx, dz = z - nz;
    let d2 = dx * dx + dz * dz;
    if (d2 >= r * r) continue;
    hit = true;
    if (d2 < 1e-8) { // center inside box: push out along smallest axis
      const px = Math.min(x - c.x0 + r, c.x1 - x + r);
      const pz = Math.min(z - c.z0 + r, c.z1 - z + r);
      if (px < pz) x += (x - c.x0 < c.x1 - x) ? -(x - c.x0 + r) : (c.x1 - x + r);
      else z += (z - c.z0 < c.z1 - z) ? -(z - c.z0 + r) : (c.z1 - z + r);
    } else {
      const d = Math.sqrt(d2), push = (r - d) / d;
      x += dx * push; z += dz * push;
    }
  }
  return { x, z, hit };
}

// Raycast a 2D segment against colliders (for line of sight). Returns true if blocked.
export function losBlocked(hash, x0, z0, x1, z1, minH = 1.2) {
  const dx = x1 - x0, dz = z1 - z0;
  const len = Math.hypot(dx, dz);
  const steps = Math.max(2, Math.ceil(len / 4));
  for (let i = 1; i < steps; i++) {
    const t = i / steps, x = x0 + dx * t, z = z0 + dz * t;
    hash.query(x, z, 0.5, _q);
    for (const c of _q) {
      if ((c.h ?? 99) < minH) continue;
      if (x >= c.x0 && x <= c.x1 && z >= c.z0 && z <= c.z1) return true;
    }
  }
  return false;
}
