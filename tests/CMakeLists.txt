# qt_standard_project_setup points Windows executables at the top build dir, which would share
# drift's staged effects/ — core-only in packaged builds — and the tests need the full set.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

qt_add_executable(tst_mediaprobe tst_mediaprobe.cpp)
target_link_libraries(tst_mediaprobe PRIVATE Qt6::Test driftengine)
add_test(NAME MediaProbe COMMAND tst_mediaprobe)

qt_add_executable(tst_core tst_core.cpp)
target_include_directories(tst_core PRIVATE ../src)
target_link_libraries(tst_core PRIVATE Qt6::Test driftcore)
target_compile_definitions(tst_core PRIVATE DRIFT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME Core COMMAND tst_core)

# Dafin: planning behind the Beat markers and Auto ducking buttons.
qt_add_executable(tst_beatduck tst_beatduck.cpp)
target_include_directories(tst_beatduck PRIVATE ../src)
target_link_libraries(tst_beatduck PRIVATE Qt6::Test driftcore)
add_test(NAME BeatDuck COMMAND tst_beatduck)

qt_add_executable(tst_keyframeview
    tst_keyframeview.cpp
    ../src/app/editor/sessions/KeyframeCurves.cpp
    ../src/app/editor/sessions/KeyframeRows.cpp
    ../src/app/editor/sessions/KeyframeSelection.cpp
    ../src/app/editor/sessions/KeyframeTimeView.cpp)
target_include_directories(tst_keyframeview PRIVATE ../src)
target_link_libraries(tst_keyframeview PRIVATE Qt6::Test Qt6::Core driftcore)
add_test(NAME KeyframeView COMMAND tst_keyframeview)

qt_add_executable(tst_polylinedraft
    tst_polylinedraft.cpp
    ../src/app/editor/sessions/PolylineDraft.cpp)
target_include_directories(tst_polylinedraft PRIVATE ../src)
target_link_libraries(tst_polylinedraft PRIVATE Qt6::Test Qt6::Core)
add_test(NAME PolylineDraft COMMAND tst_polylinedraft)

# Dafin: GPU encoder -> CPU fallback decisions (pure logic, no GPU needed).
qt_add_executable(tst_hwencodefallback
    tst_hwencodefallback.cpp
    ../src/engine/encode/export/HwEncodeFallback.cpp)
target_include_directories(tst_hwencodefallback PRIVATE ../src)
target_link_libraries(tst_hwencodefallback PRIVATE Qt6::Test Qt6::Core)
add_test(NAME HwEncodeFallback COMMAND tst_hwencodefallback)

qt_add_executable(tst_trackeroverlay tst_trackeroverlay.cpp)
target_include_directories(tst_trackeroverlay PRIVATE ../src)
target_link_libraries(tst_trackeroverlay PRIVATE Qt6::Test Qt6::Core Qt6::Gui)
add_test(NAME TrackerOverlay COMMAND tst_trackeroverlay)

qt_add_executable(tst_curveedit
    tst_curveedit.cpp
    ../src/ui/color/CurveEdit.cpp)
target_include_directories(tst_curveedit PRIVATE ../src)
target_link_libraries(tst_curveedit PRIVATE Qt6::Test Qt6::Core driftengine)
add_test(NAME CurveEdit COMMAND tst_curveedit)

qt_add_executable(tst_colorwheel tst_colorwheel.cpp)
target_include_directories(tst_colorwheel PRIVATE ../src)
target_link_libraries(tst_colorwheel PRIVATE Qt6::Test Qt6::Core Qt6::Gui)
add_test(NAME ColorWheel COMMAND tst_colorwheel)

qt_add_executable(tst_pdfsource tst_pdfsource.cpp)
target_include_directories(tst_pdfsource PRIVATE ../src)
target_link_libraries(tst_pdfsource PRIVATE Qt6::Test driftcore)
add_test(NAME PdfSource COMMAND tst_pdfsource)

