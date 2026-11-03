# Classic Studio G

All-in-one vocal chain for Logic Pro (Audio Unit). 16 hardware-style modules in two rows,
every module can be dragged to a new place in the chain, and the window resizes by dragging
its bottom-right corner.

## Build and install (no coding tools needed on your Mac)

1. Create a new GitHub repository and upload everything in this folder
   (keep the folder structure: `CMakeLists.txt`, `Source/`, `.github/workflows/build.yml`).
2. Open the repository's **Actions** tab, choose **Build Classic Studio G**, press **Run workflow**.
   It takes about 10 minutes.
3. When it finishes, open the run and download the **ClassicStudioG-mac** artifact. Unzip it.
4. Copy `Classic Studio G.component` into `~/Library/Audio/Plug-Ins/Components/`
   (in Finder: Go menu, hold Option, choose Library).
5. If macOS says the file is damaged or from an unidentified developer, open Terminal and run:
   `xattr -cr ~/Library/Audio/Plug-Ins/Components/"Classic Studio G.component"`
6. Restart Logic Pro. If it was open, go to Logic Pro, Settings, Plug-in Manager, and press **Reset & Rescan Selection**.
7. Add it on a vocal track: Audio FX slot, Audio Units, Minnkhai, Classic Studio G.

## Files

- `Source/Params.h`  knobs and their value mappings
- `Source/Dsp.h`  the audio engine for every module
- `Source/PluginProcessor.*`  plugin glue, chain order, saving and loading
- `Source/PluginEditor.cpp`  the hardware-style window
