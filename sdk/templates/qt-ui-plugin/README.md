# External Qt UI plugin

Copy this entire directory outside the Cordis repository. With the matching x64 MSVC developer environment and Qt 5.15.2 development installation:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL -DCMAKE_PREFIX_PATH="C:/CordisSdk;C:/Qt/5.15.2/msvc2019_64"
cmake --build build
```

Use the configuration installed by the SDK producer. The result is `build/module/external_page/{external_page.dll,module.json}`. Keep both files together. The host must declare the actual plugin ID and required `ui.pages` service through its existing Module/Session assembly; copying the DLL into a directory does not register it.

The factory source includes `<source-stem>.moc` and uses `FILE "cordis_plugin_metadata.json"`. `cordis_add_qt_ui_plugin` generates metadata from the SDK version and actual compiler/configuration and generates the digest after linking. Rename the source and its moc include together. Extra implementation sources can be supplied with `SOURCES`; put the factory metadata in the single `SOURCE` translation unit. Public headers used by that source must be supplied in `SOURCES` when they affect generated moc output.

Change module/plugin IDs in CMake and the factory together. These IDs are application contracts, not unload approvals. The template has no persistence or background work; the page is owned by the context's effect and is removed at cleanup. Add real plugin data contracts explicitly rather than treating page text or a hot-replacement snapshot as durable storage.

See the installed `share/Cordis/sdk-consumer.md` for ownership, compatibility and diagnostics.