qt_add_executable(tst_editorstate
    tst_editorstate.cpp
    # Compiled here rather than in driftapp: they read the DRIFT_VERSION/endpoint defines, which
    # the app sets to release values and this test pins to its own, or they carry QML_ELEMENT.
    ../src/app/online/addons/AddonManager.cpp
    ../src/app/mcp/protocol/McpProtocol.cpp
    ../src/ui/theme/ThemeManager.cpp
    ../src/app/online/marketplace/MarketSourceBrowser.cpp
    ../src/app/online/marketplace/MarketTransport.cpp
    ../src/app/online/market/MarketIdentity.cpp
    # Dafin: the Titles tab's card renderer (image://textstyle), which the app compiles itself.
    ../src/ui/imageproviders/TextStylePreviewImageProvider.cpp
)
target_include_directories(tst_editorstate PRIVATE ../src)
# driftapp holds the multicam provider too, not just the store: the id QML addresses a tile by is
# parsed there. Costs a Qt6::Quick link, but needs no QML engine — requestImage is called directly.
target_link_libraries(tst_editorstate PRIVATE Qt6::Test Qt6::Gui Qt6::Quick Qt6::Multimedia Qt6::Concurrent Qt6::Network driftapp driftcore driftengine)
target_compile_definitions(tst_editorstate PRIVATE
    DRIFT_TEST_DENOISE_MODEL_DIR="${CMAKE_SOURCE_DIR}/models/deepfilternet3"
    DRIFT_VERSION="${PROJECT_VERSION}")
if(DRIFT_WITH_SKIA)
    target_compile_definitions(tst_editorstate PRIVATE DRIFT_WITH_SKIA)
endif()
add_test(NAME EditorState COMMAND tst_editorstate)
drift_copy_effects(tst_editorstate)
drift_copy_audio_effects(tst_editorstate)
drift_copy_transitions(tst_editorstate)

qt_add_executable(tst_playback
    tst_playback.cpp
    ../src/playback/PlaybackClock.cpp
    ../src/playback/AudioOutputChannel.cpp
)
target_include_directories(tst_playback PRIVATE ../src)
target_link_libraries(tst_playback PRIVATE Qt6::Test Qt6::Multimedia driftcore)
add_test(NAME Playback COMMAND tst_playback)

qt_add_executable(tst_layoutstore tst_layoutstore.cpp ../src/app/editor/LayoutStore.cpp)
target_include_directories(tst_layoutstore PRIVATE ../src)
# Qt6::Gui for QScreen: restoring a window means checking it against the displays
# that are actually attached now.
target_link_libraries(tst_layoutstore PRIVATE Qt6::Test Qt6::Gui)
add_test(NAME LayoutStore COMMAND tst_layoutstore)
# Screen-dependent assertions, and saveWindowState deliberately drops the window
# position under Wayland (the compositor owns it there). Pin the platform so the
# test means the same thing on a dev machine as it does headless in CI.
set_tests_properties(LayoutStore PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_projectsafety tst_projectsafety.cpp ../src/app/editor/ProjectSafety.cpp)
target_include_directories(tst_projectsafety PRIVATE ../src)
target_link_libraries(tst_projectsafety PRIVATE Qt6::Test Qt6::Core)
add_test(NAME ProjectSafety COMMAND tst_projectsafety)

qt_add_executable(tst_diagnosticslog tst_diagnosticslog.cpp ../src/app/platform/DiagnosticsLog.cpp)
target_include_directories(tst_diagnosticslog PRIVATE ../src)
target_link_libraries(tst_diagnosticslog PRIVATE Qt6::Test Qt6::Core Qt6::Gui driftcore)
add_test(NAME DiagnosticsLog COMMAND tst_diagnosticslog)

qt_add_executable(tst_updateasset tst_updateasset.cpp ../src/app/online/updates/UpdateAsset.cpp)
target_include_directories(tst_updateasset PRIVATE ../src)
target_link_libraries(tst_updateasset PRIVATE Qt6::Test)
add_test(NAME UpdateAsset COMMAND tst_updateasset)

qt_add_executable(tst_uifont tst_uifont.cpp)
target_link_libraries(tst_uifont PRIVATE Qt6::Test Qt6::Gui)
target_compile_definitions(tst_uifont PRIVATE
    DRIFT_TEST_UI_FONT_DIR="${CMAKE_SOURCE_DIR}/resources/fonts")
