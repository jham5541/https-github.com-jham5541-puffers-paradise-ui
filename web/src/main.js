import * as THREE from 'three';
import { createEvents } from './core/events.js';
import { createInput } from './core/input.js';
import { buildCity } from './world/city.js';
import { createAtmosphere } from './world/atmosphere.js';
import { createVehicleSystem } from './entities/vehicles.js';
import { createPedSystem } from './entities/peds.js';
// Namespace import: initUnlocks is optional — a static named import of a
// not-yet-existing export would break the whole boot.
import * as CHARS from './entities/characters.js';
const { CHARACTERS, buildCharacter } = CHARS;
import { createPlayer } from './game/player.js';
import { createPoliceSystem } from './game/wanted.js';
import { createMissionSystem } from './game/missions.js';
import { createHUD } from './ui/hud.js';
import { createMenu } from './ui/menu.js';
import { createAudio } from './core/audio.js';
import { createPostFX } from './core/postfx.js';

const params = new URLSearchParams(location.search);
const bootbar = document.getElementById('bootbar');
const bootmsg = document.getElementById('bootmsg');
const setBoot = (p, msg) => { bootbar.style.width = `${(p * 100) | 0}%`; if (msg) bootmsg.textContent = msg; };
const nextFrame = () => new Promise((r) => requestAnimationFrame(r));

async function boot() {
  const app = document.getElementById('app');
  const renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: 'high-performance' });
  renderer.setSize(innerWidth, innerHeight);
  renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFSoftShadowMap;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.0;
  app.appendChild(renderer.domElement);

  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(60, innerWidth / innerHeight, 0.1, 4000);
  camera.position.set(0, 60, 120);

  const G = {
    scene, camera, renderer,
    events: createEvents(),
    input: createInput(renderer.domElement),
    time: { dt: 0, elapsed: 0, timeOfDay: params.has('tod') ? parseFloat(params.get('tod')) : 17.2 },
    timeScale: 1,
    world: {},
    systems: {},
    player: null,
    state: 'menu',
    money: parseInt(localStorage.getItem('pp_money') || '500', 10),
    quality: { pixelRatio: Math.min(devicePixelRatio, 2), shadows: true, postfx: !params.has('nofx') },
    debug: {
      autostart: params.get('autostart'),
      cam: params.get('cam'),
      seed: parseInt(params.get('seed') || '1337', 10),
      freeze: params.has('freeze'),
    },
    CHARACTERS, buildCharacter,
  };
  window.__game = G;

  setBoot(0.1, 'pouring concrete…'); await nextFrame();
  G.world.city = buildCity(G, G.debug.seed);
  scene.add(G.world.city.group);

  setBoot(0.45, 'sparking the sky…'); await nextFrame();
  G.world.atmosphere = createAtmosphere(G);
  if (params.get('weather')) G.world.atmosphere.setWeather(params.get('weather'));

  setBoot(0.6, 'tuning the lens…'); await nextFrame();
  G.systems.postfx = G.quality.postfx ? createPostFX(G) : null;

  setBoot(0.7, 'waking the streets…'); await nextFrame();
  G.systems.vehicles = createVehicleSystem(G);
  G.systems.peds = createPedSystem(G);

  setBoot(0.85, 'calling the crew…'); await nextFrame();
  G.systems.hud = createHUD(G);
  G.systems.audio = createAudio(G);
  const menu = createMenu(G, buildCharacter);
  CHARS.initUnlocks?.(G);

  G.events.on('startGame', (characterId) => {
    if (G.state !== 'menu') return;
    menu.hide();
    startGame(G, characterId);
  });

  setBoot(1, 'ready.');
  document.getElementById('boot').style.opacity = '0';
  setTimeout(() => document.getElementById('boot').remove(), 700);

  if (G.debug.autostart) {
    G.events.emit('startGame', G.debug.autostart === '1' ? 'purp' : G.debug.autostart);
  } else {
    menu.show();
  }

  // ---------- main loop ----------
  const clock = new THREE.Clock();
  renderer.setAnimationLoop(() => {
    const raw = Math.min(clock.getDelta(), 0.1);
    const dt = raw * G.timeScale;
    G.time.dt = dt; G.time.elapsed += dt;

    if (!G.debug.freeze) {
      G.world.city.update?.(dt);
      G.world.atmosphere.update(dt);
    }
    if (G.state === 'playing' || G.state === 'wasted' || G.state === 'busted') {
      G.player?.update(dt);
      G.systems.vehicles.update(dt);
      G.systems.peds.update(dt);
      G.systems.police?.update(dt);
      G.systems.missions?.update(dt);
    } else {
      G.systems.vehicles.update(dt); // ambient traffic behind the menu
      G.systems.peds.update(dt);
      menu.update?.(dt);
    }
    applyDebugCamera(G);
    G.systems.hud.update(dt);
    G.systems.audio.update(dt);

    if (G.systems.postfx) { G.systems.postfx.update(dt); G.systems.postfx.composer.render(); }
    else renderer.render(scene, camera);
    G.input.endFrame();
  });

  addEventListener('resize', () => {
    camera.aspect = innerWidth / innerHeight;
    camera.updateProjectionMatrix();
    renderer.setSize(innerWidth, innerHeight);
    G.systems.postfx?.composer.setSize(innerWidth, innerHeight);
  });
}

