# External Qt UI plugin

After installing the SDK, copy its `share/Eve/templates/qt-ui-plugin` directory to your project. The installed CMakeLists.txt pins the SDK producer version. Build with the matching x64 MSVC developer environment and Qt 5.15.2 development installation.

```powershell
$sdkPrefix = 'C:/path/to/eve-sdk'
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL "-DCMAKE_PREFIX_PATH=$sdkPrefix;$env:EVE_QT5152_ROOT"
cmake --build build
```

Use the configuration installed by the SDK producer. The result is `build/module/external_page/{external_page.dll,module.json}`. Keep both files together. Start the workbench with `--module <path-to-module.json>` in a writable workspace. The module declares the plugin ID, and the plugin depends on the host's `ui.pages` service.

The factory source includes `<source-stem>.moc` and uses `FILE "eve_plugin_metadata.json"`. `eve_add_qt_ui_plugin` generates metadata from the SDK version and actual compiler/configuration and generates the digest after linking. Rename the source and its moc include together. Extra implementation sources can be supplied with `SOURCES`; put the factory metadata in the single `SOURCE` translation unit. Public headers used by that source must be supplied in `SOURCES` when they affect generated moc output.

Set the module and plugin IDs in CMake. The helper passes `PLUGIN_ID` to the factory as the private string literal `EVE_QT_UI_PLUGIN_ID` and writes the same value to metadata and the manifest. The context owns the page and removes it at cleanup. Define persistent data and background work explicitly when extending the template.

See the installed `share/Eve/sdk-consumer.md` for ownership, compatibility and diagnostics.
