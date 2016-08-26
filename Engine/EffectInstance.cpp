/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of Natron <http://www.natron.fr/>,
 * Copyright (C) 2016 INRIA and Alexandre Gauthier-Foichat
 *
 * Natron is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Natron is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Natron.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "EffectInstance.h"
#include "EffectInstancePrivate.h"

#include <map>
#include <sstream>
#include <algorithm> // min, max
#include <fstream>
#include <bitset>
#include <cassert>
#include <stdexcept>

#if !defined(SBK_RUN) && !defined(Q_MOC_RUN)
GCC_DIAG_UNUSED_LOCAL_TYPEDEFS_OFF
// /usr/local/include/boost/bind/arg.hpp:37:9: warning: unused typedef 'boost_static_assert_typedef_37' [-Wunused-local-typedef]
#include <boost/bind.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/scoped_ptr.hpp>
GCC_DIAG_UNUSED_LOCAL_TYPEDEFS_ON
#endif

#include <QtCore/QReadWriteLock>
#include <QtCore/QCoreApplication>
#include <QtConcurrentMap> // QtCore on Qt4, QtConcurrent on Qt5
#include <QtConcurrentRun> // QtCore on Qt4, QtConcurrent on Qt5

#include <SequenceParsing.h>

#include "Global/MemoryInfo.h"
#include "Global/QtCompat.h"

#include "Engine/AbortableRenderInfo.h"
#include "Engine/AppInstance.h"
#include "Engine/AppManager.h"
#include "Engine/BlockingBackgroundRender.h"
#include "Engine/DiskCacheNode.h"
#include "Engine/EffectOpenGLContextData.h"
#include "Engine/Image.h"
#include "Engine/ImageParams.h"
#include "Engine/KnobFile.h"
#include "Engine/KnobTypes.h"
#include "Engine/Log.h"
#include "Engine/Node.h"
#include "Engine/OfxEffectInstance.h"
#include "Engine/OfxEffectInstance.h"
#include "Engine/OfxOverlayInteract.h"
#include "Engine/OfxImageEffectInstance.h"
#include "Engine/GPUContextPool.h"
#include "Engine/OSGLContext.h"
#include "Engine/OutputSchedulerThread.h"
#include "Engine/PluginMemory.h"
#include "Engine/Project.h"
#include "Engine/RenderStats.h"
#include "Engine/RotoContext.h"
#include "Engine/RotoDrawableItem.h"
#include "Engine/ReadNode.h"
#include "Engine/Settings.h"
#include "Engine/Timer.h"
#include "Engine/Transform.h"
#include "Engine/UndoCommand.h"
#include "Engine/ViewIdx.h"
#include "Engine/ViewerInstance.h"

//#define NATRON_ALWAYS_ALLOCATE_FULL_IMAGE_BOUNDS


NATRON_NAMESPACE_ENTER;


class KnobFile;
class KnobOutputFile;


void
EffectInstance::addThreadLocalInputImageTempPointer(int inputNb,
                                                    const ImagePtr & img)
{
    _imp->addInputImageTempPointer(inputNb, img);
}

EffectInstance::EffectInstance(const NodePtr& node)
    : NamedKnobHolder( node ? node->getApp() : AppInstancePtr() )
    , _node(node)
    , _imp( new Implementation(this) )
{
    if (node) {
        if ( !node->isRenderScaleSupportEnabledForPlugin() ) {
            setSupportsRenderScaleMaybe(eSupportsNo);
        }
    }
}

EffectInstance::EffectInstance(const EffectInstance& other)
    : NamedKnobHolder(other)
    , _node( other.getNode() )
    , _imp( new Implementation(*other._imp) )
{
    _imp->_publicInterface = this;
}

EffectInstance::~EffectInstance()
{
}

void
EffectInstance::lock(const ImagePtr & entry)
{
    NodePtr n = _node.lock();

    n->lock(entry);
}

bool
EffectInstance::tryLock(const ImagePtr & entry)
{
    NodePtr n = _node.lock();

    return n->tryLock(entry);
}

void
EffectInstance::unlock(const ImagePtr & entry)
{
    NodePtr n = _node.lock();

    n->unlock(entry);
}

void
EffectInstance::clearPluginMemoryChunks()
{
    // This will remove the mem from the pluginMemoryChunks list
    PluginMemoryPtr mem;
    do {
        mem.reset();
        {
            QMutexLocker l(&_imp->pluginMemoryChunksMutex);
            if ( !_imp->pluginMemoryChunks.empty() ) {
                mem = ( *_imp->pluginMemoryChunks.begin() ).lock();
#pragma message WARN("BUG: if mem is not NULL, it is never removed from the list and this goes into an infinite loop!!! should the following condition (!mem) be reversed?")
                while ( !mem && !_imp->pluginMemoryChunks.empty() ) {
                    _imp->pluginMemoryChunks.pop_front();
                    mem.reset();
                    if ( !_imp->pluginMemoryChunks.empty() ) {
                        mem = ( *_imp->pluginMemoryChunks.begin() ).lock();
                    }
                }
            }
        }
    } while (mem);
}

#ifdef DEBUG
void
EffectInstance::setCanSetValue(bool can)
{
    _imp->tlsData->getOrCreateTLSData()->canSetValue.push_back(can);
}

void
EffectInstance::invalidateCanSetValueFlag()
{
    EffectDataTLSPtr tls = _imp->tlsData->getTLSData();

    assert(tls);
    assert( !tls->canSetValue.empty() );
    tls->canSetValue.pop_back();
}

bool
EffectInstance::isDuringActionThatCanSetValue() const
{
    EffectDataTLSPtr tls = _imp->tlsData->getTLSData();

    if (!tls) {
        return true;
    }
    if ( tls->canSetValue.empty() ) {
        return true;
    }

    return tls->canSetValue.back();
}

#endif //DEBUG

void
EffectInstance::setNodeRequestThreadLocal(const NodeFrameRequestPtr & nodeRequest)
{
    EffectDataTLSPtr tls = _imp->tlsData->getTLSData();

    if (!tls) {
        assert(false);

        return;
    }
    std::list<ParallelRenderArgsPtr >& argsList = tls->frameArgs;
    if ( argsList.empty() ) {
        return;
    }
    argsList.back()->request = nodeRequest;
}

void
EffectInstance::setParallelRenderArgsTLS(const SetParallelRenderTLSArgsPtr& inArgs)
{
    EffectDataTLSPtr tls = _imp->tlsData->getOrCreateTLSData();
    std::list<ParallelRenderArgsPtr >& argsList = tls->frameArgs;
    ParallelRenderArgsPtr args(new ParallelRenderArgs);

    args->time = inArgs->time;
    args->timeline = inArgs->timeline;
    args->view = inArgs->view;
    args->isRenderResponseToUserInteraction = inArgs->isRenderUserInteraction;
    args->isSequentialRender = inArgs->isSequential;
    args->request = inArgs->nodeRequest;
    if (inArgs->nodeRequest) {
        args->nodeHash = inArgs->nodeRequest->nodeHash;
    } else {
        args->nodeHash = inArgs->nodeHash;
    }
    assert(inArgs->abortInfo);
    args->abortInfo = inArgs->abortInfo;
    args->treeRoot = inArgs->treeRoot;
    args->visitsCount = inArgs->visitsCount;
    args->textureIndex = inArgs->textureIndex;
    args->isAnalysis = inArgs->isAnalysis;
    args->isDuringPaintStrokeCreation = inArgs->isDuringPaintStrokeCreation;
    args->currentThreadSafety = inArgs->currentThreadSafety;
    args->currentOpenglSupport = inArgs->currentOpenGLSupport;
    args->rotoPaintNodes = inArgs->rotoPaintNodes;
    args->doNansHandling = inArgs->isAnalysis ? false : inArgs->doNanHandling;
    args->draftMode = inArgs->draftMode;
    args->tilesSupported = getNode()->getCurrentSupportTiles();
    args->stats = inArgs->stats;
    args->openGLContext = inArgs->glContext;
    args->cpuOpenGLContext = inArgs->cpuGlContext;
    argsList.push_back(args);
}

bool
EffectInstance::getThreadLocalRotoPaintTreeNodes(NodesList* nodes) const
{
    EffectDataTLSPtr tls = _imp->tlsData->getTLSData();

    if (!tls) {
        return false;
    }
    if ( tls->frameArgs.empty() ) {
        return false;
    }
    *nodes = tls->frameArgs.back()->rotoPaintNodes;

    return true;
}

void
EffectInstance::setDuringPaintStrokeCreationThreadLocal(bool duringPaintStroke)
{
    EffectDataTLSPtr tls = _imp->tlsData->getOrCreateTLSData();

    tls->frameArgs.back()->isDuringPaintStrokeCreation = duringPaintStroke;
}

void
EffectInstance::setParallelRenderArgsTLS(const ParallelRenderArgsPtr & args)
{
    EffectDataTLSPtr tls = _imp->tlsData->getOrCreateTLSData();

    assert( args->abortInfo.lock() );
    tls->frameArgs.push_back(args);
}

void
EffectInstance::invalidateParallelRenderArgsTLS()
{
    EffectDataTLSPtr tls = _imp->tlsData->getTLSData();

    if (!tls) {
        return;
    }

    assert( !tls->frameArgs.empty() );
    const ParallelRenderArgsPtr& back = tls->frameArgs.back();
    for (NodesList::iterator it = back->rotoPaintNodes.begin(); it != back->rotoPaintNodes.end(); ++it) {
        (*it)->getEffectInstance()->invalidateParallelRenderArgsTLS();
    }
    tls->frameArgs.pop_back();
}

ParallelRenderArgsPtr
EffectInstance::getParallelRenderArgsTLS() const
{
    EffectDataTLSPtr tls = _imp->tlsData->getTLSData();

    if ( !tls || tls->frameArgs.empty() ) {
        return ParallelRenderArgsPtr();
    }

    return tls->frameArgs.back();
}

U64
EffectInstance::getHash() const
{
    NodePtr n = _node.lock();

    return n->getHashValue();
}

U64
EffectInstance::getRenderHash() const
{
    EffectDataTLSPtr tls = _imp->tlsData->getTLSData();

    if ( !tls || tls->frameArgs.empty() ) {
        //No tls: get the GUI hash
        return getHash();
    }

    const ParallelRenderArgsPtr &args = tls->frameArgs.back();

    if (args->request) {
        //A request pass was made, Hash for this thread was already computed, use it
        return args->request->nodeHash;
    }

    //Use the hash that was computed when we set the ParallelRenderArgs TLS
    return args->nodeHash;
}

bool
EffectInstance::Implementation::aborted(bool isRenderResponseToUserInteraction,
                                        const AbortableRenderInfoPtr& abortInfo,
                                        const EffectInstancePtr& treeRoot)
{
    if (!isRenderResponseToUserInteraction) {
        // Rendering is playback or render on disk

        // If we have abort info, e just peek the atomic int inside the abort info, this is very fast
        if ( abortInfo && abortInfo->isAborted() ) {
            return true;
        }

        // Fallback on the flag set on the node that requested the render in OutputSchedulerThread
        if (treeRoot) {
            OutputEffectInstancePtr effect = toOutputEffectInstance(treeRoot);
            assert(effect);
            if (effect) {
                return effect->isSequentialRenderBeingAborted();
            }
        }

        // We have no other means to know if abort was called
        return false;
    } else {
        // This is a render issued to refresh the image on the Viewer

        if ( !abortInfo || !abortInfo->canAbort() ) {
            // We do not have any abortInfo set or this render is not abortable. This should be avoided as much as possible!
            return false;
        }

        // This is very fast, we just peek the atomic int inside the abort info
        if ( (int)abortInfo->isAborted() ) {
            return true;
        }

        // If this node can start sequential renders (e.g: start playback like on the viewer or render on disk) and it is already doing a sequential render, abort
        // this render
        OutputEffectInstancePtr isRenderEffect = toOutputEffectInstance(treeRoot);
        if (isRenderEffect) {
            if ( isRenderEffect->isDoingSequentialRender() ) {
                return true;
            }
        }

        // The render was not aborted
        return false;
    }
}

bool
EffectInstance::aborted() const
{
    QThread* thisThread = QThread::currentThread();

    /* If this thread is an AbortableThread, this function will be extremely fast*/
    AbortableThread* isAbortableThread = dynamic_cast<AbortableThread*>(thisThread);

    /**
       The solution here is to store per-render info on the thread that we retrieve.
       These info contain an atomic integer determining whether this particular render was aborted or not.
       If this thread does not have abort info yet on it, we retrieve them from the thread local storage of this node
       and set it.
       Threads that start a render generally already have the AbortableThread::setAbortInfo function called on them, but
       threads spawned from the thread pool may not.
     **/
    bool isRenderUserInteraction;
    AbortableRenderInfoPtr abortInfo;
    EffectInstancePtr treeRoot;


    if ( !isAbortableThread || !isAbortableThread->getAbortInfo(&isRenderUserInteraction, &abortInfo, &treeRoot) ) {
        // If this thread is not abortable or we did not set the abort info for this render yet, retrieve them from the TLS of this node.
        EffectDataTLSPtr tls = _imp->tlsData->getTLSData();
        if (!tls) {
            return false;
        }
        if ( tls->frameArgs.empty() ) {
            return false;
        }
        const ParallelRenderArgsPtr & args = tls->frameArgs.back();
        isRenderUserInteraction = args->isRenderResponseToUserInteraction;
        abortInfo = args->abortInfo.lock();
        if (args->treeRoot) {
            treeRoot = args->treeRoot->getEffectInstance();
        }

        if (isAbortableThread) {
            isAbortableThread->setAbortInfo(isRenderUserInteraction, abortInfo, treeRoot);
        }
    }

    // The internal function that given a AbortableRenderInfoPtr determines if a render was aborted or not
    return Implementation::aborted(isRenderUserInteraction,
                                   abortInfo,
                                   treeRoot);
} // EffectInstance::aborted

bool
EffectInstance::shouldCacheOutput(bool isFrameVaryingOrAnimated,
                                  double time,
                                  ViewIdx view,
                                  int visitsCount) const
{
    NodePtr n = _node.lock();

    return n->shouldCacheOutput(isFrameVaryingOrAnimated, time, view, visitsCount);
}

U64
EffectInstance::getKnobsAge() const
{
    return getNode()->getKnobsAge();
}

void
EffectInstance::setKnobsAge(U64 age)
{
    getNode()->setKnobsAge(age);
}

const std::string &
EffectInstance::getScriptName() const
{
    return getNode()->getScriptName();
}

std::string
EffectInstance::getScriptName_mt_safe() const
{
    return getNode()->getScriptName_mt_safe();
}

void
EffectInstance::getRenderFormat(Format *f) const
{
    assert(f);
    getApp()->getProject()->getProjectDefaultFormat(f);
}

int
EffectInstance::getRenderViewsCount() const
{
    return getApp()->getProject()->getProjectViewsCount();
}

bool
EffectInstance::hasOutputConnected() const
{
    return getNode()->hasOutputConnected();
}

EffectInstancePtr
EffectInstance::getInput(int n) const
{
    NodePtr inputNode = getNode()->getInput(n);

    if (inputNode) {
        return inputNode->getEffectInstance();
    }

    return EffectInstancePtr();
}

std::string
EffectInstance::getInputLabel(int inputNb) const
{
    std::string out;

    out.append( 1, (char)(inputNb + 65) );

    return out;
}

std::string
EffectInstance::getInputHint(int /*inputNb*/) const
{
    return std::string();
}

bool
EffectInstance::retrieveGetImageDataUponFailure(const double time,
                                                const ViewIdx view,
                                                const RenderScale & scale,
                                                const RectD* optionalBoundsParam,
                                                U64* nodeHash_p,
                                                bool* isIdentity_p,
                                                double* identityTime,
                                                ViewIdx* inputView,
                                                EffectInstancePtr* identityInput_p,
                                                bool* duringPaintStroke_p,
                                                RectD* rod_p,
                                                RoIMap* inputRois_p, //!< output, only set if optionalBoundsParam != NULL
                                                RectD* optionalBounds_p) //!< output, only set if optionalBoundsParam != NULL
{
    /////Update 09/02/14
    /// We now AUTHORIZE GetRegionOfDefinition and isIdentity and getRegionsOfInterest to be called recursively.
    /// It didn't make much sense to forbid them from being recursive.

//#ifdef DEBUG
//    if (QThread::currentThread() != qApp->thread()) {
//        ///This is a bad plug-in
//        qDebug() << getNode()->getScriptName_mt_safe().c_str() << " is trying to call clipGetImage during an unauthorized time. "
//        "Developers of that plug-in should fix it. \n Reminder from the OpenFX spec: \n "
//        "Images may be fetched from an attached clip in the following situations... \n"
//        "- in the kOfxImageEffectActionRender action\n"
//        "- in the kOfxActionInstanceChanged and kOfxActionEndInstanceChanged actions with a kOfxPropChangeReason or kOfxChangeUserEdited";
//    }
//#endif

    ///Try to compensate for the mistake

    *nodeHash_p = getHash();
    *duringPaintStroke_p = getNode()->isDuringPaintStrokeCreation();
    const U64 & nodeHash = *nodeHash_p;

    {
        RECURSIVE_ACTION();
        StatusEnum stat = getRegionOfDefinition(nodeHash, time, scale, view, rod_p);
        if (stat == eStatusFailed) {
            return false;
        }
    }
    const RectD & rod = *rod_p;

    ///OptionalBoundsParam is the optional rectangle passed to getImage which may be NULL, in which case we use the RoD.
    if (!optionalBoundsParam) {
        ///// We cannot recover the RoI, we just assume the plug-in wants to render the full RoD.
        *optionalBounds_p = rod;
        ifInfiniteApplyHeuristic(nodeHash, time, scale, view, optionalBounds_p);
        const RectD & optionalBounds = *optionalBounds_p;

        /// If the region parameter is not set to NULL, then it will be clipped to the clip's
        /// Region of Definition for the given time. The returned image will be m at m least as big as this region.
        /// If the region parameter is not set, then the region fetched will be at least the Region of Interest
        /// the effect has previously specified, clipped the clip's Region of Definition.
        /// (renderRoI will do the clipping for us).


        ///// This code is wrong but executed ONLY IF THE PLUG-IN DOESN'T RESPECT THE SPECIFICATIONS. Recursive actions
        ///// should never happen.
        getRegionsOfInterest(time, scale, optionalBounds, optionalBounds, ViewIdx(0), inputRois_p);
    }

    assert( !( (supportsRenderScaleMaybe() == eSupportsNo) && !(scale.x == 1. && scale.y == 1.) ) );
    RectI pixelRod;
    rod.toPixelEnclosing(scale, getAspectRatio(-1), &pixelRod);
    try {
        int identityInputNb;
        *isIdentity_p = isIdentity_public(true, nodeHash, time, scale, pixelRod, view, identityTime, inputView, &identityInputNb);
        if (*isIdentity_p) {
            if (identityInputNb >= 0) {
                *identityInput_p = getInput(identityInputNb);
            } else if (identityInputNb == -2) {
                *identityInput_p = shared_from_this();
            }
        }
    } catch (...) {
        return false;
    }

    return true;
} // EffectInstance::retrieveGetImageDataUponFailure

void
EffectInstance::getThreadLocalInputImages(InputImagesMap* images) const
{
    EffectDataTLSPtr tls = _imp->tlsData->getTLSData();

    if (!tls) {
        return;
    }
    *images = tls->currentRenderArgs.inputImages;
}

bool
EffectInstance::getThreadLocalRegionsOfInterests(RoIMap & roiMap) const
{
    EffectDataTLSPtr tls = _imp->tlsData->getTLSData();

    if (!tls) {
        return false;
    }
    roiMap = tls->currentRenderArgs.regionOfInterestResults;

    return true;
}

OSGLContextPtr
EffectInstance::getThreadLocalOpenGLContext() const
{
    EffectDataTLSPtr tls = _imp->tlsData->getTLSData();

    if ( !tls || tls->frameArgs.empty() ) {
        return OSGLContextPtr();
    }

    return tls->frameArgs.back()->openGLContext.lock();
}

