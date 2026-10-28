# Later

## Remove what only works online (done; see quests.c and patches.c)

Done: offer, ad, Facebook, rating, news, Tapjoy and raid-recruiting quests
off; energy dialog without "GET FREE"; settings as on Amazon; no top-bar
"+", no shop "Free" tab, no map raid toggle. controls.txt remove_online.

Left on purpose:
- The shop's "Payment" tab (real-money gems and coins). Every store build
  has it, so there is no tested variant without it, and quests open it.
- The pack download quests (packs are installed, so they never fire), the
  GDPR consent and the version check (never fire offline), and quests that
  need a real purchase or a raid to have happened.

## Ideas for the controls (not started)

- ✕ / ○ confirm and cancel in dialogs; d-pad + ✕ to move through menus
  (a PSTV with no touch screen).
- Remappable buttons in controls.txt, or presets.
- START pauses outside fights too; right stick for dodges; rear touch for
  magic; hold-to-repeat attacks; stick deadzone in Advanced Settings.