add_test(NAME UiFont COMMAND tst_uifont)
# Font matching only, no window: offscreen keeps the result the same on a dev machine as in CI.
set_tests_properties(UiFont PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_addonpackage tst_addonpackage.cpp)
target_include_directories(tst_addonpackage PRIVATE ../src)
target_link_libraries(tst_addonpackage PRIVATE Qt6::Test driftengine)
target_compile_definitions(tst_addonpackage PRIVATE
    DRIFT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME AddonPackage COMMAND tst_addonpackage)

qt_add_executable(tst_addoncatalog
    tst_addoncatalog.cpp
    ../src/app/online/addons/AddonCatalog.cpp
)
target_include_directories(tst_addoncatalog PRIVATE ../src)
target_link_libraries(tst_addoncatalog PRIVATE Qt6::Test Qt6::Core)
add_test(NAME AddonCatalog COMMAND tst_addoncatalog)

qt_add_executable(tst_marketidentity
    tst_marketidentity.cpp
    ../src/app/online/market/MarketIdentity.cpp
)
target_include_directories(tst_marketidentity PRIVATE ../src)
target_link_libraries(tst_marketidentity PRIVATE Qt6::Test Qt6::Core)
target_compile_definitions(tst_marketidentity PRIVATE
    DRIFT_MARKET_API_URL=""
    # Valid hex on purpose: it is the shape that used to be silently decoded.
    DRIFT_MARKET_CLIENT_KEY="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
    DRIFT_VERSION="0")
add_test(NAME MarketIdentity COMMAND tst_marketidentity)

qt_add_executable(tst_marketplace
    tst_marketplace.cpp
    ../src/app/online/market/MarketIdentity.cpp
    ../src/app/online/marketplace/MarketTransport.cpp
    ../src/app/online/marketplace/MarketplaceManifest.cpp
    ../src/app/online/marketplace/MarketSourceBrowser.cpp
    ../src/app/online/marketplace/MarketInstall.cpp
    ../src/app/online/marketplace/StockInstaller.cpp
    ../src/app/online/marketplace/CommunityInstaller.cpp
    ../src/app/online/marketplace/MarketInstallRouter.cpp
    ../src/app/online/assets/DriftAssetStore.cpp
    ../src/app/online/assets/SfxLibrary.cpp
    ../src/app/online/downloads/DownloadCenter.cpp
)
target_include_directories(tst_marketplace PRIVATE ../src)
target_link_libraries(tst_marketplace PRIVATE Qt6::Test Qt6::Core Qt6::Network Qt6::Qml)
target_compile_definitions(tst_marketplace PRIVATE
    DRIFT_MARKET_API_URL=""
    DRIFT_MARKET_CLIENT_KEY="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
    DRIFT_VERSION="0")
qt_add_resources(tst_marketplace "marketplace_default"
    PREFIX "/"
    BASE ../resources
    FILES ../resources/marketplace/default-manifest.json
)
add_test(NAME Marketplace COMMAND tst_marketplace)

qt_add_executable(tst_downloads
    tst_downloads.cpp
    ../src/app/online/downloads/DownloadCenter.cpp
)
target_include_directories(tst_downloads PRIVATE ../src)
target_link_libraries(tst_downloads PRIVATE Qt6::Test Qt6::Core)
add_test(NAME Downloads COMMAND tst_downloads)

qt_add_executable(tst_projectbundle tst_projectbundle.cpp)
target_include_directories(tst_projectbundle PRIVATE ../src)
# zstd directly: the test forges a manifest to exercise the format version gate.
target_link_libraries(tst_projectbundle PRIVATE Qt6::Test driftengine ${DRIFT_ZSTD_TARGET})
add_test(NAME ProjectBundle COMMAND tst_projectbundle)

# Dafin's FFmpeg audio tools (loudnorm, afftdn, silencedetect) on synthetic audio.
qt_add_executable(tst_audiotools tst_audiotools.cpp)
target_include_directories(tst_audiotools PRIVATE ../src)
target_link_libraries(tst_audiotools PRIVATE Qt6::Test driftengine)
add_test(NAME AudioTools COMMAND tst_audiotools)