ImagePtr
EffectInstance::getImage(int inputNb,
                         const double time,
                         const RenderScale & scale,
                         const ViewIdx view,
                         const RectD *optionalBoundsParam, //!< optional region in canonical coordinates
                         const ImageComponents* layer,
                         const bool mapToClipPrefs,
                         const bool dontUpscale,
                         const StorageModeEnum returnStorage,
                         const ImageBitDepthEnum* /*textureDepth*/, // < ignore requested texture depth because internally we use 32bit fp textures, so we offer the highest possible quality anyway.
                         RectI* roiPixel,
                         boost::shared_ptr<Transform::Matrix3x3>* transform)
{
    if (time != time) {
        // time is NaN
        return ImagePtr();
    }

    ///The input we want the image from
    EffectInstancePtr inputEffect;

    // Check for transform redirections
    InputMatrixMapPtr transformRedirections;
    EffectDataTLSPtr tls = _imp->tlsData->getTLSData();
    if (tls && tls->currentRenderArgs.validArgs) {
        transformRedirections = tls->currentRenderArgs.transformRedirections;
        if (transformRedirections) {
            InputMatrixMap::const_iterator foundRedirection = transformRedirections->find(inputNb);
            if ( ( foundRedirection != transformRedirections->end() ) && foundRedirection->second.newInputEffect ) {
                inputEffect = foundRedirection->second.newInputEffect->getInput(foundRedirection->second.newInputNbToFetchFrom);
                if (transform) {
                    *transform = foundRedirection->second.cat;
                }
            }
        }
    }

    if (!inputEffect) {
        inputEffect = getInput(inputNb);
    }

    // Is this input a mask or not
    bool isMask = isInputMask(inputNb);

    // If the input is a mask, this is the channel index in the layer of the mask channel
    int channelForMask = -1;

    // This is the actual layer that we are fetching in input
    ImageComponents maskComps;
    if ( !isMaskEnabled(inputNb) ) {
        return ImagePtr();
    }

    // If this is a mask, fetch the image from the effect indicated by the mask channel
    NodePtr maskInput;
    if (isMask) {
        channelForMask = getMaskChannel(inputNb, &maskComps, &maskInput);
    }
    if ( maskInput && (channelForMask != -1) ) {
        inputEffect = maskInput->getEffectInstance();
    }

    // Invalid mask
    if ( isMask && ( (channelForMask == -1) || (maskComps.getNumComponents() == 0) ) ) {
        return ImagePtr();
    }


    if (!inputEffect) {
        // Disconnected input
        return ImagePtr();
    }

    ///If optionalBounds have been set, use this for the RoI instead of the data int the TLS
    RectD optionalBounds;
    if (optionalBoundsParam) {
        optionalBounds = *optionalBoundsParam;
    }

    /*
     * These are the data fields stored in the TLS from the on-going render action or instance changed action
     */
    unsigned int mipMapLevel = Image::getLevelFromScale(scale.x);
    RoIMap inputsRoI;
    bool isIdentity = false;
    EffectInstancePtr identityInput;
    double inputIdentityTime = 0.;
    ViewIdx inputIdentityView(view);
    U64 nodeHash;
    bool duringPaintStroke;
    /// Never by-pass the cache here because we already computed the image in renderRoI and by-passing the cache again can lead to
    /// re-computing of the same image many many times
    bool byPassCache = false;

    ///The caller thread MUST be a thread owned by Natron. It cannot be a thread from the multi-thread suite.
    ///A call to getImage is forbidden outside an action running in a thread launched by Natron.

    /// From http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#ImageEffectsImagesAndClipsUsingClips
    //    Images may be fetched from an attached clip in the following situations...
    //    in the kOfxImageEffectActionRender action
    //    in the kOfxActionInstanceChanged and kOfxActionEndInstanceChanged actions with a kOfxPropChangeReason of kOfxChangeUserEdited
    RectD roi;
    bool roiWasInRequestPass = false;
    bool isAnalysisPass = false;
    RectD thisRod;
    double thisEffectRenderTime = time;

    ///Try to find in the input images thread local storage if we already pre-computed the image
    EffectInstance::InputImagesMap inputImagesThreadLocal;
    OSGLContextPtr gpuGlContext, cpuGlContext;
    AbortableRenderInfoPtr renderInfo;
    if ( !tls || ( !tls->currentRenderArgs.validArgs && tls->frameArgs.empty() ) ) {
        /*
           This is either a huge bug or an unknown thread that called clipGetImage from the OpenFX plug-in.
           Make-up some reasonable arguments
         */
        if ( !retrieveGetImageDataUponFailure(time, view, scale, optionalBoundsParam, &nodeHash, &isIdentity, &inputIdentityTime, &inputIdentityView, &identityInput, &duringPaintStroke, &thisRod, &inputsRoI, &optionalBounds) ) {
            return ImagePtr();
        }
    } else {
        assert( tls->currentRenderArgs.validArgs || !tls->frameArgs.empty() );

        if (inputEffect) {
            //When analysing we do not compute a request pass so we do not enter this condition
            ParallelRenderArgsPtr inputFrameArgs = inputEffect->getParallelRenderArgsTLS();
            const FrameViewRequest* request = 0;
            if (inputFrameArgs && inputFrameArgs->request) {
                request = inputFrameArgs->request->getFrameViewRequest(time, view);
            }
            if (request) {
                roiWasInRequestPass = true;
                roi = request->finalData.finalRoi;
            }
        }

        if ( !tls->frameArgs.empty() ) {
            const ParallelRenderArgsPtr& frameRenderArgs = tls->frameArgs.back();
            nodeHash = frameRenderArgs->nodeHash;
            duringPaintStroke = frameRenderArgs->isDuringPaintStrokeCreation;
            isAnalysisPass = frameRenderArgs->isAnalysis;
            gpuGlContext = frameRenderArgs->openGLContext.lock();
            cpuGlContext = frameRenderArgs->cpuOpenGLContext.lock();
            renderInfo = frameRenderArgs->abortInfo.lock();
        } else {
            //This is a bug, when entering here, frameArgs TLS should always have been set, except for unknown threads.
            nodeHash = getHash();
            duringPaintStroke = false;
        }
        if (tls->currentRenderArgs.validArgs) {
            //This will only be valid for render pass, not analysis
            const RenderArgs& renderArgs = tls->currentRenderArgs;
            if (!roiWasInRequestPass) {
                inputsRoI = renderArgs.regionOfInterestResults;
            }
            thisEffectRenderTime = renderArgs.time;
            isIdentity = renderArgs.isIdentity;
            inputIdentityTime = renderArgs.identityTime;
            identityInput = renderArgs.identityInput;
            inputImagesThreadLocal = renderArgs.inputImages;
            thisRod = renderArgs.rod;
        }
    }

    if ( ((!gpuGlContext && !cpuGlContext) || !renderInfo) && returnStorage == eStorageModeGLTex ) {
        qDebug() << "[BUG]: " << getScriptName_mt_safe().c_str() << "is doing an OpenGL render but no context is bound to the current render.";

        return ImagePtr();
    }



    RectD inputRoD;
    bool inputRoDSet = false;
    if (optionalBoundsParam) {
        //Set the RoI from the parameters given to clipGetImage
        roi = optionalBounds;
    } else if (!roiWasInRequestPass) {
        //We did not have a request pass, use if possible the result of getRegionsOfInterest found in the TLS
        //If not, fallback on input RoD
        RoIMap::iterator found = inputsRoI.find(inputEffect);
        if ( found != inputsRoI.end() ) {
            ///RoI is in canonical coordinates since the results of getRegionsOfInterest is in canonical coords.
            roi = found->second;
        } else {
            ///Oops, we didn't find the roi in the thread-storage... use  the RoD instead...
            if (inputEffect && !isAnalysisPass) {
                qDebug() << QThread::currentThread() << getScriptName_mt_safe().c_str() << "[Bug] RoI not found in TLS...falling back on RoD when calling getImage() on" <<
                inputEffect->getScriptName_mt_safe().c_str();
            }


            //We are either in analysis or in an unknown thread
            //do not set identity flags, request for RoI the full RoD of the input
            if (inputEffect) {
                StatusEnum stat = inputEffect->getRegionOfDefinition(inputEffect->getRenderHash(), time, scale, view, &inputRoD);
                if (stat != eStatusFailed) {
                    inputRoDSet = true;
                }
            }

            roi = inputRoD;

        }
    }

    if ( roi.isNull() ) {
        return ImagePtr();
    }


    if (isIdentity) {
        assert(identityInput.get() != this);
        ///If the effect is an identity but it didn't ask for the effect's image of which it is identity
        ///return a null image (only when non analysis)
        if ( (identityInput != inputEffect) && !isAnalysisPass ) {
            return ImagePtr();
        }
    }


    ///Does this node supports images at a scale different than 1
    bool renderFullScaleThenDownscale = (!supportsRenderScale() && mipMapLevel != 0 && returnStorage == eStorageModeRAM);

    ///Do we want to render the graph upstream at scale 1 or at the requested render scale ? (user setting)
    bool renderScaleOneUpstreamIfRenderScaleSupportDisabled = false;
    unsigned int renderMappedMipMapLevel = mipMapLevel;
    if (renderFullScaleThenDownscale) {
        renderScaleOneUpstreamIfRenderScaleSupportDisabled = getNode()->useScaleOneImagesWhenRenderScaleSupportIsDisabled();
        if (renderScaleOneUpstreamIfRenderScaleSupportDisabled) {
            renderMappedMipMapLevel = 0;
        }
    }

    ///Both the result of getRegionsOfInterest and optionalBounds are in canonical coordinates, we have to convert in both cases
    ///Convert to pixel coordinates
    const double par = getAspectRatio(inputNb);
    ImageBitDepthEnum depth = getBitDepth(inputNb);
    ImageComponents components;
    ImageComponents clipPrefComps = getComponents(inputNb);

    if (layer) {
        components = *layer;
    } else {
        components = clipPrefComps;
    }


    RectI pixelRoI;
    roi.toPixelEnclosing(renderScaleOneUpstreamIfRenderScaleSupportDisabled ? 0 : mipMapLevel, par, &pixelRoI);

    ImagePtr inputImg;


    std::list<ImageComponents> requestedComps;
    requestedComps.push_back(isMask ? maskComps : components);
    std::map<ImageComponents, ImagePtr> inputImages;
    RenderRoIRetCode retCode = inputEffect->renderRoI(RenderRoIArgs(time,
                                                                    scale,
                                                                    renderMappedMipMapLevel,
                                                                    view,
                                                                    byPassCache,
                                                                    pixelRoI,
                                                                    RectD(),
                                                                    requestedComps,
                                                                    depth,
                                                                    true,
                                                                    shared_from_this(),
                                                                    returnStorage,
                                                                    thisEffectRenderTime,
                                                                    inputImagesThreadLocal), &inputImages);

    if ( inputImages.empty() || (retCode != eRenderRoIRetCodeOk) ) {
        return ImagePtr();
    }
    assert(inputImages.size() == 1);

    inputImg = inputImages.begin()->second;

    if ( !pixelRoI.intersects( inputImg->getBounds() ) ) {
        //The RoI requested does not intersect with the bounds of the input image, return a NULL image.
#ifdef DEBUG
        qDebug() << getNode()->getScriptName_mt_safe().c_str() << ": The RoI requested to" << inputEffect->getScriptName_mt_safe().c_str() << "does not intersect with the bounds of the input image";
#endif

        return ImagePtr();
    }

    /*
     * From now on this is the generic part. We first call renderRoI and then convert to the appropriate scale/components if needed.
     * Note that since the image has been pre-rendered before by the recursive nature of the algorithm, the call to renderRoI will be
     * instantaneous thanks to the image cache.
     */


    if (roiPixel) {
        *roiPixel = pixelRoI;
    }
    unsigned int inputImgMipMapLevel = inputImg->getMipMapLevel();

    ///If the plug-in doesn't support the render scale, but the image is downscaled, up-scale it.
    ///Note that we do NOT cache it because it is really low def!
    ///For OpenGL textures, we do not do it because GL_TEXTURE_2D uses normalized texture coordinates anyway, so any OpenGL plug-in should support render scale.
    if (!dontUpscale  && renderFullScaleThenDownscale && (inputImgMipMapLevel != 0) && returnStorage == eStorageModeRAM) {
        assert(inputImgMipMapLevel != 0);
        ///Resize the image according to the requested scale
        ImageBitDepthEnum bitdepth = inputImg->getBitDepth();
        RectI bounds;
        inputImg->getRoD().toPixelEnclosing(0, par, &bounds);
        ImagePtr rescaledImg( new Image( inputImg->getComponents(), inputImg->getRoD(),
                                         bounds, 0, par, bitdepth, inputImg->getPremultiplication(), inputImg->getFieldingOrder() ) );
        inputImg->upscaleMipMap( inputImg->getBounds(), inputImgMipMapLevel, 0, rescaledImg.get() );
        if (roiPixel) {
            RectD canonicalPixelRoI;

            if (!inputRoDSet) {
                StatusEnum st = inputEffect->getRegionOfDefinition(inputEffect->getRenderHash(), time, scale, view, &inputRoD);
                Q_UNUSED(st);
            }

            pixelRoI.toCanonical(inputImgMipMapLevel, par, inputRoD, &canonicalPixelRoI);
            canonicalPixelRoI.toPixelEnclosing(0, par, roiPixel);
            pixelRoI = *roiPixel;
        }

        inputImg = rescaledImg;
    }


    //Remap if needed
    ImagePremultiplicationEnum outputPremult;
    if ( components.isColorPlane() ) {
        outputPremult = inputEffect->getPremult();
    } else {
        outputPremult = eImagePremultiplicationOpaque;
    }


    if (mapToClipPrefs) {
        inputImg = convertPlanesFormatsIfNeeded(getApp(), inputImg, pixelRoI, clipPrefComps, depth, getNode()->usesAlpha0ToConvertFromRGBToRGBA(), outputPremult, channelForMask);
    }

#ifdef DEBUG
    ///Check that the rendered image contains what we requested.
    if ( !mapToClipPrefs && ( ( !isMask && (inputImg->getComponents() != components) ) || ( isMask && (inputImg->getComponents() != maskComps) ) ) ) {
        ImageComponents cc;
        if (isMask) {
            cc = maskComps;
        } else {
            cc = components;
        }
        qDebug() << "WARNING:" << getNode()->getScriptName_mt_safe().c_str() << "requested" << cc.getComponentsGlobalName().c_str() << "but" << inputEffect->getScriptName_mt_safe().c_str() << "returned an image with"
                 << inputImg->getComponents().getComponentsGlobalName().c_str();
        qDebug() << inputEffect->getScriptName_mt_safe().c_str() << "output clip preference is" << inputEffect->getComponents(-1).getComponentsGlobalName().c_str();
    }

#endif

    if ( inputImagesThreadLocal.empty() ) {
        ///If the effect is analysis (e.g: Tracker) there's no input images in the tread local storage, hence add it
        tls->currentRenderArgs.inputImages[inputNb].push_back(inputImg);
    }

    return inputImg;
} // getImage

void
EffectInstance::calcDefaultRegionOfDefinition(U64 /*hash*/,
                                              double /*time*/,
                                              const RenderScale & /*scale*/,
                                              ViewIdx /*view*/,
                                              RectD *rod)
{
    Format projectDefault;

    getRenderFormat(&projectDefault);
    *rod = RectD( projectDefault.left(), projectDefault.bottom(), projectDefault.right(), projectDefault.top() );
}

StatusEnum
EffectInstance::getRegionOfDefinition(U64 hash,
                                      double time,
                                      const RenderScale & scale,
                                      ViewIdx view,
                                      RectD* rod) //!< rod is in canonical coordinates
{
    bool firstInput = true;
    RenderScale renderMappedScale = scale;

    assert( !( (supportsRenderScaleMaybe() == eSupportsNo) && !(scale.x == 1. && scale.y == 1.) ) );

    for (int i = 0; i < getMaxInputCount(); ++i) {
        if ( isInputMask(i) ) {
            continue;
        }
        EffectInstancePtr input = getInput(i);
        if (input) {
            RectD inputRod;
            bool isProjectFormat;
            StatusEnum st = input->getRegionOfDefinition_public(hash, time, renderMappedScale, view, &inputRod, &isProjectFormat);
            assert(inputRod.x2 >= inputRod.x1 && inputRod.y2 >= inputRod.y1);
            if (st == eStatusFailed) {
                return st;
            }

            if (firstInput) {
                *rod = inputRod;
                firstInput = false;
            } else {
                rod->merge(inputRod);
            }
            assert(rod->x1 <= rod->x2 && rod->y1 <= rod->y2);
        }
    }

    // if rod was not set, return default, else return OK
    return firstInput ? eStatusReplyDefault : eStatusOK;
}

bool
EffectInstance::ifInfiniteApplyHeuristic(U64 hash,
                                         double time,
                                         const RenderScale & scale,
                                         ViewIdx view,
                                         RectD* rod) //!< input/output
{
    /*If the rod is infinite clip it to the project's default*/

    Format projectFormat;

    getRenderFormat(&projectFormat);
    RectD projectDefault = projectFormat.toCanonicalFormat();
    /// FIXME: before removing the assert() (I know you are tempted) please explain (here: document!) if the format rectangle can be empty and in what situation(s)
    assert( !projectDefault.isNull() );

    assert(rod);
    if ( rod->isNull() ) {
        // if the RoD is empty, set it to a "standard" empty RoD (0,0,0,0)
        rod->clear();
    }
    assert(rod->x1 <= rod->x2 && rod->y1 <= rod->y2);
    bool x1Infinite = rod->x1 <= kOfxFlagInfiniteMin;
    bool y1Infinite = rod->y1 <= kOfxFlagInfiniteMin;
    bool x2Infinite = rod->x2 >= kOfxFlagInfiniteMax;
    bool y2Infinite = rod->y2 >= kOfxFlagInfiniteMax;

    ///Get the union of the inputs.
    RectD inputsUnion;

    ///Do the following only if one coordinate is infinite otherwise we wont need the RoD of the input
    if (x1Infinite || y1Infinite || x2Infinite || y2Infinite) {
        // initialize with the effect's default RoD, because inputs may not be connected to other effects (e.g. Roto)
        calcDefaultRegionOfDefinition(hash, time, scale, view, &inputsUnion);
        bool firstInput = true;
        for (int i = 0; i < getMaxInputCount(); ++i) {
            EffectInstancePtr input = getInput(i);
            if (input) {
                RectD inputRod;
                bool isProjectFormat;
                RenderScale inputScale = scale;
                if (input->supportsRenderScaleMaybe() == eSupportsNo) {
                    inputScale.x = inputScale.y = 1.;
                }
                StatusEnum st = input->getRegionOfDefinition_public(hash, time, inputScale, view, &inputRod, &isProjectFormat);
                if (st != eStatusFailed) {
                    if (firstInput) {
                        inputsUnion = inputRod;
                        firstInput = false;
                    } else {
                        inputsUnion.merge(inputRod);
                    }
                }
            }
        }
    }
    ///If infinite : clip to inputsUnion if not null, otherwise to project default

    // BE CAREFUL:
    // std::numeric_limits<int>::infinity() does not exist (check std::numeric_limits<int>::has_infinity)
    bool isProjectFormat = false;
    if (x1Infinite) {
        if ( !inputsUnion.isNull() ) {
            rod->x1 = std::min(inputsUnion.x1, projectDefault.x1);
        } else {
            rod->x1 = projectDefault.x1;
            isProjectFormat = true;
        }
        rod->x2 = std::max(rod->x1, rod->x2);
    }
    if (y1Infinite) {
        if ( !inputsUnion.isNull() ) {
            rod->y1 = std::min(inputsUnion.y1, projectDefault.y1);
        } else {
            rod->y1 = projectDefault.y1;
            isProjectFormat = true;
        }
        rod->y2 = std::max(rod->y1, rod->y2);
    }
    if (x2Infinite) {
        if ( !inputsUnion.isNull() ) {
            rod->x2 = std::max(inputsUnion.x2, projectDefault.x2);
        } else {
            rod->x2 = projectDefault.x2;
            isProjectFormat = true;
        }
        rod->x1 = std::min(rod->x1, rod->x2);
    }
    if (y2Infinite) {
        if ( !inputsUnion.isNull() ) {
            rod->y2 = std::max(inputsUnion.y2, projectDefault.y2);
        } else {
            rod->y2 = projectDefault.y2;
            isProjectFormat = true;
        }
        rod->y1 = std::min(rod->y1, rod->y2);
    }
    if ( isProjectFormat && !isGenerator() ) {
        isProjectFormat = false;
    }
    assert(rod->x1 <= rod->x2 && rod->y1 <= rod->y2);

    return isProjectFormat;
} // ifInfiniteApplyHeuristic

void
EffectInstance::getRegionsOfInterest(double time,
                                     const RenderScale & scale,
                                     const RectD & /*outputRoD*/, //!< the RoD of the effect, in canonical coordinates
                                     const RectD & renderWindow, //!< the region to be rendered in the output image, in Canonical Coordinates
                                     ViewIdx view,
                                     RoIMap* ret)
{
    bool tilesSupported = supportsTiles();

    for (int i = 0; i < getMaxInputCount(); ++i) {
        EffectInstancePtr input = getInput(i);
        if (input) {
            if (tilesSupported) {
                ret->insert( std::make_pair(input, renderWindow) );
            } else {
                //Tiles not supported: get the RoD as RoI
                RectD rod;
                bool isPF;
                RenderScale inpScale(input->supportsRenderScale() ? scale.x : 1.);
                StatusEnum stat = input->getRegionOfDefinition_public(input->getRenderHash(), time, inpScale, view, &rod, &isPF);
                if (stat == eStatusFailed) {
                    return;
                }
                ret->insert( std::make_pair(input, rod) );
            }
        }
    }
}

FramesNeededMap
EffectInstance::getFramesNeeded(double time,
                                ViewIdx view)
{
    FramesNeededMap ret;
    RangeD defaultRange;

    defaultRange.min = defaultRange.max = time;
    std::vector<RangeD> ranges;
    ranges.push_back(defaultRange);
    FrameRangesMap defViewRange;
    defViewRange.insert( std::make_pair(view, ranges) );
    for (int i = 0; i < getMaxInputCount(); ++i) {

        EffectInstancePtr input = getInput(i);
        if (input) {
            ret.insert( std::make_pair(i, defViewRange) );
        }

    }

    return ret;
}

void
EffectInstance::getFrameRange(double *first,
                              double *last)
{
    // default is infinite if there are no non optional input clips
    *first = INT_MIN;
    *last = INT_MAX;
    for (int i = 0; i < getMaxInputCount(); ++i) {
        EffectInstancePtr input = getInput(i);
        if (input) {
            double inpFirst, inpLast;
            input->getFrameRange(&inpFirst, &inpLast);
            if (i == 0) {
                *first = inpFirst;
                *last = inpLast;
            } else {
                if (inpFirst < *first) {
                    *first = inpFirst;
                }
                if (inpLast > *last) {
                    *last = inpLast;
                }
            }
        }
    }
}

EffectInstance::NotifyRenderingStarted_RAII::NotifyRenderingStarted_RAII(Node* node)
    : _node(node)
    , _didGroupEmit(false)
{
    _didEmit = node->notifyRenderingStarted();

    // If the node is in a group, notify also the group
    NodeCollectionPtr group = node->getGroup();
    if (group) {
        NodeGroupPtr isGroupNode = toNodeGroup(group);
        if (isGroupNode) {
            _didGroupEmit = isGroupNode->getNode()->notifyRenderingStarted();
        }
    }
}

EffectInstance::NotifyRenderingStarted_RAII::~NotifyRenderingStarted_RAII()
{
    if (_didEmit) {
        _node->notifyRenderingEnded();
    }
    if (_didGroupEmit) {
        NodeCollectionPtr group = _node->getGroup();
        if (group) {
            NodeGroupPtr isGroupNode = toNodeGroup(group);
            if (isGroupNode) {
                isGroupNode->getNode()->notifyRenderingEnded();
            }
        }
    }
}

EffectInstance::NotifyInputNRenderingStarted_RAII::NotifyInputNRenderingStarted_RAII(Node* node,
                                                                                     int inputNumber)
    : _node(node)
    , _inputNumber(inputNumber)
{
    _didEmit = node->notifyInputNIsRendering(inputNumber);
}

EffectInstance::NotifyInputNRenderingStarted_RAII::~NotifyInputNRenderingStarted_RAII()
{
    if (_didEmit) {
        _node->notifyInputNIsFinishedRendering(_inputNumber);
    }
}

static void
getOrCreateFromCacheInternal(const ImageKey & key,
                             const ImageParamsPtr & params,
                             const OSGLContextPtr& glContext,
                             bool useCache,
                             ImagePtr* image)
{
    if (!useCache) {
        image->reset( new Image(key, params) );
    } else {
        if (params->getStorageInfo().mode == eStorageModeRAM || params->getStorageInfo().mode == eStorageModeGLTex) {
            appPTR->getImageOrCreate(key, params, 0, image);
        } else if (params->getStorageInfo().mode == eStorageModeDisk) {
            appPTR->getImageOrCreate_diskCache(key, params, image);
        }

        if (!*image) {
            std::stringstream ss;
            ss << "Failed to allocate an image of ";
            const CacheEntryStorageInfo& info = params->getStorageInfo();
            std::size_t size = info.dataTypeSize * info.numComponents * info.bounds.area();
            ss << printAsRAM(size).toStdString();
            Dialogs::errorDialog( QCoreApplication::translate("EffectInstance", "Out of memory").toStdString(), ss.str() );

            return;
        }

        /*
         * Note that at this point the image is already exposed to other threads and another one might already have allocated it.
         * This function does nothing if it has been reallocated already.
         */
        (*image)->allocateMemory();


        /*
         * Another thread might have allocated the same image in the cache but with another RoI, make sure
         * it is big enough for us, or resize it to our needs.
         */


        (*image)->ensureBounds( glContext, params->getBounds() );
    }
}

ImagePtr
EffectInstance::convertOpenGLTextureToCachedRAMImage(const ImagePtr& image, bool enableCaching)
{
    assert(image->getStorageMode() == eStorageModeGLTex);

    ImageParamsPtr params( new ImageParams( *image->getParams() ) );
    CacheEntryStorageInfo& info = params->getStorageInfo();
    info.mode = eStorageModeRAM;

    OSGLContextPtr context = getThreadLocalOpenGLContext();
    assert(context);
    if (!context) {
        throw std::runtime_error("No OpenGL context attached");
    }

    ImagePtr ramImage;
    getOrCreateFromCacheInternal(image->getKey(), params, context, enableCaching, &ramImage);
    if (!ramImage) {
        return ramImage;
    }



    ramImage->pasteFrom(*image, image->getBounds(), false, context);
    ramImage->markForRendered(image->getBounds());

    return ramImage;
}


template <typename GL>
static ImagePtr
convertRAMImageToOpenGLTextureForGL(const ImagePtr& image,
                                    const RectI& roi,
                                    const OSGLContextPtr& glContext)
{
    assert(image->getStorageMode() != eStorageModeGLTex);
    RectI srcBounds = image->getBounds();

    ImageParamsPtr params( new ImageParams( *image->getParams() ) );
    CacheEntryStorageInfo& info = params->getStorageInfo();
    info.bounds = roi;
    info.mode = eStorageModeGLTex;
    info.textureTarget = GL_TEXTURE_2D;
    info.isGPUTexture = GL::isGPU();


    GLuint pboID = glContext->getOrCreatePBOId();
    assert(pboID != 0);
    GL::glEnable(GL_TEXTURE_2D);
    // bind PBO to update texture source
    GL::glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, pboID);

    std::size_t pixelSize = 4 * info.dataTypeSize;
    std::size_t dstRowBytes = roi.width() * pixelSize;
    std::size_t dataSize = dstRowBytes * roi.height();

    // Note that glMapBufferARB() causes sync issue.
    // If GPU is working with this buffer, glMapBufferARB() will wait(stall)
    // until GPU to finish its job. To avoid waiting (idle), you can call
    // first glBufferDataARB() with NULL pointer before glMapBufferARB().
    // If you do that, the previous data in PBO will be discarded and
    // glMapBufferARB() returns a new allocated pointer immediately
    // even if GPU is still working with the previous data.
    GL::glBufferDataARB(GL_PIXEL_UNPACK_BUFFER_ARB, dataSize, 0, GL_DYNAMIC_DRAW_ARB);

    bool useTmpImage = image->getComponentsCount() != 4;
    ImagePtr tmpImg;
    std::size_t srcRowBytes;
    if (useTmpImage) {
        tmpImg.reset( new Image( ImageComponents::getRGBAComponents(), image->getRoD(), roi, 0, image->getPixelAspectRatio(), eImageBitDepthFloat, image->getPremultiplication(), image->getFieldingOrder(), false, eStorageModeRAM) );
        tmpImg->setKey(image->getKey());
        if (tmpImg->getComponents() == image->getComponents()) {
            tmpImg->pasteFrom(*image, roi);
        } else {
            image->convertToFormat(roi, eViewerColorSpaceLinear, eViewerColorSpaceLinear, -1, false, false, tmpImg.get());
        }
        srcRowBytes = tmpImg->getRowElements() * sizeof(float);
    } else {
        srcRowBytes = image->getRowElements() * sizeof(float);
    }

    // Intersect the Roi with the src image

    RectI realRoI;
    roi.intersect(image->getBounds(), &realRoI);

    Image::ReadAccess racc( tmpImg ? tmpImg.get() : image.get() );
    const unsigned char* srcRoIPixels = racc.pixelAt(realRoI.x1, realRoI.y1);
    assert(srcRoIPixels);



    unsigned char* gpuData = (unsigned char*)GL::glMapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, GL_WRITE_ONLY_ARB);
    if (gpuData) {
        // Copy the RoI
        std::size_t roiRowBytes = realRoI.width() * pixelSize;
        // update data directly on the mapped buffer

        unsigned char* dstData = gpuData;
        const unsigned char* srcRoIData = srcRoIPixels;
        for (int y = realRoI.y1; y < realRoI.y2; ++y) {
            memcpy(dstData, srcRoIData, roiRowBytes);
            srcRoIData += srcRowBytes;
            dstData += dstRowBytes;
        }

        // Null the 4 potential rectangles between the realRoI and RoI
        RectI aRect,bRect,cRect,dRect;
        Image::getABCDRectangles(realRoI, roi, aRect, bRect, cRect, dRect);


        if (!aRect.isNull()) {
            unsigned char* pix = Image::getPixelAddress_internal(aRect.x1, aRect.y1, gpuData, (int)pixelSize, roi);
            assert(pix);
            std::size_t memsize = aRect.area() * pixelSize;
            std::memset(pix, 0, memsize);
        }

        if (!cRect.isNull()) {
            unsigned char* pix = Image::getPixelAddress_internal(cRect.x1, cRect.y1, gpuData, (int)pixelSize, roi);
            assert(pix);
            std::size_t memsize = cRect.area() * pixelSize;
            std::memset(pix, 0, memsize);
        }
        if ( !bRect.isNull() ) {
            unsigned char* pix = Image::getPixelAddress_internal(bRect.x1, bRect.y1, gpuData, (int)pixelSize, roi);
            assert(pix);
            int mw = roi.width();
            std::size_t rowsize = mw * pixelSize;
            int bw = bRect.width();
            std::size_t rectRowSize = bw * pixelSize;
            for (int y = bRect.y1; y < bRect.y2; ++y, pix += rowsize) {
                std::memset(pix, 0, rectRowSize);
            }
        }

        if ( !dRect.isNull() ) {
            unsigned char* pix = Image::getPixelAddress_internal(dRect.x1, dRect.y1, gpuData, (int)pixelSize, roi);
            assert(pix);
            int mw = roi.width();
            std::size_t rowsize = mw * pixelSize;
            int dw = dRect.width();
            std::size_t rectRowSize = dw * pixelSize;
            for (int y = dRect.y1; y < dRect.y2; ++y, pix += rowsize) {
                std::memset(pix, 0, rectRowSize);
            }
        }

        GLboolean result = GL::glUnmapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB); // release the mapped buffer
        assert(result == GL_TRUE);
        Q_UNUSED(result);
    }
    glCheckError(GL);

    // The creation of the image will use glTexImage2D and will get filled with the PBO
    ImagePtr gpuImage;
    getOrCreateFromCacheInternal(image->getKey(), params, glContext, false /*useCache*/, &gpuImage);

    // it is good idea to release PBOs with ID 0 after use.
    // Once bound with 0, all pixel operations are back to normal ways.
    GL::glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
    //glBindTexture(GL_TEXTURE_2D, 0); // useless, we didn't bind anything
    glCheckError(GL);
    
    
    return gpuImage;

}   // convertRAMImageToOpenGLTextureForGL


ImagePtr
EffectInstance::convertRAMImageRoIToOpenGLTexture(const ImagePtr& image, const RectI& roi, const OSGLContextPtr& glContext)
{
    if (glContext->isGPUContext()) {
        return convertRAMImageToOpenGLTextureForGL<GL_GPU>(image, roi, glContext);
    } else {
        return convertRAMImageToOpenGLTextureForGL<GL_CPU>(image, roi, glContext);
    }
}

ImagePtr
EffectInstance::convertRAMImageToOpenGLTexture(const ImagePtr& image, const OSGLContextPtr& glContext)
{
    if (glContext->isGPUContext()) {
        return convertRAMImageToOpenGLTextureForGL<GL_GPU>(image, image->getBounds(), glContext);
    } else {
        return convertRAMImageToOpenGLTextureForGL<GL_CPU>(image, image->getBounds(), glContext);
    }
}

ImagePtr
EffectInstance::convertRAMImageToOpenGLTexture(const ImagePtr& image)
{
    OSGLContextPtr context = getThreadLocalOpenGLContext();
    assert(context);
    if (!context) {
        throw std::runtime_error("No OpenGL context attached");
    }
    return convertRAMImageToOpenGLTexture(image, context);
}

