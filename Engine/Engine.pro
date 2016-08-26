# ***** BEGIN LICENSE BLOCK *****
# This file is part of Natron <http://www.natron.fr/>,
# Copyright (C) 2016 INRIA and Alexandre Gauthier-Foichat
#
# Natron is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# Natron is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Natron.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
# ***** END LICENSE BLOCK *****

TARGET = Engine
TEMPLATE = lib
CONFIG += staticlib
# Cairo is still the default renderer for Roto
!enable-osmesa {
   CONFIG += enable-cairo
}
CONFIG += moc
CONFIG += boost qt python shiboken pyside
enable-cairo: CONFIG += cairo
QT += core network
greaterThan(QT_MAJOR_VERSION, 4): QT += concurrent

!noexpat: CONFIG += expat

# Do not uncomment the following: pyside requires QtGui, because PySide/QtCore/pyside_qtcore_python.h includes qtextdocument.h
#QT -= gui

CONFIG += libmv-flags openmvg-flags glad-flags libtess-flags

include(../global.pri)

log {
    DEFINES += NATRON_LOG
}

precompile_header {
  #message("Using precompiled header")
  # Use Precompiled headers (PCH)
  # we specify PRECOMPILED_DIR, or qmake places precompiled headers in Natron/c++.pch, thus blocking the creation of the Unix executable
  PRECOMPILED_DIR = pch
  PRECOMPILED_HEADER = pch.h
}

#OpenFX C api includes and OpenFX c++ layer includes that are located in the submodule under /libs/OpenFX
INCLUDEPATH += $$PWD/../libs/OpenFX/include
DEPENDPATH  += $$PWD/../libs/OpenFX/include
INCLUDEPATH += $$PWD/../libs/OpenFX_extensions
DEPENDPATH  += $$PWD/../libs/OpenFX_extensions
INCLUDEPATH += $$PWD/../libs/OpenFX/HostSupport/include
DEPENDPATH  += $$PWD/../libs/OpenFX/HostSupport/include
INCLUDEPATH += $$PWD/..
INCLUDEPATH += $$PWD/../Global
INCLUDEPATH += $$PWD/../libs/SequenceParsing

INCLUDEPATH += $$PWD/../google-breakpad/src

# hoedown
INCLUDEPATH += $$PWD/../libs/hoedown/src

#To overcome wrongly generated #include <...> by shiboken
INCLUDEPATH += $$PWD
INCLUDEPATH += $$PWD/NatronEngine
DEPENDPATH += $$PWD/NatronEngine
DEPENDPATH += $$PWD/../Global