qt_add_executable(tst_engine
    tst_engine.cpp
    # Diagnostics live in the playback layer but are engine-shaped: a collector plus a
    # plain-text rendering, exactly like DebugReport, and tested alongside it.
    ../src/playback/stats/PlaybackStats.cpp
    ../src/playback/stats/PlaybackDiagnostics.cpp
)
target_include_directories(tst_engine PRIVATE ../src)
target_link_libraries(tst_engine PRIVATE Qt6::Test Qt6::Gui Qt6::OpenGL driftengine driftcore)
target_compile_definitions(tst_engine PRIVATE
    DRIFT_TEST_EFFECTS_DIR="${CMAKE_SOURCE_DIR}/effects"
    # Beauty / makeup packages live in the effects.core addon (drift-addons staging), not the
    # bundled VideoEd/effects tree. Optional so a checkout without the sibling repo still builds.
    DRIFT_TEST_ADDON_EFFECTS_DIR="${CMAKE_SOURCE_DIR}/../drift-addons/staging/effects-core/effects"
    DRIFT_TEST_TRANSITIONS_DIR="${CMAKE_SOURCE_DIR}/transitions"
    DRIFT_TEST_FONTS_DIR="${CMAKE_SOURCE_DIR}/fonts"
    DRIFT_TEST_EMOJI_FONT_DIR="${CMAKE_SOURCE_DIR}/emoji-fonts"
    DRIFT_TEST_AUDIO_EFFECTS_DIR="${CMAKE_SOURCE_DIR}/audio-effects"
    DRIFT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    # Not committed (models/ is gitignored). The denoise tests skip when it is absent so a
    # checkout without the model still passes.
    DRIFT_TEST_DENOISE_MODEL_DIR="${CMAKE_SOURCE_DIR}/models/deepfilternet3"
    # Staged by drift-addons/recipes/fetch-*.py; the model tests skip when absent.
    DRIFT_TEST_VAD_MODEL_DIR="${CMAKE_SOURCE_DIR}/../drift-addons/staging/silero-vad/models/silero-vad"
)
if(DRIFT_WITH_SKIA)
    target_compile_definitions(tst_engine PRIVATE DRIFT_WITH_SKIA)
endif()
add_test(NAME Engine COMMAND tst_engine)
drift_copy_effects(tst_engine)
drift_copy_transitions(tst_engine)

