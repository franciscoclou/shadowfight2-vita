# Later

## Remove what only works online

Hide everything the port can never do (no network, no store), keeping the
story and anything playable offline. Only where it cannot break progress.

- **Offers and prompts** are quests in `quests.xml` (539 of them):
  `StarterPack*`, `promo_*`, `PROMO_XMAS14_*`, `RatingOffer*`,
  `ShowAdVideo*`, `RewardedMapVideo`, `ShowRewardedVideoNoNetwork`,
  `Tapjoy*`, `FBFirstTimeOffer`, `SurvivalPayReminder`, the May "special
  offer" reminders, the arena/volcano unlock messages.
  Approach: drop them as the game reads `quests.xml` (file.c), and accept
  that file in the save-hash check (0x4a1f849c), or rewrite its .hash
  the way packs.c does. Check each removed quest is not a prerequisite of
  a story quest first.
- **Buttons**: the gem shop "+" in the top bar, the arena toggle, the
  Shadow Fight 3 toggle in the dojo. Each needs its builder found and the
  button hidden (setVisible, vtable 0x90) after it is made.
- Keep: story banners and everything reachable offline.

## Ideas for the controls (not started)

- ✕ / ○ confirm and cancel in dialogs; d-pad + ✕ to move through menus
  (a PSTV with no touch screen).
- Remappable buttons in controls.txt, or presets.
- START pauses outside fights too; right stick for dodges; rear touch for
  magic; hold-to-repeat attacks; stick deadzone in Advanced Settings.
