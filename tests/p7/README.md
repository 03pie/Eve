# P7 external-plugin acceptance

The integration executable consumes actual external DLL/manifest paths. It links the application's `cordis_external_workbench`, uses its original Module/Kernel/Session/Ui lifecycle, and interacts with the reader's real Qt controls. It does not link the reader implementation, replace its data model, register a second factory, approve Native unloading, or change a production diagnostic for testing.

Each CTest case has an independent process and unique retained artifact directory. `QPdfWriter` creates a three-page real PDF locally; the Windows PDF worker must actually produce non-empty, different page images. The persistence case starts this executable twice. The upgrade case starts three independent processes: version 1.0 writes and exits naturally, version 1.1 reopens the same data and saves bookmarks/annotations, and a third version 1.1 process verifies the real controls restore those saved marks without rewriting the library. Qt libraries remain resident until their process exits. This is cross-process durable-data compatibility, not live Qt unloading or a hot-replacement-state claim.

The fixture owns Workbench until `close()` completes, checks that pages disappear, and only then closes its scheduler. A failing assertion still attempts that normal close; no process is killed to manufacture an exit. Child stdout/stderr, actual PID/exit, library data, generated PDF and a real workbench PNG are retained below the artifact path printed by each case.

Configure the main build with `CORDIS_P7_ACCEPTANCE=ON` and three absolute manifest paths:

```text
-DP7_READER_V1_MANIFEST=<external-v1>/module.json
-DP7_READER_V2_MANIFEST=<external-v2>/module.json
-DP7_TEMPLATE_MANIFEST=<external-sdk-template>/module.json
```

Run `ctest --test-dir <build> -R ^p7.external_ --output-on-failure -j1` with the matching host/Qt/Ela runtime DLL directories on PATH. Tests use the offscreen Qt platform, real controls and the actual Windows PDF interpreter; this is not a human desktop acceptance claim.

For an additional local reading sample, run `cordis_p7_acceptance paper <v1-manifest> <v1-manifest> <artifact-parent> <absolute-paper.pdf>`. This uses the supplied real PDF for a two-process notes/page reopen and preserves rendered-page/workbench PNGs. Such third-party paper content and the resulting library object are local evidence only: do not redistribute them with the SDK or runtime package. The regular seven cases only use the reproducible generated fixture.

## Actual validation, 2026-11-24

The final seven cases passed together, using actual external 1.0.0 and 1.1.0 manifests and the installed SDK template. The final outer command PID was 46072, exit 0; logs and receipts are in `build/p7-e2e-check-20261124-1/run-final-reopen-verified.*`. A separate Windows-platform run in that command also completed all three upgrade processes, including PID 12736 verifying persisted bookmarks and annotations through the real controls. It preserved the saved library byte for byte. The earlier third-process checker failure is retained: the assertion incorrectly expected the quote in the list text, while the implementation correctly puts the note in the text and quote in its tooltip. The corrected test verifies both.

The initial 1.0.0 render failure is also retained. WinRT required native Windows separators at its file API boundary; the corrected external 1.0.0 build then passed all relevant cases. No earlier failed record was rewritten. Additional local real-paper sessions rendered two distinct pages of a 12-page PDF, saved page/zoom/notes, reopened them in a fresh process and produced readable Windows-platform workbench screenshots. Offscreen-platform control fonts are blank on this machine, so those offscreen PNGs are not used as visual acceptance evidence.

The maintained evidence index is `build/p7-e2e-check-20261124-1/evidence-index.json`. The tests cover this same-toolchain integration and durable data; they do not claim general C++ ABI stability, Native unload qualification, or human acceptance.
