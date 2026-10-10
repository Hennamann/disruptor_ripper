## To-do list
- Finish the partly understood tables dumped in `tables.txt`: door/lift controllers (32 bytes, linked to area
  objects by type id; their 16 byte sub-records hold speed, direction, target states and a key index),
  switchable objects (12 bytes), effect emitters (36 bytes), the 16 byte table after them, and the area scripts
- Animation sequences: `Set NN - frames.txt` lists the RLE frames in table order and the object type records with
  their per-direction frame bytes; the state machine in FUN_800267f0 (3,600 lines) that selects them is not decoded

## Answered
- Entity "types": the data has no names. The type definition's class byte is the AI movement mode (0 static,
  1/2 random turning, 3 patrol path, 4 free movement, 5 rotating sweep) and the look comes from the sprite set;
  `entities.txt` and `typedefs.txt` expose both.
- Music is mono: the streamer drives a single SPU voice (two voices alternate only for cross-fading between
  tracks). The "B" variants are separate arrangements of each song (different lengths), not a second channel.
- Weapon and psyonic sound sample rates come from the descriptor tables inside their blocks.
- The menu font is a 46 glyph 12x14 8bpp strip inside each title block (`Title/Variant N/Menu font 12x14.png`),
  with an 8x8 companion font; the in-level fonts live in the level data.
- The 0x11000 byte block at header entry 0x4F0 is a filler pattern used as a placeholder weapon block.
- Mission names are stored in the title blocks' menu string table (`Text/`).