static ImagePtr ensureImageScale(unsigned int mipMapLevel,
                                 const ImagePtr& image,
                                 const ImageKey & key,
                                 const RectI* boundsParam,
                                 const RectD* rodParam,
                                 const OSGLContextAttacherPtr& glContextAttacher)
{
    if (image->getMipMapLevel() == mipMapLevel) {
        return image;
    }

    ImagePtr imageToConvert = image;

    ImageParamsPtr oldParams = imageToConvert->getParams();

    if (imageToConvert->getMipMapLevel() < mipMapLevel) {

        //This is the bounds of the upscaled image
        RectI imgToConvertBounds = imageToConvert->getBounds();

        //The rodParam might be different of oldParams->getRoD() simply because the RoD is dependent on the mipmap level
        const RectD & rod = rodParam ? *rodParam : oldParams->getRoD();

        RectI downscaledBounds;
        rod.toPixelEnclosing(mipMapLevel, imageToConvert->getPixelAspectRatio(), &downscaledBounds);

        if (boundsParam) {
            downscaledBounds.merge(*boundsParam);
        }
        ImageParamsPtr imageParams = Image::makeParams(rod,
                                                       downscaledBounds,
                                                       oldParams->getPixelAspectRatio(),
                                                       mipMapLevel,
                                                       oldParams->isRodProjectFormat(),
                                                       oldParams->getComponents(),
                                                       oldParams->getBitDepth(),
                                                       oldParams->getPremultiplication(),
                                                       oldParams->getFieldingOrder(),
                                                       eStorageModeRAM);



        imageParams->setMipMapLevel(mipMapLevel);


        ImagePtr img;
        getOrCreateFromCacheInternal(key, imageParams, glContextAttacher ? glContextAttacher->getContext() : OSGLContextPtr(), imageToConvert->usesBitMap(), &img);
        if (!img) {
            return img;
        }


        /*
         Since the RoDs of the 2 mipmaplevels are different, their bounds do not match exactly as po2
         To determine which portion we downscale, we downscale the initial image bounds to the mipmap level
         of the downscale image, clip it against the bounds of the downscale image, re-upscale it to the
         original mipmap level and ensure that it lies into the original image bounds
         */
        int downscaleLevels = img->getMipMapLevel() - imageToConvert->getMipMapLevel();
        RectI dstRoi = imgToConvertBounds.downscalePowerOfTwoSmallestEnclosing(downscaleLevels);
        dstRoi.intersect(downscaledBounds, &dstRoi);
        dstRoi = dstRoi.upscalePowerOfTwo(downscaleLevels);
        dstRoi.intersect(imgToConvertBounds, &dstRoi);

        if (imgToConvertBounds.area() > 1) {
            imageToConvert->downscaleMipMap( rod,
                                            dstRoi,
                                            imageToConvert->getMipMapLevel(), img->getMipMapLevel(),
                                            imageToConvert->usesBitMap(),
                                            img.get() );
        } else {
            img->pasteFrom(*imageToConvert, imgToConvertBounds);
        }

        imageToConvert = img;
    } else {

        //This is the bounds of the downscaled image
        RectI upscaledImgBounds;
        //The rodParam might be different of oldParams->getRoD() simply because the RoD is dependent on the mipmap level
        const RectD & rod = rodParam ? *rodParam : oldParams->getRoD();
        rod.toPixelEnclosing(mipMapLevel, imageToConvert->getPixelAspectRatio(), &upscaledImgBounds);

        ImageParamsPtr imageParams = Image::makeParams(rod,
                                                       upscaledImgBounds,
                                                       oldParams->getPixelAspectRatio(),
                                                       mipMapLevel,
                                                       oldParams->isRodProjectFormat(),
                                                       oldParams->getComponents(),
                                                       oldParams->getBitDepth(),
                                                       oldParams->getPremultiplication(),
                                                       oldParams->getFieldingOrder(),
                                                       eStorageModeRAM);



        imageParams->setMipMapLevel(mipMapLevel);


        ImagePtr img;
        getOrCreateFromCacheInternal(key, imageParams, glContextAttacher ? glContextAttacher->getContext() : OSGLContextPtr(), imageToConvert->usesBitMap(), &img);
        if (!img) {
            return img;
        }

        imageToConvert->upscaleMipMap( imageToConvert->getBounds(), imageToConvert->getMipMapLevel(), mipMapLevel, img.get() );
        imageToConvert = img;
    }
    return imageToConvert;
}

void
EffectInstance::getImageFromCacheAndConvertIfNeeded(bool /*useCache*/,
                                                    bool isDuringPaintStroke,
                                                    StorageModeEnum storage,
                                                    StorageModeEnum returnStorage,
                                                    const ImageKey & key,
                                                    unsigned int mipMapLevel,
                                                    const RectI* boundsParam,
                                                    const RectD* rodParam,
                                                    const RectI& roi,
                                                    ImageBitDepthEnum bitdepth,
                                                    const ImageComponents & components,
                                                    const EffectInstance::InputImagesMap & inputImages,
                                                    const RenderStatsPtr & stats,
                                                    const OSGLContextAttacherPtr& glContextAttacher,
                                                    ImagePtr* image)
{
    ImageList cachedImages;
    bool isCached = false;

    ///Find first something in the input images list
    if ( !inputImages.empty() ) {
        for (InputImagesMap::const_iterator it = inputImages.begin(); it != inputImages.end(); ++it) {
            for (ImageList::const_iterator it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
                if ( !it2->get() ) {
                    continue;
                }
                const ImageKey & imgKey = (*it2)->getKey();
                if (imgKey == key) {
                    cachedImages.push_back(*it2);
                    isCached = true;
                }
            }
        }
    }

    if (!isCached && isDuringPaintStroke) {

        ImagePtr strokeImage = getNode()->getPaintBuffer();
        if (strokeImage && strokeImage->getStorageMode() == storage) {
            if (strokeImage->getMipMapLevel() != mipMapLevel) {
                // conver the image to RAM if needed and convert scale and convert back to GPU if needed
                if (strokeImage->getStorageMode() == eStorageModeGLTex) {
                    assert(glContextAttacher);
                    glContextAttacher->attach();
                    strokeImage = convertOpenGLTextureToCachedRAMImage(strokeImage, false);
                }
                strokeImage = ensureImageScale(mipMapLevel, strokeImage, key, boundsParam, rodParam, glContextAttacher);
                if (storage == eStorageModeGLTex) {
                    strokeImage = convertRAMImageToOpenGLTexture(strokeImage, glContextAttacher ? glContextAttacher->getContext() : OSGLContextPtr());
                }
            }
            getNode()->setPaintBuffer(strokeImage);
            *image = strokeImage;
            return;
        }
    }

    if (!isCached) {
        // For textures, we lookup for a RAM image, if found we convert it to a texture
        if ( (storage == eStorageModeRAM) || (storage == eStorageModeGLTex) ) {
            isCached = appPTR->getImage(key, &cachedImages);
        } else if (storage == eStorageModeDisk) {
            isCached = appPTR->getImage_diskCache(key, &cachedImages);
        }
    }

    if (stats && stats->isInDepthProfilingEnabled() && !isCached) {
        stats->addCacheInfosForNode(getNode(), true, false);
    }

    if (isCached) {
        ///A ptr to a higher resolution of the image or an image with different comps/bitdepth
        ImagePtr imageToConvert;

        for (ImageList::iterator it = cachedImages.begin(); it != cachedImages.end(); ++it) {
            unsigned int imgMMlevel = (*it)->getMipMapLevel();
            const ImageComponents & imgComps = (*it)->getComponents();
            ImageBitDepthEnum imgDepth = (*it)->getBitDepth();

            if ( (*it)->getParams()->isRodProjectFormat() ) {
                ////If the image was cached with a RoD dependent on the project format, but the project format changed,
                ////just discard this entry
                Format projectFormat;
                getRenderFormat(&projectFormat);
                RectD canonicalProject = projectFormat.toCanonicalFormat();
                if ( canonicalProject != (*it)->getRoD() ) {
                    appPTR->removeFromNodeCache(*it);
                    continue;
                }
            }

            ///Throw away images that are not even what the node want to render
            /*if ( ( imgComps.isColorPlane() && nodePrefComps.isColorPlane() && (imgComps != nodePrefComps) ) || (imgDepth != nodePrefDepth) ) {
                appPTR->removeFromNodeCache(*it);
                continue;
            }*/

            bool convertible = imgComps.isConvertibleTo(components);
            if ( (imgMMlevel == mipMapLevel) && convertible &&
                 ( getSizeOfForBitDepth(imgDepth) >= getSizeOfForBitDepth(bitdepth) ) /* && imgComps == components && imgDepth == bitdepth*/ ) {
                ///We found  a matching image

                *image = *it;
                break;
            } else {
                if ( !convertible || ( getSizeOfForBitDepth(imgDepth) < getSizeOfForBitDepth(bitdepth) ) ) {
                    // not enough components or bit-depth is not as deep, don't use the image
                    continue;
                }

                if (imgMMlevel > mipMapLevel) {
                    if (!isPaintingOverItselfEnabled()) {
                        // mipmap level is higher, use it only if plug-in is painting over itself and absolutely requires the data
                        continue;
                    }
                    if (imageToConvert) {
                        ///We found an image which scale is closer to the requested mipmap level we want, use it instead
                        if ( imgMMlevel < imageToConvert->getMipMapLevel() ) {
                            imageToConvert = *it;
                        }
                    } else {
                        imageToConvert = *it;
                    }
                } else if (imgMMlevel < mipMapLevel) {
                    if (imageToConvert) {
                        // We found an image which scale is closer to the requested mipmap level we want, use it instead
                        if ( imgMMlevel > imageToConvert->getMipMapLevel() ) {
                            imageToConvert = *it;
                        }
                    } else {
                        imageToConvert = *it;
                    }
                } else {
                    imageToConvert = *it;
                }

            }
        } //end for

        if (imageToConvert && !*image) {
            ///Ensure the image is allocated
            (imageToConvert)->allocateMemory();


            if (imageToConvert->getMipMapLevel() != mipMapLevel) {
                imageToConvert = ensureImageScale(mipMapLevel, imageToConvert, key, boundsParam, rodParam, glContextAttacher);
                if (!imageToConvert) {
                    return;
                }
            }

            if (storage == eStorageModeGLTex) {

                // When using the GPU, we dont want to retrieve partially rendered image because rendering the portion
                // needed then reading it back to put it in the CPU image would take much more effort than just computing
                // the GPU image.
                std::list<RectI> restToRender;
                imageToConvert->getRestToRender(roi, restToRender);
                if ( restToRender.empty() ) {
                    if (returnStorage == eStorageModeGLTex) {
                        assert(glContextAttacher);
                        glContextAttacher->attach();
                        *image = convertRAMImageToOpenGLTexture(imageToConvert, glContextAttacher ? glContextAttacher->getContext() : OSGLContextPtr());
                    } else {
                        assert(returnStorage == eStorageModeRAM && (imageToConvert->getStorageMode() == eStorageModeRAM || imageToConvert->getStorageMode() == eStorageModeDisk));
                        // If renderRoI must return a RAM image, don't convert it back again!
                        *image = imageToConvert;
                    }
                }
            } else {
                *image = imageToConvert;
            }
            //assert(imageToConvert->getBounds().contains(bounds));
            if ( stats && stats->isInDepthProfilingEnabled() ) {
                stats->addCacheInfosForNode(getNode(), false, true);
            }
        } else if (*image) { //  else if (imageToConvert && !*image)
            ///Ensure the image is allocated
            if ( (*image)->getStorageMode() != eStorageModeGLTex ) {
                (*image)->allocateMemory();

                if (storage == eStorageModeGLTex) {

                    // When using the GPU, we dont want to retrieve partially rendered image because rendering the portion
                    // needed then reading it back to put it in the CPU image would take much more effort than just computing
                    // the GPU image.
                    std::list<RectI> restToRender;
                    (*image)->getRestToRender(roi, restToRender);
                    if ( restToRender.empty() ) {
                        // If renderRoI must return a RAM image, don't convert it back again!
                        if (returnStorage == eStorageModeGLTex) {
                            assert(glContextAttacher);
                            glContextAttacher->attach();
                            *image = convertRAMImageToOpenGLTexture(*image, glContextAttacher ? glContextAttacher->getContext() : OSGLContextPtr());
                        }
                    } else {
                        image->reset();
                        return;
                    }
                }
            }

            if ( stats && stats->isInDepthProfilingEnabled() ) {
                stats->addCacheInfosForNode(getNode(), false, false);
            }
        } else {
            if ( stats && stats->isInDepthProfilingEnabled() ) {
                stats->addCacheInfosForNode(getNode(), true, false);
            }
        }
    } // isCached
} // EffectInstance::getImageFromCacheAndConvertIfNeeded

void
EffectInstance::tryConcatenateTransforms(double time,
                                         ViewIdx view,
                                         const RenderScale & scale,
                                         InputMatrixMap* inputTransforms)
{
    bool canTransform = getNode()->getCurrentCanTransform();

    //An effect might not be able to concatenate transforms but can still apply a transform (e.g CornerPinMasked)
    std::list<int> inputHoldingTransforms;
    bool canApplyTransform = getInputsHoldingTransform(&inputHoldingTransforms);

    assert(inputHoldingTransforms.empty() || canApplyTransform);

    Transform::Matrix3x3 thisNodeTransform;
    EffectInstancePtr inputToTransform;
    bool getTransformSucceeded = false;

    if (canTransform) {
        /*
         * If getting the transform does not succeed, then this effect is treated as any other ones.
         */
        StatusEnum stat = getTransform_public(time, scale, view, &inputToTransform, &thisNodeTransform);
        if (stat == eStatusOK) {
            getTransformSucceeded = true;
        }
    }


    if ( (canTransform && getTransformSucceeded) || ( !canTransform && canApplyTransform && !inputHoldingTransforms.empty() ) ) {
        for (std::list<int>::iterator it = inputHoldingTransforms.begin(); it != inputHoldingTransforms.end(); ++it) {
            EffectInstancePtr input = getInput(*it);
            if (!input) {
                continue;
            }
            std::list<Transform::Matrix3x3> matricesByOrder; // from downstream to upstream
            InputMatrix im;
            im.newInputEffect = input;
            im.newInputNbToFetchFrom = *it;


            // recursion upstream
            bool inputCanTransform = false;
            bool inputIsDisabled  =  input->getNode()->isNodeDisabled();

            if (!inputIsDisabled) {
                inputCanTransform = input->getNode()->getCurrentCanTransform();
            }


            while ( input && (inputCanTransform || inputIsDisabled) ) {
                //input is either disabled, or identity or can concatenate a transform too
                if (inputIsDisabled) {
                    int prefInput;
                    input = input->getNearestNonDisabled();
                    prefInput = input ? input->getNode()->getPreferredInput() : -1;
                    if (prefInput == -1) {
                        break;
                    }

                    if (input) {
                        im.newInputNbToFetchFrom = prefInput;
                        im.newInputEffect = input;
                    }
                } else if (inputCanTransform) {
                    Transform::Matrix3x3 m;
                    inputToTransform.reset();
                    StatusEnum stat = input->getTransform_public(time, scale, view, &inputToTransform, &m);
                    if (stat == eStatusOK) {
                        matricesByOrder.push_back(m);
                        if (inputToTransform) {
                            im.newInputNbToFetchFrom = input->getInputNumber(inputToTransform);
                            im.newInputEffect = input;
                            input = inputToTransform;
                        }
                    } else {
                        break;
                    }
                } else {
                    assert(false);
                }

                if (input) {
                    inputIsDisabled = input->getNode()->isNodeDisabled();
                    if (!inputIsDisabled) {
                        inputCanTransform = input->getNode()->getCurrentCanTransform();
                    }
                }
            }

            if ( input && !matricesByOrder.empty() ) {
                assert(im.newInputEffect);

                ///Now actually concatenate matrices together
                im.cat.reset(new Transform::Matrix3x3);
                std::list<Transform::Matrix3x3>::iterator it2 = matricesByOrder.begin();
                *im.cat = *it2;
                ++it2;
                while ( it2 != matricesByOrder.end() ) {
                    *im.cat = Transform::matMul(*im.cat, *it2);
                    ++it2;
                }

                inputTransforms->insert( std::make_pair(*it, im) );
            }
        } //  for (std::list<int>::iterator it = inputHoldingTransforms.begin(); it != inputHoldingTransforms.end(); ++it)
    } // if ((canTransform && getTransformSucceeded) || (canApplyTransform && !inputHoldingTransforms.empty()))
} // EffectInstance::tryConcatenateTransforms

bool
EffectInstance::allocateImagePlane(const ImageKey & key,
                                   const RectD & rod,
                                   const RectI & downscaleImageBounds,
                                   const RectI & fullScaleImageBounds,
                                   bool isProjectFormat,
                                   const ImageComponents & components,
                                   ImageBitDepthEnum depth,
                                   ImagePremultiplicationEnum premult,
                                   ImageFieldingOrderEnum fielding,
                                   double par,
                                   unsigned int mipmapLevel,
                                   bool renderFullScaleThenDownscale,
                                   const OSGLContextPtr& glContext,
                                   StorageModeEnum storage,
                                   bool createInCache,
                                   ImagePtr* fullScaleImage,
                                   ImagePtr* downscaleImage)
{
    //If we're rendering full scale and with input images at full scale, don't cache the downscale image since it is cheap to
    //recreate, instead cache the full-scale image
    if (renderFullScaleThenDownscale) {
        downscaleImage->reset( new Image(components, rod, downscaleImageBounds, mipmapLevel, par, depth, premult, fielding, true) );
        ImageParamsPtr upscaledImageParams = Image::makeParams(rod,
                                                                               fullScaleImageBounds,
                                                                               par,
                                                                               0,
                                                                               isProjectFormat,
                                                                               components,
                                                                               depth,
                                                                               premult,
                                                                               fielding,
                                                                               storage,
                                                                               GL_TEXTURE_2D);
        //The upscaled image will be rendered with input images at full def, it is then the best possibly rendered image so cache it!

        fullScaleImage->reset();
        getOrCreateFromCacheInternal(key, upscaledImageParams, glContext, createInCache, fullScaleImage);

        if (!*fullScaleImage) {
            return false;
        }
    } else {
        ///Cache the image with the requested components instead of the remapped ones
        ImageParamsPtr cachedImgParams = Image::makeParams(rod,
                                                                           downscaleImageBounds,
                                                                           par,
                                                                           mipmapLevel,
                                                                           isProjectFormat,
                                                                           components,
                                                                           depth,
                                                                           premult,
                                                                           fielding,
                                                                           storage,
                                                                           GL_TEXTURE_2D);

        //Take the lock after getting the image from the cache or while allocating it
        ///to make sure a thread will not attempt to write to the image while its being allocated.
        ///When calling allocateMemory() on the image, the cache already has the lock since it added it
        ///so taking this lock now ensures the image will be allocated completetly

        getOrCreateFromCacheInternal(key, cachedImgParams, glContext, createInCache, downscaleImage);
        if (!*downscaleImage) {
            return false;
        }
        *fullScaleImage = *downscaleImage;
    }

    return true;
} // EffectInstance::allocateImagePlane

void
EffectInstance::transformInputRois(const EffectInstancePtr& self,
                                   const InputMatrixMapPtr & inputTransforms,
                                   double par,
                                   const RenderScale & scale,
                                   RoIMap* inputsRoi,
                                   const ReRoutesMapPtr& reroutesMap)
{
    if (!inputTransforms) {
        return;
    }
    //Transform the RoIs by the inverse of the transform matrix (which is in pixel coordinates)
    for (InputMatrixMap::const_iterator it = inputTransforms->begin(); it != inputTransforms->end(); ++it) {
        RectD transformedRenderWindow;
        EffectInstancePtr effectInTransformInput = self->getInput(it->first);
        assert(effectInTransformInput);


        RoIMap::iterator foundRoI = inputsRoi->find(effectInTransformInput);
        if ( foundRoI == inputsRoi->end() ) {
            //There might be no RoI because it was null
            continue;
        }

        // invert it
        Transform::Matrix3x3 invertTransform;
        double det = Transform::matDeterminant(*it->second.cat);
        if (det != 0.) {
            invertTransform = Transform::matInverse(*it->second.cat, det);
        }

        Transform::Matrix3x3 canonicalToPixel = Transform::matCanonicalToPixel(par, scale.x,
                                                                               scale.y, false);
        Transform::Matrix3x3 pixelToCanonical = Transform::matPixelToCanonical(par,  scale.x,
                                                                               scale.y, false);

        invertTransform = Transform::matMul(Transform::matMul(pixelToCanonical, invertTransform), canonicalToPixel);
        Transform::transformRegionFromRoD(foundRoI->second, invertTransform, transformedRenderWindow);

        //Replace the original RoI by the transformed RoI
        inputsRoi->erase(foundRoI);
        inputsRoi->insert( std::make_pair(it->second.newInputEffect->getInput(it->second.newInputNbToFetchFrom), transformedRenderWindow) );
        reroutesMap->insert( std::make_pair(it->first, it->second.newInputEffect) );
    }
}

EffectInstance::RenderRoIRetCode
EffectInstance::renderInputImagesForRoI(const FrameViewRequest* request,
                                        bool useTransforms,
                                        StorageModeEnum renderStorageMode,
                                        double time,
                                        ViewIdx view,
                                        const RectD & rod,
                                        const RectD & canonicalRenderWindow,
                                        const InputMatrixMapPtr& inputTransforms,
                                        unsigned int mipMapLevel,
                                        const RenderScale & renderMappedScale,
                                        bool useScaleOneInputImages,
                                        bool byPassCache,
                                        const FramesNeededMap & framesNeeded,
                                        const EffectInstance::ComponentsNeededMap & neededComps,
                                        EffectInstance::InputImagesMap *inputImages,
                                        RoIMap* inputsRoi)
{
    if (!request) {
        getRegionsOfInterest_public(time, renderMappedScale, rod, canonicalRenderWindow, view, inputsRoi);
    }
#ifdef DEBUG
    if ( !inputsRoi->empty() && framesNeeded.empty() && !isReader() && !isRotoPaintNode() ) {
        qDebug() << getNode()->getScriptName_mt_safe().c_str() << ": getRegionsOfInterestAction returned 1 or multiple input RoI(s) but returned "
                 << "an empty list with getFramesNeededAction";
    }
#endif


    return treeRecurseFunctor(true,
                              getNode(),
                              framesNeeded,
                              *inputsRoi,
                              inputTransforms,
                              useTransforms,
                              renderStorageMode,
                              mipMapLevel,
                              time,
                              view,
                              NodePtr(),
                              0,
                              inputImages,
                              &neededComps,
                              useScaleOneInputImages,
                              byPassCache);
}

EffectInstance::RenderingFunctorRetEnum
EffectInstance::Implementation::tiledRenderingFunctor(EffectInstance::Implementation::TiledRenderingFunctorArgs & args,
                                                      const RectToRender & specificData,
                                                      QThread* callingThread)
{
    ///Make the thread-storage live as long as the render action is called if we're in a newly launched thread in eRenderSafetyFullySafeFrame mode
    QThread* curThread = QThread::currentThread();

    if (callingThread != curThread) {
        ///We are in the case of host frame threading, see kOfxImageEffectPluginPropHostFrameThreading
        ///We know that in the renderAction, TLS will be needed, so we do a deep copy of the TLS from the caller thread
        ///to this thread
        appPTR->getAppTLS()->copyTLS(callingThread, curThread);
    }


    EffectInstance::RenderingFunctorRetEnum ret = tiledRenderingFunctor(specificData,
                                                                        args.glContext,
                                                                        args.renderFullScaleThenDownscale,
                                                                        args.isSequentialRender,
                                                                        args.isRenderResponseToUserInteraction,
                                                                        args.firstFrame,
                                                                        args.lastFrame,
                                                                        args.preferredInput,
                                                                        args.mipMapLevel,
                                                                        args.renderMappedMipMapLevel,
                                                                        args.rod,
                                                                        args.time,
                                                                        args.view,
                                                                        args.par,
                                                                        args.byPassCache,
                                                                        args.outputClipPrefDepth,
                                                                        args.outputClipPrefsComps,
                                                                        args.compsNeeded,
                                                                        args.processChannels,
                                                                        args.planes);

    //Exit of the host frame threading thread
    appPTR->getAppTLS()->cleanupTLSForThread();

    return ret;
}

static void tryShrinkRenderWindow(const EffectInstance::EffectDataTLSPtr &tls,
                                  const EffectInstance::RectToRender & rectToRender,
                                  const EffectInstance::PlaneToRender & firstPlaneToRender,
                                  bool renderFullScaleThenDownscale,
                                  unsigned int renderMappedMipMapLevel,
                                  unsigned int mipMapLevel,
                                  double par,
                                  const RectD& rod,
                                  RectI &renderMappedRectToRender,
                                  RectI &downscaledRectToRender,
                                  bool *isBeingRenderedElseWhere,
                                  bool *bitmapMarkedForRendering)
{

    renderMappedRectToRender = rectToRender.rect;
    downscaledRectToRender = renderMappedRectToRender;


    {
        RectD canonicalRectToRender;
        renderMappedRectToRender.toCanonical(renderMappedMipMapLevel, par, rod, &canonicalRectToRender);
        if (renderFullScaleThenDownscale) {
            assert(mipMapLevel > 0 && renderMappedMipMapLevel != mipMapLevel);
            canonicalRectToRender.toPixelEnclosing(mipMapLevel, par, &downscaledRectToRender);
        }
    }

    // at this point, it may be unnecessary to call render because it was done a long time ago => check the bitmap here!
# ifndef NDEBUG

    RectI renderBounds = firstPlaneToRender.renderMappedImage->getBounds();
    assert(renderBounds.x1 <= renderMappedRectToRender.x1 && renderMappedRectToRender.x2 <= renderBounds.x2 &&
           renderBounds.y1 <= renderMappedRectToRender.y1 && renderMappedRectToRender.y2 <= renderBounds.y2);

# endif

    *isBeingRenderedElseWhere = false;
    ///At this point if we're in eRenderSafetyFullySafeFrame mode, we are a thread that might have been launched way after
    ///the time renderRectToRender was computed. We recompute it to update the portion to render.
    ///Note that if it is bigger than the initial rectangle, we don't render the bigger rectangle since we cannot
    ///now make the preliminaries call to handle that region (getRoI etc...) so just stick with the old rect to render

    // check the bitmap!
    *bitmapMarkedForRendering = false;
    const ParallelRenderArgsPtr& frameArgs = tls->frameArgs.back();
    if (frameArgs->tilesSupported) {
        if (renderFullScaleThenDownscale) {

            RectI initialRenderRect = renderMappedRectToRender;

#if NATRON_ENABLE_TRIMAP
            if ( frameArgs->isCurrentFrameRenderNotAbortable() ) {
                *bitmapMarkedForRendering = true;
                renderMappedRectToRender = firstPlaneToRender.renderMappedImage->getMinimalRectAndMarkForRendering_trimap(renderMappedRectToRender, isBeingRenderedElseWhere);
            } else {
                renderMappedRectToRender = firstPlaneToRender.renderMappedImage->getMinimalRect(renderMappedRectToRender);
            }
#else
            renderMappedRectToRender = renderMappedImage->getMinimalRect(renderMappedRectToRender);
#endif

            ///If the new rect after getMinimalRect is bigger (maybe because another thread as grown the image)
            ///we stick to what was requested
            if ( !initialRenderRect.contains(renderMappedRectToRender) ) {
                renderMappedRectToRender = initialRenderRect;
            }

            RectD canonicalReducedRectToRender;
            renderMappedRectToRender.toCanonical(renderMappedMipMapLevel, par, rod, &canonicalReducedRectToRender);
            canonicalReducedRectToRender.toPixelEnclosing(mipMapLevel, par, &downscaledRectToRender);


            assert( renderMappedRectToRender.isNull() ||
                   (renderBounds.x1 <= renderMappedRectToRender.x1 && renderMappedRectToRender.x2 <= renderBounds.x2 && renderBounds.y1 <= renderMappedRectToRender.y1 && renderMappedRectToRender.y2 <= renderBounds.y2) );
        } else {
            //The downscaled image is cached, read bitmap from it
#if NATRON_ENABLE_TRIMAP
            RectI rectToRenderMinimal;
            if ( frameArgs->isCurrentFrameRenderNotAbortable() ) {
                *bitmapMarkedForRendering = true;
                rectToRenderMinimal = firstPlaneToRender.downscaleImage->getMinimalRectAndMarkForRendering_trimap(renderMappedRectToRender, isBeingRenderedElseWhere);
            } else {
                rectToRenderMinimal = firstPlaneToRender.downscaleImage->getMinimalRect(renderMappedRectToRender);
            }
#else
            const RectI rectToRenderMinimal = downscaledImage->getMinimalRect(renderMappedRectToRender);
#endif

            assert( renderMappedRectToRender.isNull() ||
                   (renderBounds.x1 <= rectToRenderMinimal.x1 && rectToRenderMinimal.x2 <= renderBounds.x2 && renderBounds.y1 <= rectToRenderMinimal.y1 && rectToRenderMinimal.y2 <= renderBounds.y2) );


            ///If the new rect after getMinimalRect is bigger (maybe because another thread as grown the image)
            ///we stick to what was requested
            if ( !renderMappedRectToRender.contains(rectToRenderMinimal) ) {
                renderMappedRectToRender = rectToRenderMinimal;
            }
            downscaledRectToRender = renderMappedRectToRender;
        }
    } // tilesSupported

#ifndef NDEBUG
    {
        RenderScale scale( Image::getScaleFromMipMapLevel(mipMapLevel) );
        // check the dimensions of all input and output images
        const RectD & dstRodCanonical = firstPlaneToRender.renderMappedImage->getRoD();
        RectI dstBounds;
        dstRodCanonical.toPixelEnclosing(firstPlaneToRender.renderMappedImage->getMipMapLevel(), par, &dstBounds); // compute dstRod at level 0
        RectI dstRealBounds = firstPlaneToRender.renderMappedImage->getBounds();
        if (!frameArgs->tilesSupported && !frameArgs->isDuringPaintStrokeCreation) {
            assert(dstRealBounds.x1 == dstBounds.x1);
            assert(dstRealBounds.x2 == dstBounds.x2);
            assert(dstRealBounds.y1 == dstBounds.y1);
            assert(dstRealBounds.y2 == dstBounds.y2);
        }

        if (renderFullScaleThenDownscale) {
            assert(firstPlaneToRender.renderMappedImage->getMipMapLevel() == 0);
            assert(renderMappedMipMapLevel == 0);
        }
    }
#     endif // DEBUG
}

