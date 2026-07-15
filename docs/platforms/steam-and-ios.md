# Platform Plan: Steam and iOS

## Steam (primary)

- UE5 client + dedicated servers; Steamworks (or EOS) for auth, friends,
  lobbies, and commerce.
- Nanite, Lumen, and full player counts target PC first.
- Steam Early Access is a natural fit for the MVP → Phase 5 window.

## iOS (follow-on client — honest constraints)

UE5 ships iOS games, but this design needs adaptation, not a straight port:

- **Rendering:** Nanite/Lumen support on iOS is limited to recent high-end
  devices; plan a scalable renderer profile (baked lighting, lower LODs,
  reduced crowd density via Mass LOD).
- **Player counts:** start iOS at the 16–32 tier even after PC reaches 64+.
- **Input/UI:** Common UI from day one so HUD/menus re-skin to touch; driving
  and shooting need dedicated touch schemes and generous aim assist.
- **App Store policy:** all iOS purchases go through Apple IAP (no Stripe in
  app); UGC requires robust moderation, reporting, and blocking to pass
  review; realistic violence affects the age rating (expect 17+).
- **Build size:** App Store cellular-size expectations force aggressive asset
  tiering and on-demand downloads for districts.
- **Cross-play/cross-progression:** account system (EOS) is platform-agnostic
  from day one so the same character works on both stores.

## Sequencing

1. MVP on PC (internal → Steam playtest)
2. Early Access on Steam
3. iOS technical slice (one district, reduced fidelity) once Phase 4
   infrastructure is stable
4. iOS launch after cross-play economy and moderation pass Apple review
