//  Natron
//
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/*
 * Created by Alexandre GAUTHIER-FOICHAT on 6/1/2012.
 * contact: immarespond at gmail dot com
 *
 */

#ifndef NATRON_ENGINE_EFFECTINSTANCE_H_
#define NATRON_ENGINE_EFFECTINSTANCE_H_
#include <list>
#if !defined(Q_MOC_RUN) && !defined(SBK_RUN)
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#endif
#include "Global/GlobalDefines.h"
#include "Global/KeySymbols.h"
#include "Engine/Knob.h" // for KnobHolder
#include "Engine/Rect.h"
#include "Engine/ImageLocker.h"

class Hash64;
class Format;
class OverlaySupport;
class PluginMemory;
class BlockingBackgroundRender;
class RenderEngine;
class BufferableObject;
namespace Transform {
struct Matrix3x3;
}

/**
 * @brief Thread-local arguments given to render a frame by the tree.
 * This is different than the RenderArgs because it is not local to a
 * renderRoI call but to the rendering of a whole frame.
 **/
struct ParallelRenderArgs
{
    ///The initial time requested to render.
    ///This may be different than the time held in RenderArgs
    ///which are local to a renderRoI call whilst this is local
    ///to a frame being rendered by the tree.
    int time;
    
    ///The initial view requested to render.
    ///This may be different than the view held in RenderArgs
    ///which are local to a renderRoI call whilst this is local
    ///to a frame being rendered by the tree.
    int view;
    
    ///The node hash as it was when starting the rendering of the frame
    U64 nodeHash;
    
    ///The age of the roto as it was when starting the rendering of the frame
    U64 rotoAge;
    
    ///> 0 if the args were set for the current thread
    int validArgs;
    
    /// is this a render due to user interaction ? Generally this is true when rendering because
    /// of a user parameter tweek or timeline seek, or more generally by calling RenderEngine::renderCurrentFrame
    bool isRenderResponseToUserInteraction;
    
    /// Is this render sequential ? True for Viewer playback or a sequential writer such as WriteFFMPEG
    bool isSequentialRender;
    
    /// True if this frame can be aborted (false for preview and tracking)
    bool canAbort;
    
    ParallelRenderArgs()
    : validArgs(false)
    {
        
    }
};