EffectInstance::RenderingFunctorRetEnum
EffectInstance::Implementation::tiledRenderingFunctor(const RectToRender & rectToRender,
                                                      const OSGLContextPtr& glContext,
                                                      const bool renderFullScaleThenDownscale,
                                                      const bool isSequentialRender,
                                                      const bool isRenderResponseToUserInteraction,
                                                      const int firstFrame,
                                                      const int lastFrame,
                                                      const int preferredInput,
                                                      const unsigned int mipMapLevel,
                                                      const unsigned int renderMappedMipMapLevel,
                                                      const RectD & rod,
                                                      const double time,
                                                      const ViewIdx view,
                                                      const double par,
                                                      const bool byPassCache,
                                                      const ImageBitDepthEnum outputClipPrefDepth,
                                                      const ImageComponents & outputClipPrefsComps,
                                                      const ComponentsNeededMapPtr & compsNeeded,
                                                      const std::bitset<4>& processChannels,
                                                      const ImagePlanesToRenderPtr & planes) // when MT, planes is a copy so there's is no data race
{
    ///There cannot be the same thread running 2 concurrent instances of renderRoI on the same effect.
#ifdef DEBUG
    {
        EffectDataTLSPtr tls = tlsData->getTLSData();
        assert(!tls || !tls->currentRenderArgs.validArgs);
    }
#endif
    EffectDataTLSPtr tls = tlsData->getOrCreateTLSData();

    assert( !rectToRender.rect.isNull() );

    // renderMappedRectToRender is in the mapped mipmap level, i.e the expected mipmap level of the render action of the plug-in
    // downscaledRectToRender is in the mipMapLevel
    RectI renderMappedRectToRender, downscaledRectToRender;
    const EffectInstance::PlaneToRender & firstPlaneToRender = planes->planes.begin()->second;
    bool isBeingRenderedElseWhere,bitmapMarkedForRendering;
    tryShrinkRenderWindow(tls, rectToRender, firstPlaneToRender, renderFullScaleThenDownscale, renderMappedMipMapLevel, mipMapLevel, par, rod, renderMappedRectToRender, downscaledRectToRender, &isBeingRenderedElseWhere, &bitmapMarkedForRendering);

    // It might have been already rendered now
    if ( renderMappedRectToRender.isNull() ) {
        return isBeingRenderedElseWhere ? eRenderingFunctorRetTakeImageLock : eRenderingFunctorRetOK;
    }


    ///This RAII struct controls the lifetime of the validArgs Flag in tls->currentRenderArgs
    Implementation::ScopedRenderArgs scopedArgs(tls,
                                                rod,
                                                renderMappedRectToRender,
                                                time,
                                                view,
                                                rectToRender.isIdentity,
                                                rectToRender.identityTime,
                                                rectToRender.identityInput,
                                                compsNeeded,
                                                rectToRender.imgs,
                                                rectToRender.inputRois,
                                                firstFrame,
                                                lastFrame);


    boost::shared_ptr<TimeLapse> timeRecorder;
    RenderActionArgs actionArgs;
    boost::scoped_ptr<OSGLContextAttacher> glContextAttacher;
    setupRenderArgs(tls, glContext, mipMapLevel, isSequentialRender, isRenderResponseToUserInteraction, byPassCache, *planes, renderMappedRectToRender, processChannels, actionArgs, &glContextAttacher, &timeRecorder);

    // If this tile is identity, copy input image instead
    if (tls->currentRenderArgs.isIdentity) {
        return renderHandlerIdentity(tls, glContext, renderFullScaleThenDownscale, renderMappedRectToRender, downscaledRectToRender, outputClipPrefDepth, actionArgs.time, actionArgs.view, mipMapLevel, timeRecorder, *planes);
    }

    // Call render
    std::map<ImageComponents, EffectInstance::PlaneToRender> outputPlanes;
    tls->currentRenderArgs.outputPlanes = planes->planes;
    bool multiPlanar = _publicInterface->isMultiPlanar();
    {
        RenderingFunctorRetEnum internalRet = renderHandlerInternal(tls, glContext, actionArgs, *planes, multiPlanar, bitmapMarkedForRendering, outputClipPrefsComps, outputClipPrefDepth, outputPlanes, &glContextAttacher);
        if (internalRet != eRenderingFunctorRetOK) {
            return internalRet;
        }
    }

    // Apply post-processing
    renderHandlerPostProcess(tls, rectToRender, preferredInput, glContext, actionArgs, *planes, downscaledRectToRender, timeRecorder, renderFullScaleThenDownscale, mipMapLevel, outputPlanes, processChannels);

    if (isBeingRenderedElseWhere) {
        return eRenderingFunctorRetTakeImageLock;
    } else {
        return eRenderingFunctorRetOK;
    }

} // EffectInstance::tiledRenderingFunctor


EffectInstance::RenderingFunctorRetEnum
EffectInstance::Implementation::renderHandlerIdentity(const EffectInstance::EffectDataTLSPtr& tls,
                                                      const OSGLContextPtr& glContext,
                                                      const bool renderFullScaleThenDownscale,
                                                      const RectI & renderMappedRectToRender,
                                                      const RectI & downscaledRectToRender,
                                                      const ImageBitDepthEnum outputClipPrefDepth,
                                                      const double time,
                                                      const ViewIdx view,
                                                      const unsigned int mipMapLevel,
                                                      const boost::shared_ptr<TimeLapse>& timeRecorder,
                                                      EffectInstance::ImagePlanesToRender & planes)
{
    std::list<ImageComponents> comps;
    const ParallelRenderArgsPtr& frameArgs = tls->frameArgs.back();
    for (std::map<ImageComponents, EffectInstance::PlaneToRender>::iterator it = planes.planes.begin(); it != planes.planes.end(); ++it) {
        //If color plane, request the preferred comp of the identity input
        if ( tls->currentRenderArgs.identityInput && it->second.renderMappedImage->getComponents().isColorPlane() ) {
            ImageComponents prefInputComps = tls->currentRenderArgs.identityInput->getComponents(-1);
            comps.push_back(prefInputComps);
        } else {
            comps.push_back( it->second.renderMappedImage->getComponents() );
        }
    }
    assert( !comps.empty() );
    std::map<ImageComponents, ImagePtr> identityPlanes;
    boost::scoped_ptr<EffectInstance::RenderRoIArgs> renderArgs( new EffectInstance::RenderRoIArgs(tls->currentRenderArgs.identityTime,
                                                                                                   Image::getScaleFromMipMapLevel(mipMapLevel),
                                                                                                   mipMapLevel,
                                                                                                   view,
                                                                                                   false,
                                                                                                   downscaledRectToRender,
                                                                                                   RectD(),
                                                                                                   comps,
                                                                                                   outputClipPrefDepth,
                                                                                                   false,
                                                                                                   _publicInterface->shared_from_this(),
                                                                                                   planes.useOpenGL ? eStorageModeGLTex : eStorageModeRAM,
                                                                                                   time) );
    if (!tls->currentRenderArgs.identityInput) {
        for (std::map<ImageComponents, EffectInstance::PlaneToRender>::iterator it = planes.planes.begin(); it != planes.planes.end(); ++it) {
            it->second.renderMappedImage->fillZero(renderMappedRectToRender, glContext);
            it->second.renderMappedImage->markForRendered(renderMappedRectToRender);

            if ( frameArgs->stats && frameArgs->stats->isInDepthProfilingEnabled() ) {
                frameArgs->stats->addRenderInfosForNode( _publicInterface->getNode(),  NodePtr(), it->first.getComponentsGlobalName(), renderMappedRectToRender, timeRecorder->getTimeSinceCreation() );
            }
        }

        return eRenderingFunctorRetOK;
    } else {
        EffectInstance::RenderRoIRetCode renderOk;
        renderOk = tls->currentRenderArgs.identityInput->renderRoI(*renderArgs, &identityPlanes);
        if (renderOk == eRenderRoIRetCodeAborted) {
            return eRenderingFunctorRetAborted;
        } else if (renderOk == eRenderRoIRetCodeFailed) {
            return eRenderingFunctorRetFailed;
        } else if ( identityPlanes.empty() ) {
            for (std::map<ImageComponents, EffectInstance::PlaneToRender>::iterator it = planes.planes.begin(); it != planes.planes.end(); ++it) {
                it->second.renderMappedImage->fillZero(renderMappedRectToRender, glContext);
                it->second.renderMappedImage->markForRendered(renderMappedRectToRender);

                if ( frameArgs->stats && frameArgs->stats->isInDepthProfilingEnabled() ) {
                    frameArgs->stats->addRenderInfosForNode( _publicInterface->getNode(),  tls->currentRenderArgs.identityInput->getNode(), it->first.getComponentsGlobalName(), renderMappedRectToRender, timeRecorder->getTimeSinceCreation() );
                }
            }

            return eRenderingFunctorRetOK;
        } else {
            assert( identityPlanes.size() == planes.planes.size() );

            std::map<ImageComponents, ImagePtr>::iterator idIt = identityPlanes.begin();
            for (std::map<ImageComponents, EffectInstance::PlaneToRender>::iterator it = planes.planes.begin(); it != planes.planes.end(); ++it, ++idIt) {
                if ( renderFullScaleThenDownscale && ( idIt->second->getMipMapLevel() > it->second.fullscaleImage->getMipMapLevel() ) ) {
                    // We cannot be rendering using OpenGL in this case
                    assert(!planes.useOpenGL);


                    if ( !idIt->second->getBounds().contains(renderMappedRectToRender) ) {
                        ///Fill the RoI with 0's as the identity input image might have bounds contained into the RoI
                        it->second.fullscaleImage->fillZero(renderMappedRectToRender, glContext);
                    }

                    ///Convert format first if needed
                    ImagePtr sourceImage;
                    if ( ( it->second.fullscaleImage->getComponents() != idIt->second->getComponents() ) || ( it->second.fullscaleImage->getBitDepth() != idIt->second->getBitDepth() ) ) {
                        sourceImage.reset( new Image(it->second.fullscaleImage->getComponents(),
                                                     idIt->second->getRoD(),
                                                     idIt->second->getBounds(),
                                                     idIt->second->getMipMapLevel(),
                                                     idIt->second->getPixelAspectRatio(),
                                                     it->second.fullscaleImage->getBitDepth(),
                                                     idIt->second->getPremultiplication(),
                                                     idIt->second->getFieldingOrder(),
                                                     false) );

                        ViewerColorSpaceEnum colorspace = _publicInterface->getApp()->getDefaultColorSpaceForBitDepth( idIt->second->getBitDepth() );
                        ViewerColorSpaceEnum dstColorspace = _publicInterface->getApp()->getDefaultColorSpaceForBitDepth( it->second.fullscaleImage->getBitDepth() );
                        idIt->second->convertToFormat( idIt->second->getBounds(), colorspace, dstColorspace, 3, false, false, sourceImage.get() );
                    } else {
                        sourceImage = idIt->second;
                    }

                    ///then upscale
                    const RectD & rod = sourceImage->getRoD();
                    RectI bounds;
                    rod.toPixelEnclosing(it->second.renderMappedImage->getMipMapLevel(), it->second.renderMappedImage->getPixelAspectRatio(), &bounds);
                    ImagePtr inputPlane( new Image(it->first,
                                                   rod,
                                                   bounds,
                                                   it->second.renderMappedImage->getMipMapLevel(),
                                                   it->second.renderMappedImage->getPixelAspectRatio(),
                                                   it->second.renderMappedImage->getBitDepth(),
                                                   it->second.renderMappedImage->getPremultiplication(),
                                                   it->second.renderMappedImage->getFieldingOrder(),
                                                   false) );
                    sourceImage->upscaleMipMap( sourceImage->getBounds(), sourceImage->getMipMapLevel(), inputPlane->getMipMapLevel(), inputPlane.get() );
                    it->second.fullscaleImage->pasteFrom(*inputPlane, renderMappedRectToRender, false);
                    it->second.fullscaleImage->markForRendered(renderMappedRectToRender);
                } else {
                    if ( !idIt->second->getBounds().contains(downscaledRectToRender) ) {
                        ///Fill the RoI with 0's as the identity input image might have bounds contained into the RoI
                        it->second.downscaleImage->fillZero(downscaledRectToRender, glContext);
                    }

                    ///Convert format if needed or copy
                    if ( ( it->second.downscaleImage->getComponents() != idIt->second->getComponents() ) || ( it->second.downscaleImage->getBitDepth() != idIt->second->getBitDepth() ) ) {
                        ViewerColorSpaceEnum colorspace = _publicInterface->getApp()->getDefaultColorSpaceForBitDepth( idIt->second->getBitDepth() );
                        ViewerColorSpaceEnum dstColorspace = _publicInterface->getApp()->getDefaultColorSpaceForBitDepth( it->second.fullscaleImage->getBitDepth() );
                        RectI convertWindow;
                        if (idIt->second->getBounds().intersect(downscaledRectToRender, &convertWindow)) {
                            idIt->second->convertToFormat( convertWindow, colorspace, dstColorspace, 3, false, false, it->second.downscaleImage.get() );
                        }
                    } else {
                        it->second.downscaleImage->pasteFrom(*(idIt->second), downscaledRectToRender, false, glContext);
                    }
                    it->second.downscaleImage->markForRendered(downscaledRectToRender);
                }

                if ( frameArgs->stats && frameArgs->stats->isInDepthProfilingEnabled() ) {
                    frameArgs->stats->addRenderInfosForNode( _publicInterface->getNode(),  tls->currentRenderArgs.identityInput->getNode(), it->first.getComponentsGlobalName(), renderMappedRectToRender, timeRecorder->getTimeSinceCreation() );
                }
            }

            return eRenderingFunctorRetOK;
        } // if (renderOk == eRenderRoIRetCodeAborted) {
    }  //  if (!identityInput) {
} // renderHandlerIdentity

template <typename GL>
static void setupGLForRender(const ImagePtr& image,
                             const OSGLContextPtr& glContext,
                             const AbortableRenderInfoPtr& abortInfo,
                             double time,
                             const RectI& roi,
                             bool callGLFinish,
                             boost::scoped_ptr<OSGLContextAttacher>* glContextAttacher)
{
#ifndef DEBUG
    Q_UNUSED(time);
#endif
    RectI imageBounds = image->getBounds();

    RectI viewportBounds;
    if (GL::isGPU()) {
        viewportBounds = imageBounds;
        int textureTarget = image->getGLTextureTarget();
        GL::glEnable(textureTarget);
        assert(image->getStorageMode() == eStorageModeGLTex);

        GL::glActiveTexture(GL_TEXTURE0);
        GL::glBindTexture( textureTarget, image->getGLTextureID() );
        assert(GL::glIsTexture(image->getGLTextureID()));
        assert(GL::glGetError() == GL_NO_ERROR);
        glCheckError(GL);
        GL::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, textureTarget, image->getGLTextureID(), 0 /*LoD*/);
        glCheckError(GL);
        assert(GL::glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        glCheckFramebufferError(GL);
    } else {
        viewportBounds = roi;
        assert(image->getStorageMode() == eStorageModeDisk || image->getStorageMode() == eStorageModeRAM);
        Image::WriteAccess outputWriteAccess(image.get());
        unsigned char* data = outputWriteAccess.pixelAt(roi.x1, roi.y1);
        assert(data);

        // With OSMesa we render directly to the context framebuffer
        glContextAttacher->reset(new OSGLContextAttacher(glContext, abortInfo
#ifdef DEBUG
                                                         , time
#endif
                                                         , roi.width()
                                                         , roi.height()
                                                         , imageBounds.width()
                                                        , data));
        (*glContextAttacher)->attach();
    }

    // setup the output viewport
    Image::setupGLViewport<GL>(viewportBounds, roi);

    // Enable scissor to make the plug-in not render outside of the viewport...
    GL::glEnable(GL_SCISSOR_TEST);
    GL::glScissor( roi.x1 - viewportBounds.x1, roi.y1 - viewportBounds.y1, roi.width(), roi.height() );

    if (callGLFinish) {
        // Ensure that previous asynchronous operations are done (e.g: glTexImage2D) some plug-ins seem to require it (Hitfilm Ignite plugin-s)
        GL::glFinish();
    }

}

template <typename GL>
static void finishGLRender()
{
    GL::glDisable(GL_SCISSOR_TEST);
    GL::glActiveTexture(GL_TEXTURE0);
    GL::glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!GL::isGPU()) {
        GL::glFlush(); // waits until commands are submitted but does not wait for the commands to finish executing
        GL::glFinish(); // waits for all previously submitted commands to complete executing
    }
    glCheckError(GL);
}

EffectInstance::RenderingFunctorRetEnum
EffectInstance::Implementation::renderHandlerInternal(const EffectDataTLSPtr& tls,
                                                      const OSGLContextPtr& glContext,
                                                      EffectInstance::RenderActionArgs &actionArgs,
                                                      const ImagePlanesToRender & planes,
                                                      bool multiPlanar,
                                                      bool bitmapMarkedForRendering,
                                                      const ImageComponents & outputClipPrefsComps,
                                                      const ImageBitDepthEnum outputClipPrefDepth,
                                                      std::map<ImageComponents, EffectInstance::PlaneToRender>& outputPlanes,
                                                      boost::scoped_ptr<OSGLContextAttacher>* glContextAttacher)
{
    const ParallelRenderArgsPtr& frameArgs = tls->frameArgs.back();
    std::list<std::pair<ImageComponents, ImagePtr> > tmpPlanes;
    for (std::map<ImageComponents, EffectInstance::PlaneToRender>::iterator it = tls->currentRenderArgs.outputPlanes.begin(); it != tls->currentRenderArgs.outputPlanes.end(); ++it) {
        /*
         * When using the cache, allocate a local temporary buffer onto which the plug-in will render, and then safely
         * copy this buffer to the shared (among threads) image.
         * This is also needed if the plug-in does not support the number of components of the renderMappedImage
         */
        ImageComponents prefComp;
        if (multiPlanar) {
            prefComp = _publicInterface->getNode()->findClosestSupportedComponents( -1, it->second.renderMappedImage->getComponents() );
        } else {
            prefComp = outputClipPrefsComps;
        }

        // OpenGL render never use the cache and bitmaps, all images are local to a render.
        if ( ( it->second.renderMappedImage->usesBitMap() || ( prefComp != it->second.renderMappedImage->getComponents() ) ||
              ( outputClipPrefDepth != it->second.renderMappedImage->getBitDepth() ) ) && !_publicInterface->isPaintingOverItselfEnabled() && !planes.useOpenGL ) {
            it->second.tmpImage.reset( new Image(prefComp,
                                                 it->second.renderMappedImage->getRoD(),
                                                 actionArgs.roi,
                                                 it->second.renderMappedImage->getMipMapLevel(),
                                                 it->second.renderMappedImage->getPixelAspectRatio(),
                                                 outputClipPrefDepth,
                                                 it->second.renderMappedImage->getPremultiplication(),
                                                 it->second.renderMappedImage->getFieldingOrder(),
                                                 false) ); //< no bitmap
        } else {
            it->second.tmpImage = it->second.renderMappedImage;
        }
        tmpPlanes.push_back( std::make_pair(it->second.renderMappedImage->getComponents(), it->second.tmpImage) );
    }

#if NATRON_ENABLE_TRIMAP
    if ( !bitmapMarkedForRendering && frameArgs->isCurrentFrameRenderNotAbortable() ) {
        for (std::map<ImageComponents, EffectInstance::PlaneToRender>::iterator it = tls->currentRenderArgs.outputPlanes.begin(); it != tls->currentRenderArgs.outputPlanes.end(); ++it) {
            it->second.renderMappedImage->markForRendering(actionArgs.roi);
        }
    }
#endif

    std::list< std::list<std::pair<ImageComponents, ImagePtr> > > planesLists;
    if (!multiPlanar) {
        for (std::list<std::pair<ImageComponents, ImagePtr> >::iterator it = tmpPlanes.begin(); it != tmpPlanes.end(); ++it) {
            std::list<std::pair<ImageComponents, ImagePtr> > tmp;
            tmp.push_back(*it);
            planesLists.push_back(tmp);
        }
    } else {
        planesLists.push_back(tmpPlanes);
    }


    bool renderAborted = false;
    for (std::list<std::list<std::pair<ImageComponents, ImagePtr> > >::iterator it = planesLists.begin(); it != planesLists.end(); ++it) {
        if (!multiPlanar) {
            assert( !it->empty() );
            tls->currentRenderArgs.outputPlaneBeingRendered = it->front().first;
        }
        actionArgs.outputPlanes = *it;
        const ImagePtr& mainImagePlane = actionArgs.outputPlanes.front().second;
        if (planes.useOpenGL) {
            actionArgs.glContextData = planes.glContextData;

            // Effects that render multiple planes at once are NOT supported by the OpenGL render suite
            // We only bind to the framebuffer color attachment 0 the "main" output image plane
            assert(actionArgs.outputPlanes.size() == 1);
            if (glContext->isGPUContext()) {
                setupGLForRender<GL_GPU>(mainImagePlane, glContext, frameArgs->abortInfo.lock(), actionArgs.time, actionArgs.roi, _publicInterface->getNode()->isGLFinishRequiredBeforeRender(), glContextAttacher);
            } else {
                setupGLForRender<GL_CPU>(mainImagePlane, glContext, frameArgs->abortInfo.lock(), actionArgs.time, actionArgs.roi, _publicInterface->getNode()->isGLFinishRequiredBeforeRender(), glContextAttacher);
            }
        }

        StatusEnum st = _publicInterface->render_public(actionArgs);

        if (planes.useOpenGL) {
            if (glContext->isGPUContext()) {
                GL_GPU::glBindTexture(mainImagePlane->getGLTextureTarget(), 0);
                finishGLRender<GL_GPU>();
            } else {
                finishGLRender<GL_CPU>();
            }
        }

        renderAborted = _publicInterface->aborted();

        /*
         * Since new planes can have been allocated on the fly by allocateImagePlaneAndSetInThreadLocalStorage(), refresh
         * the planes map from the thread local storage once the render action is finished
         */
        if ( it == planesLists.begin() ) {
            outputPlanes = tls->currentRenderArgs.outputPlanes;
            assert( !outputPlanes.empty() );
        }

        if ( (st != eStatusOK) || renderAborted ) {
#if NATRON_ENABLE_TRIMAP
            if ( frameArgs->isCurrentFrameRenderNotAbortable() ) {
                /*
                 At this point, another thread might have already gotten this image from the cache and could end-up
                 using it while it has still pixels marked to PIXEL_UNAVAILABLE, hence clear the bitmap
                 */
                for (std::map<ImageComponents, EffectInstance::PlaneToRender>::const_iterator it = outputPlanes.begin(); it != outputPlanes.end(); ++it) {
                    it->second.renderMappedImage->clearBitmap(actionArgs.roi);
                }
            }
#endif
            switch (st) {
                case eStatusFailed:

                    return eRenderingFunctorRetFailed;
                case eStatusOutOfMemory:

                    return eRenderingFunctorRetOutOfGPUMemory;
                case eStatusOK:
                default:

                    return eRenderingFunctorRetAborted;
            }
        } // if (st != eStatusOK || renderAborted) {
    } // for (std::list<std::list<std::pair<ImageComponents,ImagePtr> > >::iterator it = planesLists.begin(); it != planesLists.end(); ++it)
    
    assert(!renderAborted);
    return eRenderingFunctorRetOK;
} // EffectInstance::Implementation::renderHandlerInternal


