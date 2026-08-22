// Tiny event emitter shared across all game systems.
export function createEvents() {
  const map = new Map();
  return {
    on(ev, fn) { (map.get(ev) || map.set(ev, new Set()).get(ev)).add(fn); return fn; },
    off(ev, fn) { map.get(ev)?.delete(fn); },
    emit(ev, ...args) {
      const set = map.get(ev);
      if (!set) return;
      for (const fn of [...set]) {
        try { fn(...args); } catch (e) { console.error(`[events:${ev}]`, e); }
      }
    },
  };
}