namespace Natron {
class Node;
class ImageKey;
class Image;
class ImageParams;
/**
 * @brief This is the base class for visual effects.
 * A live instance is always living throughout the lifetime of a Node and other copies are
 * created on demand when a render is needed.
 **/
class EffectInstance
    : public NamedKnobHolder
    , public LockManagerI<Natron::Image>
{
public:

    typedef std::map<EffectInstance*,RectD> RoIMap; // RoIs are in canonical coordinates
    typedef std::map<int, std::vector<RangeD> > FramesNeededMap;

    struct RenderRoIArgs
    {
        SequenceTime time; //< the time at which to render
        RenderScale scale; //< the scale at which to render
        unsigned int mipMapLevel; //< the mipmap level (redundant with the scale, stored here to avoid refetching it everytimes)
        int view; //< the view to render
        bool byPassCache;
        RectI roi; //< the renderWindow (in pixel coordinates) , watch out OpenFX action getRegionsOfInterest expects canonical coords!
        RectD preComputedRoD; //<  pre-computed region of definition in canonical coordinates for this effect to speed-up the call to renderRoi
        Natron::ImageComponentsEnum components; //< the requested image components
        Natron::ImageBitDepthEnum bitdepth; //< the requested bit depth
        int channelForAlpha; //< if this is a mask this is from this channel that we will fetch the mask
        bool calledFromGetImage;
        
        RenderRoIArgs()
        {
        }

        RenderRoIArgs(SequenceTime time_,
                      const RenderScale & scale_,
                      unsigned int mipMapLevel_,
                      int view_,
                      bool byPassCache_,
                      const RectI & roi_,
                      const RectD & preComputedRoD_,
                      Natron::ImageComponentsEnum components_,
                      Natron::ImageBitDepthEnum bitdepth_,
                      int channelForAlpha_ = 3,
                      bool calledFromGetImage = false)
            : time(time_)
              , scale(scale_)
              , mipMapLevel(mipMapLevel_)
              , view(view_)
              , byPassCache(byPassCache_)
              , roi(roi_)
              , preComputedRoD(preComputedRoD_)
              , components(components_)
              , bitdepth(bitdepth_)
              , channelForAlpha(channelForAlpha_)
              , calledFromGetImage(calledFromGetImage)
        {
        }
    };

    enum SupportsEnum
    {
        eSupportsMaybe = -1,
        eSupportsNo = 0,
        eSupportsYes = 1
    };

public:


    /**
     * @brief Constructor used once for each node created. Its purpose is to create the "live instance".
     * You shouldn't do any heavy processing here nor lengthy initialization as the constructor is often
     * called just to be able to call a few virtuals fonctions.
     * The constructor is always called by the main thread of the application.
     **/
    explicit EffectInstance(boost::shared_ptr<Natron::Node> node);

    virtual ~EffectInstance();

    /**
     * @brief Returns true once the effect has been fully initialized and is ready to have its actions called apart from
     * the createInstanceAction
     **/
    virtual bool isEffectCreated() const { return true; }
   
    /**
     * @brief Returns a pointer to the node holding this effect.
     **/
    boost::shared_ptr<Natron::Node> getNode() const WARN_UNUSED_RETURN
    {
        return _node;
    }

    /**
     * @brief Returns the "real" hash of the node synchronized with the gui state
     **/
    U64 getHash() const WARN_UNUSED_RETURN;

    /**
     * @brief Returns the hash the node had at the start of renderRoI. This will return the same value
     * at any time during the same render call.
     * @returns This function returns true if case of success, false otherwise.
     **/
    U64 getRenderHash() const WARN_UNUSED_RETURN;

    U64 getKnobsAge() const WARN_UNUSED_RETURN;

    /**
     * @brief Set the knobs age of this node to be 'age'. Note that this can be called
     * for 2 reasons:
     * - loading a project
     * - If this node is a clone and the master node changed its hash.
     **/
    void setKnobsAge(U64 age);

    /**
     * @brief Forwarded to the node's name
     **/
    const std::string & getName() const WARN_UNUSED_RETURN;
    virtual std::string getName_mt_safe() const OVERRIDE FINAL WARN_UNUSED_RETURN;

    /**
     * @brief Forwarded to the node's render format
     **/
    void getRenderFormat(Format *f) const;

    /**
     * @brief Forwarded to the node's render views count
     **/
    int getRenderViewsCount() const WARN_UNUSED_RETURN;

    /**
     * @brief Returns input n. It might be NULL if the input is not connected.
     * MT-Safe
     **/
    EffectInstance* getInput(int n) const WARN_UNUSED_RETURN;

    /**
     * @brief Forwarded to the node holding the effect
     **/
    bool hasOutputConnected() const WARN_UNUSED_RETURN;

    /**
     * @brief Must return the plugin's major version.
     **/
    virtual int getMajorVersion() const WARN_UNUSED_RETURN = 0;

    /**
     * @brief Must return the plugin's minor version.
     **/
    virtual int getMinorVersion() const WARN_UNUSED_RETURN = 0;

    /**
     * @brief Is this node an input node ? An input node means
     * it has no input.
     **/
    virtual bool isGenerator() const WARN_UNUSED_RETURN
    {
        return false;
    }

    /**
     * @brief Is the node a reader ?
     **/
    virtual bool isReader() const WARN_UNUSED_RETURN
    {
        return false;
    }

    /**
     * @brief Is the node a writer ?
     **/
    virtual bool isWriter() const WARN_UNUSED_RETURN
    {
        return false;
    }

    /**
     * @brief Is this node an output node ? An output node means
     * it has no output.
     **/
    virtual bool isOutput() const WARN_UNUSED_RETURN
    {
        return false;
    }

    /**
     * @brief Returns true if the node is capable of generating
     * data and process data on the input as well
     **/
    virtual bool isGeneratorAndFilter() const WARN_UNUSED_RETURN
    {
        return false;
    }

    /**
     * @brief Is this node an OpenFX node?
     **/
    virtual bool isOpenFX() const WARN_UNUSED_RETURN
    {
        return false;
    }

    /**
     * @brief How many input can we have at most. (i.e: how many input arrows)
     * This function should be MT-safe and should never change the value returned.
     **/
    virtual int getMaxInputCount() const WARN_UNUSED_RETURN = 0;

    /**
     * @brief Is inputNb optional ? In which case the render can be made without it.
     **/
    virtual bool isInputOptional(int inputNb) const WARN_UNUSED_RETURN = 0;

    /**
     * @brief Is inputNb a mask ? In which case the effect will have an additionnal mask parameter.
     **/
    virtual bool isInputMask(int /*inputNb*/) const WARN_UNUSED_RETURN
    {
        return false;
    };

    /**
     * @brief Is the input a roto brush ?
     **/
    virtual bool isInputRotoBrush(int /*inputNb*/) const WARN_UNUSED_RETURN
    {
        return false;
    }

    virtual int getRotoBrushInputIndex() const WARN_UNUSED_RETURN
    {
        return -1;
    }
    
    /**
     * @brief Returns the index of the channel to use to produce the mask.
     * None = -1
     * R = 0
     * G = 1
     * B = 2
     * A = 3
     **/
    int getMaskChannel(int inputNb) const;

    /**
     * @brief Returns whether masking is enabled or not
     **/
    bool isMaskEnabled(int inputNb) const;

    /**
     * @brief Routine called after the creation of an effect. This function must
     * fill for the given input what image components we can feed it with.
     * This function is also called to specify what image components this effect can output.
     * In that case inputNb equals -1.
     **/
    virtual void addAcceptedComponents(int inputNb,std::list<Natron::ImageComponentsEnum>* comps) = 0;
    virtual void addSupportedBitDepth(std::list<Natron::ImageBitDepthEnum>* depths) const = 0;

    /**
     * @brief Must return the deepest bit depth that this plug-in can support.
     * If 32 float is supported then return Natron::eImageBitDepthFloat, otherwise
     * return eImageBitDepthShort if 16 bits is supported, and as a last resort, return
     * eImageBitDepthByte. At least one must be returned.
     **/
    Natron::ImageBitDepthEnum getBitDepth() const;

    bool isSupportedBitDepth(Natron::ImageBitDepthEnum depth) const;

    /**
     * @brief Returns true if the given input supports the given components. If inputNb equals -1
     * then this function will check whether the effect can produce the given components.
     **/
    bool isSupportedComponent(int inputNb,Natron::ImageComponentsEnum comp) const;

    /**
     * @brief Returns the most appropriate components that can be supported by the inputNb.
     * If inputNb equals -1 then this function will check the output components.
     **/
    Natron::ImageComponentsEnum findClosestSupportedComponents(int inputNb,Natron::ImageComponentsEnum comp) const WARN_UNUSED_RETURN;

    /**
     * @brief Returns the preferred depth and components for the given input.
     * If inputNb equals -1 then this function will check the output components.
     **/
    virtual void getPreferredDepthAndComponents(int inputNb,Natron::ImageComponentsEnum* comp,Natron::ImageBitDepthEnum* depth) const;

    /**
     * @brief Override to get the preffered premultiplication flag for the output image
     **/
    virtual Natron::ImagePremultiplicationEnum getOutputPremultiplication() const
    {
        return Natron::eImagePremultiplicationPremultiplied;
    }

    /**
     * @brief Can be derived to give a more meaningful label to the input 'inputNb'
     **/
    virtual std::string getInputLabel(int inputNb) const WARN_UNUSED_RETURN;

    /**
     * @brief Must be implemented to give the plugin internal id(i.e: net.sf.openfx:invertPlugin)
     **/
    virtual std::string getPluginID() const WARN_UNUSED_RETURN = 0;

    /**
     * @brief Must be implemented to give the plugin a label that will be used by the graphical
     * user interface.
     **/
    virtual std::string getPluginLabel() const WARN_UNUSED_RETURN = 0;

    /**
     * @brief The grouping of the plug-in. For instance Views/Stereo/MyStuff
     * Each string being one level of the grouping. The first one being the name
     * of one group that will appear in the user interface.
     **/
    virtual void getPluginGrouping(std::list<std::string>* grouping) const = 0;

    /**
     * @brief Must be implemented to give a desription of the effect that this node does. This is typically
     * what you'll see displayed when the user clicks the '?' button on the node's panel in the user interface.
     **/
    virtual std::string getDescription() const WARN_UNUSED_RETURN = 0;


    /**
     * @bried Returns the effect render order preferences:
     * eSequentialPreferenceNotSequential: The effect does not need to be run in a sequential order
     * eSequentialPreferenceOnlySequential: The effect can only be run in a sequential order (i.e like the background render would do)
     * eSequentialPreferencePreferSequential: This indicates that the effect would work better by rendering sequential. This is merely
     * a hint to Natron but for now we just consider it as eSequentialPreferenceNotSequential.
     **/
    virtual Natron::SequentialPreferenceEnum getSequentialPreference() const
    {
        return Natron::eSequentialPreferenceNotSequential;
    }

    /**
     * @brief Renders the image at the given time,scale and for the given view & render window.
     * @param args See the definition of the class for comments on each argument.
     **/
    boost::shared_ptr<Image> renderRoI(const RenderRoIArgs & args) WARN_UNUSED_RETURN;


    void getImageFromCacheAndConvertIfNeeded(const Natron::ImageKey& key,
                                             unsigned int mipMapLevel,
                                             Natron::ImageBitDepthEnum bitdepth,
                                             Natron::ImageComponentsEnum components,
                                             int channelForAlpha,
                                             const RectD& rod,
                                             boost::shared_ptr<Natron::Image>* image);


    class NotifyRenderingStarted_RAII
    {
        Node* _node;
        bool _didEmit;
    public:
    
        NotifyRenderingStarted_RAII(Node* node);
    
        ~NotifyRenderingStarted_RAII();
    };

    class NotifyInputNRenderingStarted_RAII
    {
        Node* _node;
        bool _didEmit;
        int _inputNumber;
    public:
        
        NotifyInputNRenderingStarted_RAII(Node* node,int inputNumber);
        
        ~NotifyInputNRenderingStarted_RAII();
    };


    /**
    * @brief Sets render preferences for the rendering of a frame for the current thread.
    * This is thread local storage. This is NOT local to a call to renderRoI
    **/
    void setParallelRenderArgs(int time,
                               int view,
                               bool isRenderUserInteraction,
                               bool isSequential,
                               bool canAbort,
                               U64 nodeHash,
                               U64 rotoAge);

    void invalidateParallelRenderArgs();

    /**
     * @breif Don't override this one, override onKnobValueChanged instead.
     **/
    virtual void onKnobValueChanged_public(KnobI* k,Natron::ValueChangedReasonEnum reason,SequenceTime time) OVERRIDE FINAL;

    /**
     * @brief Returns a pointer to the first non disabled upstream node.
     * When cycling through the tree, we prefer non optional inputs and we span inputs
     * from last to first.
     * If this not is not disabled, it will return a pointer to this.
     **/
    Natron::EffectInstance* getNearestNonDisabled() const;

    /**
     * @brief Same as getNearestNonDisabled() except that it returns the *last* disabled node before the nearest non disabled node.
     * @param inputNb[out] The inputNb of the node that is non disabled.
     **/
    Natron::EffectInstance* getNearestNonDisabledPrevious(int* inputNb);
    
    /**
     * @brief Same as getNearestNonDisabled except that it looks for the nearest non identity node.
     * This function calls the action isIdentity and getRegionOfDefinition and can be expensive!
     **/
    Natron::EffectInstance* getNearestNonIdentity(int time);

    /**
     * @brief This is purely for the OfxEffectInstance derived class, but passed here for the sake of abstraction
     **/
    virtual void checkOFXClipPreferences(double /*time*/,
                                         const RenderScale & /*scale*/,
                                         const std::string & /*reason*/,
                                        bool /*forceGetClipPrefAction*/) {}
    
    /**
     * @brief Returns the output aspect ratio to render with
     **/
    virtual double getPreferredAspectRatio() const { return 1.; }

    virtual void lock(const boost::shared_ptr<Natron::Image>& entry) OVERRIDE FINAL;
    virtual void unlock(const boost::shared_ptr<Natron::Image>& entry) OVERRIDE FINAL ;

    virtual bool canSetValue() const OVERRIDE FINAL WARN_UNUSED_RETURN;

    virtual SequenceTime getCurrentTime() const OVERRIDE FINAL WARN_UNUSED_RETURN;

    virtual bool getCanTransform() const { return false; }

    virtual bool getCanApplyTransform(Natron::EffectInstance** /*effect*/) const { return false; }

    virtual void rerouteInputAndSetTransform(int /*inputNb*/,Natron::EffectInstance* /*newInput*/,
                                             int /*newInputNb*/,const Transform::Matrix3x3& /*m*/) {}

    virtual void clearTransform(int /*inputNb*/) {}

    bool getThreadLocalRegionsOfInterests(EffectInstance::RoIMap& roiMap) const;


    void addThreadLocalInputImageTempPointer(const boost::shared_ptr<Natron::Image> & img);

    /**
     * @brief Returns whether the effect is frame-varying (i.e: a Reader with different images in the sequence)
     **/
    virtual bool isFrameVarying() const { return false; }

    /**
     * @brief Returns whether the current node and/or the tree upstream is frame varying or animated.
     * It is frame varying/animated if at least one of the node is animated/varying
     **/
    bool isFrameVaryingOrAnimated_Recursive() const;

    
protected:
    /**
     * @brief Must fill the image 'output' for the region of interest 'roi' at the given time and
     * at the given scale.
     * Pre-condition: render() has been called for all inputs so the portion of the image contained
     * in output corresponding to the roi is valid.
     * Note that this function can be called concurrently for the same output image but with different
     * rois, depending on the threading-affinity of the plug-in.
     **/
    virtual Natron::StatusEnum render(SequenceTime /*time*/,
                                      const RenderScale & /*originalScale*/,
                                      const RenderScale & /*mappedScale*/,
                                      const RectI & /*roi*/,
                                      int /*view*/,
                                      bool /*isSequentialRender*/,
                                      bool /*isRenderResponseToUserInteraction*/,
                                      boost::shared_ptr<Natron::Image> /*output*/) WARN_UNUSED_RETURN
    {
        return Natron::eStatusOK;
    }

    virtual Natron::StatusEnum getTransform(SequenceTime /*time*/,
                                            const RenderScale& /*renderScale*/,
                                            int /*view*/,
                                            Natron::EffectInstance** /*inputToTransform*/,
                                            Transform::Matrix3x3* /*transform*/) WARN_UNUSED_RETURN
    {
        return Natron::eStatusReplyDefault;
    }



public:

    Natron::StatusEnum render_public(SequenceTime time,
                                 const RenderScale& originalScale,
                                 const RenderScale & mappedScale,
                                 const RectI & roi,
                                 int view,
                                 bool isSequentialRender,
                                 bool isRenderResponseToUserInteraction,
                                 boost::shared_ptr<Natron::Image> output) WARN_UNUSED_RETURN;

    Natron::StatusEnum getTransform_public(SequenceTime time,
                                           const RenderScale& renderScale,
                                           int view,
                                           Natron::EffectInstance** inputToTransform,
                                           Transform::Matrix3x3* transform) WARN_UNUSED_RETURN;

protected:
/**
     * @brief Can be overloaded to indicates whether the effect is an identity, i.e it doesn't produce
     * any change in output.
     * @param time The time of interest
     * @param scale The scale of interest
     * @param rod The image region of definition, in canonical coordinates
     * @param view The view we 're interested in
     * @param inputTime[out] the input time to which this plugin is identity of
     * @param inputNb[out] the input number of the effect that is identity of.
     * The special value of -2 indicates that the plugin is identity of itself at another time
     **/
    virtual bool isIdentity(SequenceTime /*time*/,
                            const RenderScale & /*scale*/,
                            const RectD & /*rod*/,
                            const double /*par*/,
                            int /*view*/,
                            SequenceTime* /*inputTime*/,
                            int* /*inputNb*/) WARN_UNUSED_RETURN
    {
        return false;
    }

public:

    bool isIdentity_public(U64 hash,
                           SequenceTime time,
                           const RenderScale & scale,
                           const RectD & rod, //!< image rod in canonical coordinates
                           const double par,
                           int view,SequenceTime* inputTime,
                           int* inputNb) WARN_UNUSED_RETURN;
    enum RenderSafetyEnum
    {
        eRenderSafetyUnsafe = 0,
        eRenderSafetyInstanceSafe = 1,
        eRenderSafetyFullySafe = 2,
        eRenderSafetyFullySafeFrame = 3,
    };

    /**
     * @brief Indicates how many simultaneous renders the plugin can deal with.
     * RenderSafetyEnum::eRenderSafetyUnsafe - indicating that only a single 'render' call can be made at any time amoung all instances,
     * RenderSafetyEnum::eRenderSafetyInstanceSafe - indicating that any instance can have a single 'render' call at any one time,
     * RenderSafetyEnum::eRenderSafetyFullySafe - indicating that any instance of a plugin can have multiple renders running simultaneously
     * RenderSafetyEnum::eRenderSafetyFullySafeFrame - Same as eRenderSafetyFullySafe but the plug-in also flagged  kOfxImageEffectPluginPropHostFrameThreading to true.
     **/
    virtual RenderSafetyEnum renderThreadSafety() const WARN_UNUSED_RETURN = 0;

    /**
     * @brief Can be derived to indicate that the data rendered by the plug-in is expensive
     * and should be stored in a persistent manner such as on disk.
     **/
    virtual bool shouldRenderedDataBePersistent() const WARN_UNUSED_RETURN
    {
        return false;
    }

    /*@brief The derived class should query this to abort any long process
       in the engine function.*/
    bool aborted() const WARN_UNUSED_RETURN;

    /**
     * @brief Used internally by aborted()
     **/
    bool isAbortedFromPlayback() const WARN_UNUSED_RETURN;

    /**
     * @brief Called externally when the rendering is aborted. You should never
     * call this yourself.
     **/
    void setAborted(bool b);

    /** @brief Returns the image computed by the input 'inputNb' at the given time and scale for the given view.
     * @param dontUpscale If the image is retrieved is downscaled but the plug-in doesn't support the user of
     * downscaled images by default we upscale the image. If dontUpscale is true then we don't do this upscaling.
     *
     * @param roiPixel If non NULL will be set to the render window used to render the image, that is, either the
     * region of interest of this effect on the input effect we want to render or the optionalBounds if set, but
     * converted to pixel coordinates
     */
    boost::shared_ptr<Image> getImage(int inputNb,
                                      const SequenceTime time,
                                      const RenderScale & scale,
                                      const int view,
                                      const RectD *optionalBounds, //!< optional region in canonical coordinates
                                      const Natron::ImageComponentsEnum comp,
                                      const Natron::ImageBitDepthEnum depth,
                                      const double par,
                                      const bool dontUpscale,
                                      RectI* roiPixel) WARN_UNUSED_RETURN;



    virtual void aboutToRestoreDefaultValues() OVERRIDE FINAL;

protected:


    /**
     * @brief Can be derived to get the region that the plugin is capable of filling.
     * This is meaningful for plugins that generate images or transform images.
     * By default it returns in rod the union of all inputs RoD and eStatusReplyDefault is returned.
     * @param isProjectFormat[out] If set to true, then rod is taken to be equal to the current project format.
     * In case of failure the plugin should return eStatusFailed.
     * @returns eStatusOK, eStatusReplyDefault, or eStatusFailed. rod is set except if return value is eStatusOK or eStatusReplyDefault.
     **/
    virtual Natron::StatusEnum getRegionOfDefinition(U64 hash,SequenceTime time, const RenderScale & scale, int view, RectD* rod) WARN_UNUSED_RETURN;
    virtual void calcDefaultRegionOfDefinition(U64 hash,SequenceTime time,int view, const RenderScale & scale, RectD *rod) ;

    /**
     * @brief If the instance rod is infinite, returns the union of all connected inputs. If there's no input this returns the
     * project format.
     * @returns true if the rod is set to the project format.
     **/
    bool ifInfiniteApplyHeuristic(U64 hash,
                                  SequenceTime time,
                                  const RenderScale & scale,
                                  int view,
                                  RectD* rod); //!< input/output

    /**
     * @brief Can be derived to indicate for each input node what is the region of interest
     * of the node at time 'time' and render scale 'scale' given a render window.
     * For exemple a blur plugin would specify what it needs
     * from inputs in order to do a blur taking into account the size of the blurring kernel.
     * By default, it returns renderWindow for each input.
     **/
    virtual void getRegionsOfInterest(SequenceTime time,
                                        const RenderScale & scale,
                                        const RectD & outputRoD, //!< the RoD of the effect, in canonical coordinates
                                        const RectD & renderWindow, //!< the region to be rendered in the output image, in Canonical Coordinates
                                        int view,
                                        EffectInstance::RoIMap* ret);

    /**
     * @brief Can be derived to indicate for each input node what is the frame range(s) (which can be discontinuous)
     * that this effects need in order to render the frame at the given time.
     **/
    virtual FramesNeededMap getFramesNeeded(SequenceTime time) WARN_UNUSED_RETURN;


    /**
     * @brief Can be derived to get the frame range wherein the plugin is capable of producing frames.
     * By default it merges the frame range of the inputs.
     * In case of failure the plugin should return eStatusFailed.
     **/
    virtual void getFrameRange(SequenceTime *first,SequenceTime *last);

public:

    Natron::StatusEnum getRegionOfDefinition_public(U64 hash,
                                                SequenceTime time,
                                                const RenderScale & scale,
                                                int view,
                                                RectD* rod,
                                                bool* isProjectFormat) WARN_UNUSED_RETURN;

    void getRegionsOfInterest_public(SequenceTime time,
                                       const RenderScale & scale,
                                       const RectD & outputRoD,
                                       const RectD & renderWindow, //!< the region to be rendered in the output image, in Canonical Coordinates
                                       int view,
                                      RoIMap* ret);

    FramesNeededMap getFramesNeeded_public(SequenceTime time) WARN_UNUSED_RETURN;

    void getFrameRange_public(U64 hash,SequenceTime *first,SequenceTime *last);

    /**
     * @brief Override to initialize the overlay interact. It is called only on the
     * live instance.
     **/
    virtual void initializeOverlayInteract()
    {
    }

    virtual void setCurrentViewportForOverlays(OverlaySupport* /*viewport*/)
    {
    }

    /**
     * @brief Overload this and return true if your operator should dislay a preview image by default.
     **/
    virtual bool makePreviewByDefault() const WARN_UNUSED_RETURN
    {
        return false;
    }

    /**
     * @brief Called on generator effects upon creation if they have an image input file field.
     **/
    void openImageFileKnob();


    /**
     * @brief Used to bracket a series of call to onKnobValueChanged(...) in case many complex changes are done
     * at once. If not called, onKnobValueChanged() will call automatically bracket its call be a begin/end
     * but this can lead to worse performance. You can overload this to make all changes to params at once.
     **/
    virtual void beginKnobsValuesChanged(Natron::ValueChangedReasonEnum /*reason*/) OVERRIDE
    {
    }

    /**
     * @brief Used to bracket a series of call to onKnobValueChanged(...) in case many complex changes are done
     * at once. If not called, onKnobValueChanged() will call automatically bracket its call be a begin/end
     * but this can lead to worse performance. You can overload this to make all changes to params at once.
     **/
    virtual void endKnobsValuesChanged(Natron::ValueChangedReasonEnum /*reason*/) OVERRIDE
    {
    }

    /**
     * @brief Can be overloaded to clear any cache the plugin might be
     * handling on his side.
     **/
    virtual void purgeCaches()
    {
    };

    virtual void clearLastRenderedImage();

    /**
     * @brief Use this function to post a transient message to the user. It will be displayed using
     * a dialog. The message can be of 4 types...
     * INFORMATION_MESSAGE : you just want to inform the user about something.
     * eMessageTypeWarning : you want to inform the user that something important happened.
     * eMessageTypeError : you want to inform the user an error occured.
     * eMessageTypeQuestion : you want to ask the user about something.
     * The function will return true always except for a message of type eMessageTypeQuestion, in which
     * case the function may return false if the user pressed the 'No' button.
     * @param content The message you want to pass.
     **/
    bool message(Natron::MessageTypeEnum type,const std::string & content) const;

    /**
     * @brief Use this function to post a persistent message to the user. It will be displayed on the
     * node's graphical interface and on any connected viewer. The message can be of 3 types...
     * INFORMATION_MESSAGE : you just want to inform the user about something.
     * eMessageTypeWarning : you want to inform the user that something important happened.
     * eMessageTypeError : you want to inform the user an error occured.
     * @param content The message you want to pass.
     **/
    void setPersistentMessage(Natron::MessageTypeEnum type,const std::string & content);

    /**
     * @brief Clears any message posted previously by setPersistentMessage.
     **/
    void clearPersistentMessage();

    /**
     * @brief Does this effect supports tiling ?
     * http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#kOfxImageEffectPropSupportsTiles
     * If a clip or plugin does not support tiled images, then the host should supply
     * full RoD images to the effect whenever it fetches one.
     **/
    virtual bool supportsTiles() const
    {
        return false;
    }

    /**
     * @brief Does this effect supports multiresolution ?
     * http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#kOfxImageEffectPropSupportsMultiResolution
     * Multiple resolution images mean...
     * input and output images can be of any size
     * input and output images can be offset from the origin
     **/
    virtual bool supportsMultiResolution() const
    {
        return false;
    }

    /**
     * @brief Does this effect supports rendering at a different scale than 1 ?
     * There is no OFX property for this purpose. The only solution found for OFX is that if a render
     * or isIdentity with renderscale != 1 fails, the host retries with renderscale = 1 (and upscaled images).
     * If the renderScale support was not set, this throws an exception.
     **/
    bool supportsRenderScale() const;

    SupportsEnum supportsRenderScaleMaybe() const;

    /// should be set during effect initialization, but may also be set by the first getRegionOfDefinition with scale != 1 that succeeds
    void setSupportsRenderScaleMaybe(EffectInstance::SupportsEnum s) const;

    /**
     * @brief Does this effect can support multiple clips PAR ?
     * http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#kOfxImageEffectPropSupportsMultipleClipPARs
     * If a plugin does not accept clips of differing PARs, then the host must resample all images fed to that effect to agree with the output's PAR.
     * If a plugin does accept clips of differing PARs, it will need to specify the output clip's PAR in the kOfxImageEffectActionGetClipPreferences action.
     **/
    virtual bool supportsMultipleClipsPAR() const
    {
        return false;
    }

    /**
     * @brief If this effect is a writer then the file path corresponding to the output images path will be fed
     * with the content of pattern.
     **/
    void setOutputFilesForWriter(const std::string & pattern);

    /**
     * @brief Constructs a new memory holder, with nBytes allocated. If the allocation failed, bad_alloc is thrown
     **/
    PluginMemory* newMemoryInstance(size_t nBytes) WARN_UNUSED_RETURN;

    /// used to count the memory used by a plugin
    /// Don't call these, they're called by PluginMemory automatically
    void registerPluginMemory(size_t nBytes);
    void unregisterPluginMemory(size_t nBytes);

    void addPluginMemoryPointer(PluginMemory* mem);
    void removePluginMemoryPointer(PluginMemory* mem);

    void clearPluginMemoryChunks();

    /**
     * @brief Called right away when the user first opens the settings panel of the node.
     * This is called after each params had its default value set.
     **/
    virtual void beginEditKnobs()
    {
    }

    virtual std::vector<std::string> supportedFileFormats() const
    {
        return std::vector<std::string>();
    }

    /**
     * @brief Called everytimes an input connection is changed
     **/
    virtual void onInputChanged(int /*inputNo*/)
    {
    }

    /**
     * @brief Same as onInputChanged but called once for many changes.
     **/
    virtual void onMultipleInputsChanged()
    {
    }

    /**
     * @brief Returns the current frame this effect is rendering depending
     * on the state of the renderer. If it is not actively rendering this
     * node then returns the timeline current time, otherwise the ongoing render
     * current frame is returned. This function uses thread storage to determine
     * exactly what writer is actively calling this function.
     *
     * WARNING: This is MUCH MORE expensive than calling getApp()->getTimeLine()->currentFrame()
     * so use with caution when you know you're on a render thread and during an action.
     **/
    int getThreadLocalRenderTime() const;


    /**
     * @brief If the plug-in calls timelineGoTo and we're during a render/instance changed action,
     * then all the knobs will retrieve the current time as being the one in the last render args thread-storage.
     * This function is here to update the last render args thread storage.
     **/
    void updateThreadLocalRenderTime(int time);
    
    /**
     * @brief If the caller thread is currently rendering an image, it will return a pointer to it
     * otherwise it will return NULL.
     * This function also returns the current renderWindow that is being rendered on that image
     * To be called exclusively on a render thread.
     *
     * WARNING: This call isexpensive and this function should not be called many times.
     **/
    bool getThreadLocalRenderedImage(boost::shared_ptr<Natron::Image>* image,RectI* renderWindow) const;

    /**
     * @brief Called when the associated node's hash has changed.
     * This is always called on the main-thread.
     **/
    void onNodeHashChanged(U64 hash);

    virtual void initializeData() {}

#ifdef DEBUG
    void checkCanSetValueAndWarn() const;
#endif

protected:

#ifdef DEBUG
    virtual bool checkCanSetValue() const { return true; }
#endif
    /**
     * @brief Called whenever a param changes. It calls the virtual
     * portion paramChangedByUser(...) and brackets the call by a begin/end if it was
     * not done already.
     **/
    virtual void knobChanged(KnobI* /*k*/,
                             Natron::ValueChangedReasonEnum /*reason*/,
                             int /*view*/,
                             SequenceTime /*time*/)
    {
    }

    virtual Natron::StatusEnum beginSequenceRender(SequenceTime /*first*/,
                                               SequenceTime /*last*/,
                                               SequenceTime /*step*/,
                                               bool /*interactive*/,
                                               const RenderScale & /*scale*/,
                                               bool /*isSequentialRender*/,
                                               bool /*isRenderResponseToUserInteraction*/,
                                               int /*view*/)
    {
        return Natron::eStatusOK;
    }

    virtual Natron::StatusEnum endSequenceRender(SequenceTime /*first*/,
                                             SequenceTime /*last*/,
                                             SequenceTime /*step*/,
                                             bool /*interactive*/,
                                             const RenderScale & /*scale*/,
                                             bool /*isSequentialRender*/,
                                             bool /*isRenderResponseToUserInteraction*/,
                                             int /*view*/)
    {
        return Natron::eStatusOK;
    }

public:

    ///Doesn't do anything, instead we overriden onKnobValueChanged_public
    virtual void onKnobValueChanged(KnobI* k, Natron::ValueChangedReasonEnum reason,SequenceTime time) OVERRIDE FINAL;
    Natron::StatusEnum beginSequenceRender_public(SequenceTime first, SequenceTime last,
                                              SequenceTime step, bool interactive, const RenderScale & scale,
                                              bool isSequentialRender, bool isRenderResponseToUserInteraction,
                                              int view);
    Natron::StatusEnum endSequenceRender_public(SequenceTime first, SequenceTime last,
                                            SequenceTime step, bool interactive, const RenderScale & scale,
                                            bool isSequentialRender, bool isRenderResponseToUserInteraction,
                                            int view);


    void drawOverlay_public(double scaleX,double scaleY);

    bool onOverlayPenDown_public(double scaleX,double scaleY,const QPointF & viewportPos, const QPointF & pos) WARN_UNUSED_RETURN;

    bool onOverlayPenMotion_public(double scaleX,double scaleY,const QPointF & viewportPos, const QPointF & pos) WARN_UNUSED_RETURN;

    bool onOverlayPenUp_public(double scaleX,double scaleY,const QPointF & viewportPos, const QPointF & pos) WARN_UNUSED_RETURN;

    bool onOverlayKeyDown_public(double scaleX,double scaleY,Natron::Key key,Natron::KeyboardModifiers modifiers) WARN_UNUSED_RETURN;

    bool onOverlayKeyUp_public(double scaleX,double scaleY,Natron::Key key,Natron::KeyboardModifiers modifiers) WARN_UNUSED_RETURN;

    bool onOverlayKeyRepeat_public(double scaleX,double scaleY,Natron::Key key,Natron::KeyboardModifiers modifiers) WARN_UNUSED_RETURN;

    bool onOverlayFocusGained_public(double scaleX,double scaleY) WARN_UNUSED_RETURN;

    bool onOverlayFocusLost_public(double scaleX,double scaleY) WARN_UNUSED_RETURN;

    bool isDoingInteractAction() const WARN_UNUSED_RETURN;

protected:

    /**
     * @brief Must be implemented to initialize any knob using the
     * KnobFactory.
     **/
    virtual void initializeKnobs() OVERRIDE
    {
    };



    /**
     * @brief This function is provided for means to copy more data than just the knobs from the live instance
     * to the render clones.
     **/
    virtual void cloneExtras()
    {
    }

    /* @brief Overlay support:
     * Just overload this function in your operator.
     * No need to include any OpenGL related header.
     * The coordinate space is  defined by the displayWindow
     * (i.e: (0,0) = bottomLeft and  width() and height() being
     * respectivly the width and height of the frame.)
     */
    virtual bool hasOverlay() const
    {
        return false;
    }

    virtual void drawOverlay(double /*scaleX*/,
                             double /*scaleY*/)
    {
    }

    virtual bool onOverlayPenDown(double /*scaleX*/,
                                  double /*scaleY*/,
                                  const QPointF & /*viewportPos*/,
                                  const QPointF & /*pos*/) WARN_UNUSED_RETURN
    {
        return false;
    }

    virtual bool onOverlayPenMotion(double /*scaleX*/,
                                    double /*scaleY*/,
                                    const QPointF & /*viewportPos*/,
                                    const QPointF & /*pos*/) WARN_UNUSED_RETURN
    {
        return false;
    }

    virtual bool onOverlayPenUp(double /*scaleX*/,
                                double /*scaleY*/,
                                const QPointF & /*viewportPos*/,
                                const QPointF & /*pos*/) WARN_UNUSED_RETURN
    {
        return false;
    }

    virtual bool onOverlayKeyDown(double /*scaleX*/,
                                  double /*scaleY*/,
                                  Natron::Key /*key*/,
                                  Natron::KeyboardModifiers /*modifiers*/) WARN_UNUSED_RETURN
    {
        return false;
    }

    virtual bool onOverlayKeyUp(double /*scaleX*/,
                                double /*scaleY*/,
                                Natron::Key /*key*/,
                                Natron::KeyboardModifiers /*modifiers*/) WARN_UNUSED_RETURN
    {
        return false;
    }

    virtual bool onOverlayKeyRepeat(double /*scaleX*/,
                                    double /*scaleY*/,
                                    Natron::Key /*key*/,
                                    Natron::KeyboardModifiers /*modifiers*/) WARN_UNUSED_RETURN
    {
        return false;
    }

    virtual bool onOverlayFocusGained(double /*scaleX*/,
                                      double /*scaleY*/) WARN_UNUSED_RETURN
    {
        return false;
    }

    virtual bool onOverlayFocusLost(double /*scaleX*/,
                                    double /*scaleY*/) WARN_UNUSED_RETURN
    {
        return false;
    }
   
    
    boost::shared_ptr<Node> _node; //< the node holding this effect

private:

    struct Implementation;
    boost::scoped_ptr<Implementation> _imp; // PIMPL: hide implementation details
    struct RenderArgs;
    enum RenderRoIStatusEnum
    {
        eRenderRoIStatusImageAlreadyRendered = 0, // there was nothing left to render
        eRenderRoIStatusImageRendered, // we rendered what was missing
        eRenderRoIStatusRenderFailed // render failed
    };

    /**
     * @brief The internal of renderRoI, mainly it calls render and handles the thread safety of the effect.
     * @param time The time at which to render
     * @param scale The scale at which to render
     * @param mipMapLevel Redundant with scale
     * @param view The view on which to render
     * @param renderWindow The rectangle to render of the image, in pixel coordinates
     * @param cachedImgParams The parameters of the image to render as they are in the cache.
     * @param image This is the "full-scale" image, if the effect does support the render scale, then
     * image and downscaledImage are pointing to the SAME image.
     * @param downscaledImage If the effect doesn't support the render scale, then this is a pointer to the
     * downscaled image. If the effect doesn't support render scale then it will render in the "image" parameter
     * and then downscale the results into downscaledImage.
     * @param isSequentialRender True when the render is sequential
     * @param isRenderMadeInResponseToUserInteraction True when the render is made due to user interaction
     * @param byPassCache Cache look-ups have been already handled by renderRoI(...) but we pass it here because
     * we need to call renderRoI() on the input effects with this parameter too.
     * @param nodeHash The hash of the node used to render. This might no longer be equal to the value returned by
     * getHash() because the user might have changed something in the project (parameters...links..)
     * @param channelForAlpha This is passed here so that we can remember it later when converting the mask
     * which channel we wanted for the alpha channel.
     * @param renderFullScaleThenDownscale means that rendering should be done at full resolution and then
     * downscaled, because the plugin does not support render scale.
     * @returns True if the render call succeeded, false otherwise.
     **/
    RenderRoIStatusEnum renderRoIInternal(SequenceTime time,
                                          const RenderScale & scale,
                                          unsigned int mipMapLevel,
                                          int view,
                                          const RectI & renderWindow,
                                          const RectD & rod, //!< rod in canonical coordinates
                                          const double par,
                                          const FramesNeededMap &framesNeeded,
                                          const boost::shared_ptr<Image> & image,
                                          const boost::shared_ptr<Image> & downscaledImage,
                                          bool outputUseImage,
                                          bool isSequentialRender,
                                          bool isRenderMadeInResponseToUserInteraction,
                                          bool byPassCache,
                                          U64 nodeHash,
                                          int channelForAlpha,
                                          bool renderFullScaleThenDownscale,
                                          bool useScaleOneInputImages,
                                          const boost::shared_ptr<Transform::Matrix3x3>& transformMatrix,
                                          int transformInputNb,
                                          int newTransformedInputNb,
                                          Natron::EffectInstance* transformRerouteInput);

    /**
     * @brief Check if Transform effects concatenation is possible on the current node and node upstream.
     * @param inputTransformNb[out] if this node can concatenate, then it will be set to the input number concatenated
     * @param newInputEffect[out] will be set to the new input upstream replacing the original main input.
     * @param cat[out] the concatenation matrix of all transforms
     * @param isResultIdentity[out] if true then the result of all the transforms upstream plus the one of this node is an identity matrix
     * @return True if the nodes has concatenated nodes, false otherwise.
     **/
    bool tryConcatenateTransforms(const RenderRoIArgs& args,
                                  int* inputTransformNb,
                                  Natron::EffectInstance** newInputEffect,
                                  int *newInputNbToFetchFrom,
                                  boost::shared_ptr<Transform::Matrix3x3>* cat,
                                  bool* isResultIdentity);

    /**
     * @brief Called by getImage when the thread-storage was not set by the caller thread (mostly because this is a thread that is not
     * a thread controlled by Natron).
     **/
    // TODO: shouldn't this be documented a bit more? (parameters?)
    bool retrieveGetImageDataUponFailure(const int time,
                                         const int view,
                                         const RenderScale& scale,
                                         const RectD* optionalBoundsParam,
                                         U64* nodeHash_p,
                                         U64* rotoAge_p,
                                         bool* isIdentity_p,
                                         int* identityTime,
                                         int* identityInputNb_p,
                                         RectD* rod_p,
                                         RoIMap* inputRois_p, //!< output, only set if optionalBoundsParam != NULL
                                         RectD* optionalBounds_p); //!< output, only set if optionalBoundsParam != NULL

    /**
     * @brief Must be implemented to evaluate a value change
     * made to a knob(e.g: force a new render).
     * @param knob[in] The knob whose value changed.
     **/
    void evaluate(KnobI* knob,bool isSignificant,Natron::ValueChangedReasonEnum reason) OVERRIDE;


    virtual void onAllKnobsSlaved(bool isSlave,KnobHolder* master) OVERRIDE FINAL;
    virtual void onKnobSlaved(KnobI* slave,KnobI* master,
                              int dimension,
                              bool isSlave) OVERRIDE FINAL;


    struct TiledRenderingFunctorArgs
    {
        const RenderArgs* args;
        bool renderFullScaleThenDownscale;
        bool renderUseScaleOneInputs;
        bool isSequentialRender;
        bool isRenderResponseToUserInteraction;
        double par;
        boost::shared_ptr<Natron::Image>  downscaledImage;
        boost::shared_ptr<Natron::Image>  fullScaleImage;
        boost::shared_ptr<Natron::Image>  downscaledMappedImage;
        boost::shared_ptr<Natron::Image>  fullScaleMappedImage;
        boost::shared_ptr<Natron::Image>  renderMappedImage;
    };
    ///These are the image passed to the plug-in to render
    /// - fullscaleMappedImage is the fullscale image remapped to what the plugin can support (components/bitdepth)
    /// - downscaledMappedImage is the downscaled image remapped to what the plugin can support (components/bitdepth wise)
    /// - fullscaleMappedImage is pointing to "image" if the plug-in does support the renderscale, meaning we don't use it.
    /// - Similarily downscaledMappedImage is pointing to "downscaledImage" if the plug-in doesn't support the render scale.
    ///
    /// - renderMappedImage is what is given to the plug-in to render the image into,it is mapped to an image that the plug-in
    ///can render onto (good scale, good components, good bitdepth)
    ///
    /// These are the possible scenarios:
    /// - 1) Plugin doesn't need remapping and doesn't need downscaling
    ///    * We render in downscaledImage always, all image pointers point to it.
    /// - 2) Plugin doesn't need remapping but needs downscaling (doesn't support the renderscale)
    ///    * We render in fullScaleImage, fullscaleMappedImage points to it and then we downscale into downscaledImage.
    ///    * renderMappedImage points to fullScaleImage
    /// - 3) Plugin needs remapping (doesn't support requested components or bitdepth) but doesn't need downscaling
    ///    * renderMappedImage points to downscaledMappedImage
    ///    * We render in downscaledMappedImage and then convert back to downscaledImage with requested comps/bitdepth
    /// - 4) Plugin needs remapping and downscaling
    ///    * renderMappedImage points to fullScaleMappedImage
    ///    * We render in fullScaledMappedImage, then convert into "image" and then downscale into downscaledImage.
    Natron::StatusEnum tiledRenderingFunctor(const TiledRenderingFunctorArgs& args,
                                         const ParallelRenderArgs& frameArgs,
                                         bool setThreadLocalStorage,
                                         const RectI & roi );

    Natron::StatusEnum tiledRenderingFunctor(const RenderArgs & args,
                                    const ParallelRenderArgs& frameArgs,
                                     bool setThreadLocalStorage,
                                     bool renderFullScaleThenDownscale,
                                     bool renderUseScaleOneInputs,
                                     bool isSequentialRender,
                                     bool isRenderResponseToUserInteraction,
                                     const RectI & roi,
                                     const double par,
                                     const boost::shared_ptr<Natron::Image> & downscaledImage,
                                     const boost::shared_ptr<Natron::Image> & fullScaleImage,
                                     const boost::shared_ptr<Natron::Image> & downscaledMappedImage,
                                     const boost::shared_ptr<Natron::Image> & fullScaleMappedImage,
                                     const boost::shared_ptr<Natron::Image> & renderMappedImage);

    /**
     * @brief Returns the index of the input if inputEffect is a valid input connected to this effect, otherwise returns -1.
     **/
    int getInputNumber(Natron::EffectInstance* inputEffect) const;
};





/**
 * @typedef Any plug-in should have a static function called BuildEffect with the following signature.
 * It is used to build a new instance of an effect. Basically it should just call the constructor.
 **/
typedef Natron::EffectInstance* (*EffectBuilder)(boost::shared_ptr<Node>);

class OutputEffectInstance
    : public Natron::EffectInstance
{
    SequenceTime _writerCurrentFrame; /*!< for writers only: indicates the current frame
                                         It avoids snchronizing all viewers in the app to the render*/
    SequenceTime _writerFirstFrame;
    SequenceTime _writerLastFrame;
    mutable QMutex* _outputEffectDataLock;
    BlockingBackgroundRender* _renderController; //< pointer to a blocking renderer
    
    RenderEngine* _engine;
public:

    OutputEffectInstance(boost::shared_ptr<Node> node);

    virtual ~OutputEffectInstance();
    
    virtual bool isOutput() const
    {
        return true;
    }

    RenderEngine* getRenderEngine() const
    {
        return _engine;
    }

    /**
     * @brief Starts rendering of all the sequence available, from start to end.
     * This function is meant to be called for on-disk renderer only (i.e: not viewers).
     **/
    void renderFullSequence(BlockingBackgroundRender* renderController,int first,int last);

    void notifyRenderFinished();

    void renderCurrentFrame(bool canAbort);

    bool ifInfiniteclipRectToProjectDefault(RectD* rod) const;

    /**
     * @brief Returns the frame number this effect is currently rendering.
     * Note that this function can be used only for Writers or OpenFX writers,
     * it doesn't work with the Viewer.
     **/
    int getCurrentFrame() const;

    void setCurrentFrame(int f);
    
    void incrementCurrentFrame();
    void decrementCurrentFrame();

    int getFirstFrame() const;

    void setFirstFrame(int f);

    int getLastFrame() const;

    void setLastFrame(int f);
    
    virtual void initializeData() OVERRIDE FINAL;
    
protected:
        
    /**
     * @brief Creates the engine that will control the output rendering
     **/
    virtual RenderEngine* createRenderEngine();
    
    
 
};
} // Natron
#endif // NATRON_ENGINE_EFFECTINSTANCE_H_