void
EffectInstance::Implementation::renderHandlerPostProcess(const EffectDataTLSPtr& tls,
                                                         const RectToRender & rectToRender,
                                                         int preferredInput,
                                                         const OSGLContextPtr& glContext,
                                                         const EffectInstance::RenderActionArgs &actionArgs,
                                                         const ImagePlanesToRender & planes,
                                                         const RectI& downscaledRectToRender,
                                                         const boost::shared_ptr<TimeLapse>& timeRecorder,
                                                         bool renderFullScaleThenDownscale,
                                                         unsigned int mipMapLevel,
                                                         const std::map<ImageComponents, EffectInstance::PlaneToRender>& outputPlanes,
                                                         const std::bitset<4>& processChannels)
{

    const ParallelRenderArgsPtr& frameArgs = tls->frameArgs.back();

    ImagePtr originalInputImage, maskImage;
    ImagePremultiplicationEnum originalImagePremultiplication;
    EffectInstance::InputImagesMap::const_iterator foundPrefInput = rectToRender.imgs.find(preferredInput);
    EffectInstance::InputImagesMap::const_iterator foundMaskInput = rectToRender.imgs.end();

    bool hostMasking = _publicInterface->isHostMaskingEnabled();
    if ( hostMasking ) {
        foundMaskInput = rectToRender.imgs.find(_publicInterface->getMaxInputCount() - 1);
    }
    if ( ( foundPrefInput != rectToRender.imgs.end() ) && !foundPrefInput->second.empty() ) {
        originalInputImage = foundPrefInput->second.front();
    }
    std::map<int, ImagePremultiplicationEnum>::const_iterator foundPrefPremult = planes.inputPremult.find(preferredInput);
    if ( ( foundPrefPremult != planes.inputPremult.end() ) && originalInputImage ) {
        originalImagePremultiplication = foundPrefPremult->second;
    } else {
        originalImagePremultiplication = eImagePremultiplicationOpaque;
    }


    if ( ( foundMaskInput != rectToRender.imgs.end() ) && !foundMaskInput->second.empty() ) {
        maskImage = foundMaskInput->second.front();
    }

    bool unPremultIfNeeded = planes.outputPremult == eImagePremultiplicationPremultiplied;
    bool useMaskMix = hostMasking || _publicInterface->isHostMixingEnabled();
    double mix = useMaskMix ? _publicInterface->getNode()->getHostMixingValue(actionArgs.time, actionArgs.view) : 1.;
    bool doMask = useMaskMix ? _publicInterface->getNode()->isMaskEnabled(_publicInterface->getMaxInputCount() - 1) : false;

    //Check for NaNs, copy to output image and mark for rendered
    for (std::map<ImageComponents, EffectInstance::PlaneToRender>::const_iterator it = outputPlanes.begin(); it != outputPlanes.end(); ++it) {
        bool unPremultRequired = unPremultIfNeeded && it->second.tmpImage->getComponentsCount() == 4 && it->second.renderMappedImage->getComponentsCount() == 3;

        if ( frameArgs->doNansHandling && it->second.tmpImage->checkForNaNs(actionArgs.roi) ) {
            QString warning = QString::fromUtf8( _publicInterface->getNode()->getScriptName_mt_safe().c_str() );
            warning.append( QString::fromUtf8(": ") );
            warning.append( tr("rendered rectangle (") );
            warning.append( QString::number(actionArgs.roi.x1) );
            warning.append( QChar::fromLatin1(',') );
            warning.append( QString::number(actionArgs.roi.y1) );
            warning.append( QString::fromUtf8(")-(") );
            warning.append( QString::number(actionArgs.roi.x2) );
            warning.append( QChar::fromLatin1(',') );
            warning.append( QString::number(actionArgs.roi.y2) );
            warning.append( QString::fromUtf8(") ") );
            warning.append( tr("contains NaN values. They have been converted to 1.") );
            _publicInterface->setPersistentMessage( eMessageTypeWarning, warning.toStdString() );
        }
        if (it->second.isAllocatedOnTheFly) {
            ///Plane allocated on the fly only have a temp image if using the cache and it is defined over the render window only
            if (it->second.tmpImage != it->second.renderMappedImage) {
                // We cannot be rendering using OpenGL in this case
                assert(!planes.useOpenGL);

                assert(it->second.tmpImage->getBounds() == actionArgs.roi);

                if ( ( it->second.renderMappedImage->getComponents() != it->second.tmpImage->getComponents() ) ||
                    ( it->second.renderMappedImage->getBitDepth() != it->second.tmpImage->getBitDepth() ) ) {
                    it->second.tmpImage->convertToFormat( it->second.tmpImage->getBounds(),
                                                         _publicInterface->getApp()->getDefaultColorSpaceForBitDepth( it->second.tmpImage->getBitDepth() ),
                                                         _publicInterface->getApp()->getDefaultColorSpaceForBitDepth( it->second.renderMappedImage->getBitDepth() ),
                                                         -1, false, unPremultRequired, it->second.renderMappedImage.get() );
                } else {
                    it->second.renderMappedImage->pasteFrom(*(it->second.tmpImage), it->second.tmpImage->getBounds(), false);
                }
            }
            it->second.renderMappedImage->markForRendered(actionArgs.roi);
        } else {
            if (renderFullScaleThenDownscale) {
                // We cannot be rendering using OpenGL in this case
                assert(!planes.useOpenGL);

                ///copy the rectangle rendered in the full scale image to the downscaled output
                assert(mipMapLevel != 0);

                assert(it->second.fullscaleImage != it->second.downscaleImage && it->second.renderMappedImage == it->second.fullscaleImage);

                ImagePtr mappedOriginalInputImage = originalInputImage;

                if ( originalInputImage && (originalInputImage->getMipMapLevel() != 0) ) {
                    bool mustCopyUnprocessedChannels = it->second.tmpImage->canCallCopyUnProcessedChannels(processChannels);
                    if (mustCopyUnprocessedChannels || useMaskMix) {
                        ///there is some processing to be done by copyUnProcessedChannels or applyMaskMix
                        ///but originalInputImage is not in the correct mipMapLevel, upscale it
                        assert(originalInputImage->getMipMapLevel() > it->second.tmpImage->getMipMapLevel() &&
                               originalInputImage->getMipMapLevel() == mipMapLevel);
                        ImagePtr tmp( new Image(it->second.tmpImage->getComponents(),
                                                it->second.tmpImage->getRoD(),
                                                actionArgs.roi,
                                                0,
                                                it->second.tmpImage->getPixelAspectRatio(),
                                                it->second.tmpImage->getBitDepth(),
                                                it->second.tmpImage->getPremultiplication(),
                                                it->second.tmpImage->getFieldingOrder(),
                                                false) );
                        originalInputImage->upscaleMipMap( downscaledRectToRender, originalInputImage->getMipMapLevel(), 0, tmp.get() );
                        mappedOriginalInputImage = tmp;
                    }
                }

                if (mappedOriginalInputImage) {
                    it->second.tmpImage->copyUnProcessedChannels(actionArgs.roi, planes.outputPremult, originalImagePremultiplication, processChannels, mappedOriginalInputImage, true);
                    if (useMaskMix) {
                        it->second.tmpImage->applyMaskMix(actionArgs.roi, maskImage.get(), mappedOriginalInputImage.get(), doMask, false, mix);
                    }
                }
                if ( ( it->second.fullscaleImage->getComponents() != it->second.tmpImage->getComponents() ) ||
                    ( it->second.fullscaleImage->getBitDepth() != it->second.tmpImage->getBitDepth() ) ) {
                    /*
                     * BitDepth/Components conversion required as well as downscaling, do conversion to a tmp buffer
                     */
                    ImagePtr tmp( new Image(it->second.fullscaleImage->getComponents(),
                                            it->second.tmpImage->getRoD(),
                                            actionArgs.roi,
                                            mipMapLevel,
                                            it->second.tmpImage->getPixelAspectRatio(),
                                            it->second.fullscaleImage->getBitDepth(),
                                            it->second.fullscaleImage->getPremultiplication(),
                                            it->second.fullscaleImage->getFieldingOrder(),
                                            false) );

                    it->second.tmpImage->convertToFormat( actionArgs.roi,
                                                         _publicInterface->getApp()->getDefaultColorSpaceForBitDepth( it->second.tmpImage->getBitDepth() ),
                                                         _publicInterface->getApp()->getDefaultColorSpaceForBitDepth( it->second.fullscaleImage->getBitDepth() ),
                                                         -1, false, unPremultRequired, tmp.get() );
                    tmp->downscaleMipMap( it->second.tmpImage->getRoD(),
                                         actionArgs.roi, 0, mipMapLevel, false, it->second.downscaleImage.get() );
                    it->second.fullscaleImage->pasteFrom(*tmp, actionArgs.roi, false);
                } else {
                    /*
                     *  Downscaling required only
                     */
                    it->second.tmpImage->downscaleMipMap( it->second.tmpImage->getRoD(),
                                                         actionArgs.roi, 0, mipMapLevel, false, it->second.downscaleImage.get() );
                    it->second.fullscaleImage->pasteFrom(*(it->second.tmpImage), actionArgs.roi, false);
                }


                it->second.fullscaleImage->markForRendered(actionArgs.roi);
            } else { // if (renderFullScaleThenDownscale) {
                ///Copy the rectangle rendered in the downscaled image
                if (it->second.tmpImage != it->second.downscaleImage) {
                    // We cannot be rendering using OpenGL in this case
                    assert(!planes.useOpenGL);

                    if ( ( it->second.downscaleImage->getComponents() != it->second.tmpImage->getComponents() ) ||
                        ( it->second.downscaleImage->getBitDepth() != it->second.tmpImage->getBitDepth() ) ) {
                        /*
                         * BitDepth/Components conversion required
                         */


                        it->second.tmpImage->convertToFormat( it->second.tmpImage->getBounds(),
                                                             _publicInterface->getApp()->getDefaultColorSpaceForBitDepth( it->second.tmpImage->getBitDepth() ),
                                                             _publicInterface->getApp()->getDefaultColorSpaceForBitDepth( it->second.downscaleImage->getBitDepth() ),
                                                             -1, false, unPremultRequired, it->second.downscaleImage.get() );
                    } else {
                        /*
                         * No conversion required, copy to output
                         */

                        it->second.downscaleImage->pasteFrom(*(it->second.tmpImage), it->second.downscaleImage->getBounds(), false);
                    }
                }

                it->second.downscaleImage->copyUnProcessedChannels(actionArgs.roi, planes.outputPremult, originalImagePremultiplication, processChannels, originalInputImage, true, glContext);
                if (useMaskMix) {
                    it->second.downscaleImage->applyMaskMix(actionArgs.roi, maskImage.get(), originalInputImage.get(), doMask, false, mix, glContext);
                }
                it->second.downscaleImage->markForRendered(downscaledRectToRender);
            } // if (renderFullScaleThenDownscale) {
        } // if (it->second.isAllocatedOnTheFly) {

        if ( frameArgs->stats && frameArgs->stats->isInDepthProfilingEnabled() ) {
            frameArgs->stats->addRenderInfosForNode( _publicInterface->getNode(),  NodePtr(), it->first.getComponentsGlobalName(), actionArgs.roi, timeRecorder->getTimeSinceCreation() );
        }
    } // for (std::map<ImageComponents,PlaneToRender>::const_iterator it = outputPlanes.begin(); it != outputPlanes.end(); ++it) {

} // EffectInstance::Implementation::renderHandlerPostProcess


void
EffectInstance::Implementation::setupRenderArgs(const EffectDataTLSPtr& tls,
                                                const OSGLContextPtr& glContext,
                                                unsigned int mipMapLevel,
                                                bool isSequentialRender,
                                                bool isRenderResponseToUserInteraction,
                                                bool byPassCache,
                                                const ImagePlanesToRender & planes,
                                                const RectI & renderMappedRectToRender,
                                                const std::bitset<4>& processChannels,
                                                EffectInstance::RenderActionArgs &actionArgs,
                                                boost::scoped_ptr<OSGLContextAttacher>* glContextAttacher,
                                                boost::shared_ptr<TimeLapse> *timeRecorder)
{
    const ParallelRenderArgsPtr& frameArgs = tls->frameArgs.back();

    if (frameArgs->stats) {
        timeRecorder->reset( new TimeLapse() );
    }

    const EffectInstance::PlaneToRender & firstPlane = planes.planes.begin()->second;
    const double time = tls->currentRenderArgs.time;
    const ViewIdx view = tls->currentRenderArgs.view;

    // at this point, it may be unnecessary to call render because it was done a long time ago => check the bitmap here!
# ifndef NDEBUG
    {
        RectI renderBounds = firstPlane.renderMappedImage->getBounds();
        assert(renderBounds.x1 <= renderMappedRectToRender.x1 && renderMappedRectToRender.x2 <= renderBounds.x2 &&
               renderBounds.y1 <= renderMappedRectToRender.y1 && renderMappedRectToRender.y2 <= renderBounds.y2);
    }
# endif

    std::list<std::pair<ImageComponents, ImagePtr> > tmpPlanes;

    actionArgs.byPassCache = byPassCache;
    actionArgs.processChannels = processChannels;
    actionArgs.mappedScale.x = actionArgs.mappedScale.y = Image::getScaleFromMipMapLevel( firstPlane.renderMappedImage->getMipMapLevel() );
    assert( !( (_publicInterface->supportsRenderScaleMaybe() == eSupportsNo) && !(actionArgs.mappedScale.x == 1. && actionArgs.mappedScale.y == 1.) ) );
    actionArgs.originalScale.x = Image::getScaleFromMipMapLevel(mipMapLevel);
    actionArgs.originalScale.y = actionArgs.originalScale.x;
    actionArgs.draftMode = frameArgs->draftMode;
    actionArgs.useOpenGL = planes.useOpenGL;
    actionArgs.roi = renderMappedRectToRender;
    actionArgs.time = time;
    actionArgs.view = view;
    actionArgs.isSequentialRender = isSequentialRender;
    actionArgs.isRenderResponseToUserInteraction = isRenderResponseToUserInteraction;
    actionArgs.inputImages = tls->currentRenderArgs.inputImages;
    actionArgs.glContext = glContext;

    // Setup the context when rendering using OpenGL
    if (planes.useOpenGL) {
        // Setup the viewport and the framebuffer
        AbortableRenderInfoPtr abortInfo = frameArgs->abortInfo.lock();
        assert(abortInfo);
        assert(glContext);

        // Ensure the context is current
        if (glContext->isGPUContext()) {
            glContextAttacher->reset( new OSGLContextAttacher(glContext, abortInfo
#ifdef DEBUG
                                                             , actionArgs.time
#endif
                                                             ) );
            (*glContextAttacher)->attach();


            GLuint fboID = glContext->getOrCreateFBOId();
            GL_GPU::glBindFramebuffer(GL_FRAMEBUFFER, fboID);
            glCheckError(GL_GPU);
        }
    }

}


ImagePtr
EffectInstance::allocateImagePlaneAndSetInThreadLocalStorage(const ImageComponents & plane)
{
    /*
     * The idea here is that we may have asked the plug-in to render say motion.forward, but it can only render both fotward
     * and backward at a time.
     * So it needs to allocate motion.backward and store it in the cache for efficiency.
     * Note that when calling this, the plug-in is already in the render action, hence in case of Host frame threading,
     * this function will be called as many times as there were thread used by the host frame threading.
     * For all other planes, there was a local temporary image, shared among all threads for the calls to render.
     * Since we may be in a thread of the host frame threading, only allocate a temporary image of the size of the rectangle
     * to render and mark that we're a plane allocated on the fly so that the tiledRenderingFunctor can know this is a plane
     * to handle specifically.
     */
    EffectDataTLSPtr tls = _imp->tlsData->getTLSData();

    if (!tls || !tls->currentRenderArgs.validArgs) {
        return ImagePtr();
    }

    assert( !tls->currentRenderArgs.outputPlanes.empty() );
    assert(!tls->frameArgs.empty());

    const ParallelRenderArgsPtr& frameArgs = tls->frameArgs.back();

    const EffectInstance::PlaneToRender & firstPlane = tls->currentRenderArgs.outputPlanes.begin()->second;
    bool useCache = firstPlane.fullscaleImage->usesBitMap() || firstPlane.downscaleImage->usesBitMap();
    if ( boost::starts_with(getNode()->getPluginID(), "uk.co.thefoundry.furnace") ) {
        //Furnace plug-ins are bugged and do not render properly both planes, just wipe the image.
        useCache = false;
    }
    const ImagePtr & img = firstPlane.fullscaleImage->usesBitMap() ? firstPlane.fullscaleImage : firstPlane.downscaleImage;
    ImageParamsPtr params = img->getParams();
    EffectInstance::PlaneToRender p;
    bool ok = allocateImagePlane(img->getKey(),
                                 tls->currentRenderArgs.rod,
                                 tls->currentRenderArgs.renderWindowPixel,
                                 tls->currentRenderArgs.renderWindowPixel,
                                 false /*isProjectFormat*/,
                                 plane,
                                 img->getBitDepth(),
                                 img->getPremultiplication(),
                                 img->getFieldingOrder(),
                                 img->getPixelAspectRatio(),
                                 img->getMipMapLevel(),
                                 false,
                                 frameArgs->openGLContext.lock(),
                                 img->getParams()->getStorageInfo().mode,
                                 useCache,
                                 &p.fullscaleImage,
                                 &p.downscaleImage);
    if (!ok) {
        return ImagePtr();
    } else {
        p.renderMappedImage = p.downscaleImage;
        p.isAllocatedOnTheFly = true;

        /*
         * Allocate a temporary image for rendering only if using cache
         */
        if (useCache) {
            p.tmpImage.reset( new Image(p.renderMappedImage->getComponents(),
                                        p.renderMappedImage->getRoD(),
                                        tls->currentRenderArgs.renderWindowPixel,
                                        p.renderMappedImage->getMipMapLevel(),
                                        p.renderMappedImage->getPixelAspectRatio(),
                                        p.renderMappedImage->getBitDepth(),
                                        p.renderMappedImage->getPremultiplication(),
                                        p.renderMappedImage->getFieldingOrder(),
                                        false /*useBitmap*/,
                                        img->getParams()->getStorageInfo().mode) );
        } else {
            p.tmpImage = p.renderMappedImage;
        }
        tls->currentRenderArgs.outputPlanes.insert( std::make_pair(plane, p) );

        return p.downscaleImage;
    }
} // allocateImagePlaneAndSetInThreadLocalStorage

void
EffectInstance::openImageFileKnob()
{
    const std::vector< KnobIPtr > & knobs = getKnobs();

    for (U32 i = 0; i < knobs.size(); ++i) {
        if ( knobs[i]->typeName() == KnobFile::typeNameStatic() ) {
            KnobFilePtr fk = toKnobFile(knobs[i]);
            assert(fk);
            if ( fk->isInputImageFile() ) {
                std::string file = fk->getValue();
                if ( file.empty() ) {
                    fk->open_file();
                }
                break;
            }
        } else if ( knobs[i]->typeName() == KnobOutputFile::typeNameStatic() ) {
            KnobOutputFilePtr fk = toKnobOutputFile(knobs[i]);
            assert(fk);
            if ( fk->isOutputImageFile() ) {
                std::string file = fk->getValue();
                if ( file.empty() ) {
                    fk->open_file();
                }
                break;
            }
        }
    }
}

void
EffectInstance::onSignificantEvaluateAboutToBeCalled(const KnobIPtr& knob)
{
    //We changed, abort any ongoing current render to refresh them with a newer version
    abortAnyEvaluation();

    NodePtr node = getNode();
    if ( !node->isNodeCreated() ) {
        return;
    }

    bool isMT = QThread::currentThread() == qApp->thread();

    if ( isMT && ( !knob || knob->getEvaluateOnChange() ) ) {
        getApp()->triggerAutoSave();
    }


    if (isMT) {
        node->refreshIdentityState();

        //Increments the knobs age following a change
        node->incrementKnobsAge();
    }
}

void
EffectInstance::evaluate(bool isSignificant,
                         bool refreshMetadatas)
{
    NodePtr node = getNode();

    if ( refreshMetadatas && node->isNodeCreated() ) {
        refreshMetaDatas_public(true);
    }

    /*
       We always have to trigger a render because this might be a tree not connected via a link to the knob who changed
       but just an expression

       if (reason == eValueChangedReasonSlaveRefresh) {
        //do not trigger a render, the master will do it already
        return;
       }*/


    double time = getCurrentTime();
    std::list<ViewerInstancePtr> viewers;
    node->hasViewersConnected(&viewers);
    for (std::list<ViewerInstancePtr>::iterator it = viewers.begin();
         it != viewers.end();
         ++it) {
        if (isSignificant) {
            (*it)->renderCurrentFrame(true);
        } else {
            (*it)->redrawViewer();
        }
    }
    if (isSignificant) {
        node->refreshPreviewsRecursivelyDownstream(time);
    }
} // evaluate

bool
EffectInstance::message(MessageTypeEnum type,
                        const std::string & content) const
{
    return getNode()->message(type, content);
}

void
EffectInstance::setPersistentMessage(MessageTypeEnum type,
                                     const std::string & content)
{
    getNode()->setPersistentMessage(type, content);
}

bool
EffectInstance::hasPersistentMessage()
{
    return getNode()->hasPersistentMessage();
}

void
EffectInstance::clearPersistentMessage(bool recurse)
{
    NodePtr node = getNode();

    if (node) {
        node->clearPersistentMessage(recurse);
    }
}

int
EffectInstance::getInputNumber(const EffectInstancePtr& inputEffect) const
{
    for (int i = 0; i < getMaxInputCount(); ++i) {
        if (getInput(i) == inputEffect) {
            return i;
        }
    }

    return -1;
}

/**
 * @brief Does this effect supports rendering at a different scale than 1 ?
 * There is no OFX property for this purpose. The only solution found for OFX is that if a isIdentity
 * with renderscale != 1 fails, the host retries with renderscale = 1 (and upscaled images).
 * If the renderScale support was not set, this throws an exception.
 **/
bool
EffectInstance::supportsRenderScale() const
{
    if (_imp->supportsRenderScale == eSupportsMaybe) {
        qDebug() << "EffectInstance::supportsRenderScale should be set before calling supportsRenderScale(), or use supportsRenderScaleMaybe() instead";
        throw std::runtime_error("supportsRenderScale not set");
    }

    return _imp->supportsRenderScale == eSupportsYes;
}

EffectInstance::SupportsEnum
EffectInstance::supportsRenderScaleMaybe() const
{
    QMutexLocker l(&_imp->supportsRenderScaleMutex);

    return _imp->supportsRenderScale;
}

/// should be set during effect initialization, but may also be set by the first getRegionOfDefinition that succeeds
void
EffectInstance::setSupportsRenderScaleMaybe(EffectInstance::SupportsEnum s) const
{
    {
        QMutexLocker l(&_imp->supportsRenderScaleMutex);

        _imp->supportsRenderScale = s;
    }
    NodePtr node = getNode();

    if (node) {
        node->onSetSupportRenderScaleMaybeSet( (int)s );
    }
}

void
EffectInstance::setOutputFilesForWriter(const std::string & pattern)
{
    if ( !isWriter() ) {
        return;
    }

    const KnobsVec & knobs = getKnobs();
    for (U32 i = 0; i < knobs.size(); ++i) {
        if ( knobs[i]->typeName() == KnobOutputFile::typeNameStatic() ) {
            KnobOutputFilePtr fk = toKnobOutputFile(knobs[i]);
            assert(fk);
            if ( fk->isOutputImageFile() ) {
                fk->setValue(pattern);
                break;
            }
        }
    }
}

PluginMemoryPtr
EffectInstance::newMemoryInstance(size_t nBytes)
{
    PluginMemoryPtr ret( new PluginMemory( shared_from_this() ) ); //< hack to get "this" as a shared ptr

    addPluginMemoryPointer(ret);
    bool wasntLocked = ret->alloc(nBytes);

    assert(wasntLocked);
    Q_UNUSED(wasntLocked);

    return ret;
}

void
EffectInstance::addPluginMemoryPointer(const PluginMemoryPtr& mem)
{
    QMutexLocker l(&_imp->pluginMemoryChunksMutex);

    _imp->pluginMemoryChunks.push_back(mem);
}

void
EffectInstance::removePluginMemoryPointer(const PluginMemory* mem)
{
    std::list<boost::shared_ptr<PluginMemory> > safeCopy;

    {
        QMutexLocker l(&_imp->pluginMemoryChunksMutex);
        // make a copy of the list so that elements don't get deleted while the mutex is held

        for (std::list<boost::weak_ptr<PluginMemory> >::iterator it = _imp->pluginMemoryChunks.begin(); it != _imp->pluginMemoryChunks.end(); ++it) {
            PluginMemoryPtr p = it->lock();
            if (!p) {
                continue;
            }
            safeCopy.push_back(p);
            if (p.get() == mem) {
                _imp->pluginMemoryChunks.erase(it);

                return;
            }
        }
    }
}

void
EffectInstance::registerPluginMemory(size_t nBytes)
{
    getNode()->registerPluginMemory(nBytes);
}

void
EffectInstance::unregisterPluginMemory(size_t nBytes)
{
    getNode()->unregisterPluginMemory(nBytes);
}

void
EffectInstance::onAllKnobsSlaved(bool isSlave,
                                 const KnobHolderPtr& master)
{
    getNode()->onAllKnobsSlaved(isSlave, master);
}

void
EffectInstance::onKnobSlaved(const KnobIPtr& slave,
                             const KnobIPtr& master,
                             int dimension,
                             bool isSlave)
{
    getNode()->onKnobSlaved(slave, master, dimension, isSlave);
}

void
EffectInstance::setCurrentViewportForOverlays_public(OverlaySupport* viewport)
{
    assert( QThread::currentThread() == qApp->thread() );
    getNode()->setCurrentViewportForHostOverlays(viewport);
    _imp->overlaysViewport = viewport;
    setCurrentViewportForOverlays(viewport);
}

OverlaySupport*
EffectInstance::getCurrentViewportForOverlays() const
{
    assert( QThread::currentThread() == qApp->thread() );

    return _imp->overlaysViewport;
}

void
EffectInstance::setDoingInteractAction(bool doing)
{
    _imp->setDuringInteractAction(doing);
}

void
EffectInstance::drawOverlay_public(double time,
                                   const RenderScale & renderScale,
                                   ViewIdx view)
{
    ///cannot be run in another thread
    assert( QThread::currentThread() == qApp->thread() );
    if ( !hasOverlay() && !getNode()->hasHostOverlay() ) {
        return;
    }

    RECURSIVE_ACTION();

    RenderScale actualScale;
    if ( !canHandleRenderScaleForOverlays() ) {
        actualScale.x = actualScale.y = 1.;
    } else {
        actualScale = renderScale;
    }

    _imp->setDuringInteractAction(true);
    bool drawHostOverlay = shouldDrawHostOverlay();
    drawOverlay(time, actualScale, view);
    if (drawHostOverlay) {
        getNode()->drawHostOverlay(time, actualScale, view);
    }
    _imp->setDuringInteractAction(false);
}

bool
EffectInstance::onOverlayPenDown_public(double time,
                                        const RenderScale & renderScale,
                                        ViewIdx view,
                                        const QPointF & viewportPos,
                                        const QPointF & pos,
                                        double pressure,
                                        double timestamp,
                                        PenType pen)
{
    ///cannot be run in another thread
    assert( QThread::currentThread() == qApp->thread() );
    if ( !hasOverlay()  && !getNode()->hasHostOverlay() ) {
        return false;
    }

    RenderScale actualScale;
    if ( !canHandleRenderScaleForOverlays() ) {
        actualScale.x = actualScale.y = 1.;
    } else {
        actualScale = renderScale;
    }

    bool ret;
    {
        NON_RECURSIVE_ACTION();
        _imp->setDuringInteractAction(true);
        bool drawHostOverlay = shouldDrawHostOverlay();
        if (!shouldPreferPluginOverlayOverHostOverlay()) {
            ret = drawHostOverlay ? getNode()->onOverlayPenDownDefault(time, actualScale, view, viewportPos, pos, pressure) : false;
            if (!ret) {
                ret |= onOverlayPenDown(time, actualScale, view, viewportPos, pos, pressure, timestamp, pen);
            }
        } else {
            ret = onOverlayPenDown(time, actualScale, view, viewportPos, pos, pressure, timestamp, pen);
            if (!ret && drawHostOverlay) {
                ret |= getNode()->onOverlayPenDownDefault(time, actualScale, view, viewportPos, pos, pressure);
            }
        }

        _imp->setDuringInteractAction(false);
    }
    checkIfRenderNeeded();

    return ret;
}