function startGame(G, characterId) {
  const s = G.world.city.spawns.player;
  G.player = createPlayer(G, characterId || 'purp');
  G.player.respawn(s.x, s.z);
  G.systems.police = createPoliceSystem(G);
  G.systems.missions = createMissionSystem(G);
  G.state = 'playing';
  G.events.emit('moneyChanged', G.money);
  G.events.emit('notification', `Welcome to Empire City, ${(CHARACTERS.find(c => c.id === (characterId || 'purp'))?.name) || 'Puffer'}.`);

  G.events.on('playerDied', () => {
    if (G.state !== 'playing') return;
    G.state = 'wasted'; G.timeScale = 0.35;
    setTimeout(() => {
      G.timeScale = 1; G.money = Math.max(0, G.money - 100);
      localStorage.setItem('pp_money', String(G.money));
      G.events.emit('moneyChanged', G.money);
      const h = G.world.city.spawns.hospital || G.world.city.spawns.player;
      G.player.respawn(h.x, h.z); G.state = 'playing';
    }, 3500);
  });
  G.events.on('playerBusted', () => {
    if (G.state !== 'playing') return;
    G.state = 'busted'; G.timeScale = 0.35;
    setTimeout(() => {
      G.timeScale = 1; G.money = Math.max(0, G.money - 200);
      localStorage.setItem('pp_money', String(G.money));
      G.events.emit('moneyChanged', G.money);
      const p = G.world.city.spawns.police || G.world.city.spawns.player;
      G.player.respawn(p.x, p.z); G.state = 'playing';
    }, 3500);
  });
}

// Fixed cinematic camera modes for the screenshot harness.
function applyDebugCamera(G) {
  const m = G.debug.cam;
  if (!m) return;
  const t = G.time.elapsed;
  const cam = G.camera;
  const px = G.player?.pos?.x ?? 0, pz = G.player?.pos?.z ?? 0;
  if (m === 'orbit') {
    cam.position.set(Math.sin(t * 0.05) * 260, 130, Math.cos(t * 0.05) * 260);
    cam.lookAt(0, 30, 0);
  } else if (m === 'street') {
    cam.position.set(px + 6, 1.7, pz + 10);
    cam.lookAt(px, 2, pz - 30);
  } else if (m === 'high') {
    cam.position.set(px + 40, 90, pz + 40);
    cam.lookAt(px, 0, pz);
  } else if (m === 'vista') {
    cam.position.set(-420, 25, 380);
    cam.lookAt(80, 60, -120);
  }
}

boot().catch((e) => {
  console.error('BOOT FAILED', e);
  document.getElementById('bootmsg').textContent = 'BOOT FAILED: ' + e.message;
});
