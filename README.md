# NMSSaveExplorer-Qt

Cross-platform Qt/C++ save explorer and editor for No Man's Sky with a traditional, compact desktop UI.

## Description
NMSSaveExplorer-Qt focuses on browsing, inspecting, and editing save data with Qt Widgets. It includes a JSON explorer, inventory editors, and supporting tooling for decoding/encoding `.hg` saves while preserving file integrity.

## Features
- Open your save files and see them in a clean, familiar desktop layout.
- Browse and edit data with a structured tree view and an easy-to-read editor.
- Manage inventories for ships and multitools, with clear slots and highlights.
- Adjust currencies, expedition progress, and settlement details in dedicated panels.
- Keep save integrity intact while reading and writing changes.
- Uses built-in catalogs for item names and visuals.

## Build (Qt6)

```bash
cmake -S . -B build
cmake --build build
```

Qt6 (Widgets + Xml) is required. On Windows, use the Qt installer, choose Custom Install, and select the `msvc2022_64` kit (or your MSVC version). Add the Qt `bin` folder (e.g. `C:\Qt\6.10.1\msvc2022_64\bin`) to your PATH.

## Cross-platform notes
- Windows: install MSVC Build Tools + Qt for MSVC; after building, run `windeployqt` on the built exe to bundle Qt DLLs.
- macOS: install Xcode + Qt; use `macdeployqt` to bundle frameworks (codesign/notarize for distribution).
- Linux: install GCC/Clang + Qt; distribute with system Qt packages or bundle (AppImage/Flatpak/Snap).

## Resources
- Runtime assets live in `src/resources`.
- If you bundle assets elsewhere, set `NMS_SAVE_EXPLORER_RESOURCES` to the resources root directory.
- To bundle assets into resource libraries/binaries (Windows default, optional on Linux), pass `-DNMS_RESOURCE_LIBS=ON`.

## Packaging (portable)

### Windows (zip)
```bash
cmake -S . -B winbuild
cmake --build winbuild --config Release
cmake --install winbuild --config Release --prefix winbuild/package
windeployqt winbuild/package/bin/NMSSaveExplorer-Qt.exe
cmake -E tar "cfv" winbuild/NMSSaveExplorer-Qt-windows.zip --format=zip winbuild/package
```

### macOS (dmg)
```bash
cmake -S . -B build
cmake --build build --config Release
cmake --install build --config Release --prefix build/package
macdeployqt build/package/NMSSaveExplorer-Qt.app -dmg
```

### Linux (AppImage)
```bash
cmake -S . -B build
cmake --build build --config Release
cmake --install build --config Release --prefix build/AppDir/usr
linuxdeployqt build/AppDir/usr/bin/NMSSaveExplorer-Qt -appimage
```

## Notes
- `src/MainWindow.cpp` builds a classic tree + detail tab layout.
- This is a UI shell; wire up data and actions as you go.
