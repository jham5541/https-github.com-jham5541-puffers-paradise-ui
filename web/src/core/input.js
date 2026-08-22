// Keyboard + mouse input state. Pointer lock is managed by player camera.
export function createInput(dom) {
  const keys = new Set();
  const state = {
    keys,
    down: (k) => keys.has(k),
    // one-frame pressed flags
    pressed: new Set(),
    mouse: { dx: 0, dy: 0, wheel: 0, lmb: false, rmb: false, lmbPressed: false, rmbPressed: false },
    pointerLocked: false,
    requestLock() { dom.requestPointerLock?.(); },
    exitLock() { document.exitPointerLock?.(); },
    endFrame() {
      state.pressed.clear();
      state.mouse.dx = 0; state.mouse.dy = 0; state.mouse.wheel = 0;
      state.mouse.lmbPressed = false; state.mouse.rmbPressed = false;
    },
  };
  window.addEventListener('keydown', (e) => {
    if (e.repeat) return;
    keys.add(e.code); state.pressed.add(e.code);
    if (['Space', 'Tab'].includes(e.code)) e.preventDefault();
  });
  window.addEventListener('keyup', (e) => keys.delete(e.code));
  window.addEventListener('blur', () => keys.clear());
  dom.addEventListener('mousedown', (e) => {
    if (e.button === 0) { state.mouse.lmb = true; state.mouse.lmbPressed = true; }
    if (e.button === 2) { state.mouse.rmb = true; state.mouse.rmbPressed = true; }
  });
  window.addEventListener('mouseup', (e) => {
    if (e.button === 0) state.mouse.lmb = false;
    if (e.button === 2) state.mouse.rmb = false;
  });
  window.addEventListener('mousemove', (e) => {
    if (state.pointerLocked) { state.mouse.dx += e.movementX; state.mouse.dy += e.movementY; }
  });
  window.addEventListener('wheel', (e) => { state.mouse.wheel += Math.sign(e.deltaY); }, { passive: true });
  dom.addEventListener('contextmenu', (e) => e.preventDefault());
  document.addEventListener('pointerlockchange', () => {
    state.pointerLocked = document.pointerLockElement === dom;
  });
  return state;
}