SOURCES += \
    AbortableRenderInfo.cpp \
    AppInstance.cpp \
    AppManager.cpp \
    AppManagerPrivate.cpp \
    Backdrop.cpp \
    Bezier.cpp \
    BezierCP.cpp \
    BlockingBackgroundRender.cpp \
    Cache.cpp \
    CLArgs.cpp \
    CoonsRegularization.cpp \
    CreateNodeArgs.cpp \
    Curve.cpp \
    CurveSerialization.cpp \
    DiskCacheNode.cpp \
    Dot.cpp \
    EffectInstance.cpp \
    EffectInstancePrivate.cpp \
    EffectInstanceRenderRoI.cpp \
    EffectOpenGLContextData.cpp \
    ExistenceCheckThread.cpp \
    FileDownloader.cpp \
    FileSystemModel.cpp \
    FitCurve.cpp \
    FrameEntry.cpp \
    FrameKey.cpp \
    FrameParamsSerialization.cpp \
    FStreamsSupport.cpp \
    GenericSchedulerThread.cpp \
    GenericSchedulerThreadWatcher.cpp \
    GPUContextPool.cpp \
    GroupInput.cpp \
    GroupOutput.cpp \
    Hash64.cpp \
    HistogramCPU.cpp \
    HostOverlaySupport.cpp \
    Image.cpp \
    ImageConvert.cpp \
    ImageCopyChannels.cpp \
    ImageComponents.cpp \
    ImageKey.cpp \
    ImageMaskMix.cpp \
    ImageParamsSerialization.cpp \
    Interpolation.cpp \
    JoinViewsNode.cpp \
    Knob.cpp \
    KnobSerialization.cpp \
    KnobFactory.cpp \
    KnobFile.cpp \
    KnobTypes.cpp \
    LibraryBinary.cpp \
    Log.cpp \
    Lut.cpp \
    Markdown.cpp \
    MemoryFile.cpp \
    Node.cpp \
    NodeGroup.cpp \
    NodeMetadata.cpp \
    NonKeyParams.cpp \
    NonKeyParamsSerialization.cpp \
    NodeSerialization.cpp \
    NodeGroupSerialization.cpp \
    NoOpBase.cpp \
    OSGLContext.cpp \
    OSGLContext_osmesa.cpp \
    OSGLContext_mac.cpp \
    OSGLContext_win.cpp \
    OSGLContext_x11.cpp \
    OSGLFunctions_gl.cpp \
    OSGLFunctions_mesa.cpp \
    OfxClipInstance.cpp \
    OfxHost.cpp \
    OfxImageEffectInstance.cpp \
    OfxEffectInstance.cpp \
    OfxMemory.cpp \
    OfxOverlayInteract.cpp \
    OfxParamInstance.cpp \
    OneViewNode.cpp \
    OutputEffectInstance.cpp \
    OutputSchedulerThread.cpp \
    ParallelRenderArgs.cpp \
    Plugin.cpp \
    PluginMemory.cpp \
    PrecompNode.cpp \
    ProcessHandler.cpp \
    Project.cpp \
    ProjectPrivate.cpp \
    ProjectSerialization.cpp \
    PyAppInstance.cpp \
    PyNodeGroup.cpp \
    PyNode.cpp \
    PyParameter.cpp \
    PyRoto.cpp \
    PySideCompat.cpp \
    PyTracker.cpp \
    ReadNode.cpp \
    RectD.cpp \
    RectI.cpp \
    RenderStats.cpp \
    RotoBezierTriangulation.cpp \
    RotoContext.cpp \
    RotoDrawableItem.cpp \
    RotoItem.cpp \
    RotoLayer.cpp \
    RotoPaint.cpp \
    RotoPaintInteract.cpp \
    RotoShapeRenderNode.cpp \
    RotoShapeRenderNodePrivate.cpp \
    RotoShapeRenderCairo.cpp \
    RotoShapeRenderGL.cpp \
    RotoStrokeItem.cpp \
    RotoUndoCommand.cpp \
    ScriptObject.cpp \
    Settings.cpp \
    StandardPaths.cpp \
    StringAnimationManager.cpp \
    Texture.cpp \
    TextureRect.cpp \
    ThreadPool.cpp \
    TimeLine.cpp \
    Timer.cpp \
    TrackerContext.cpp \
    TrackerContextPrivate.cpp \
    TrackerFrameAccessor.cpp \
    TrackMarker.cpp \
    TrackerNode.cpp \
    TrackerNodeInteract.cpp \
    TrackerUndoCommand.cpp \
    TLSHolder.cpp \
    Transform.cpp \
    Utils.cpp \
    ViewerInstance.cpp \
    WriteNode.cpp \
    ../Global/glad_source.c \
    ../Global/ProcInfo.cpp \
    ../libs/SequenceParsing/SequenceParsing.cpp \
    NatronEngine/natronengine_module_wrapper.cpp \
    NatronEngine/natron_namespace_wrapper.cpp \
    NatronEngine/app_wrapper.cpp \
    NatronEngine/effect_wrapper.cpp \
    NatronEngine/intparam_wrapper.cpp \
    NatronEngine/param_wrapper.cpp \
    NatronEngine/doubleparam_wrapper.cpp \
    NatronEngine/colortuple_wrapper.cpp \
    NatronEngine/double2dparam_wrapper.cpp \
    NatronEngine/double2dtuple_wrapper.cpp \
    NatronEngine/double3dparam_wrapper.cpp \
    NatronEngine/double3dtuple_wrapper.cpp \
    NatronEngine/int2dparam_wrapper.cpp \
    NatronEngine/int2dtuple_wrapper.cpp \
    NatronEngine/int3dparam_wrapper.cpp \
    NatronEngine/int3dtuple_wrapper.cpp \
    NatronEngine/colorparam_wrapper.cpp \
    NatronEngine/booleanparam_wrapper.cpp \
    NatronEngine/buttonparam_wrapper.cpp \
    NatronEngine/choiceparam_wrapper.cpp \
    NatronEngine/fileparam_wrapper.cpp \
    NatronEngine/outputfileparam_wrapper.cpp \
    NatronEngine/stringparam_wrapper.cpp \
    NatronEngine/stringparambase_wrapper.cpp \
    NatronEngine/pathparam_wrapper.cpp \
    NatronEngine/animatedparam_wrapper.cpp \
    NatronEngine/parametricparam_wrapper.cpp \
    NatronEngine/group_wrapper.cpp \
    NatronEngine/beziercurve_wrapper.cpp \
    NatronEngine/itembase_wrapper.cpp \
    NatronEngine/imagelayer_wrapper.cpp \
    NatronEngine/layer_wrapper.cpp \
    NatronEngine/roto_wrapper.cpp \
    NatronEngine/track_wrapper.cpp \
    NatronEngine/tracker_wrapper.cpp \
    NatronEngine/groupparam_wrapper.cpp \
    NatronEngine/pageparam_wrapper.cpp \
    NatronEngine/appsettings_wrapper.cpp \
    NatronEngine/pycoreapplication_wrapper.cpp \
    NatronEngine/userparamholder_wrapper.cpp \
    NatronEngine/rectd_wrapper.cpp \
    NatronEngine/recti_wrapper.cpp \
    NatronEngine/separatorparam_wrapper.cpp \
    NatronEngine/boolnodecreationproperty_wrapper.cpp \
    NatronEngine/floatnodecreationproperty_wrapper.cpp \
    NatronEngine/intnodecreationproperty_wrapper.cpp \
    NatronEngine/nodecreationproperty_wrapper.cpp \
    NatronEngine/stringnodecreationproperty_wrapper.cpp