if(DRIFT_WITH_SKIA)
    qt_add_executable(tst_skia tst_skia.cpp)
    target_include_directories(tst_skia PRIVATE ../src)
    # Skia::Skia directly: the test's painters draw with SkCanvas themselves.
    target_link_libraries(tst_skia PRIVATE Qt6::Test Qt6::Gui Qt6::OpenGL driftengine Skia::Skia)
    target_compile_definitions(tst_skia PRIVATE
        DRIFT_TEST_FONTS_DIR="${CMAKE_SOURCE_DIR}/fonts"
        DRIFT_TEST_EMOJI_FONT_DIR="${CMAKE_SOURCE_DIR}/emoji-fonts")
    add_test(NAME Skia COMMAND tst_skia)

    qt_add_executable(tst_vector tst_vector.cpp)
    target_include_directories(tst_vector PRIVATE ../src)
    target_link_libraries(tst_vector PRIVATE Qt6::Test Qt6::Gui Qt6::OpenGL driftengine driftcore)
    target_compile_definitions(tst_vector PRIVATE DRIFT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    add_test(NAME Vector COMMAND tst_vector)
endif()

qt_add_executable(tst_mcp
    tst_mcp.cpp
    ../src/app/mcp/protocol/McpSession.cpp
    ../src/app/platform/RunningInstances.cpp
    ../src/app/mcp/tools/McpCatalog.cpp
    ../src/app/mcp/protocol/McpProtocol.cpp
    ../src/app/mcp/transport/McpHttp.cpp
    ../src/ui/imageproviders/segment/SegmentImageStore.cpp
    ../src/playback/preview/ClipPreviewImageStore.cpp
    ../src/ui/imageproviders/multicam/MulticamImageStore.cpp
    ../src/app/editor/AppController.cpp
    ../src/app/editor/appcontroller/AppControllerClipModel.cpp
    ../src/app/editor/appcontroller/AppControllerInternal.cpp
    ../src/app/editor/appcontroller/AppControllerTimeline.cpp
    ../src/app/editor/appcontroller/AppControllerSubtitleMulticam.cpp
    ../src/app/editor/appcontroller/AppControllerAdjustments.cpp
    ../src/app/editor/appcontroller/AppControllerClipProps.cpp
    ../src/app/editor/appcontroller/AppControllerComposite.cpp
    ../src/app/editor/appcontroller/AppControllerVector.cpp
    ../src/app/editor/appcontroller/AppControllerKeyframes.cpp
    ../src/app/editor/appcontroller/AppControllerEffects.cpp
    ../src/app/editor/appcontroller/AppControllerTracks.cpp
    ../src/app/editor/appcontroller/AppControllerProject.cpp
    ../src/app/editor/appcontroller/AppControllerTextLayers.cpp
    ../src/app/editor/appcontroller/AppControllerDafin.cpp
    ../src/app/editor/appcontroller/AppControllerSpeed.cpp
    ../src/app/editor/appcontroller/AppControllerBeatsDucking.cpp
    ../src/app/editor/appcontroller/AppControllerTemplates.cpp
    ../src/app/mcp/McpController.cpp
    ../src/app/mcp/AgentImageLog.cpp
    ../src/app/editor/PreferencesController.cpp
    ../src/app/GraphicsApiChoice.cpp
    ../src/app/editor/ProjectFileController.cpp
    ../src/app/platform/DiagnosticsLog.cpp
    ../src/app/editor/ProjectSafety.cpp
    ../src/app/bin/AssetPreviewController.cpp
    ../src/app/editor/sessions/SpeedCurveController.cpp
    ../src/app/editor/sessions/CurveEditorController.cpp
    ../src/app/editor/sessions/KeyframeController.cpp
    ../src/app/editor/sessions/KeyframeCurves.cpp
    ../src/app/editor/sessions/KeyframeCurvesModel.cpp
    ../src/app/editor/sessions/KeyframeRows.cpp
    ../src/app/editor/sessions/KeyframeRowsModel.cpp
    ../src/app/editor/sessions/KeyframeSelection.cpp
    ../src/app/editor/sessions/KeyframeTimeView.cpp
    ../src/app/editor/sessions/MaskEditSession.cpp
    ../src/app/editor/sessions/MaskStackModel.cpp
    ../src/app/editor/sessions/MaskingController.cpp
    ../src/app/editor/sessions/SubtitleController.cpp
    ../src/app/editor/sessions/MogrtTitleController.cpp
    ../src/app/online/library/MogrtInfo.cpp
    ../src/app/online/library/MogrtRenderer.cpp
    ../src/app/editor/sessions/SubtitleCueModel.cpp
    ../src/app/editor/sessions/SubtitleCueFilterModel.cpp
    ../src/app/jobs/vision/SegmentationController.cpp
    ../src/app/jobs/vision/SceneDetectController.cpp
    ../src/app/jobs/vision/DepthController.cpp
    ../src/app/jobs/audio/DenoiseController.cpp
    ../src/app/jobs/audio/AudioAlignController.cpp
    ../src/app/jobs/ai/AiToolsController.cpp
    ../src/app/color/ColorController.cpp
    ../src/app/color/ScopesController.cpp
    ../src/app/color/LookThumbnailService.cpp
    ../src/ui/imageproviders/color/ColorImageProvider.cpp
    ../src/app/jobs/vision/RestoreController.cpp
    ../src/app/jobs/vision/FaceTrackingController.cpp
    ../src/app/jobs/vision/StabilizeController.cpp
    ../src/app/jobs/vision/TrackingController.cpp
    ../src/app/jobs/vision/TrackerListModel.cpp
    ../src/app/bin/MediaBinController.cpp
    ../src/app/bin/SourceLabController.cpp
    ../src/app/jobs/audio/TtsController.cpp
    ../src/app/jobs/audio/VoiceLibrary.cpp
    ../src/ui/preview/Outliner3dModel.cpp
    ../src/ui/preview/PreviewController.cpp
    ../src/app/online/addons/AddonCatalog.cpp
    ../src/app/online/addons/AddonManager.cpp
    ../src/app/jobs/JobRegistry.cpp
    ../src/app/online/CloudProviders.cpp
    ../src/app/bin/AssetLibrary.cpp
    ../src/app/bin/BinFolderListModel.cpp
    ../src/app/editor/TimelineClipsModel.cpp
    ../src/ui/timeline/ClipThumbnailStore.cpp
    ../src/playback/PlaybackEngine.cpp
    ../src/playback/PlaybackClock.cpp
    ../src/playback/AudioOutputChannel.cpp
    ../src/playback/preview/CompositorService.cpp
    ../src/playback/stats/PerfLog.cpp
    ../src/playback/stats/PlaybackStats.cpp
    ../src/playback/stats/PlaybackDiagnostics.cpp
    ../src/playback/preview/ClipPreviewPlayer.cpp
    ../src/app/mcp/tools/McpDispatcher.cpp
    ../src/app/mcp/tools/McpDispatcherExtended.cpp
    ../src/ui/theme/ThemeManager.cpp
    ../src/app/mcp/tools/McpEffectAuthoring.cpp
    ../src/app/mcp/protocol/McpValidate.cpp
    ../src/app/mcp/tools/McpMarket.cpp
    ../src/app/online/marketplace/MarketInstall.cpp
    ../src/app/online/marketplace/StockInstaller.cpp
    ../src/app/online/marketplace/MarketSourceBrowser.cpp
    ../src/app/online/marketplace/MarketplaceManifest.cpp
    ../src/app/online/marketplace/CommunityInstaller.cpp
    ../src/app/online/marketplace/MarketInstallRouter.cpp
    ../src/app/online/assets/SfxLibrary.cpp
    ../src/app/online/library/SessionFiles.cpp
    ../src/app/online/downloads/DownloadCenter.cpp
    ../src/app/online/marketplace/MarketTransport.cpp
    ../src/app/online/assets/DriftAssetStore.cpp
    ../src/app/online/market/MarketIdentity.cpp
    ../src/app/mcp/transport/McpServer.cpp
    ../src/app/mcp/transport/McpStdio.cpp
    ../src/app/mcp/transport/McpBridge.cpp
    ../src/app/mcp/transport/McpStdioServer.cpp
)
target_include_directories(tst_mcp PRIVATE ../src)
target_link_libraries(tst_mcp PRIVATE Qt6::Test Qt6::Gui Qt6::Quick Qt6::Network Qt6::Multimedia Qt6::Concurrent driftcore driftengine)
target_compile_definitions(tst_mcp PRIVATE DRIFT_VERSION="${PROJECT_VERSION}"
    DRIFT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME Mcp COMMAND tst_mcp)
drift_copy_effects(tst_mcp)
drift_copy_transitions(tst_mcp)

qt_add_executable(tst_thememanager
    tst_thememanager.cpp
    ../src/ui/theme/ThemeManager.cpp
    ../src/core/base/ZipArchive.cpp
    ../src/engine/platform/AndroidUri.cpp)
target_include_directories(tst_thememanager PRIVATE ../src)
target_compile_definitions(tst_thememanager PRIVATE
    "DRIFT_MAIN_CPP=\"${CMAKE_SOURCE_DIR}/src/app/main.cpp\""
)
target_link_libraries(tst_thememanager PRIVATE Qt6::Test Qt6::Qml Qt6::Quick ZLIB::ZLIB)
qt_add_resources(tst_thememanager "drift_themes"
    PREFIX "/themes"
    BASE "${CMAKE_SOURCE_DIR}/resources/themes"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/themes/dark.json"
        "${CMAKE_SOURCE_DIR}/resources/themes/light.json"
        "${CMAKE_SOURCE_DIR}/resources/themes/graphite.json"
        "${CMAKE_SOURCE_DIR}/resources/themes/frost.json"
        "${CMAKE_SOURCE_DIR}/resources/themes/filmora.json"
        "${CMAKE_SOURCE_DIR}/resources/themes/theme.schema.json"
)
add_test(NAME ThemeManager COMMAND tst_thememanager)
set_tests_properties(ThemeManager PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Runs lupdate and fails if i18n/*.ts would change — i.e. someone added qsTr/tr and
# did not run `cmake --build build --target update_translations`. Skipped where lupdate cannot
# parse QML: there the test would rewrite the checkout's catalogs without their QML strings
# before failing on the diff it caused itself (#167).
if(DRIFT_LUPDATE_HAS_QML)
    add_test(NAME Translations
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DBINARY_DIR=${CMAKE_BINARY_DIR}
            -DCONFIG=$<CONFIG>
            -P ${CMAKE_SOURCE_DIR}/cmake/CheckTranslations.cmake
    )
else()
    message(STATUS "lupdate has no QML support; skipping the Translations test.")
endif()

set(_drift_windowbackdrop_platform)
foreach(_src ${DRIFT_BACKDROP_PLATFORM_SOURCES})
    list(APPEND _drift_windowbackdrop_platform "${CMAKE_SOURCE_DIR}/${_src}")
endforeach()
qt_add_executable(tst_windowbackdrop
    tst_windowbackdrop.cpp
    ../src/ui/theme/backdrop/WindowBackdrop.cpp
    ../src/ui/theme/ThemeManager.cpp
    ../src/core/base/ZipArchive.cpp
    ../src/engine/platform/AndroidUri.cpp
    ${_drift_windowbackdrop_platform}
)
target_include_directories(tst_windowbackdrop PRIVATE ../src)
target_compile_definitions(tst_windowbackdrop PRIVATE
    "DRIFT_MAIN_CPP=\"${CMAKE_SOURCE_DIR}/src/app/main.cpp\""
    ${DRIFT_BACKDROP_PLATFORM_DEFS}
)
target_link_libraries(tst_windowbackdrop PRIVATE Qt6::Test Qt6::Qml Qt6::Quick ZLIB::ZLIB ${DRIFT_BACKDROP_PLATFORM_LIBS})
qt_add_resources(tst_windowbackdrop "drift_themes"
    PREFIX "/themes"
    BASE "${CMAKE_SOURCE_DIR}/resources/themes"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/themes/dark.json"
        "${CMAKE_SOURCE_DIR}/resources/themes/light.json"
        "${CMAKE_SOURCE_DIR}/resources/themes/graphite.json"
        "${CMAKE_SOURCE_DIR}/resources/themes/frost.json"
        "${CMAKE_SOURCE_DIR}/resources/themes/filmora.json"
        "${CMAKE_SOURCE_DIR}/resources/themes/theme.schema.json"
)
add_test(NAME WindowBackdrop COMMAND tst_windowbackdrop)
set_tests_properties(WindowBackdrop PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_workspacestore tst_workspacestore.cpp ../src/app/editor/WorkspaceStore.cpp)
target_include_directories(tst_workspacestore PRIVATE ../src)
target_link_libraries(tst_workspacestore PRIVATE Qt6::Test Qt6::Core)
qt_add_resources(tst_workspacestore "workspace_presets"
    PREFIX "/workspaces"
    BASE ../resources/workspaces
    FILES
        ../resources/workspaces/filmora.json
        ../resources/workspaces/edit.json
        ../resources/workspaces/edit-portrait.json
        ../resources/workspaces/source-lab.json
        ../resources/workspaces/audio.json
        ../resources/workspaces/record.json
        ../resources/workspaces/subtitles.json
        ../resources/workspaces/ai.json
        ../resources/workspaces/color.json
        ../resources/workspaces/keyframes.json
        ../resources/workspaces/masking-tracking.json
        ../resources/workspaces/3d.json
)
add_test(NAME WorkspaceStore COMMAND tst_workspacestore)

# Online asset providers, search, credentials, downloads and the local asset database.
set(DAFIN_ASSET_LIBRARY_SOURCES
    ../src/app/online/library/AssetProvider.cpp
    ../src/app/online/library/AssetSearchService.cpp
    ../src/app/online/library/AuthManager.cpp
    ../src/app/online/library/DownloadManager.cpp
    ../src/core/project/sources/LottieJson.cpp
    ../src/app/online/library/LocalAssetDatabase.cpp
    ../src/app/online/library/ProviderRegistry.cpp
    ../src/app/online/library/SecretStore.cpp
    ../src/app/online/library/SessionFiles.cpp
    ../src/app/online/library/ThumbnailCache.cpp
    ../src/app/online/library/providers/DafinLibraryProvider.cpp
    ../src/app/online/library/providers/DafinSoundEffectsProvider.cpp
    ../src/app/online/library/providers/CatalogProvider.cpp
    ../src/app/online/library/providers/FreesoundProvider.cpp
    ../src/app/online/library/providers/JamendoProvider.cpp
    ../src/app/online/library/providers/OpenverseProvider.cpp
    ../src/app/online/library/providers/PexelsProvider.cpp
    ../src/app/online/library/providers/PixabayProvider.cpp)
qt_add_executable(tst_onlineassets tst_onlineassets.cpp ${DAFIN_ASSET_LIBRARY_SOURCES})
target_include_directories(tst_onlineassets PRIVATE ../src)
target_compile_definitions(tst_onlineassets PRIVATE DAFIN_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(tst_onlineassets PRIVATE Qt6::Test Qt6::Core Qt6::Network Qt6::Gui Qt6::Quick)
if(WIN32)
    target_link_libraries(tst_onlineassets PRIVATE crypt32)
endif()
add_test(NAME OnlineAssets COMMAND tst_onlineassets)

# Long-press drag reordering of the header, preview transport and timeline toolbar buttons.
find_package(Qt6 COMPONENTS QuickTest QUIET)
if(TARGET Qt6::QuickTest)
    set(_barreorder_imports "${CMAKE_CURRENT_BINARY_DIR}/barreorder_imports")
    foreach(_file BarReorder.qml BarReorderHandle.qml BarReorderLayer.qml BarSlot.qml)
        configure_file(../src/qml/components/controls/${_file} "${_barreorder_imports}/Drift/${_file}" COPYONLY)
    endforeach()
    configure_file(qml/barreorder/stub/Theme.qml "${_barreorder_imports}/Drift/Theme.qml" COPYONLY)
    configure_file(qml/barreorder/stub/qmldir "${_barreorder_imports}/Drift/qmldir" COPYONLY)
    qt_add_executable(tst_barreorder tst_barreorder.cpp)
    target_compile_definitions(tst_barreorder PRIVATE
        BARREORDER_IMPORTS="${_barreorder_imports}"
        QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml/barreorder")
    target_link_libraries(tst_barreorder PRIVATE Qt6::QuickTest Qt6::Test Qt6::Quick Qt6::Qml)
    add_test(NAME BarReorder COMMAND tst_barreorder)
    set_tests_properties(BarReorder PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
set_tests_properties(OnlineAssets PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DAFIN_PLAIN_SECRETS=1")

qt_add_executable(tst_searchindex tst_searchindex.cpp ../src/app/editor/SearchIndex.cpp)
target_include_directories(tst_searchindex PRIVATE ../src)
target_link_libraries(tst_searchindex PRIVATE Qt6::Test Qt6::Core)
target_compile_definitions(tst_searchindex PRIVATE DRIFT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME SearchIndex COMMAND tst_searchindex)

qt_add_executable(tst_tracking tst_tracking.cpp ../src/engine/tracking/MotionTracking.cpp)
target_include_directories(tst_tracking PRIVATE ../src)
target_link_libraries(tst_tracking PRIVATE Qt6::Test Qt6::Core Qt6::Gui)
add_test(NAME Tracking COMMAND tst_tracking)

qt_add_executable(tst_transform3d tst_transform3d.cpp)
target_include_directories(tst_transform3d PRIVATE ../src)
target_link_libraries(tst_transform3d PRIVATE Qt6::Test Qt6::Gui driftengine driftcore)
add_test(NAME Transform3d COMMAND tst_transform3d)

# Dafin: .mogrt template titles — the project-file state, reading a template's controls, and the
# converter queue against a stand-in script (Unix only).
qt_add_executable(tst_mogrt
    tst_mogrt.cpp
    ../src/app/online/library/MogrtInfo.cpp
    ../src/app/online/library/MogrtRenderer.cpp)
target_include_directories(tst_mogrt PRIVATE ../src)
target_link_libraries(tst_mogrt PRIVATE Qt6::Test Qt6::Core Qt6::Gui driftcore ZLIB::ZLIB)
add_test(NAME Mogrt COMMAND tst_mogrt)

# Pure C++ (no Qt): the automatic preview quality's step rules.
add_executable(tst_adaptivepolicy tst_adaptivepolicy.cpp)
target_include_directories(tst_adaptivepolicy PRIVATE ../src)
add_test(NAME AdaptivePolicy COMMAND tst_adaptivepolicy)

# Pure C++ (no Qt): when a start-up goes to safe mode.
add_executable(tst_safemode tst_safemode.cpp)
target_include_directories(tst_safemode PRIVATE ../src)
add_test(NAME SafeMode COMMAND tst_safemode)
