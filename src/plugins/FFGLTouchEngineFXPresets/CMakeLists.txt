if (APPLE)
    # On macOS, FFGL plugins must be .bundle (loadable bundles)
    add_library(FFGLTouchEngineFXPresets MODULE)
    set_target_properties(FFGLTouchEngineFXPresets PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "bundle"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/BundleInfo.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "FFGLTouchEngineFXPresets"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.evanclark.FFGLTouchEngineFXPresets"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    )
else()
    add_library(FFGLTouchEngineFXPresets SHARED)
endif()

# Reuses the whole FX plugin class; FFGLTE_NO_PLUGININFO strips that file's
# CFFGLPluginInfo/CFFGLThumbnailInfo singletons so this target's own (in
# TouchEngineFXPresets.cpp) are the only ones in the module.
target_compile_definitions(FFGLTouchEngineFXPresets PRIVATE FFGLTE_NO_PLUGININFO)

target_sources(FFGLTouchEngineFXPresets PRIVATE
    TouchEngineFXPresets.h
    TouchEngineFXPresets.cpp
    ../FFGLTouchEngineFX/TouchEngineFX.h
    ../FFGLTouchEngineFX/TouchEngineFX.cpp
    ../../lib/FFGL/FFGLSDK.h
    ../../lib/FFGL/FFGLSDK.cpp
    ../shared/TouchEnginePluginBase.h
    ../shared/TouchEnginePluginBase.cpp
    ../shared/TouchEnginePresetRecall.cpp
    ../shared/PresetMorph.h
    ../shared/PresetMorph.cpp
)

if (WIN32)
    target_link_directories(FFGLTouchEngineFXPresets PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/TouchEngine
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/Glew
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/Spout
    )
    target_link_libraries(FFGLTouchEngineFXPresets PRIVATE
        Spout_static.lib
        TouchEngine.lib
        glew32s.lib
    )
endif()
if (APPLE)
    # Link TouchEngine.framework from lib/TouchEngine
    set(TOUCHENGINE_FRAMEWORK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/TouchEngine/TouchEngine.framework")
    target_link_libraries(FFGLTouchEngineFXPresets PRIVATE
        "${TOUCHENGINE_FRAMEWORK_PATH}"
        "-framework OpenGL"
        "-framework Metal"
        "-framework IOSurface"
        "-framework CoreFoundation"
        "-framework QuartzCore"
        "-framework Foundation"
    )
    # Framework search path for linking, and rpath for runtime inside the bundle
    # Disable Xcode's own code signing — our post-build script handles it
    set_target_properties(FFGLTouchEngineFXPresets PROPERTIES
        XCODE_ATTRIBUTE_FRAMEWORK_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/TouchEngine"
        XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@loader_path/../Frameworks"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
        BUILD_RPATH "@loader_path/../Frameworks"
        INSTALL_RPATH "@loader_path/../Frameworks"
    )
    # Copy TouchEngine.framework into the bundle, sign everything, copy to Release
    set(SIGN_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/sign_bundle.sh")
    add_custom_command(TARGET FFGLTouchEngineFXPresets POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:FFGLTouchEngineFXPresets>/Contents/Frameworks"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${TOUCHENGINE_FRAMEWORK_PATH}"
            "$<TARGET_BUNDLE_DIR:FFGLTouchEngineFXPresets>/Contents/Frameworks/TouchEngine.framework"
        COMMAND bash "${SIGN_SCRIPT}" "$<TARGET_BUNDLE_DIR:FFGLTouchEngineFXPresets>"
        # Copy final bundle to top-level Release directory
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_BINARY_DIR}/Release"
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "${CMAKE_BINARY_DIR}/Release/FFGLTouchEngineFXPresets.bundle"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_BUNDLE_DIR:FFGLTouchEngineFXPresets>"
            "${CMAKE_BINARY_DIR}/Release/FFGLTouchEngineFXPresets.bundle"
    )
    target_include_directories(FFGLTouchEngineFXPresets PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/metal-cpp
    )
    # TEMetal.h uses #import <Metal/Metal.h> which requires Obj-C++
    set_source_files_properties(
        TouchEngineFXPresets.cpp
        ../FFGLTouchEngineFX/TouchEngineFX.cpp
        ../shared/TouchEnginePluginBase.cpp
        ../shared/TouchEnginePresetRecall.cpp
        ../../lib/FFGL/FFGLSDK.cpp
        PROPERTIES COMPILE_FLAGS "-x objective-c++"
    )
endif()

target_include_directories(FFGLTouchEngineFXPresets PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../../include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../lib>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../plugins/shared>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(FFGLTouchEngineFXPresets PUBLIC OpenGL::GL)
