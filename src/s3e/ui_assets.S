/* The port's UI images (src/assets, made by scripts/make_ui_art.py),
 * linked in so ui.c can serve them as files. Paths are found through
 * the -I src include path. */
    .section .rodata.sf_assets, "a"

    .macro asset name, file
    .balign 4
    .global sf_asset_\name, sf_asset_\name\()_end
sf_asset_\name:
    .incbin "\file"
sf_asset_\name\()_end:
    .endm

    asset controls_help, "assets/controls_help.png"
    asset pause_controls, "assets/pause_controls.png"
    asset pause_selected, "assets/pause_selected.png"
    asset watermark, "assets/watermark.png"

    .section .note.GNU-stack, "", %progbits
