# KikiEmu launcher icon

`kikiemu-icon.png` is the transparent square master; `kikiemu.ico` contains
16/24/32/48/64/128/256-pixel PNG frames for the native Windows applications,
installer and shortcuts. Native CLI/desktop resource integration is implemented;
installer and installed shortcut acceptance is pending.
No host desktop icon or Android wallpaper was changed.

Created 2026-09-30 using the built-in image_gen tool via the imagegen skill,
not the API/CLI fallback. This is AI-generated artwork from an original brief,
not a copied Android mascot or third-party character. The project offers this
asset under GPL-2.0-or-later; this is not a promise about copyright protection
or a completed distribution license audit.

The `.ico` is a format/size conversion of this master, preserving transparency:

```powershell
.\tools\build_kikiemu_icon.ps1 -OutputIco .\build\kikiemu-icon-rebuilt.ico
```

Generation prompt (verbatim):

> Use case: illustration-story. Asset type: a polished Windows desktop application icon for KikiEmu, square format. Primary request: an adorable short-haired girl warmly hugging a small ORIGINAL robot in a close-up portrait, pastel pink and peach color palette. Style: clean cute manga-inspired illustration, rounded shapes, crisp readable silhouette and bold enough outlines to remain legible at 32x32. Composition: faces and affectionate hug fill the center with a modest transparent safety margin, shoulders-up portrait; both girl and robot fully recognizable. Girl: short dark bob haircut, gentle smile, blush, simple pink top. Robot: ORIGINAL cream-and-coral rounded rectangular head and body, two expressive teal eyes, small rounded ear discs, no antennae and no resemblance to Google's green Android mascot. Background outside the silhouette genuinely transparent. No text, no letters, no logos, no watermark, no frame, no icon mockup, no other subjects.