HEADERS += \
    AbortableRenderInfo.h \
    AfterQuitProcessingI.h \
    AppInstance.h \
    AppManager.h \
    AppManagerPrivate.h \
    Backdrop.h \
    Bezier.h \
    BezierSerialization.h \
    BezierCP.h \
    BezierCPPrivate.h \
    BezierCPSerialization.h \
    BlockingBackgroundRender.h \
    BufferableObject.h \
    CLArgs.h \
    Cache.h \
    CacheEntry.h \
    CacheEntryHolder.h \
    CacheSerialization.h \
    CoonsRegularization.h \
    CreateNodeArgs.h \
    Curve.h \
    CurveSerialization.h \
    CurvePrivate.h \
    DockablePanelI.h \
    Dot.h \
    DiskCacheNode.h \
    EffectInstance.h \
    EffectInstancePrivate.h \
    EffectOpenGLContextData.h \
    ExistenceCheckThread.h \
    EngineFwd.h \
    FeatherPoint.h \
    FileDownloader.h \
    FileSystemModel.h \
    FitCurve.h \
    Format.h \
    FormatSerialization.h \
    FrameEntry.h \
    FrameKey.h \
    FrameEntrySerialization.h \
    FrameParams.h \
    FrameParamsSerialization.h \
    FStreamsSupport.h \
    fstream_mingw.h \
    GenericSchedulerThread.h \
    GenericSchedulerThreadWatcher.h \
    GLShader.h \
    GPUContextPool.h \
    GroupInput.h \
    GroupOutput.h \
    Hash64.h \
    HistogramCPU.h \
    HostOverlaySupport.h \
    Image.h \
    ImageComponents.h \
    ImageKey.h \
    ImageLocker.h \
    ImageSerialization.h \
    ImageParams.h \
    ImageParamsSerialization.h \
    Interpolation.h \
    JoinViewsNode.h \
    KeyHelper.h \
    Knob.h \
    KnobGuiI.h \
    KnobImpl.h \
    KnobSerialization.h \
    KnobFactory.h \
    KnobFile.h \
    KnobTypes.h \
    LibraryBinary.h \
    Log.h \
    LogEntry.h \
    LRUHashTable.h \
    Lut.h \
    Markdown.h \
    MemoryFile.h \
    MergingEnum.h \
    Node.h \
    NodeGroup.h \
    NodeGroupSerialization.h \
    NodeGraphI.h \
    NodeGuiI.h \
    NodeMetadata.h \
    NonKeyParams.h \
    NonKeyParamsSerialization.h \
    NodeSerialization.h \
    NoOpBase.h \
    OSGLContext.h \
    OSGLContext_osmesa.h \
    OSGLContext_mac.h \
    OSGLContext_win.h \
    OSGLContext_x11.h \
    OSGLFunctions.h \
    OSGLFramebufferConfig.h \
    OfxClipInstance.h \
    OfxEffectInstance.h \
    OfxHost.h \
    OfxImageEffectInstance.h \
    OfxOverlayInteract.h \
    OfxMemory.h \
    OfxParamInstance.h \
    OneViewNode.h \
    OpenGLViewerI.h \
    OutputEffectInstance.h \
    OutputSchedulerThread.h \
    OverlaySupport.h \
    ParallelRenderArgs.h \
    Plugin.h \
    PluginActionShortcut.h \
    PluginMemory.h \
    PrecompNode.h \
    ProcessHandler.h \
    Project.h \
    ProjectPrivate.h \
    ProjectSerialization.h \
    PyAppInstance.h \
    PyGlobalFunctions.h \
    PyNodeGroup.h \
    PyNode.h \
    PyParameter.h \
    PyRoto.h \
    PyTracker.h \
    Pyside_Engine_Python.h \
    ReadNode.h \
    RectD.h \
    RectDSerialization.h \
    RectI.h \
    RectISerialization.h \
    RenderStats.h \
    RotoBezierTriangulation.h \
    RotoContext.h \
    RotoContextPrivate.h \
    RotoContextSerialization.h \
    RotoDrawableItem.h \
    RotoDrawableItemSerialization.h \
    RotoLayer.h \
    RotoLayerSerialization.h \
    RotoItem.h \
    RotoItemSerialization.h \
    RotoPaint.h \
    RotoPaintInteract.h \
    RotoPoint.h \
    RotoShapeRenderNode.h \
    RotoShapeRenderNodePrivate.h \
    RotoShapeRenderCairo.h \
    RotoShapeRenderGL.h \
    RotoStrokeItem.h \
    RotoStrokeItemSerialization.h \
    RotoUndoCommand.h \
    ScriptObject.h \
    Settings.h \
    Singleton.h \
    StandardPaths.h \
    StringAnimationManager.h \
    Texture.h \
    TextureRect.h \
    TextureRectSerialization.h \
    ThreadStorage.h \
    ThreadPool.h \
    TimeLine.h \
    TimeLineKeyFrames.h \
    Timer.h \
    TrackerContext.h \
    TrackerContextPrivate.h \
    TrackerFrameAccessor.h \
    TrackerNode.h \
    TrackerNodeInteract.h \
    TrackerUndoCommand.h \
    TrackMarker.h \
    TrackerSerialization.h \
    TLSHolder.h \
    TLSHolderImpl.h \
    Transform.h \
    UpdateViewerParams.h \
    UndoCommand.h \
    Utils.h \
    Variant.h \
    VariantSerialization.h \
    ViewerInstance.h \
    ViewerInstancePrivate.h \
    ViewIdx.h \
    WriteNode.h \
    ../Global/Enums.h \
    ../Global/GitVersion.h \
    ../Global/glad_include.h \
    ../Global/GLIncludes.h \
    ../Global/GlobalDefines.h \
    ../Global/KeySymbols.h \
    ../Global/Macros.h \
    ../Global/MemoryInfo.h \
    ../Global/ProcInfo.h \
    ../Global/QtCompat.h \
    ../libs/SequenceParsing/SequenceParsing.h \
    ../libs/OpenFX/include/ofxCore.h \
    ../libs/OpenFX/include/ofxDialog.h \
    ../libs/OpenFX/include/ofxImageEffect.h \
    ../libs/OpenFX/include/ofxInteract.h \
    ../libs/OpenFX/include/ofxKeySyms.h \
    ../libs/OpenFX/include/ofxMemory.h \
    ../libs/OpenFX/include/ofxMessage.h \
    ../libs/OpenFX/include/ofxMultiThread.h \
    ../libs/OpenFX/include/ofxNatron.h \
    ../libs/OpenFX/include/ofxOpenGLRender.h \
    ../libs/OpenFX/include/ofxParam.h \
    ../libs/OpenFX/include/ofxParametricParam.h \
    ../libs/OpenFX/include/ofxPixels.h \
    ../libs/OpenFX/include/ofxProgress.h \
    ../libs/OpenFX/include/ofxProperty.h \
    ../libs/OpenFX/include/ofxSonyVegas.h \
    ../libs/OpenFX/include/ofxTimeLine.h \
    ../libs/OpenFX/include/nuke/camera.h \
    ../libs/OpenFX/include/nuke/fnOfxExtensions.h \
    ../libs/OpenFX/include/nuke/fnPublicOfxExtensions.h \
    ../libs/OpenFX/include/tuttle/ofxReadWrite.h \
    ../libs/OpenFX_extensions/ofxhParametricParam.h \
    NatronEngine/natronengine_python.h \
    NatronEngine/app_wrapper.h \
    NatronEngine/effect_wrapper.h \
    NatronEngine/intparam_wrapper.h \
    NatronEngine/param_wrapper.h \
    NatronEngine/doubleparam_wrapper.h \
    NatronEngine/colortuple_wrapper.h \
    NatronEngine/double2dparam_wrapper.h \
    NatronEngine/double2dtuple_wrapper.h \
    NatronEngine/double3dparam_wrapper.h \
    NatronEngine/double3dtuple_wrapper.h \
    NatronEngine/int2dparam_wrapper.h \
    NatronEngine/int2dtuple_wrapper.h \
    NatronEngine/int3dparam_wrapper.h \
    NatronEngine/int3dtuple_wrapper.h \
    NatronEngine/colorparam_wrapper.h \
    NatronEngine/booleanparam_wrapper.h \
    NatronEngine/buttonparam_wrapper.h \
    NatronEngine/choiceparam_wrapper.h \
    NatronEngine/fileparam_wrapper.h \
    NatronEngine/outputfileparam_wrapper.h \
    NatronEngine/stringparam_wrapper.h \
    NatronEngine/stringparambase_wrapper.h \
    NatronEngine/pathparam_wrapper.h \
    NatronEngine/animatedparam_wrapper.h \
    NatronEngine/parametricparam_wrapper.h \
    NatronEngine/group_wrapper.h \
    NatronEngine/beziercurve_wrapper.h \
    NatronEngine/itembase_wrapper.h \
    NatronEngine/imagelayer_wrapper.h \
    NatronEngine/layer_wrapper.h \
    NatronEngine/roto_wrapper.h \
    NatronEngine/track_wrapper.h \
    NatronEngine/tracker_wrapper.h \
    NatronEngine/groupparam_wrapper.h \
    NatronEngine/pageparam_wrapper.h \
    NatronEngine/appsettings_wrapper.h \
    NatronEngine/pycoreapplication_wrapper.h \
    NatronEngine/userparamholder_wrapper.h \
    NatronEngine/rectd_wrapper.h \
    NatronEngine/recti_wrapper.h \
    NatronEngine/separatorparam_wrapper.h \
    NatronEngine/boolnodecreationproperty_wrapper.h \
    NatronEngine/floatnodecreationproperty_wrapper.h \
    NatronEngine/intnodecreationproperty_wrapper.h \
    NatronEngine/nodecreationproperty_wrapper.h \
    NatronEngine/stringnodecreationproperty_wrapper.h