bool
EffectInstance::onOverlayPenDoubleClicked_public(double time,
                                                 const RenderScale & renderScale,
                                                 ViewIdx view,
                                                 const QPointF & viewportPos,
                                                 const QPointF & pos)
{
    ///cannot be run in another thread
    assert( QThread::currentThread() == qApp->thread() );
    if ( !hasOverlay()  && !getNode()->hasHostOverlay() ) {
        return false;
    }

    RenderScale actualScale;
    if ( !canHandleRenderScaleForOverlays() ) {
        actualScale.x = actualScale.y = 1.;
    } else {
        actualScale = renderScale;
    }

    bool ret;
    {
        NON_RECURSIVE_ACTION();
        _imp->setDuringInteractAction(true);
        bool drawHostOverlay = shouldDrawHostOverlay();
        if (!shouldPreferPluginOverlayOverHostOverlay()) {
            ret = drawHostOverlay ? getNode()->onOverlayPenDoubleClickedDefault(time, actualScale, view, viewportPos, pos) : false;
            if (!ret) {
                ret |= onOverlayPenDoubleClicked(time, actualScale, view, viewportPos, pos);
            }
        } else {
            ret = onOverlayPenDoubleClicked(time, actualScale, view, viewportPos, pos);
            if (!ret && drawHostOverlay) {
                ret |= getNode()->onOverlayPenDoubleClickedDefault(time, actualScale, view, viewportPos, pos);
            }
        }

        _imp->setDuringInteractAction(false);
    }
    checkIfRenderNeeded();

    return ret;
}

bool
EffectInstance::onOverlayPenMotion_public(double time,
                                          const RenderScale & renderScale,
                                          ViewIdx view,
                                          const QPointF & viewportPos,
                                          const QPointF & pos,
                                          double pressure,
                                          double timestamp)
{
    ///cannot be run in another thread
    assert( QThread::currentThread() == qApp->thread() );
    if ( !hasOverlay()  && !getNode()->hasHostOverlay() ) {
        return false;
    }

    RenderScale actualScale;
    if ( !canHandleRenderScaleForOverlays() ) {
        actualScale.x = actualScale.y = 1.;
    } else {
        actualScale = renderScale;
    }


    NON_RECURSIVE_ACTION();
    _imp->setDuringInteractAction(true);
    bool ret;
    bool drawHostOverlay = shouldDrawHostOverlay();
    if (!shouldPreferPluginOverlayOverHostOverlay()) {
        ret = drawHostOverlay ? getNode()->onOverlayPenMotionDefault(time, actualScale, view, viewportPos, pos, pressure) : false;
        if (!ret) {
            ret |= onOverlayPenMotion(time, actualScale, view, viewportPos, pos, pressure, timestamp);
        }
    } else {
        ret = onOverlayPenMotion(time, actualScale, view, viewportPos, pos, pressure, timestamp);
        if (!ret && drawHostOverlay) {
            ret |= getNode()->onOverlayPenMotionDefault(time, actualScale, view, viewportPos, pos, pressure);
        }
    }

    _imp->setDuringInteractAction(false);
    //Don't chek if render is needed on pen motion, wait for the pen up

    //checkIfRenderNeeded();
    return ret;
}

bool
EffectInstance::onOverlayPenUp_public(double time,
                                      const RenderScale & renderScale,
                                      ViewIdx view,
                                      const QPointF & viewportPos,
                                      const QPointF & pos,
                                      double pressure,
                                      double timestamp)
{
    ///cannot be run in another thread
    assert( QThread::currentThread() == qApp->thread() );
    if ( !hasOverlay()  && !getNode()->hasHostOverlay() ) {
        return false;
    }

    RenderScale actualScale;
    if ( !canHandleRenderScaleForOverlays() ) {
        actualScale.x = actualScale.y = 1.;
    } else {
        actualScale = renderScale;
    }

    bool ret;
    {
        NON_RECURSIVE_ACTION();
        _imp->setDuringInteractAction(true);
        bool drawHostOverlay = shouldDrawHostOverlay();
        if (!shouldPreferPluginOverlayOverHostOverlay()) {
            ret = drawHostOverlay ? getNode()->onOverlayPenUpDefault(time, actualScale, view, viewportPos, pos, pressure) : false;
            if (!ret) {
                ret |= onOverlayPenUp(time, actualScale, view, viewportPos, pos, pressure, timestamp);
            }
        } else {
            ret = onOverlayPenUp(time, actualScale, view, viewportPos, pos, pressure, timestamp);
            if (!ret && drawHostOverlay) {
                ret |= getNode()->onOverlayPenUpDefault(time, actualScale, view, viewportPos, pos, pressure);
            }
        }

        _imp->setDuringInteractAction(false);
    }
    checkIfRenderNeeded();

    return ret;
}

bool
EffectInstance::onOverlayKeyDown_public(double time,
                                        const RenderScale & renderScale,
                                        ViewIdx view,
                                        Key key,
                                        KeyboardModifiers modifiers)
{
    ///cannot be run in another thread
    assert( QThread::currentThread() == qApp->thread() );
    if ( !hasOverlay()  && !getNode()->hasHostOverlay() ) {
        return false;
    }

    RenderScale actualScale;
    if ( !canHandleRenderScaleForOverlays() ) {
        actualScale.x = actualScale.y = 1.;
    } else {
        actualScale = renderScale;
    }


    bool ret;
    {
        NON_RECURSIVE_ACTION();
        _imp->setDuringInteractAction(true);
        ret = onOverlayKeyDown(time, actualScale, view, key, modifiers);
        if (!ret && shouldDrawHostOverlay()) {
            ret |= getNode()->onOverlayKeyDownDefault(time, actualScale, view, key, modifiers);
        }
        _imp->setDuringInteractAction(false);
    }
    checkIfRenderNeeded();

    return ret;
}

bool
EffectInstance::onOverlayKeyUp_public(double time,
                                      const RenderScale & renderScale,
                                      ViewIdx view,
                                      Key key,
                                      KeyboardModifiers modifiers)
{
    ///cannot be run in another thread
    assert( QThread::currentThread() == qApp->thread() );
    if ( !hasOverlay()  && !getNode()->hasHostOverlay() ) {
        return false;
    }

    RenderScale actualScale;
    if ( !canHandleRenderScaleForOverlays() ) {
        actualScale.x = actualScale.y = 1.;
    } else {
        actualScale = renderScale;
    }

    bool ret;
    {
        NON_RECURSIVE_ACTION();

        _imp->setDuringInteractAction(true);
        ret = onOverlayKeyUp(time, actualScale, view, key, modifiers);
        if (!ret && shouldDrawHostOverlay()) {
            ret |= getNode()->onOverlayKeyUpDefault(time, actualScale, view, key, modifiers);
        }
        _imp->setDuringInteractAction(false);
    }
    checkIfRenderNeeded();

    return ret;
}

bool
EffectInstance::onOverlayKeyRepeat_public(double time,
                                          const RenderScale & renderScale,
                                          ViewIdx view,
                                          Key key,
                                          KeyboardModifiers modifiers)
{
    ///cannot be run in another thread
    assert( QThread::currentThread() == qApp->thread() );
    if ( !hasOverlay()  && !getNode()->hasHostOverlay() ) {
        return false;
    }

    RenderScale actualScale;
    if ( !canHandleRenderScaleForOverlays() ) {
        actualScale.x = actualScale.y = 1.;
    } else {
        actualScale = renderScale;
    }

    bool ret;
    {
        NON_RECURSIVE_ACTION();
        _imp->setDuringInteractAction(true);
        ret = onOverlayKeyRepeat(time, actualScale, view, key, modifiers);
        if (!ret && shouldDrawHostOverlay()) {
            ret |= getNode()->onOverlayKeyRepeatDefault(time, actualScale, view, key, modifiers);
        }
        _imp->setDuringInteractAction(false);
    }
    checkIfRenderNeeded();

    return ret;
}

bool
EffectInstance::onOverlayFocusGained_public(double time,
                                            const RenderScale & renderScale,
                                            ViewIdx view)
{
    ///cannot be run in another thread
    assert( QThread::currentThread() == qApp->thread() );
    if ( !hasOverlay() && !getNode()->hasHostOverlay() ) {
        return false;
    }

    RenderScale actualScale;
    if ( !canHandleRenderScaleForOverlays() ) {
        actualScale.x = actualScale.y = 1.;
    } else {
        actualScale = renderScale;
    }

    bool ret;
    {
        NON_RECURSIVE_ACTION();
        _imp->setDuringInteractAction(true);
        ret = onOverlayFocusGained(time, actualScale, view);
        if (shouldDrawHostOverlay()) {
            ret |= getNode()->onOverlayFocusGainedDefault(time, actualScale, view);
        }

        _imp->setDuringInteractAction(false);
    }
    checkIfRenderNeeded();

    return ret;
}

bool
EffectInstance::onOverlayFocusLost_public(double time,
                                          const RenderScale & renderScale,
                                          ViewIdx view)
{
    ///cannot be run in another thread
    assert( QThread::currentThread() == qApp->thread() );
    if ( !hasOverlay() && !getNode()->hasHostOverlay() ) {
        return false;
    }

    RenderScale actualScale;
    if ( !canHandleRenderScaleForOverlays() ) {
        actualScale.x = actualScale.y = 1.;
    } else {
        actualScale = renderScale;
    }


    bool ret;
    {
        NON_RECURSIVE_ACTION();
        _imp->setDuringInteractAction(true);
        ret = onOverlayFocusLost(time, actualScale, view);
        if (shouldDrawHostOverlay()) {
            ret |= getNode()->onOverlayFocusLostDefault(time, actualScale, view);
        }

        _imp->setDuringInteractAction(false);
    }
    checkIfRenderNeeded();

    return ret;
}

void
EffectInstance::setInteractColourPicker_public(const OfxRGBAColourD& color, bool setColor, bool hasColor)
{
    const KnobsVec& knobs = getKnobs();
    for (KnobsVec::const_iterator it2 = knobs.begin(); it2 != knobs.end(); ++it2) {
        const KnobIPtr& k = *it2;
        if (!k) {
            continue;
        }
        boost::shared_ptr<OfxParamOverlayInteract> interact = k->getCustomInteract();
        if (!interact) {
            continue;
        }

        if (!interact->isColorPickerRequired()) {
            continue;
        }
        if (!hasColor) {
            interact->setHasColorPicker(false);
        } else {
            if (setColor) {
                interact->setLastColorPickerColor(color);
            }
            interact->setHasColorPicker(true);
        }

        k->redraw();
    }

    setInteractColourPicker(color, setColor, hasColor);

}

bool
EffectInstance::isDoingInteractAction() const
{
    QReadLocker l(&_imp->duringInteractActionMutex);

    return _imp->duringInteractAction;
}

StatusEnum
EffectInstance::render_public(const RenderActionArgs & args)
{
    NON_RECURSIVE_ACTION();
    REPORT_CURRENT_THREAD_ACTION( "kOfxImageEffectActionRender", getNode() );
    try {
        return render(args);
    } catch (...) {
        // Any exception thrown here will fail render
        return eStatusFailed;
    }
}

StatusEnum
EffectInstance::getTransform_public(double time,
                                    const RenderScale & renderScale,
                                    ViewIdx view,
                                    EffectInstancePtr* inputToTransform,
                                    Transform::Matrix3x3* transform)
{
    RECURSIVE_ACTION();
    assert( getNode()->getCurrentCanTransform() );

    return getTransform(time, renderScale, view, inputToTransform, transform);
}

bool
EffectInstance::isIdentity_public(bool useIdentityCache, // only set to true when calling for the whole image (not for a subrect)
                                  U64 hash,
                                  double time,
                                  const RenderScale & scale,
                                  const RectI & renderWindow,
                                  ViewIdx view,
                                  double* inputTime,
                                  ViewIdx* inputView,
                                  int* inputNb)
{
    assert( !( (supportsRenderScaleMaybe() == eSupportsNo) && !(scale.x == 1. && scale.y == 1.) ) );

    if (useIdentityCache) {
        double timeF = 0.;
        bool foundInCache = _imp->actionsCache->getIdentityResult(hash, time, view, inputNb, inputView, &timeF);
        if (foundInCache) {
            *inputTime = timeF;

            return *inputNb >= 0 || *inputNb == -2;
        }
    }


    ///EDIT: We now allow isIdentity to be called recursively.
    RECURSIVE_ACTION();


    bool ret = false;
    RotoDrawableItemPtr rotoItem = getNode()->getAttachedRotoItem();
    if ( ( rotoItem && !rotoItem->isActivated(time) ) || getNode()->isNodeDisabled() || !getNode()->hasAtLeastOneChannelToProcess() ) {
        ret = true;
        *inputNb = getNode()->getPreferredInput();
        *inputTime = time;
        *inputView = view;
    } else if ( appPTR->isBackground() && (dynamic_cast<DiskCacheNode*>(this) != NULL) ) {
        ret = true;
        *inputNb = 0;
        *inputTime = time;
        *inputView = view;
    } else {
        /// Don't call isIdentity if plugin is sequential only.
        if (getSequentialPreference() != eSequentialPreferenceOnlySequential) {
            try {
                *inputView = view;
                ret = isIdentity(time, scale, renderWindow, view, inputTime, inputView, inputNb);
            } catch (...) {
                throw;
            }
        }
    }
    if (!ret) {
        *inputNb = -1;
        *inputTime = time;
        *inputView = view;
    }

    if (useIdentityCache) {
        _imp->actionsCache->setIdentityResult(hash, time, view, *inputNb, *inputView, *inputTime);
    }

    return ret;
} // EffectInstance::isIdentity_public

void
EffectInstance::onInputChanged(int /*inputNo*/)
{
}

StatusEnum
EffectInstance::getRegionOfDefinitionFromCache(U64 hash,
                                               double time,
                                               const RenderScale & scale,
                                               ViewIdx view,
                                               RectD* rod,
                                               bool* isProjectFormat)
{
    unsigned int mipMapLevel = Image::getLevelFromScale(scale.x);
    bool foundInCache = _imp->actionsCache->getRoDResult(hash, time, view, mipMapLevel, rod);

    if (foundInCache) {
        if (isProjectFormat) {
            *isProjectFormat = false;
        }
        if ( rod->isNull() ) {
            return eStatusFailed;
        }

        return eStatusOK;
    }

    return eStatusFailed;
}

StatusEnum
EffectInstance::getRegionOfDefinition_public(U64 hash,
                                             double time,
                                             const RenderScale & scale,
                                             ViewIdx view,
                                             RectD* rod,
                                             bool* isProjectFormat)
{
    if ( !isEffectCreated() ) {
        return eStatusFailed;
    }

    unsigned int mipMapLevel = Image::getLevelFromScale(scale.x);
    bool foundInCache = _imp->actionsCache->getRoDResult(hash, time, view, mipMapLevel, rod);
    if (foundInCache) {
        if (isProjectFormat) {
            *isProjectFormat = false;
        }
        if ( rod->isNull() ) {
            return eStatusFailed;
        }

        return eStatusOK;
    } else {
        ///If this is running on a render thread, attempt to find the RoD in the thread local storage.

        if ( QThread::currentThread() != qApp->thread() ) {
            EffectDataTLSPtr tls = _imp->tlsData->getTLSData();
            if (tls && tls->currentRenderArgs.validArgs) {
                *rod = tls->currentRenderArgs.rod;
                if (isProjectFormat) {
                    *isProjectFormat = false;
                }

                return eStatusOK;
            }
        }

        if ( getNode()->isNodeDisabled() ) {
            NodePtr preferredInput = getNode()->getPreferredInputNode();
            if (!preferredInput) {
                return eStatusFailed;
            }

            return preferredInput->getEffectInstance()->getRegionOfDefinition_public(preferredInput->getEffectInstance()->getRenderHash(), time, scale, view, rod, isProjectFormat);
        }

        StatusEnum ret;
        RenderScale scaleOne(1.);
        {
            RECURSIVE_ACTION();


            ret = getRegionOfDefinition(hash, time, supportsRenderScaleMaybe() == eSupportsNo ? scaleOne : scale, view, rod);

            if ( (ret != eStatusOK) && (ret != eStatusReplyDefault) ) {
                // rod is not valid
                //if (!isDuringStrokeCreation) {
                _imp->actionsCache->invalidateAll(hash);
                _imp->actionsCache->setRoDResult( hash, time, view, mipMapLevel, RectD() );

                // }
                return ret;
            }

            if ( rod->isNull() ) {
                // RoD is empty, which means output is black and transparent
                _imp->actionsCache->setRoDResult( hash, time, view, mipMapLevel, RectD() );

                return ret;
            }

            assert( (ret == eStatusOK || ret == eStatusReplyDefault) && (rod->x1 <= rod->x2 && rod->y1 <= rod->y2) );
        }
        bool isProject = ifInfiniteApplyHeuristic(hash, time, scale, view, rod);
        if (isProjectFormat) {
            *isProjectFormat = isProject;
        }
        assert(rod->x1 <= rod->x2 && rod->y1 <= rod->y2);

        //if (!isDuringStrokeCreation) {
        _imp->actionsCache->setRoDResult(hash, time, view,  mipMapLevel, *rod);

        //}
        return ret;
    }
} // EffectInstance::getRegionOfDefinition_public

void
EffectInstance::getRegionsOfInterest_public(double time,
                                            const RenderScale & scale,
                                            const RectD & outputRoD, //!< effect RoD in canonical coordinates
                                            const RectD & renderWindow, //!< the region to be rendered in the output image, in Canonical Coordinates
                                            ViewIdx view,
                                            RoIMap* ret)
{
    NON_RECURSIVE_ACTION();
    assert(outputRoD.x2 >= outputRoD.x1 && outputRoD.y2 >= outputRoD.y1);
    assert(renderWindow.x2 >= renderWindow.x1 && renderWindow.y2 >= renderWindow.y1);

    getRegionsOfInterest(time, scale, outputRoD, renderWindow, view, ret);
}

FramesNeededMap
EffectInstance::getFramesNeeded_public(U64 hash,
                                       double time,
                                       ViewIdx view,
                                       unsigned int mipMapLevel)
{
    NON_RECURSIVE_ACTION();
    FramesNeededMap framesNeeded;
    bool foundInCache = _imp->actionsCache->getFramesNeededResult(hash, time, view, mipMapLevel, &framesNeeded);
    if (foundInCache) {
        return framesNeeded;
    }

    try {
        framesNeeded = getFramesNeeded(time, view);
    } catch (std::exception &e) {
        if ( !hasPersistentMessage() ) { // plugin may already have set a message
            setPersistentMessage( eMessageTypeError, e.what() );
        }
    }

    _imp->actionsCache->setFramesNeededResult(hash, time, view, mipMapLevel, framesNeeded);

    return framesNeeded;
}

void
EffectInstance::getFrameRange_public(U64 hash,
                                     double *first,
                                     double *last,
                                     bool bypasscache)
{
    double fFirst = 0., fLast = 0.;
    bool foundInCache = false;

    if (!bypasscache) {
        foundInCache = _imp->actionsCache->getTimeDomainResult(hash, &fFirst, &fLast);
    }
    if (foundInCache) {
        *first = std::floor(fFirst + 0.5);
        *last = std::floor(fLast + 0.5);
    } else {
        ///If this is running on a render thread, attempt to find the info in the thread local storage.
        if ( QThread::currentThread() != qApp->thread() ) {
            EffectDataTLSPtr tls = _imp->tlsData->getTLSData();
            if (tls && tls->currentRenderArgs.validArgs) {
                *first = tls->currentRenderArgs.firstFrame;
                *last = tls->currentRenderArgs.lastFrame;

                return;
            }
        }

        NON_RECURSIVE_ACTION();
        getFrameRange(first, last);
        _imp->actionsCache->setTimeDomainResult(hash, *first, *last);
    }
}

StatusEnum
EffectInstance::beginSequenceRender_public(double first,
                                           double last,
                                           double step,
                                           bool interactive,
                                           const RenderScale & scale,
                                           bool isSequentialRender,
                                           bool isRenderResponseToUserInteraction,
                                           bool draftMode,
                                           ViewIdx view,
                                           bool isOpenGLRender,
                                           const EffectOpenGLContextDataPtr& glContextData)
{
    NON_RECURSIVE_ACTION();
    REPORT_CURRENT_THREAD_ACTION( "kOfxImageEffectActionBeginSequenceRender", getNode() );
    EffectDataTLSPtr tls = _imp->tlsData->getOrCreateTLSData();
    assert(tls);
    ++tls->beginEndRenderCount;

    return beginSequenceRender(first, last, step, interactive, scale,
                               isSequentialRender, isRenderResponseToUserInteraction, draftMode, view, isOpenGLRender, glContextData);
}

StatusEnum
EffectInstance::endSequenceRender_public(double first,
                                         double last,
                                         double step,
                                         bool interactive,
                                         const RenderScale & scale,
                                         bool isSequentialRender,
                                         bool isRenderResponseToUserInteraction,
                                         bool draftMode,
                                         ViewIdx view,
                                         bool isOpenGLRender,
                                         const EffectOpenGLContextDataPtr& glContextData)
{
    NON_RECURSIVE_ACTION();
    REPORT_CURRENT_THREAD_ACTION( "kOfxImageEffectActionEndSequenceRender", getNode() );
    EffectDataTLSPtr tls = _imp->tlsData->getOrCreateTLSData();
    assert(tls);
    --tls->beginEndRenderCount;
    assert(tls->beginEndRenderCount >= 0);

    return endSequenceRender(first, last, step, interactive, scale, isSequentialRender, isRenderResponseToUserInteraction, draftMode, view, isOpenGLRender, glContextData);
}

EffectInstancePtr
EffectInstance::getOrCreateRenderInstance()
{
    QMutexLocker k(&_imp->renderClonesMutex);
    if (!_imp->isDoingInstanceSafeRender) {
        // The main instance is not rendering, use it
        _imp->isDoingInstanceSafeRender = true;
        return shared_from_this();
    }
    // Ok get a clone
    if (!_imp->renderClonesPool.empty()) {
        EffectInstancePtr ret =  _imp->renderClonesPool.front();
        _imp->renderClonesPool.pop_front();
        ret->_imp->isDoingInstanceSafeRender = true;
        return ret;
    }

    EffectInstancePtr clone = createRenderClone();
    if (!clone) {
        // We have no way but to use this node since the effect does not support render clones
        _imp->isDoingInstanceSafeRender = true;
        return shared_from_this();
    }
    clone->_imp->isDoingInstanceSafeRender = true;
    return clone;
}

void
EffectInstance::clearRenderInstances()
{
    QMutexLocker k(&_imp->renderClonesMutex);
    _imp->renderClonesPool.clear();
}

void
EffectInstance::releaseRenderInstance(const EffectInstancePtr& instance)
{
    if (!instance) {
        return;
    }
    QMutexLocker k(&_imp->renderClonesMutex);
    instance->_imp->isDoingInstanceSafeRender = false;
    if (instance.get() == this) {
        return;
    }

    // Make this instance available again
    _imp->renderClonesPool.push_back(instance);
}

/**
 * @brief This function calls the impementation specific attachOpenGLContext()
 **/
StatusEnum
EffectInstance::attachOpenGLContext_public(const OSGLContextPtr& glContext,
                                           EffectOpenGLContextDataPtr* data)
{
    NON_RECURSIVE_ACTION();
    bool concurrentGLRender = supportsConcurrentOpenGLRenders();
    boost::scoped_ptr<QMutexLocker> locker;
    if (concurrentGLRender) {
        locker.reset( new QMutexLocker(&_imp->attachedContextsMutex) );
    } else {
        _imp->attachedContextsMutex.lock();
    }

    std::map<OSGLContextWPtr, EffectOpenGLContextDataPtr>::iterator found = _imp->attachedContexts.find(glContext);
    if ( found != _imp->attachedContexts.end() ) {
        // The context is already attached
        *data = found->second;

        return eStatusOK;
    }


    StatusEnum ret = attachOpenGLContext(glContext, data);

    if ( (ret == eStatusOK) || (ret == eStatusReplyDefault) ) {
        if (!concurrentGLRender) {
            (*data)->setHasTakenLock(true);
        }
        _imp->attachedContexts.insert( std::make_pair(glContext, *data) );
    } else {
        _imp->attachedContextsMutex.unlock();
    }

    // Take the lock until dettach is called for plug-ins that do not support concurrent GL renders
    return ret;
}

void
EffectInstance::dettachAllOpenGLContexts()
{
    QMutexLocker locker(&_imp->attachedContextsMutex);

    for (std::map<OSGLContextWPtr, EffectOpenGLContextDataPtr>::iterator it = _imp->attachedContexts.begin(); it != _imp->attachedContexts.end(); ++it) {
        OSGLContextPtr context = it->first.lock();
        if (!context) {
            continue;
        }
        context->setContextCurrentNoRender();
        if (it->second.use_count() == 1) {
            // If no render is using it, dettach the context
            dettachOpenGLContext(context, it->second);
        }
    }
    if ( !_imp->attachedContexts.empty() ) {
        OSGLContext::unsetCurrentContextNoRenderInternal(true, 0);
    }
    _imp->attachedContexts.clear();
}

/**
 * @brief This function calls the impementation specific dettachOpenGLContext()
 **/
StatusEnum
EffectInstance::dettachOpenGLContext_public(const OSGLContextPtr& glContext, const EffectOpenGLContextDataPtr& data)
{
    NON_RECURSIVE_ACTION();
    bool concurrentGLRender = supportsConcurrentOpenGLRenders();
    boost::scoped_ptr<QMutexLocker> locker;
    if (concurrentGLRender) {
        locker.reset( new QMutexLocker(&_imp->attachedContextsMutex) );
    }


    bool mustUnlock = data->getHasTakenLock();
    std::map<OSGLContextWPtr, EffectOpenGLContextDataPtr>::iterator found = _imp->attachedContexts.find(glContext);
    if ( found != _imp->attachedContexts.end() ) {
        _imp->attachedContexts.erase(found);
    }

    StatusEnum ret = dettachOpenGLContext(glContext, data);
    if (mustUnlock) {
        _imp->attachedContextsMutex.unlock();
    }

    return ret;
}

bool
EffectInstance::isSupportedComponent(int inputNb,
                                     const ImageComponents & comp) const
{
    return getNode()->isSupportedComponent(inputNb, comp);
}

ImageBitDepthEnum
EffectInstance::getBestSupportedBitDepth() const
{
    return getNode()->getBestSupportedBitDepth();
}

bool
EffectInstance::isSupportedBitDepth(ImageBitDepthEnum depth) const
{
    return getNode()->isSupportedBitDepth(depth);
}

ImageComponents
EffectInstance::findClosestSupportedComponents(int inputNb,
                                               const ImageComponents & comp) const
{
    return getNode()->findClosestSupportedComponents(inputNb, comp);
}

void
EffectInstance::clearActionsCache()
{
    _imp->actionsCache->clearAll();
}

void
EffectInstance::setComponentsAvailableDirty(bool dirty)
{
    QMutexLocker k(&_imp->componentsAvailableMutex);

    _imp->componentsAvailableDirty = dirty;
}

