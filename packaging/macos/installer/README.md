# macOS drag-and-drop installer

The DMG opens directly to the branded Finder window: **Mirdain Forward → Applications**.
There is no extra launcher or installer helper. Both icons are real Finder items;
the Forward icon stays visible after copying. Open Forward from Applications after
installation to start the existing Qt menu-bar app. 

`cmake/MacPackaging.cmake` uses `background.tiff` and `Finder.DS_Store` for the release
DMG. The existing app, license files, Applications link, signing, and update behavior
are preserved. Licenses are present but hidden from the two-icon installation view.

`render-background.py` uses the website’s near-black `#010305` background, a subtle
lighter grid, a static `#e967c8` pink wave illuminating its lines, ice `#b2cee3`
accents, and muted `#a8b7c5` text. Small ice nameplates
keep Finder’s native black filenames readable. It renders the static grid artwork using Forward's existing
Aileron fonts and logo. The TIFF contains normal and Retina resolutions. Change the
artwork here when the new brand image is ready; retain the icon positions and window
size or update the layout together.

For a local packaging preview of an existing app bundle:

```sh
python3 -m venv dist/dmg-tools
dist/dmg-tools/bin/pip install dmgbuild Pillow
dist/dmg-tools/bin/python packaging/macos/installer/render-background.py
dist/dmg-tools/bin/python packaging/macos/installer/preview.py '/Applications/Mirdain Forward.app'
```

Default output: `dist/mirdain-forward-local-grid.dmg`. The reviewed local build is
`dist/Mirdain Forward Pink Clean.dmg` (created with `--output`). The preview script also refreshes the
saved Finder layout, with the same hidden background location used by CPack. It
refuses to replace an existing output; use `--output` for another iteration.

This packages the supplied Forward app unchanged and does not install or launch
it. It does not compile the Qt app, publish a release, or modify the website.
Normal CMake/CPack builds continue to package their freshly built Qt app.