OTHER_FILES += \
    typesystem_engine.xml



# GENERATED_SOURCES =				\
# NatronEngine/animatedparam_wrapper.cpp		\
# NatronEngine/app_wrapper.cpp			\
# NatronEngine/beziercurve_wrapper.cpp		\
# NatronEngine/booleanparam_wrapper.cpp		\
# NatronEngine/buttonparam_wrapper.cpp		\
# NatronEngine/choiceparam_wrapper.cpp		\
# NatronEngine/colorparam_wrapper.cpp		\
# NatronEngine/colortuple_wrapper.cpp		\
# NatronEngine/double2dparam_wrapper.cpp		\
# NatronEngine/double2dtuple_wrapper.cpp		\
# NatronEngine/double3dparam_wrapper.cpp		\
# NatronEngine/double3dtuple_wrapper.cpp		\
# NatronEngine/doubleparam_wrapper.cpp		\
# NatronEngine/effect_wrapper.cpp			\
# NatronEngine/fileparam_wrapper.cpp		\
# NatronEngine/group_wrapper.cpp			\
# NatronEngine/groupparam_wrapper.cpp		\
# NatronEngine/int2dparam_wrapper.cpp		\
# NatronEngine/int2dtuple_wrapper.cpp		\
# NatronEngine/int3dparam_wrapper.cpp		\
# NatronEngine/int3dtuple_wrapper.cpp		\
# NatronEngine/intparam_wrapper.cpp		\
# NatronEngine/itembase_wrapper.cpp		\
# NatronEngine/layer_wrapper.cpp			\
# NatronEngine/natron_wrapper.cpp			\
# NatronEngine/natronengine_module_wrapper.cpp	\
# NatronEngine/outputfileparam_wrapper.cpp	\
# NatronEngine/pageparam_wrapper.cpp		\
# NatronEngine/param_wrapper.cpp			\
# NatronEngine/parametricparam_wrapper.cpp	\
# NatronEngine/pathparam_wrapper.cpp		\
# NatronEngine/roto_wrapper.cpp			\
# NatronEngine/stringparam_wrapper.cpp		\
# NatronEngine/stringparambase_wrapper.cpp

# defineReplace(shibokenWorkaround) {
#     SOURCES += $$GENERATED_SOURCES
#     return("%_wrapper.cpp")
# }

# isEmpty(SHIBOKEN) {
#    SHIBOKEN = shiboken
# }

# SHIBOKEN_FILE  = . # Need to give some bogus input
# SHIBOKEN.input = SHIBOKEN_FILE
# SHIBOKEN.output_function = shibokenWorkaround
# SHIBOKEN.commands = $$SHIBOKEN --include-paths=..:$$system(pkg-config --variable=includedir pyside)  --typesystem-paths=$$system(pkg-config --variable=typesystemdir pyside) Pyside_Engine_Python.h typesystem_engine.xml
# SHIBOKEN.CONFIG = no_link # don't add the .cpp target file to OBJECTS
# SHIBOKEN.clean = dummy # don't remove the %_wrapper.cpp file by "make clean"

# QMAKE_EXTRA_COMPILERS += SHIBOKEN
macx {

OBJECTIVE_SOURCES += \
    QUrlFix.mm
}