void
EffectInstance::getComponentsAvailableRecursive(bool useLayerChoice,
                                                bool useThisNodeComponentsNeeded,
                                                double time,
                                                ViewIdx view,
                                                ComponentsAvailableMap* comps,
                                                std::list<EffectInstancePtr>* markedNodes)
{
    if ( std::find(markedNodes->begin(), markedNodes->end(), shared_from_this()) != markedNodes->end() ) {
        return;
    }

    if (useLayerChoice && useThisNodeComponentsNeeded) {
        QMutexLocker k(&_imp->componentsAvailableMutex);
        if (!_imp->componentsAvailableDirty) {
            comps->insert( _imp->outputComponentsAvailable.begin(), _imp->outputComponentsAvailable.end() );

            return;
        }
    }


    NodePtr node  = getNode();
    if (!node) {
        return;
    }
    EffectInstance::ComponentsNeededMap neededComps;
    SequenceTime ptTime;
    int ptView;
    NodePtr ptInput;
    bool processAll;
    std::bitset<4> processChannels;
    getComponentsNeededAndProduced_public(useLayerChoice, useThisNodeComponentsNeeded, time, view, &neededComps, &processAll, &ptTime, &ptView, &processChannels, &ptInput);


    ///If the plug-in is not pass-through, only consider the components processed by the plug-in in output,
    ///so we do not need to recurse.
    PassThroughEnum passThrough = isPassThroughForNonRenderedPlanes();
    if ( (passThrough == ePassThroughPassThroughNonRenderedPlanes) ||
         ( passThrough == ePassThroughRenderAllRequestedPlanes) ) {
        if (!isMultiPlanar() || !ptInput) {
            ptInput = node->getInput( node->getPreferredInput() );
        }

        if (ptInput) {
            ptInput->getEffectInstance()->getComponentsAvailableRecursive(useLayerChoice, true, time, view, comps, markedNodes);
        }
    }
    if (processAll) {
        //The node makes available everything available upstream
        for (ComponentsAvailableMap::iterator it = comps->begin(); it != comps->end(); ++it) {
            if ( it->second.lock() ) {
                it->second = node;
            }
        }
    }


    EffectInstance::ComponentsNeededMap::iterator foundOutput = neededComps.find(-1);
    if ( foundOutput != neededComps.end() ) {
        ///Foreach component produced by the node at the given (view, time),  try
        ///to add it to the components available. Since we already handled upstream nodes, it is probably
        ///already in there, in which case we mark that this node is producing the component instead
        for (std::vector<ImageComponents>::iterator it = foundOutput->second.begin();
             it != foundOutput->second.end(); ++it) {
            ComponentsAvailableMap::iterator alreadyExisting = comps->end();

            if ( it->isColorPlane() ) {
                ComponentsAvailableMap::iterator colorMatch = comps->end();

                for (ComponentsAvailableMap::iterator it2 = comps->begin(); it2 != comps->end(); ++it2) {
                    if (it2->first == *it) {
                        alreadyExisting = it2;
                        break;
                    } else if ( it2->first.isColorPlane() ) {
                        colorMatch = it2;
                    }
                }

                if ( ( alreadyExisting == comps->end() ) && ( colorMatch != comps->end() ) ) {
                    comps->erase(colorMatch);
                }
            } else {
                for (ComponentsAvailableMap::iterator it2 = comps->begin(); it2 != comps->end(); ++it2) {
                    if (it2->first == *it) {
                        alreadyExisting = it2;
                        break;
                    }
                }
            }


            if ( alreadyExisting == comps->end() ) {
                comps->insert( std::make_pair(*it, node) );
            } else {
                //If the component already exists from upstream in the tree, mark that we produce it instead
                alreadyExisting->second = node;
            }
        }
    }

    ///If the user has selected "All", do not add created components as they will not be available
    if (!processAll) {
        std::list<ImageComponents> userComps;
        node->getUserCreatedComponents(&userComps);

        ///Add to the user comps the project components
        std::vector<ImageComponents> projectLayers = getApp()->getProject()->getProjectDefaultLayers();
        userComps.insert( userComps.end(), projectLayers.begin(), projectLayers.end() );

        ///Foreach user component, add it as an available component, but use this node only if it is also
        ///in the "needed components" list
        for (std::list<ImageComponents>::iterator it = userComps.begin(); it != userComps.end(); ++it) {
            ///If this is a user comp and used by the node it will be in the needed output components
            bool found = false;
            if ( foundOutput != neededComps.end() ) {
                for (std::vector<ImageComponents>::iterator it2 = foundOutput->second.begin();
                     it2 != foundOutput->second.end(); ++it2) {
                    if (*it2 == *it) {
                        found = true;
                        break;
                    }
                }
            }


            ComponentsAvailableMap::iterator alreadyExisting = comps->end();

            if ( it->isColorPlane() ) {
                ComponentsAvailableMap::iterator colorMatch = comps->end();

                for (ComponentsAvailableMap::iterator it2 = comps->begin(); it2 != comps->end(); ++it2) {
                    if (it2->first == *it) {
                        alreadyExisting = it2;
                        break;
                    } else if ( it2->first.isColorPlane() ) {
                        colorMatch = it2;
                    }
                }

                if ( ( alreadyExisting == comps->end() ) && ( colorMatch != comps->end() ) ) {
                    comps->erase(colorMatch);
                }
            } else {
                alreadyExisting = comps->find(*it);
            }

            ///If the component already exists from above in the tree, do not add it
            if ( alreadyExisting == comps->end() ) {
                comps->insert( std::make_pair( *it, (found) ? node : NodePtr() ) );
            } else {
                ///The user component may very well have been created on a node upstream
                ///Set the component as available only if the node uses it actively,i.e if
                ///it was found in the needed output components
                if (found) {
                    alreadyExisting->second = node;
                }
            }
        }
    }

    markedNodes->push_back( shared_from_this() );


    if (useLayerChoice && useThisNodeComponentsNeeded) {
        QMutexLocker k(&_imp->componentsAvailableMutex);
        _imp->componentsAvailableDirty = false;
        _imp->outputComponentsAvailable = *comps;
    }
} // EffectInstance::getComponentsAvailableRecursive

void
EffectInstance::getComponentsAvailable(bool useLayerChoice,
                                       bool useThisNodeComponentsNeeded,
                                       double time,
                                       ComponentsAvailableMap* comps,
                                       std::list<EffectInstancePtr>* markedNodes)
{
    getComponentsAvailableRecursive(useLayerChoice, useThisNodeComponentsNeeded, time, ViewIdx(0), comps, markedNodes);
}

void
EffectInstance::getComponentsAvailable(bool useLayerChoice,
                                       bool useThisNodeComponentsNeeded,
                                       double time,
                                       ComponentsAvailableMap* comps)
{
    //int nViews = getApp()->getProject()->getProjectViewsCount();

    ///Union components over all views
    //for (int view = 0; view < nViews; ++view) {
    ///Edit: Just call for 1 view, it should not matter as this should be view agnostic.
    std::list<EffectInstancePtr> marks;

    getComponentsAvailableRecursive(useLayerChoice, useThisNodeComponentsNeeded, time, ViewIdx(0), comps, &marks);

    //}
}

void
EffectInstance::getComponentsNeededAndProduced(double time,
                                               ViewIdx view,
                                               EffectInstance::ComponentsNeededMap* comps,
                                               SequenceTime* passThroughTime,
                                               int* passThroughView,
                                               NodePtr* passThroughInput)
{
    *passThroughTime = time;
    *passThroughView = view;

    ImageComponents outputComp = getComponents(-1);
    std::vector<ImageComponents> outputCompVec;
    outputCompVec.push_back(outputComp);

    comps->insert( std::make_pair(-1, outputCompVec) );

    NodePtr firstConnectedOptional;
    for (int i = 0; i < getMaxInputCount(); ++i) {
        NodePtr node = getNode()->getInput(i);
        if (!node) {
            continue;
        }

        ImageComponents comp = getComponents(i);
        std::vector<ImageComponents> compVect;
        compVect.push_back(comp);

        comps->insert( std::make_pair(i, compVect) );

        if ( !isInputOptional(i) ) {
            *passThroughInput = node;
        } else {
            firstConnectedOptional = node;
        }
    }
    if (!*passThroughInput) {
        *passThroughInput = firstConnectedOptional;
    }
}

void
EffectInstance::getComponentsNeededAndProduced_public(bool useLayerChoice,
                                                      bool useThisNodeComponentsNeeded,
                                                      double time,
                                                      ViewIdx view,
                                                      EffectInstance::ComponentsNeededMap* comps,
                                                      bool* processAllRequested,
                                                      SequenceTime* passThroughTime,
                                                      int* passThroughView,
                                                      std::bitset<4> *processChannels,
                                                      NodePtr* passThroughInput)

{
    RECURSIVE_ACTION();

    if ( isMultiPlanar() ) {
        for (int i = 0; i < 4; ++i) {
            (*processChannels)[i] = getNode()->getProcessChannel(i);
        }
        if (useThisNodeComponentsNeeded) {
            getComponentsNeededAndProduced(time, view, comps, passThroughTime, passThroughView, passThroughInput);
        }
        *processAllRequested = false;

        return;
    }


    *passThroughTime = time;
    *passThroughView = view;
    int idx = getNode()->getPreferredInput();
    *passThroughInput = getNode()->getInput(idx);
    *processAllRequested = false;
    if (!useThisNodeComponentsNeeded) {
        return;
    }

    ///Get the output needed components
    {
        ImageComponents layer;
        std::vector<ImageComponents> compVec;
        bool ok = false;
        if (useLayerChoice) {
            ok = getNode()->getSelectedLayer(-1, processChannels, processAllRequested, &layer);
        }

        std::vector<ImageComponents> clipPrefsAllComps;
        ImageComponents clipPrefsComps = getComponents(-1);
        {
            if ( clipPrefsComps.isPairedComponents() ) {
                ImageComponents first, second;
                clipPrefsComps.getPlanesPair(&first, &second);
                clipPrefsAllComps.push_back(first);
                clipPrefsAllComps.push_back(second);
            } else {
                clipPrefsAllComps.push_back(clipPrefsComps);
            }
        }

        if ( ok && (layer.getNumComponents() != 0) && !layer.isColorPlane() ) {
            compVec.push_back(layer);

            if ( !clipPrefsComps.isColorPlane() ) {
                compVec.insert( compVec.end(), clipPrefsAllComps.begin(), clipPrefsAllComps.end() );
            }
        } else {
            compVec.insert( compVec.end(), clipPrefsAllComps.begin(), clipPrefsAllComps.end() );
        }

        comps->insert( std::make_pair(-1, compVec) );
    }

    ///For each input get their needed components
    int maxInput = getMaxInputCount();
    for (int i = 0; i < maxInput; ++i) {
        EffectInstancePtr input = getInput(i);
        if (input) {
            std::vector<ImageComponents> compVec;
            std::bitset<4> inputProcChannels;
            ImageComponents layer;
            bool isAll;
            bool ok = getNode()->getSelectedLayer(i, &inputProcChannels, &isAll, &layer);
            ImageComponents maskComp;
            NodePtr maskInput;
            int channelMask = getNode()->getMaskChannel(i, &maskComp, &maskInput);
            std::vector<ImageComponents> clipPrefsAllComps;
            {
                ImageComponents clipPrefsComps = getComponents(i);
                if ( clipPrefsComps.isPairedComponents() ) {
                    ImageComponents first, second;
                    clipPrefsComps.getPlanesPair(&first, &second);
                    clipPrefsAllComps.push_back(first);
                    clipPrefsAllComps.push_back(second);
                } else {
                    clipPrefsAllComps.push_back(clipPrefsComps);
                }
            }

            if ( (channelMask != -1) && (maskComp.getNumComponents() > 0) ) {
                std::vector<ImageComponents> compVec;
                compVec.push_back(maskComp);
                comps->insert( std::make_pair(i, compVec) );
            } else if (ok && !isAll) {
                if ( !layer.isColorPlane() ) {
                    compVec.push_back(layer);
                } else {
                    //Use regular clip preferences
                    compVec.insert( compVec.end(), clipPrefsAllComps.begin(), clipPrefsAllComps.end() );
                }
            } else {
                //Use regular clip preferences
                compVec.insert( compVec.end(), clipPrefsAllComps.begin(), clipPrefsAllComps.end() );
            }
            comps->insert( std::make_pair(i, compVec) );
        }
    }
} // EffectInstance::getComponentsNeededAndProduced_public

bool
EffectInstance::getCreateChannelSelectorKnob() const
{
    return ( !isMultiPlanar() && !isReader() && !isWriter() && !isTrackerNodePlugin() &&
             !boost::starts_with(getPluginID(), "uk.co.thefoundry.furnace") );
}

int
EffectInstance::getMaskChannel(int inputNb,
                               ImageComponents* comps,
                               NodePtr* maskInput) const
{
    return getNode()->getMaskChannel(inputNb, comps, maskInput);
}

bool
EffectInstance::isMaskEnabled(int inputNb) const
{
    return getNode()->isMaskEnabled(inputNb);
}

bool
EffectInstance::onKnobValueChanged(const KnobIPtr& /*k*/,
                                   ValueChangedReasonEnum /*reason*/,
                                   double /*time*/,
                                   ViewSpec /*view*/,
                                   bool /*originatedFromMainThread*/)
{
    return false;
}

bool
EffectInstance::getThreadLocalRenderedPlanes(std::map<ImageComponents, EffectInstance::PlaneToRender> *outputPlanes,
                                             ImageComponents* planeBeingRendered,
                                             RectI* renderWindow) const
{
    EffectDataTLSPtr tls = _imp->tlsData->getTLSData();

    if (tls && tls->currentRenderArgs.validArgs) {
        assert( !tls->currentRenderArgs.outputPlanes.empty() );
        *planeBeingRendered = tls->currentRenderArgs.outputPlaneBeingRendered;
        *outputPlanes = tls->currentRenderArgs.outputPlanes;
        *renderWindow = tls->currentRenderArgs.renderWindowPixel;

        return true;
    }

    return false;
}

bool
EffectInstance::getThreadLocalNeededComponents(ComponentsNeededMapPtr* neededComps) const
{
    EffectDataTLSPtr tls = _imp->tlsData->getTLSData();

    if (tls && tls->currentRenderArgs.validArgs) {
        assert( !tls->currentRenderArgs.outputPlanes.empty() );
        *neededComps = tls->currentRenderArgs.compsNeeded;

        return true;
    }

    return false;
}

void
EffectInstance::updateThreadLocalRenderTime(double time)
{
    if ( QThread::currentThread() != qApp->thread() ) {
        EffectDataTLSPtr tls = _imp->tlsData->getTLSData();
        if (tls && tls->currentRenderArgs.validArgs) {
            tls->currentRenderArgs.time = time;
        }
    }
}

bool
EffectInstance::isDuringPaintStrokeCreationThreadLocal() const
{
    EffectDataTLSPtr tls = _imp->tlsData->getTLSData();

    if ( tls && !tls->frameArgs.empty() ) {
        return tls->frameArgs.back()->isDuringPaintStrokeCreation;
    }

    return getNode()->isDuringPaintStrokeCreation();
}

void
EffectInstance::redrawOverlayInteract()
{
    if ( isDoingInteractAction() ) {
        getApp()->queueRedrawForAllViewers();
    } else {
        getApp()->redrawAllViewers();
    }
}

RenderScale
EffectInstance::getOverlayInteractRenderScale() const
{
    RenderScale renderScale(1.);

    if (isDoingInteractAction() && _imp->overlaysViewport) {
        unsigned int mmLevel = _imp->overlaysViewport->getCurrentRenderScale();
        renderScale.x = renderScale.y = 1 << mmLevel;
    }

    return renderScale;
}

void
EffectInstance::pushUndoCommand(UndoCommand* command)
{
    UndoCommandPtr ptr(command);

    getNode()->pushUndoCommand(ptr);
}

void
EffectInstance::pushUndoCommand(const UndoCommandPtr& command)
{
    getNode()->pushUndoCommand(command);
}

bool
EffectInstance::setCurrentCursor(CursorEnum defaultCursor)
{
    if ( !isDoingInteractAction() ) {
        return false;
    }
    getNode()->setCurrentCursor(defaultCursor);

    return true;
}

bool
EffectInstance::setCurrentCursor(const QString& customCursorFilePath)
{
    if ( !isDoingInteractAction() ) {
        return false;
    }

    return getNode()->setCurrentCursor(customCursorFilePath);
}

void
EffectInstance::addOverlaySlaveParam(const KnobIPtr& knob)
{
    _imp->overlaySlaves.push_back(knob);
}

bool
EffectInstance::isOverlaySlaveParam(const KnobIConstPtr& knob) const
{
    for (std::list<KnobIWPtr >::const_iterator it = _imp->overlaySlaves.begin(); it != _imp->overlaySlaves.end(); ++it) {
        KnobIPtr k = it->lock();
        if (!k) {
            continue;
        }
        if (k == knob) {
            return true;
        }
    }

    return false;
}

bool
EffectInstance::onKnobValueChanged_public(const KnobIPtr& k,
                                          ValueChangedReasonEnum reason,
                                          double time,
                                          ViewSpec view,
                                          bool originatedFromMainThread)
{
    NodePtr node = getNode();

    ///If the param changed is a button and the node is disabled don't do anything which might
    ///trigger an analysis
    if ( (reason == eValueChangedReasonUserEdited) && toKnobButton(k) && node->isNodeDisabled() ) {
        return false;
    }

    if ( (reason != eValueChangedReasonTimeChanged) && ( isReader() || isWriter() ) && (k->getName() == kOfxImageEffectFileParamName) ) {
        node->onFileNameParameterChanged(k);
    }

    bool ret = false;

    // assert(!(view.isAll() || view.isCurrent())); // not yet implemented
    const ViewIdx viewIdx( ( view.isAll() || view.isCurrent() ) ? 0 : view );
    bool wasFormatKnobCaught = node->handleFormatKnob(k);
    KnobHelperPtr kh = boost::dynamic_pointer_cast<KnobHelper>(k);
    assert(kh);
    if (kh && kh->isDeclaredByPlugin() && !wasFormatKnobCaught) {
        ////We set the thread storage render args so that if the instance changed action
        ////tries to call getImage it can render with good parameters.
        boost::shared_ptr<ParallelRenderArgsSetter> setter;
        if (reason != eValueChangedReasonTimeChanged) {
            AbortableRenderInfoPtr abortInfo = AbortableRenderInfo::create(false, 0);
            const bool isRenderUserInteraction = true;
            const bool isSequentialRender = false;
            AbortableThread* isAbortable = dynamic_cast<AbortableThread*>( QThread::currentThread() );
            if (isAbortable) {
                isAbortable->setAbortInfo( isRenderUserInteraction, abortInfo, node->getEffectInstance() );
            }


            ParallelRenderArgsSetter::CtorArgsPtr tlsArgs(new ParallelRenderArgsSetter::CtorArgs);
            tlsArgs->time = time;
            tlsArgs->view = viewIdx;
            tlsArgs->isRenderUserInteraction = isRenderUserInteraction;
            tlsArgs->isSequential = isSequentialRender;
            tlsArgs->abortInfo = abortInfo;
            tlsArgs->treeRoot = node;
            tlsArgs->textureIndex = 0;
            tlsArgs->timeline = getApp()->getTimeLine();
            tlsArgs->activeRotoPaintNode = NodePtr();
            tlsArgs->activeRotoDrawableItem = RotoDrawableItemPtr();
            tlsArgs->isDoingRotoNeatRender = false;
            tlsArgs->isAnalysis = true;
            tlsArgs->draftMode = false;
            tlsArgs->stats = RenderStatsPtr();
            setter.reset( new ParallelRenderArgsSetter(tlsArgs) );
        }
        {
            RECURSIVE_ACTION();
            REPORT_CURRENT_THREAD_ACTION( "kOfxActionInstanceChanged", getNode() );
            // Map to a plug-in known reason
            if (reason == eValueChangedReasonNatronGuiEdited) {
                reason = eValueChangedReasonUserEdited;
            } 
            ret |= knobChanged(k, reason, view, time, originatedFromMainThread);
        }
    }

    if ( kh && ( QThread::currentThread() == qApp->thread() ) &&
         originatedFromMainThread && ( reason != eValueChangedReasonTimeChanged) ) {
        ///Run the following only in the main-thread
        if ( hasOverlay() && node->shouldDrawOverlay() && !node->hasHostOverlayForParam(k) ) {
            // Some plugins (e.g. by digital film tools) forget to set kOfxInteractPropSlaveToParam.
            // Most hosts trigger a redraw if the plugin has an active overlay.
            incrementRedrawNeededCounter();

            if ( !isDequeueingValuesSet() && (getRecursionLevel() == 0) && checkIfOverlayRedrawNeeded() ) {
                redrawOverlayInteract();
            }
        }
        if (isOverlaySlaveParam(kh)) {
            kh->redraw();
        }
    }

    ret |= node->onEffectKnobValueChanged(k, reason);

    //Don't call the python callback if the reason is time changed
    if (reason == eValueChangedReasonTimeChanged) {
        return false;
    }

    ///If there's a knobChanged Python callback, run it
    std::string pythonCB = getNode()->getKnobChangedCallback();

    if ( !pythonCB.empty() ) {
        bool userEdited = reason == eValueChangedReasonNatronGuiEdited ||
                          reason == eValueChangedReasonUserEdited;
        _imp->runChangedParamCallback(k, userEdited, pythonCB);
    }

    ///Refresh the dynamic properties that can be changed during the instanceChanged action
    node->refreshDynamicProperties();

    ///Clear input images pointers that were stored in getImage() for the main-thread.
    ///This is safe to do so because if this is called while in render() it won't clear the input images
    ///pointers for the render thread. This is helpful for analysis effects which call getImage() on the main-thread
    ///and whose render() function is never called.
    _imp->clearInputImagePointers();

    // If there are any render clones, kill them as the plug-in might have changed internally
    clearRenderInstances();

    return ret;
} // onKnobValueChanged_public

void
EffectInstance::clearLastRenderedImage()
{
}

void
EffectInstance::aboutToRestoreDefaultValues()
{
    ///Invalidate the cache by incrementing the age
    NodePtr node = getNode();

    node->incrementKnobsAge();

    if ( node->areKeyframesVisibleOnTimeline() ) {
        node->hideKeyframesFromTimeline(true);
    }
}

/**
 * @brief Returns a pointer to the first non disabled upstream node.
 * When cycling through the tree, we prefer non optional inputs and we span inputs
 * from last to first.
 **/
EffectInstancePtr
EffectInstance::getNearestNonDisabled() const
{
    NodePtr node = getNode();

    if ( !node->isNodeDisabled() ) {
        return node->getEffectInstance();
    } else {
        ///Test all inputs recursively, going from last to first, preferring non optional inputs.
        std::list<EffectInstancePtr> nonOptionalInputs;
        std::list<EffectInstancePtr> optionalInputs;
        bool useInputA = appPTR->getCurrentSettings()->isMergeAutoConnectingToAInput();

        ///Find an input named A
        std::string inputNameToFind, otherName;
        if (useInputA) {
            inputNameToFind = "A";
            otherName = "B";
        } else {
            inputNameToFind = "B";
            otherName = "A";
        }
        int foundOther = -1;
        int maxinputs = getMaxInputCount();
        for (int i = 0; i < maxinputs; ++i) {
            std::string inputLabel = getInputLabel(i);
            if (inputLabel == inputNameToFind) {
                EffectInstancePtr inp = getInput(i);
                if (inp) {
                    nonOptionalInputs.push_front(inp);
                    break;
                }
            } else if (inputLabel == otherName) {
                foundOther = i;
            }
        }

        if ( (foundOther != -1) && nonOptionalInputs.empty() ) {
            EffectInstancePtr inp = getInput(foundOther);
            if (inp) {
                nonOptionalInputs.push_front(inp);
            }
        }

        ///If we found A or B so far, cycle through them
        for (std::list<EffectInstancePtr> ::iterator it = nonOptionalInputs.begin(); it != nonOptionalInputs.end(); ++it) {
            EffectInstancePtr inputRet = (*it)->getNearestNonDisabled();
            if (inputRet) {
                return inputRet;
            }
        }


        ///We cycle in reverse by default. It should be a setting of the application.
        ///In this case it will return input B instead of input A of a merge for example.
        for (int i = 0; i < maxinputs; ++i) {
            EffectInstancePtr inp = getInput(i);
            bool optional = isInputOptional(i);
            if (inp) {
                if (optional) {
                    optionalInputs.push_back(inp);
                } else {
                    nonOptionalInputs.push_back(inp);
                }
            }
        }

        ///Cycle through all non optional inputs first
        for (std::list<EffectInstancePtr> ::iterator it = nonOptionalInputs.begin(); it != nonOptionalInputs.end(); ++it) {
            EffectInstancePtr inputRet = (*it)->getNearestNonDisabled();
            if (inputRet) {
                return inputRet;
            }
        }

        ///Cycle through optional inputs...
        for (std::list<EffectInstancePtr> ::iterator it = optionalInputs.begin(); it != optionalInputs.end(); ++it) {
            EffectInstancePtr inputRet = (*it)->getNearestNonDisabled();
            if (inputRet) {
                return inputRet;
            }
        }

        ///We didn't find anything upstream, return
        return node->getEffectInstance();
    }
} // EffectInstance::getNearestNonDisabled

EffectInstancePtr
EffectInstance::getNearestNonDisabledPrevious(int* inputNb)
{
    assert( getNode()->isNodeDisabled() );

    ///Test all inputs recursively, going from last to first, preferring non optional inputs.
    std::list<EffectInstancePtr> nonOptionalInputs;
    std::list<EffectInstancePtr> optionalInputs;
    int localPreferredInput = -1;
    bool useInputA = appPTR->getCurrentSettings()->isMergeAutoConnectingToAInput();
    ///Find an input named A
    std::string inputNameToFind, otherName;
    if (useInputA) {
        inputNameToFind = "A";
        otherName = "B";
    } else {
        inputNameToFind = "B";
        otherName = "A";
    }
    int foundOther = -1;
    int maxinputs = getMaxInputCount();
    for (int i = 0; i < maxinputs; ++i) {
        std::string inputLabel = getInputLabel(i);
        if (inputLabel == inputNameToFind) {
            EffectInstancePtr inp = getInput(i);
            if (inp) {
                nonOptionalInputs.push_front(inp);
                localPreferredInput = i;
                break;
            }
        } else if (inputLabel == otherName) {
            foundOther = i;
        }
    }

    if ( (foundOther != -1) && nonOptionalInputs.empty() ) {
        EffectInstancePtr inp = getInput(foundOther);
        if (inp) {
            nonOptionalInputs.push_front(inp);
            localPreferredInput = foundOther;
        }
    }

    ///If we found A or B so far, cycle through them
    for (std::list<EffectInstancePtr> ::iterator it = nonOptionalInputs.begin(); it != nonOptionalInputs.end(); ++it) {
        if ( (*it)->getNode()->isNodeDisabled() ) {
            EffectInstancePtr inputRet = (*it)->getNearestNonDisabledPrevious(inputNb);
            if (inputRet) {
                return inputRet;
            }
        }
    }


    ///We cycle in reverse by default. It should be a setting of the application.
    ///In this case it will return input B instead of input A of a merge for example.
    for (int i = 0; i < maxinputs; ++i) {
        EffectInstancePtr inp = getInput(i);
        bool optional = isInputOptional(i);
        if (inp) {
            if (optional) {
                if (localPreferredInput == -1) {
                    localPreferredInput = i;
                }
                optionalInputs.push_back(inp);
            } else {
                if (localPreferredInput == -1) {
                    localPreferredInput = i;
                }
                nonOptionalInputs.push_back(inp);
            }
        }
    }


    ///Cycle through all non optional inputs first
    for (std::list<EffectInstancePtr> ::iterator it = nonOptionalInputs.begin(); it != nonOptionalInputs.end(); ++it) {
        if ( (*it)->getNode()->isNodeDisabled() ) {
            EffectInstancePtr inputRet = (*it)->getNearestNonDisabledPrevious(inputNb);
            if (inputRet) {
                return inputRet;
            }
        }
    }

    ///Cycle through optional inputs...
    for (std::list<EffectInstancePtr> ::iterator it = optionalInputs.begin(); it != optionalInputs.end(); ++it) {
        if ( (*it)->getNode()->isNodeDisabled() ) {
            EffectInstancePtr inputRet = (*it)->getNearestNonDisabledPrevious(inputNb);
            if (inputRet) {
                return inputRet;
            }
        }
    }

    *inputNb = localPreferredInput;

    return shared_from_this();
} // EffectInstance::getNearestNonDisabledPrevious

EffectInstancePtr
EffectInstance::getNearestNonIdentity(double time)
{
    U64 hash = getRenderHash();
    RenderScale scale(1.);
    Format frmt;

    getApp()->getProject()->getProjectDefaultFormat(&frmt);

    double inputTimeIdentity;
    int inputNbIdentity;
    ViewIdx inputView;
    if ( !isIdentity_public(true, hash, time, scale, frmt, ViewIdx(0), &inputTimeIdentity, &inputView, &inputNbIdentity) ) {
        return shared_from_this();
    } else {
        if (inputNbIdentity < 0) {
            return shared_from_this();
        }
        EffectInstancePtr effect = getInput(inputNbIdentity);

        return effect ? effect->getNearestNonIdentity(time) : shared_from_this();
    }
}

void
EffectInstance::onNodeHashChanged(U64 hash)
{
    ///Invalidate actions cache
    _imp->actionsCache->invalidateAll(hash);

    const KnobsVec & knobs = getKnobs();
    for (KnobsVec::const_iterator it = knobs.begin(); it != knobs.end(); ++it) {
        for (int i = 0; i < (*it)->getDimension(); ++i) {
            (*it)->clearExpressionsResults(i);
        }
    }
}

bool
EffectInstance::canSetValue() const
{
    return !getNode()->isNodeRendering() || appPTR->isBackground();
}

void
EffectInstance::abortAnyEvaluation(bool keepOldestRender)
{
    /*
       Get recursively downstream all Output nodes and abort any render on them
       If an output node such as a viewer was doing playback, enable it to restart
       automatically playback when the abort finished
     */
    NodePtr node = getNode();

    assert(node);
    std::list<OutputEffectInstancePtr> outputNodes;
    NodeGroup* isGroup = dynamic_cast<NodeGroup*>(this);
    if (isGroup) {
        NodesList inputOutputs;
        isGroup->getInputsOutputs(&inputOutputs, false);
        for (NodesList::iterator it = inputOutputs.begin(); it != inputOutputs.end(); ++it) {
            (*it)->hasOutputNodesConnected(&outputNodes);
        }
    } else {
        RotoDrawableItemPtr attachedStroke = getNode()->getAttachedRotoItem();
        if (attachedStroke) {
            ///For nodes internal to the rotopaint tree, check outputs of the rotopaint node instead
            RotoContextPtr context = attachedStroke->getContext();
            assert(context);
            if (context) {
                NodePtr rotonode = context->getNode();
                if (rotonode) {
                    rotonode->hasOutputNodesConnected(&outputNodes);
                }
            }
        } else {
            node->hasOutputNodesConnected(&outputNodes);
        }
    }
    for (std::list<OutputEffectInstancePtr>::const_iterator it = outputNodes.begin(); it != outputNodes.end(); ++it) {
        //Abort and allow playback to restart but do not block, when this function returns any ongoing render may very
        //well not be finished
        if (keepOldestRender) {
            (*it)->getRenderEngine()->abortRenderingAutoRestart();
        } else {
            (*it)->getRenderEngine()->abortRenderingNoRestart(keepOldestRender);
        }
    }
}

double
EffectInstance::getCurrentTime() const
{
    EffectDataTLSPtr tls = _imp->tlsData->getTLSData();
    AppInstancePtr app = getApp();
    if (!app) {
        return 0.;
    }
    if (!tls) {
        return app->getTimeLine()->currentFrame();
    }
    if (tls->currentRenderArgs.validArgs) {
        return tls->currentRenderArgs.time;
    }


    if ( !tls->frameArgs.empty() ) {
        return tls->frameArgs.back()->time;
    }

    return app->getTimeLine()->currentFrame();
}

ViewIdx
EffectInstance::getCurrentView() const
{
    EffectDataTLSPtr tls = _imp->tlsData->getTLSData();

    if (!tls) {
        return ViewIdx(0);
    }
    if (tls->currentRenderArgs.validArgs) {
        return tls->currentRenderArgs.view;
    }
    if ( !tls->frameArgs.empty() ) {
        return tls->frameArgs.back()->view;
    }

    return ViewIdx(0);
}

SequenceTime
EffectInstance::getFrameRenderArgsCurrentTime() const
{
    EffectDataTLSPtr tls = _imp->tlsData->getTLSData();

    if ( !tls || tls->frameArgs.empty() ) {
        return getApp()->getTimeLine()->currentFrame();
    }

    return tls->frameArgs.back()->time;
}

ViewIdx
EffectInstance::getFrameRenderArgsCurrentView() const
{
    EffectDataTLSPtr tls = _imp->tlsData->getTLSData();

    if ( !tls || tls->frameArgs.empty() ) {
        return ViewIdx(0);
    }

    return tls->frameArgs.back()->view;
}

#ifdef DEBUG
void
EffectInstance::checkCanSetValueAndWarn() const
{
    if ( !checkCanSetValue() ) {
        qDebug() << getScriptName_mt_safe().c_str() << ": setValue()/setValueAtTime() was called during an action that is not allowed to call this function.";
    }
}

#endif

static
void
isFrameVaryingOrAnimated_impl(const EffectInstanceConstPtr& node,
                              bool *ret)
{
    if ( node->isFrameVarying() || node->getHasAnimation() || node->getNode()->getRotoContext() ) {
        *ret = true;
    } else {
        int maxInputs = node->getMaxInputCount();
        for (int i = 0; i < maxInputs; ++i) {
            EffectInstanceConstPtr input = node->getInput(i);
            if (input) {
                isFrameVaryingOrAnimated_impl(input, ret);
                if (*ret) {
                    return;
                }
            }
        }
    }
}

bool
EffectInstance::isFrameVaryingOrAnimated_Recursive() const
{
    bool ret = false;

    isFrameVaryingOrAnimated_impl(shared_from_this(), &ret);

    return ret;
}

bool
EffectInstance::isPaintingOverItselfEnabled() const
{
    return isDuringPaintStrokeCreationThreadLocal();
}

StatusEnum
EffectInstance::getPreferredMetaDatas_public(NodeMetadata& metadata)
{
    StatusEnum stat = getDefaultMetadata(metadata);

    if (stat == eStatusFailed) {
        return stat;
    }

    return getPreferredMetaDatas(metadata);
}

static ImageComponents
getUnmappedComponentsForInput(const EffectInstancePtr& self,
                              int inputNb,
                              const std::vector<EffectInstancePtr>& inputs,
                              const ImageComponents& firstNonOptionalConnectedInputComps)
{
    ImageComponents rawComps;

    if (inputs[inputNb]) {
        rawComps = inputs[inputNb]->getComponents(-1);
    } else {
        ///The node is not connected but optional, return the closest supported components
        ///of the first connected non optional input.
        rawComps = firstNonOptionalConnectedInputComps;
    }
    if (rawComps) {
        if (!rawComps) {
            //None comps
            return rawComps;
        } else {
            rawComps = self->findClosestSupportedComponents(inputNb, rawComps); //turn that into a comp the plugin expects on that clip
        }
    }
    if (!rawComps) {
        rawComps = ImageComponents::getRGBAComponents(); // default to RGBA
    }

    return rawComps;
}

StatusEnum
EffectInstance::getDefaultMetadata(NodeMetadata &metadata)
{
    NodePtr node = getNode();

    if (!node) {
        return eStatusFailed;
    }

    const bool multiBitDepth = supportsMultipleClipDepths();
    int nInputs = getMaxInputCount();
    metadata.clearAndResize(nInputs);

    // OK find the deepest chromatic component on our input clips and the one with the
    // most components
    bool hasSetCompsAndDepth = false;
    ImageBitDepthEnum deepestBitDepth = eImageBitDepthNone;
    ImageComponents mostComponents;

    //Default to the project frame rate
    double frameRate = getApp()->getProjectFrameRate();
    std::vector<EffectInstancePtr> inputs(nInputs);

    // Find the components of the first non optional connected input
    // They will be used for disconnected input
    ImageComponents firstNonOptionalConnectedInputComps;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        inputs[i] = getInput(i);
        if ( !firstNonOptionalConnectedInputComps && inputs[i] && !isInputOptional(i) ) {
            firstNonOptionalConnectedInputComps = inputs[i]->getComponents(-1);
        }
    }

    double inputPar = 1.;
    bool inputParSet = false;
    ImagePremultiplicationEnum premult = eImagePremultiplicationOpaque;
    bool premultSet = false;
    for (int i = 0; i < nInputs; ++i) {
        const EffectInstancePtr& input = inputs[i];
        if (input) {
            frameRate = std::max( frameRate, input->getFrameRate() );
        }


        if (input) {
            if (!inputParSet) {
                inputPar = input->getAspectRatio(-1);
                inputParSet = true;
            }
        }

        ImageComponents rawComp = getUnmappedComponentsForInput(shared_from_this(), i, inputs, firstNonOptionalConnectedInputComps);
        ImageBitDepthEnum rawDepth = input ? input->getBitDepth(-1) : eImageBitDepthFloat;
        ImagePremultiplicationEnum rawPreMult = input ? input->getPremult() : eImagePremultiplicationPremultiplied;

        if ( rawComp.isColorPlane() ) {
            // Note: first chromatic input gives the default output premult too, even if not connected
            // (else the output of generators may be opaque even if the host default is premultiplied)
            if ( ( rawComp == ImageComponents::getRGBAComponents() ) && (input || !premultSet) ) {
                if (rawPreMult == eImagePremultiplicationPremultiplied) {
                    premult = eImagePremultiplicationPremultiplied;
                    premultSet = true;
                } else if ( (rawPreMult == eImagePremultiplicationUnPremultiplied) && ( !premultSet || (premult != eImagePremultiplicationPremultiplied) ) ) {
                    premult = eImagePremultiplicationUnPremultiplied;
                    premultSet = true;
                }
            }

            if (input) {
                //Update deepest bitdepth and most components only if the infos are relevant, i.e: only if the clip is connected
                hasSetCompsAndDepth = true;
                if ( getSizeOfForBitDepth(deepestBitDepth) < getSizeOfForBitDepth(rawDepth) ) {
                    deepestBitDepth = rawDepth;
                }

                if ( rawComp.getNumComponents() > mostComponents.getNumComponents() ) {
                    mostComponents = rawComp;
                }
            }
        }
    } // for each input


    if (!hasSetCompsAndDepth) {
        mostComponents = ImageComponents::getRGBAComponents();
        deepestBitDepth = eImageBitDepthFloat;
    }

    // set some stuff up
    metadata.setOutputFrameRate(frameRate);
    metadata.setOutputFielding(eImageFieldingOrderNone);
    metadata.setIsFrameVarying( node->hasAnimatedKnob() );
    metadata.setIsContinuous(false);

    // now find the best depth that the plugin supports
    deepestBitDepth = node->getClosestSupportedBitDepth(deepestBitDepth);

    bool multipleClipsPAR = supportsMultipleClipPARs();
    double projectPAR;
    {
        Format f;
        getRenderFormat(&f);
        projectPAR =  f.getPixelAspectRatio();
    }


    // now add the input gubbins to the per inputs metadatas
    for (int i = -1; i < (int)inputs.size(); ++i) {
        EffectInstancePtr effect;
        if (i >= 0) {
            effect = inputs[i];
        } else {
            effect = shared_from_this();
        }

        double par;
        if (!multipleClipsPAR) {
            par = inputParSet ? inputPar : projectPAR;
        } else {
            if (inputParSet) {
                par = inputPar;
            } else {
                par = effect ? effect->getAspectRatio(-1) : projectPAR;
            }
        }
        metadata.setPixelAspectRatio(i, par);

        if ( (i == -1) || isInputOptional(i) ) {
            // "Optional input clips can always have their component types remapped"
            // http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#id482755
            ImageBitDepthEnum depth = deepestBitDepth;
            ImageComponents remappedComps;
            if ( !mostComponents.isColorPlane() ) {
                // hmm custom component type, don't touch it and pass it through
                metadata.setImageComponents(i, mostComponents);
            } else {
                remappedComps = mostComponents;
                remappedComps = findClosestSupportedComponents(i, remappedComps);
                metadata.setImageComponents(i, remappedComps);
                if ( (i == -1) && !premultSet &&
                     ( ( remappedComps == ImageComponents::getRGBAComponents() ) || ( remappedComps == ImageComponents::getAlphaComponents() ) ) ) {
                    premult = eImagePremultiplicationPremultiplied;
                    premultSet = true;
                }
            }

            metadata.setBitDepth(i, depth);
        } else {
            ImageComponents rawComps = getUnmappedComponentsForInput(shared_from_this(), i, inputs, firstNonOptionalConnectedInputComps);
            ImageBitDepthEnum rawDepth = effect ? effect->getBitDepth(-1) : eImageBitDepthFloat;

            if ( rawComps.isColorPlane() ) {
                ImageBitDepthEnum depth = multiBitDepth ? node->getClosestSupportedBitDepth(rawDepth) : deepestBitDepth;
                metadata.setBitDepth(i, depth);
            } else {
                metadata.setBitDepth(i, rawDepth);
            }
            metadata.setImageComponents(i, rawComps);
        }
    }
    // default to a reasonable value if there is no input
    if (!premultSet) {
        premult = eImagePremultiplicationOpaque;
    }
    // set output premultiplication
    metadata.setOutputPremult(premult);

    return eStatusOK;
} // EffectInstance::getDefaultMetadata

ImageComponents
EffectInstance::getComponents(int inputNb) const
{
    QMutexLocker k(&_imp->metadatasMutex);

    return _imp->metadatas.getImageComponents(inputNb);
}

ImageBitDepthEnum
EffectInstance::getBitDepth(int inputNb) const
{
    QMutexLocker k(&_imp->metadatasMutex);

    return _imp->metadatas.getBitDepth(inputNb);
}

double
EffectInstance::getFrameRate() const
{
    QMutexLocker k(&_imp->metadatasMutex);

    return _imp->metadatas.getOutputFrameRate();
}

double
EffectInstance::getAspectRatio(int inputNb) const
{
    QMutexLocker k(&_imp->metadatasMutex);

    return _imp->metadatas.getPixelAspectRatio(inputNb);
}

ImagePremultiplicationEnum
EffectInstance::getPremult() const
{
    QMutexLocker k(&_imp->metadatasMutex);

    return _imp->metadatas.getOutputPremult();
}

bool
EffectInstance::isFrameVarying() const
{
    QMutexLocker k(&_imp->metadatasMutex);

    return _imp->metadatas.getIsFrameVarying();
}

bool
EffectInstance::canRenderContinuously() const
{
    QMutexLocker k(&_imp->metadatasMutex);

    return _imp->metadatas.getIsContinuous();
}

/**
 * @brief Returns the field ordering of images produced by this plug-in
 **/
ImageFieldingOrderEnum
EffectInstance::getFieldingOrder() const
{
    QMutexLocker k(&_imp->metadatasMutex);

    return _imp->metadatas.getOutputFielding();
}

bool
EffectInstance::refreshMetaDatas_recursive(std::list<Node*> & markedNodes)
{
    NodePtr node = getNode();
    std::list<Node*>::iterator found = std::find( markedNodes.begin(), markedNodes.end(), node.get() );

    if ( found != markedNodes.end() ) {
        return false;
    }

    if (_imp->runningClipPreferences) {
        return false;
    }

    ClipPreferencesRunning_RAII runningflag_( shared_from_this() );
    bool ret = refreshMetaDatas_public(false);
    node->refreshIdentityState();

    if ( !node->duringInputChangedAction() ) {
        ///The channels selector refreshing is already taken care of in the inputChanged action
        node->refreshChannelSelectors();
    }

    markedNodes.push_back( node.get() );

    NodesList outputs;
    node->getOutputsWithGroupRedirection(outputs);
    for (NodesList::const_iterator it = outputs.begin(); it != outputs.end(); ++it) {
        (*it)->getEffectInstance()->refreshMetaDatas_recursive(markedNodes);
    }

    return ret;
}

static void
setComponentsDirty_recursive(const Node* node,
                             std::list<const Node*> & markedNodes)
{
    std::list<const Node*>::iterator found = std::find( markedNodes.begin(), markedNodes.end(), node );

    if ( found != markedNodes.end() ) {
        return;
    }

    markedNodes.push_back(node);

    node->getEffectInstance()->setComponentsAvailableDirty(true);


    NodesList outputs;
    node->getOutputsWithGroupRedirection(outputs);
    for (NodesList::const_iterator it = outputs.begin(); it != outputs.end(); ++it) {
        setComponentsDirty_recursive(it->get(), markedNodes);
    }
}

void
EffectInstance::setDefaultMetadata()
{
    NodeMetadata metadata;
    StatusEnum stat = getDefaultMetadata(metadata);

    if (stat == eStatusFailed) {
        return;
    }
    {
        QMutexLocker k(&_imp->metadatasMutex);
        _imp->metadatas = metadata;
    }
    onMetaDatasRefreshed(metadata);
}

bool
EffectInstance::refreshMetaDatas_internal()
{
    NodeMetadata metadata;

    getPreferredMetaDatas_public(metadata);
    _imp->checkMetadata(metadata);

    bool ret;
    {
        QMutexLocker k(&_imp->metadatasMutex);
        ret = metadata != _imp->metadatas;
        if (ret) {
            _imp->metadatas = metadata;
        }
    }
    onMetaDatasRefreshed(metadata);
    if (ret) {
        getNode()->checkForPremultWarningAndCheckboxes();
    }

    return ret;
}

bool
EffectInstance::refreshMetaDatas_public(bool recurse)
{
    assert( QThread::currentThread() == qApp->thread() );

    if (recurse) {
        {
            std::list<const Node*> markedNodes;
            setComponentsDirty_recursive(_node.lock().get(), markedNodes);
        }
        {
            std::list<Node*> markedNodes;

            return refreshMetaDatas_recursive(markedNodes);
        }
    } else {
        bool ret = refreshMetaDatas_internal();
        if (ret) {
            NodePtr node = getNode();
            NodesList children;
            node->getChildrenMultiInstance(&children);
            if ( !children.empty() ) {
                for (NodesList::iterator it = children.begin(); it != children.end(); ++it) {
                    (*it)->getEffectInstance()->refreshMetaDatas_internal();
                }
            }
        }

        return ret;
    }
}

/**
 * @brief The purpose of this function is to check that the meta data returned by the plug-ins are valid and to
 * check for warnings
 **/
void
EffectInstance::Implementation::checkMetadata(NodeMetadata &md)
{
    NodePtr node = _publicInterface->getNode();

    if (!node) {
        return;
    }
    //Make sure it is valid
    int nInputs = node->getMaxInputCount();

    for (int i = -1; i < nInputs; ++i) {
        md.setBitDepth( i, node->getClosestSupportedBitDepth( md.getBitDepth(i) ) );
        ImageComponents comps = md.getImageComponents(i);
        bool isAlpha = false;
        bool isRGB = false;
        if (i == -1) {
            if ( comps == ImageComponents::getRGBComponents() ) {
                isRGB = true;
            } else if ( comps == ImageComponents::getAlphaComponents() ) {
                isAlpha = true;
            }
        }
        if ( comps.isColorPlane() ) {
            comps = node->findClosestSupportedComponents(i, comps);
        }
        md.setImageComponents(i, comps);
        if (i == -1) {
            //Force opaque for RGB and premult for alpha
            if (isRGB) {
                md.setOutputPremult(eImagePremultiplicationOpaque);
            } else if (isAlpha) {
                md.setOutputPremult(eImagePremultiplicationPremultiplied);
            }
        }
    }


    ///Set a warning on the node if the bitdepth conversion from one of the input clip to the output clip is lossy
    QString bitDepthWarning = tr("This nodes converts higher bit depths images from its inputs to work. As "
                                 "a result of this process, the quality of the images is degraded. The following conversions are done:");
    bitDepthWarning.append( QChar::fromLatin1('\n') );
    bool setBitDepthWarning = false;
    const bool supportsMultipleClipDepths = _publicInterface->supportsMultipleClipDepths();
    const bool supportsMultipleClipPARs = _publicInterface->supportsMultipleClipPARs();
    const bool supportsMultipleClipFPSs = _publicInterface->supportsMultipleClipFPSs();
    std::vector<EffectInstancePtr> inputs(nInputs);
    for (int i = 0; i < nInputs; ++i) {
        inputs[i] = _publicInterface->getInput(i);
    }


    ImageBitDepthEnum outputDepth = md.getBitDepth(-1);
    double outputPAR = md.getPixelAspectRatio(-1);
    double inputPar = 1.;
    bool inputParSet = false;
    bool mustWarnPar = false;
    bool outputFrameRateSet = false;
    double outputFrameRate = md.getOutputFrameRate();
    bool mustWarnFPS = false;

    for (int i = 0; i < nInputs; ++i) {
        //Check that the bitdepths are all the same if the plug-in doesn't support multiple depths
        if ( !supportsMultipleClipDepths && (md.getBitDepth(i) != outputDepth) ) {
            md.setBitDepth(i, outputDepth);
        }

        if (!inputs[i]) {
            continue;
        }

        const double pixelAspect = md.getPixelAspectRatio(i);
        const double fps = inputs[i]->getFrameRate();

        if (!supportsMultipleClipPARs) {
            if (!inputParSet) {
                inputPar = pixelAspect;
                inputParSet = true;
            } else if (inputPar != pixelAspect) {
                // We have several inputs with different aspect ratio, which should be forbidden by the host.
                mustWarnPar = true;
            }
        }

        if (!supportsMultipleClipFPSs) {
            if (!outputFrameRateSet) {
                outputFrameRate = fps;
                outputFrameRateSet = true;
            } else if (std::abs(outputFrameRate - fps) > 0.01) {
                // We have several inputs with different frame rates
                mustWarnFPS = true;
            }
        }


        ImageBitDepthEnum inputOutputDepth = inputs[i]->getBitDepth(-1);

        //If the bit-depth conversion will be lossy, warn the user
        if ( Image::isBitDepthConversionLossy( inputOutputDepth, md.getBitDepth(i) ) ) {
            bitDepthWarning.append( QString::fromUtf8( inputs[i]->getNode()->getLabel_mt_safe().c_str() ) );
            bitDepthWarning.append( QString::fromUtf8(" (") + QString::fromUtf8( Image::getDepthString(inputOutputDepth).c_str() ) + QChar::fromLatin1(')') );
            bitDepthWarning.append( QString::fromUtf8(" ----> ") );
            bitDepthWarning.append( QString::fromUtf8( node->getLabel_mt_safe().c_str() ) );
            bitDepthWarning.append( QString::fromUtf8(" (") + QString::fromUtf8( Image::getDepthString( md.getBitDepth(i) ).c_str() ) + QChar::fromLatin1(')') );
            bitDepthWarning.append( QChar::fromLatin1('\n') );
            setBitDepthWarning = true;
        }


        if ( !supportsMultipleClipPARs && (pixelAspect != outputPAR) ) {
            qDebug() << node->getScriptName_mt_safe().c_str() << ": The input " << inputs[i]->getNode()->getScriptName_mt_safe().c_str()
                     << ") has a pixel aspect ratio (" << md.getPixelAspectRatio(i)
                     << ") different than the output clip (" << outputPAR << ") but it doesn't support multiple clips PAR. "
                     << "This should have been handled earlier before connecting the nodes, @see Node::canConnectInput.";
        }
    }

    std::map<Node::StreamWarningEnum, QString> warnings;
    if (setBitDepthWarning) {
        warnings[Node::eStreamWarningBitdepth] = bitDepthWarning;
    } else {
        warnings[Node::eStreamWarningBitdepth] = QString();
    }

    if (mustWarnFPS) {
        QString fpsWarning = tr("Several input with different frame rates "
                                "is not handled correctly by this node. To remove this warning make sure all inputs have "
                                "the same frame-rate, either by adjusting project settings or the upstream Read node.");
        warnings[Node::eStreamWarningFrameRate] = fpsWarning;
    } else {
        warnings[Node::eStreamWarningFrameRate] = QString();
    }

    if (mustWarnPar) {
        QString parWarnings = tr("Several input with different pixel aspect ratio is not "
                                 "handled correctly by this node and may yield unwanted results. Please adjust the "
                                 "pixel aspect ratios of the inputs so that they match by using a Reformat node.");
        warnings[Node::eStreamWarningPixelAspectRatio] = parWarnings;
    } else {
        warnings[Node::eStreamWarningPixelAspectRatio] = QString();
    }


    node->setStreamWarnings(warnings);
} //refreshMetaDataProxy

void
EffectInstance::refreshExtraStateAfterTimeChanged(bool isPlayback,
                                                  double time)
{
    KnobHolder::refreshExtraStateAfterTimeChanged(isPlayback, time);

    getNode()->refreshIdentityState();
}

void
EffectInstance::assertActionIsNotRecursive() const
{
# ifdef DEBUG
    ///Only check recursions which are on a render threads, because we do authorize recursions in getRegionOfDefinition and such
    if ( QThread::currentThread() != qApp->thread() ) {
        int recursionLvl = getRecursionLevel();
        if ( getApp() && getApp()->isShowingDialog() ) {
            return;
        }
        if (recursionLvl != 0) {
            qDebug() << "A non-recursive action has been called recursively.";
        }
    }
# endif // DEBUG
}

void
EffectInstance::incrementRecursionLevel()
{
    EffectDataTLSPtr tls = _imp->tlsData->getOrCreateTLSData();

    assert(tls);
    ++tls->actionRecursionLevel;
}

void
EffectInstance::decrementRecursionLevel()
{
    EffectDataTLSPtr tls = _imp->tlsData->getTLSData();

    assert(tls);
    --tls->actionRecursionLevel;
}

int
EffectInstance::getRecursionLevel() const
{
    EffectDataTLSPtr tls = _imp->tlsData->getTLSData();

    if (!tls) {
        return 0;
    }

    return tls->actionRecursionLevel;
}

void
EffectInstance::setClipPreferencesRunning(bool running)
{
    assert( QThread::currentThread() == qApp->thread() );
    _imp->runningClipPreferences = running;
}

NATRON_NAMESPACE_EXIT;

NATRON_NAMESPACE_USING;
#include "moc_EffectInstance.cpp"

