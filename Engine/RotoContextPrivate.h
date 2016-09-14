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

#ifndef ROTOCONTEXTPRIVATE_H
#define ROTOCONTEXTPRIVATE_H

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "Global/Macros.h"

#include <list>
#include <map>
#include <string>
#include <vector>
#include <limits>
#include <sstream>
#include <stdexcept>

#if !defined(Q_MOC_RUN) && !defined(SBK_RUN)
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#endif

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QtCore/QMutex>
#include <QtCore/QReadWriteLock>
#include <QtCore/QCoreApplication>
#include <QtCore/QThread>
#include <QtCore/QReadWriteLock>
#include <QtCore/QWaitCondition>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include "Global/GlobalDefines.h"

#include "Engine/AppManager.h"
#include "Engine/BezierCP.h"
#include "Engine/Bezier.h"
#include "Engine/Curve.h"
#include "Engine/EffectInstance.h"
#include "Engine/Image.h"
#include "Engine/KnobTypes.h"
#include "Engine/MergingEnum.h"
#include "Engine/Node.h"
#include "Engine/RotoContext.h"
#include "Engine/RotoLayer.h"
#include "Engine/RotoPaint.h"
#include "Engine/Transform.h"
#include "Engine/ViewIdx.h"
#include "Engine/EngineFwd.h"

#define ROTO_DEFAULT_OPACITY 1.
#define ROTO_DEFAULT_FEATHER 1.5
#define ROTO_DEFAULT_FEATHERFALLOFF 1.
#define ROTO_DEFAULT_COLOR_R 1.
#define ROTO_DEFAULT_COLOR_G 1.
#define ROTO_DEFAULT_COLOR_B 1.


#define kRotoScriptNameHint "Script-name of the item for Python scripts. It cannot be edited."

#define kRotoLabelHint "Label of the layer or curve"

#define kRotoNameHint "Name of the layer or curve."


#define kRotoOpacityParam "opacity"
#define kRotoOpacityParamLabel "Opacity"
#define kRotoOpacityHint \
    "Controls the opacity of the selected shape(s)."

#define kRotoFeatherParam "feather"
#define kRotoFeatherParamLabel "Feather"
#define kRotoFeatherHint \
    "Controls the distance of feather (in pixels) to add around the selected shape(s)"

#define kRotoFeatherFallOffParam "featherFallOff"
#define kRotoFeatherFallOffParamLabel "Feather fall-off"
#define kRotoFeatherFallOffHint \
    "Controls the rate at which the feather is applied on the selected shape(s)."

#define kRotoFeatherFallOffType "fallOffType"
#define kRotoFeatherFallOffTypeLabel ""
#define kRotoFeatherFallOffTypeHint "Select the type of interpolation used to create the fall-off ramp between the inner shape and the outter feather edge"

#define kRotoFeatherFallOffTypeLinear "Linear"
#define kRotoFeatherFallOffTypeLinearHint "Linear ramp"

#define kRotoFeatherFallOffTypePLinear "PLinear"
#define kRotoFeatherFallOffTypePLinearHint "Perceptually linear ramp in Rec.709"

#define kRotoFeatherFallOffTypeEaseIn "Ease-in"
#define kRotoFeatherFallOffTypeEaseInHint "Catmull-Rom spline, smooth start, linear end (a.k.a. smooth0)"

#define kRotoFeatherFallOffTypeEaseOut "Ease-out"
#define kRotoFeatherFallOffTypeEaseOutHint "Catmull-Rom spline, linear start, smooth end (a.k.a. smooth1)"

#define kRotoFeatherFallOffTypeSmooth "Smooth"
#define kRotoFeatherFallOffTypeSmoothHint "Traditional smoothstep ramp"

#define kRotoActivatedParam "activated"
#define kRotoActivatedParamLabel "Activated"
#define kRotoActivatedHint \
    "Controls whether the selected shape(s) should be rendered or not." \
    "Note that you can animate this parameter so you can activate/deactive the shape " \
    "throughout the time."

#define kRotoLockedHint \
    "Control whether the layer/curve is editable or locked."

#define kRotoInvertedParam "inverted"
#define kRotoInvertedParamLabel "Inverted"

#define kRotoInvertedHint \
    "Controls whether the selected shape(s) should be inverted. When inverted everything " \
    "outside the shape will be set to 1 and everything inside the shape will be set to 0."

#define kRotoOverlayHint "Color of the display overlay for this curve. Doesn't affect output."

#define kRotoColorParam "color"
#define kRotoColorParamLabel "Color"
#define kRotoColorHint \
    "The color of the shape. This parameter is used when the output components are set to RGBA."

#define kRotoCompOperatorParam "operator"
#define kRotoCompOperatorParamLabel "Operator"
#define kRotoCompOperatorHint \
    "The compositing operator controls how this shape is merged with the shapes that have already been rendered.\n" \
    "The roto mask is initialised as black and transparent, then each shape is drawn in the selected order, with the selected color and operator.\n" \
    "Finally, the mask is composed with the source image, if connected, using the 'over' operator.\n" \
    "See http://cairographics.org/operators/ for a full description of available operators."

#define kRotoBrushSourceColor "sourceType"
#define kRotoBrushSourceColorLabel "Source"
#define kRotoBrushSourceColorHint "Source color used for painting the stroke when the Reveal/Clone tools are used:\n" \
    "- foreground: the painted result at this point in the hierarchy\n" \
    "- background: the original image unpainted connected to bg\n" \
    "- backgroundN: the original image unpainted connected to bgN\n"

#define kRotoBrushSizeParam "brushSize"
#define kRotoBrushSizeParamLabel "Brush Size"
#define kRotoBrushSizeParamHint "This is the diameter of the brush in pixels. Shift + drag on the viewer to modify this value"

#define kRotoBrushSpacingParam "brushSpacing"
#define kRotoBrushSpacingParamLabel "Brush Spacing"
#define kRotoBrushSpacingParamHint "Spacing between stamps of the paint brush"

#define kRotoBrushHardnessParam "brushHardness"
#define kRotoBrushHardnessParamLabel "Brush Hardness"
#define kRotoBrushHardnessParamHint "Fall off of the brush effect from the center to the edge"

#define kRotoBrushEffectParam "brushEffect"
#define kRotoBrushEffectParamLabel "Brush effect"
#define kRotoBrushEffectParamHint "The strength of the effect"

#define kRotoBrushVisiblePortionParam "strokeVisiblePortion"
#define kRotoBrushVisiblePortionParamLabel "Visible portion"
#define kRotoBrushVisiblePortionParamHint "Defines the range of the stroke that should be visible: 0 is the start of the stroke and 1 the end."

#define kRotoBrushPressureLabelParam "pressureAlters"
#define kRotoBrushPressureLabelParamLabel "Pressure alters"
#define kRotoBrushPressureLabelParamHint ""

#define kRotoBrushPressureOpacityParam "pressureOpacity"
#define kRotoBrushPressureOpacityParamLabel "Opacity"
#define kRotoBrushPressureOpacityParamHint "Alters the opacity of the paint brush proportionate to changes in pen pressure"

#define kRotoBrushPressureSizeParam "pressureSize"
#define kRotoBrushPressureSizeParamLabel "Size"
#define kRotoBrushPressureSizeParamHint "Alters the size of the paint brush proportionate to changes in pen pressure"

#define kRotoBrushPressureHardnessParam "pressureHardness"
#define kRotoBrushPressureHardnessParamLabel "Hardness"
#define kRotoBrushPressureHardnessParamHint "Alters the hardness of the paint brush proportionate to changes in pen pressure"

#define kRotoBrushBuildupParam "buildUp"
#define kRotoBrushBuildupParamLabel "Build-up"
#define kRotoBrushBuildupParamHint "When checked, the paint stroke builds up when painted over itself"

#define kRotoBrushTimeOffsetParam "timeOffset"
#define kRotoBrushTimeOffsetParamLabel "Clone time offset"
#define kRotoBrushTimeOffsetParamHint "When the Clone tool is used, this determines depending on the time offset mode the source frame to " \
    "clone. When in absolute mode, this is the frame number of the source, when in relative mode, this is an offset relative to the current frame."

#define kRotoBrushTimeOffsetModeParam "timeOffsetMode"
#define kRotoBrushTimeOffsetModeParamLabel "Mode"
#define kRotoBrushTimeOffsetModeParamHint "Time offset mode: when in absolute mode, this is the frame number of the source, when in relative mode, this is an offset relative to the current frame."

#define kRotoBrushTranslateParam "cloneTranslate"
#define kRotoBrushTranslateParamLabel "Translate"
#define kRotoBrushTranslateParamHint ""

#define kRotoBrushRotateParam "cloneRotate"
#define kRotoBrushRotateParamLabel "Rotate"
#define kRotoBrushRotateParamHint ""

#define kRotoBrushScaleParam "cloneScale"
#define kRotoBrushScaleParamLabel "Scale"
#define kRotoBrushScaleParamHint ""

#define kRotoBrushScaleUniformParam "cloneUniform"
#define kRotoBrushScaleUniformParamLabel "Uniform"
#define kRotoBrushScaleUniformParamHint ""

#define kRotoBrushSkewXParam "cloneSkewx"
#define kRotoBrushSkewXParamLabel "Skew X"
#define kRotoBrushSkewXParamHint ""

#define kRotoBrushSkewYParam "cloneSkewy"
#define kRotoBrushSkewYParamLabel "Skew Y"
#define kRotoBrushSkewYParamHint ""

#define kRotoBrushSkewOrderParam "cloneSkewOrder"
#define kRotoBrushSkewOrderParamLabel "Skew Order"
#define kRotoBrushSkewOrderParamHint ""

#define kRotoBrushCenterParam "cloneCenter"
#define kRotoBrushCenterParamLabel "Center"
#define kRotoBrushCenterParamHint ""

#define kRotoBrushFilterParam "cloneFilter"
#define kRotoBrushFilterParamLabel "Filter"
#define kRotoBrushFilterParamHint "Filtering algorithm - some filters may produce values outside of the initial range (*) or modify the values even if there is no movement (+)."

#define kRotoBrushBlackOutsideParam "blackOutside"
#define kRotoBrushBlackOutsideParamLabel "Black Outside"
#define kRotoBrushBlackOutsideParamHint "Fill the area outside the source image with black"

#define kFilterImpulse "Impulse"
#define kFilterImpulseHint "(nearest neighbor / box) Use original values"
#define kFilterBilinear "Bilinear"
#define kFilterBilinearHint "(tent / triangle) Bilinear interpolation between original values"
#define kFilterCubic "Cubic"
#define kFilterCubicHint "(cubic spline) Some smoothing"
#define kFilterKeys "Keys"
#define kFilterKeysHint "(Catmull-Rom / Hermite spline) Some smoothing, plus minor sharpening (*)"
#define kFilterSimon "Simon"
#define kFilterSimonHint "Some smoothing, plus medium sharpening (*)"
#define kFilterRifman "Rifman"
#define kFilterRifmanHint "Some smoothing, plus significant sharpening (*)"
#define kFilterMitchell "Mitchell"
#define kFilterMitchellHint "Some smoothing, plus blurring to hide pixelation (*+)"
#define kFilterParzen "Parzen"
#define kFilterParzenHint "(cubic B-spline) Greatest smoothing of all filters (+)"
#define kFilterNotch "Notch"
#define kFilterNotchHint "Flat smoothing (which tends to hide moire' patterns) (+)"


#define kRotoDrawableItemTranslateParam "translate"
#define kRotoDrawableItemTranslateParamLabel "Translate"
#define kRotoDrawableItemTranslateParamHint ""

#define kRotoDrawableItemRotateParam "rotate"
#define kRotoDrawableItemRotateParamLabel "Rotate"
#define kRotoDrawableItemRotateParamHint ""

#define kRotoDrawableItemScaleParam "scale"
#define kRotoDrawableItemScaleParamLabel "Scale"
#define kRotoDrawableItemScaleParamHint ""

#define kRotoDrawableItemScaleUniformParam "uniform"
#define kRotoDrawableItemScaleUniformParamLabel "Uniform"
#define kRotoDrawableItemScaleUniformParamHint ""

#define kRotoDrawableItemSkewXParam "skewx"
#define kRotoDrawableItemSkewXParamLabel "Skew X"
#define kRotoDrawableItemSkewXParamHint ""

#define kRotoDrawableItemSkewYParam "skewy"
#define kRotoDrawableItemSkewYParamLabel "Skew Y"
#define kRotoDrawableItemSkewYParamHint ""

#define kRotoDrawableItemSkewOrderParam "skewOrder"
#define kRotoDrawableItemSkewOrderParamLabel "Skew Order"
#define kRotoDrawableItemSkewOrderParamHint ""

#define kRotoDrawableItemCenterParam "center"
#define kRotoDrawableItemCenterParamLabel "Center"
#define kRotoDrawableItemCenterParamHint ""

#define kRotoDrawableItemExtraMatrixParam "extraMatrix"
#define kRotoDrawableItemExtraMatrixParamLabel "Extra Matrix"
#define kRotoDrawableItemExtraMatrixParamHint "This matrix gets concatenated to the transform resulting from the parameter above."

#define kRotoDrawableItemLifeTimeParam "lifeTime"
#define kRotoDrawableItemLifeTimeParamLabel "Life Time"
#define kRotoDrawableItemLifeTimeParamHint "Controls the life-time of the shape/stroke"

#define kRotoDrawableItemLifeTimeAll "All"
#define kRotoDrawableItemLifeTimeAllHelp "All frames"

#define kRotoDrawableItemLifeTimeSingle "Single"
#define kRotoDrawableItemLifeTimeSingleHelp "Only for the specified frame"

#define kRotoDrawableItemLifeTimeFromStart "From start"
#define kRotoDrawableItemLifeTimeFromStartHelp "From the start of the sequence up to the specified frame"

#define kRotoDrawableItemLifeTimeToEnd "To end"
#define kRotoDrawableItemLifeTimeToEndHelp "From the specified frame to the end of the sequence"

#define kRotoDrawableItemLifeTimeCustom "Custom"
#define kRotoDrawableItemLifeTimeCustomHelp "Use the Activated parameter animation to control the life-time of the shape/stroke using keyframes"

#define kRotoDrawableItemLifeTimeFrameParam "lifeTimeFrame"
#define kRotoDrawableItemLifeTimeFrameParamLabel "Frame"
#define kRotoDrawableItemLifeTimeFrameParamHint "Use this to specify the frame when in mode Single/From start/To end"

#define kRotoResetCloneTransformParam "resetCloneTransform"
#define kRotoResetCloneTransformParamLabel "Reset Transform"
#define kRotoResetCloneTransformParamHint "Reset the clone transform to an identity"

#define kRotoResetTransformParam "resetTransform"
#define kRotoResetTransformParamLabel "Reset Transform"
#define kRotoResetTransformParamHint "Reset the transform to an identity"

#define kRotoResetCloneCenterParam "resetCloneCenter"
#define kRotoResetCloneCenterParamLabel "Reset Center"
#define kRotoResetCloneCenterParamHint "Reset the clone transform center"

#define kRotoResetCenterParam "resetTransformCenter"
#define kRotoResetCenterParamLabel "Reset Center"
#define kRotoResetCenterParamHint "Reset the transform center"

#define kRotoTransformInteractive "RotoTransformInteractive"
#define kRotoTransformInteractiveLabel "Interactive"
#define kRotoTransformInteractiveHint "When check, modifying the transform will directly render the shape in the viewer. When unchecked, modifications are applied when releasing the mouse button."

#define kRotoMotionBlurModeParam "motionBlurMode"
#define kRotoMotionBlurModeParamLabel "Mode"
#define kRotoMotionBlurModeParamHint "Per-shape motion blurs applies motion blur independently to each shape and then blends them together." \
    " This may produce artifacts when shapes blur over the same portion of the image, but might be more efficient than global motion-blur." \
    " Global motion-blur takes into account the interaction between shapes and will not create artifacts at the expense of being slightly " \
    "more expensive than the per-shape motion blur. Note that when using the global motion-blur, all shapes will have the same motion-blur " \
    "settings applied to them."

#define kRotoPerShapeMotionBlurParam "motionBlur"
#define kRotoGlobalMotionBlurParam "globalMotionBlur"
#define kRotoMotionBlurParamLabel "Motion Blur"
#define kRotoMotionBlurParamHint "The number of Motion-Blur samples used for blurring. Increase for better quality but slower rendering."

#define kRotoPerShapeShutterParam "motionBlurShutter"
#define kRotoGlobalShutterParam "globalMotionBlurShutter"
#define kRotoShutterParamLabel "Shutter"
#define kRotoShutterParamHint "The number of frames during which the shutter should be opened when motion blurring."

#define kRotoPerShapeShutterOffsetTypeParam "motionBlurShutterOffset"
#define kRotoGlobalShutterOffsetTypeParam "gobalMotionBlurShutterOffset"
#define kRotoShutterOffsetTypeParamLabel "Shutter Offset"
#define kRotoShutterOffsetTypeParamHint "This controls how the shutter operates in respect to the current frame value."

#define kRotoShutterOffsetCenteredHint "Centers the shutter around the current frame, that is the shutter will be opened from f - shutter/2 to " \
    "f + shutter/2"
#define kRotoShutterOffsetStartHint "The shutter will open at the current frame and stay open until f + shutter"
#define kRotoShutterOffsetEndHint "The shutter will open at f - shutter until the current frame"
#define kRotoShutterOffsetCustomHint "The shutter will open at the time indicated by the shutter offset parameter"

#define kRotoPerShapeShutterCustomOffsetParam "motionBlurCustomShutterOffset"
#define kRotoGlobalShutterCustomOffsetParam "globalMotionBlurCustomShutterOffset"
#define kRotoShutterCustomOffsetParamLabel "Custom Offset"
#define kRotoShutterCustomOffsetParamHint "If the Shutter Offset parameter is set to Custom then this parameter controls the frame at " \
    "which the shutter opens. The value is an offset in frames to the current frame, e.g: -1  would open the shutter 1 frame before the current frame."


NATRON_NAMESPACE_ENTER;

struct BezierPrivate
{
    BezierCPs points; //< the control points of the curve
    BezierCPs featherPoints; //< the feather points, the number of feather points must equal the number of cp.

    //updated whenever the Bezier is edited, this is used to determine if a point lies inside the bezier or not
    //it has a value for each keyframe
    mutable std::map<double, bool> isClockwiseOriented;
    mutable bool isClockwiseOrientedStatic; //< used when the bezier has no keyframes
    mutable std::map<double, bool> guiIsClockwiseOriented;
    mutable bool guiIsClockwiseOrientedStatic; //< used when the bezier has no keyframes
    bool autoRecomputeOrientation; // when true, orientation will be computed automatically on editing
    bool finished; //< when finished is true, the last point of the list is connected to the first point of the list.
    bool isOpenBezier;
    mutable QMutex guiCopyMutex;
    bool mustCopyGui;

    BezierPrivate(bool isOpenBezier)
        : points()
        , featherPoints()
        , isClockwiseOriented()
        , isClockwiseOrientedStatic(false)
        , guiIsClockwiseOriented()
        , guiIsClockwiseOrientedStatic(false)
        , autoRecomputeOrientation(true)
        , finished(false)
        , isOpenBezier(isOpenBezier)
        , guiCopyMutex()
        , mustCopyGui(false)
    {
    }

    void setMustCopyGuiBezier(bool copy)
    {
        QMutexLocker k(&guiCopyMutex);

        mustCopyGui = copy;
    }

    bool hasKeyframeAtTime(bool useGuiCurves,
                           double time) const
    {
        // PRIVATE - should not lock

        if ( points.empty() ) {
            return false;
        } else {
            KeyFrame k;

            return points.front()->hasKeyFrameAtTime(useGuiCurves, time);
        }
    }

    void getKeyframeTimes(bool useGuiCurves,
                          std::set<double>* times) const
    {
        // PRIVATE - should not lock

        if ( points.empty() ) {
            return;
        }
        points.front()->getKeyframeTimes(useGuiCurves, times);
    }

    BezierCPs::const_iterator atIndex(int index) const
    {
        // PRIVATE - should not lock

        if ( ( index >= (int)points.size() ) || (index < 0) ) {
            throw std::out_of_range("RotoSpline::atIndex: non-existent control point");
        }

        BezierCPs::const_iterator it = points.begin();
        std::advance(it, index);

        return it;
    }

    BezierCPs::iterator atIndex(int index)
    {
        // PRIVATE - should not lock

        if ( ( index >= (int)points.size() ) || (index < 0) ) {
            throw std::out_of_range("RotoSpline::atIndex: non-existent control point");
        }

        BezierCPs::iterator it = points.begin();
        std::advance(it, index);

        return it;
    }

    BezierCPs::const_iterator findControlPointNearby(double x,
                                                     double y,
                                                     double acceptance,
                                                     double time,
                                                     ViewIdx view,
                                                     const Transform::Matrix3x3& transform,
                                                     int* index) const
    {
        // PRIVATE - should not lock
        int i = 0;

        for (BezierCPs::const_iterator it = points.begin(); it != points.end(); ++it, ++i) {
            Transform::Point3D p;
            p.z = 1;
            (*it)->getPositionAtTime(true, time, view, &p.x, &p.y);
            p = Transform::matApply(transform, p);
            if ( ( p.x >= (x - acceptance) ) && ( p.x <= (x + acceptance) ) && ( p.y >= (y - acceptance) ) && ( p.y <= (y + acceptance) ) ) {
                *index = i;

                return it;
            }
        }

        return points.end();
    }

    BezierCPs::const_iterator findFeatherPointNearby(double x,
                                                     double y,
                                                     double acceptance,
                                                     double time,
                                                     ViewIdx view,
                                                     const Transform::Matrix3x3& transform,
                                                     int* index) const
    {
        // PRIVATE - should not lock
        int i = 0;

        for (BezierCPs::const_iterator it = featherPoints.begin(); it != featherPoints.end(); ++it, ++i) {
            Transform::Point3D p;
            p.z = 1;
            (*it)->getPositionAtTime(true, time, view, &p.x, &p.y);
            p = Transform::matApply(transform, p);
            if ( ( p.x >= (x - acceptance) ) && ( p.x <= (x + acceptance) ) && ( p.y >= (y - acceptance) ) && ( p.y <= (y + acceptance) ) ) {
                *index = i;

                return it;
            }
        }

        return featherPoints.end();
    }
};

class RotoLayer;
struct RotoItemPrivate
{
    boost::weak_ptr<RotoContext> context;
    std::string scriptName, label;
    boost::weak_ptr<RotoLayer> parentLayer;

    ////This controls whether the item (and all its children if it is a layer)
    ////should be visible/rendered or not at any time.
    ////This is different from the "activated" knob for RotoDrawableItem's which in that
    ////case allows to define a life-time
    bool globallyActivated;

    ////A locked item should not be modifiable by the GUI
    bool locked;

    RotoItemPrivate(const RotoContextPtr context,
                    const std::string & n,
                    const RotoLayerPtr& parent)
        : context(context)
        , scriptName(n)
        , label(n)
        , parentLayer(parent)
        , globallyActivated(true)
        , locked(false)
    {
    }
};

typedef std::list< RotoItemPtr > RotoItems;

struct RotoLayerPrivate
{
    RotoItems items;

    RotoLayerPrivate()
        : items()
    {
    }
};

struct RotoDrawableItemPrivate
{
    Q_DECLARE_TR_FUNCTIONS(RotoDrawableItem)

public:
    /*
     * The effect node corresponds to the following given the selected tool:
     * Stroke= RotoOFX
     * Blur = BlurCImg
     * Clone = TransformOFX
     * Sharpen = SharpenCImg
     * Smear = hand made tool
     * Reveal = Merge(over) with A being color type and B the tree upstream
     * Dodge/Burn = Merge(color-dodge/color-burn) with A being the tree upstream and B the color type
     *
     * Each effect is followed by a merge (except for the ones that use a merge) with the user given operator
     * onto the previous tree upstream of the effectNode.
     */
    NodePtr effectNode, maskNode;
    NodePtr mergeNode;
    NodePtr timeOffsetNode, frameHoldNode;
    double overlayColor[4]; //< the color the shape overlay should be drawn with, defaults to smooth red
    KnobDoublePtr opacity; //< opacity of the rendered shape between 0 and 1
    KnobDoublePtr feather; //< number of pixels to add to the feather distance (from the feather point), between -100 and 100
    KnobDoublePtr featherFallOff; //< the rate of fall-off for the feather, between 0 and 1,  0.5 meaning the
                                                  //alpha value is half the original value when at half distance from the feather distance
    KnobChoicePtr fallOffRampType;
    KnobChoicePtr lifeTime;
    KnobBoolPtr activated; //< should the curve be visible/rendered ? (animable)
    KnobIntPtr lifeTimeFrame;
#ifdef NATRON_ROTO_INVERTIBLE
    KnobBoolPtr inverted; //< invert the rendering
#endif
    KnobColorPtr color;
    KnobChoicePtr compOperator;
    KnobDoublePtr translate;
    KnobDoublePtr rotate;
    KnobDoublePtr scale;
    KnobBoolPtr scaleUniform;
    KnobDoublePtr skewX;
    KnobDoublePtr skewY;
    KnobChoicePtr skewOrder;
    KnobDoublePtr center;
    KnobDoublePtr extraMatrix;
    KnobDoublePtr brushSize;
    KnobDoublePtr brushSpacing;
    KnobDoublePtr brushHardness;
    KnobDoublePtr effectStrength;
    KnobBoolPtr pressureOpacity, pressureSize, pressureHardness, buildUp;
    KnobDoublePtr visiblePortion; // [0,1] by default
    KnobDoublePtr cloneTranslate;
    KnobDoublePtr cloneRotate;
    KnobDoublePtr cloneScale;
    KnobBoolPtr cloneScaleUniform;
    KnobDoublePtr cloneSkewX;
    KnobDoublePtr cloneSkewY;
    KnobChoicePtr cloneSkewOrder;
    KnobDoublePtr cloneCenter;
    KnobChoicePtr cloneFilter;
    KnobBoolPtr cloneBlackOutside;
    KnobChoicePtr sourceColor;
    KnobIntPtr timeOffset;
    KnobChoicePtr timeOffsetMode;

#ifdef NATRON_ROTO_ENABLE_MOTION_BLUR
    KnobDoublePtr motionBlur;
    KnobDoublePtr shutter;
    KnobChoicePtr shutterType;
    KnobDoublePtr customOffset;
#endif
    std::list<KnobIPtr > knobs; //< list for easy access to all knobs

    RotoDrawableItemPrivate(bool isPaintingNode)
        : effectNode()
        , maskNode()
        , mergeNode()
        , timeOffsetNode()
        , frameHoldNode()
        , opacity()
        , feather()
        , featherFallOff()
        , fallOffRampType()
        , lifeTime()
        , activated()
        , lifeTimeFrame()
#ifdef NATRON_ROTO_INVERTIBLE
        , inverted()
#endif
        , color()
        , compOperator()
        , translate()
        , rotate()
        , scale()
        , scaleUniform()
        , skewX()
        , skewY()
        , skewOrder()
        , center()
        , brushSize( KnobDouble::create(KnobHolderPtr(), tr(kRotoBrushSizeParamLabel), 1, true) )
        , brushSpacing( KnobDouble::create(KnobHolderPtr(), tr(kRotoBrushSpacingParamLabel), 1, true) )
        , brushHardness( KnobDouble::create(KnobHolderPtr(), tr(kRotoBrushHardnessParamLabel), 1, true) )
        , effectStrength( KnobDouble::create(KnobHolderPtr(), tr(kRotoBrushEffectParamLabel), 1, true) )
        , pressureOpacity( KnobBool::create(KnobHolderPtr(), tr(kRotoBrushPressureOpacityParamLabel), 1, true) )
        , pressureSize( KnobBool::create(KnobHolderPtr(), tr(kRotoBrushPressureSizeParamLabel), 1, true) )
        , pressureHardness( KnobBool::create(KnobHolderPtr(), tr(kRotoBrushPressureHardnessParamLabel), 1, true) )
        , buildUp( KnobBool::create(KnobHolderPtr(), tr(kRotoBrushBuildupParamLabel), 1, true) )
        , visiblePortion( KnobDouble::create(KnobHolderPtr(), tr(kRotoBrushVisiblePortionParamLabel), 2, true) )
        , cloneTranslate( KnobDouble::create(KnobHolderPtr(), tr(kRotoBrushTranslateParamLabel), 2, true) )
        , cloneRotate( KnobDouble::create(KnobHolderPtr(), tr(kRotoBrushRotateParamLabel), 1, true) )
        , cloneScale( KnobDouble::create(KnobHolderPtr(), tr(kRotoBrushScaleParamLabel), 2, true) )
        , cloneScaleUniform( KnobBool::create(KnobHolderPtr(), tr(kRotoBrushScaleUniformParamLabel), 1, true) )
        , cloneSkewX( KnobDouble::create(KnobHolderPtr(), tr(kRotoBrushSkewXParamLabel), 1, true) )
        , cloneSkewY( KnobDouble::create(KnobHolderPtr(), tr(kRotoBrushSkewYParamLabel), 1, true) )
        , cloneSkewOrder( KnobChoice::create(KnobHolderPtr(), tr(kRotoBrushSkewOrderParamLabel), 1, true) )
        , cloneCenter( KnobDouble::create(KnobHolderPtr(), tr(kRotoBrushCenterParamLabel), 2, true) )
        , cloneFilter( KnobChoice::create(KnobHolderPtr(), tr(kRotoBrushFilterParamLabel), 1, true) )
        , cloneBlackOutside( KnobBool::create(KnobHolderPtr(), tr(kRotoBrushBlackOutsideParamLabel), 1, true) )
        , sourceColor( KnobChoice::create(KnobHolderPtr(), tr(kRotoBrushSourceColorLabel), 1, true) )
        , timeOffset( KnobInt::create(KnobHolderPtr(), tr(kRotoBrushTimeOffsetParamLabel), 1, true) )
        , timeOffsetMode( KnobChoice::create(KnobHolderPtr(), tr(kRotoBrushTimeOffsetModeParamLabel), 1, true) )
        , knobs()
    {
        opacity = KnobDouble::create(KnobHolderPtr(), tr(kRotoOpacityParamLabel), 1, true);
        opacity->setHintToolTip( tr(kRotoOpacityHint) );
        opacity->setName(kRotoOpacityParam);
        opacity->populate();
        opacity->setMinimum(0.);
        opacity->setMaximum(1.);
        opacity->setDisplayMinimum(0.);
        opacity->setDisplayMaximum(1.);
        opacity->setDefaultValue(ROTO_DEFAULT_OPACITY);
        knobs.push_back(opacity);

        feather = KnobDouble::create(KnobHolderPtr(), tr(kRotoFeatherParamLabel), 1, true);
        feather->setHintToolTip( tr(kRotoFeatherHint) );
        feather->setName(kRotoFeatherParam);
        feather->populate();
        feather->setMinimum(0);
        feather->setDisplayMinimum(0);
        feather->setDisplayMaximum(500);
        feather->setDefaultValue(ROTO_DEFAULT_FEATHER);
        knobs.push_back(feather);

        featherFallOff = KnobDouble::create(KnobHolderPtr(), tr(kRotoFeatherFallOffParamLabel), 1, true);
        featherFallOff->setHintToolTip( tr(kRotoFeatherFallOffHint) );
        featherFallOff->setName(kRotoFeatherFallOffParam);
        featherFallOff->populate();
        featherFallOff->setMinimum(0.001);
        featherFallOff->setMaximum(5.);
        featherFallOff->setDisplayMinimum(0.2);
        featherFallOff->setDisplayMaximum(5.);
        featherFallOff->setDefaultValue(ROTO_DEFAULT_FEATHERFALLOFF);
        knobs.push_back(featherFallOff);

        fallOffRampType = KnobChoice::create(KnobHolderPtr(), tr(kRotoFeatherFallOffTypeLabel), 1, true);
        fallOffRampType->setHintToolTip( tr(kRotoFeatherFallOffTypeHint) );
        fallOffRampType->setName(kRotoFeatherFallOffType);
        fallOffRampType->populate();
        {
            std::vector<std::string> entries,helps;
            entries.push_back(kRotoFeatherFallOffTypeLinear);
            helps.push_back(kRotoFeatherFallOffTypeLinearHint);
            entries.push_back(kRotoFeatherFallOffTypePLinear);
            helps.push_back(kRotoFeatherFallOffTypePLinearHint);
            entries.push_back(kRotoFeatherFallOffTypeEaseIn);
            helps.push_back(kRotoFeatherFallOffTypeEaseInHint);
            entries.push_back(kRotoFeatherFallOffTypeEaseOut);
            helps.push_back(kRotoFeatherFallOffTypeEaseOutHint);
            entries.push_back(kRotoFeatherFallOffTypeSmooth);
            helps.push_back(kRotoFeatherFallOffTypeSmoothHint);
            fallOffRampType->populateChoices(entries, helps);
        }
        knobs.push_back(fallOffRampType);


        lifeTime = KnobChoice::create(KnobHolderPtr(), tr(kRotoDrawableItemLifeTimeParamLabel), 1, true);
        lifeTime->setHintToolTip( tr(kRotoDrawableItemLifeTimeParamHint) );
        lifeTime->populate();
        lifeTime->setName(kRotoDrawableItemLifeTimeParam);
        {
            std::vector<std::string> choices;
            choices.push_back(kRotoDrawableItemLifeTimeSingle);
            choices.push_back(kRotoDrawableItemLifeTimeFromStart);
            choices.push_back(kRotoDrawableItemLifeTimeToEnd);
            choices.push_back(kRotoDrawableItemLifeTimeCustom);
            lifeTime->populateChoices(choices);
        }
        lifeTime->setDefaultValue(isPaintingNode ? 0 : 3);
        knobs.push_back(lifeTime);

        lifeTimeFrame = KnobInt::create(KnobHolderPtr(), tr(kRotoDrawableItemLifeTimeFrameParamLabel), 1, true);
        lifeTimeFrame->setHintToolTip( tr(kRotoDrawableItemLifeTimeFrameParamHint) );
        lifeTimeFrame->setName(kRotoDrawableItemLifeTimeFrameParam);
        lifeTimeFrame->populate();
        knobs.push_back(lifeTimeFrame);

        activated = KnobBool::create(KnobHolderPtr(), tr(kRotoActivatedParamLabel), 1, true);
        activated->setHintToolTip( tr(kRotoActivatedHint) );
        activated->setName(kRotoActivatedParam);
        activated->populate();
        activated->setDefaultValue(true);
        knobs.push_back(activated);

#ifdef NATRON_ROTO_INVERTIBLE
        inverted = KnobBool::create(NULL, tr(kRotoInvertedParamLabel), 1, true);
        inverted->setHintToolTip( tr(kRotoInvertedHint) );
        inverted->setName(kRotoInvertedParam);
        inverted->populate();
        inverted->setDefaultValue(false);
        knobs.push_back(inverted);
#endif

        color = KnobColor::create(KnobHolderPtr(), tr(kRotoColorParamLabel), 3, true);
        color->setHintToolTip( tr(kRotoColorHint) );
        color->setName(kRotoColorParam);
        color->populate();
        color->setDefaultValue(ROTO_DEFAULT_COLOR_R, 0);
        color->setDefaultValue(ROTO_DEFAULT_COLOR_G, 1);
        color->setDefaultValue(ROTO_DEFAULT_COLOR_B, 2);
        knobs.push_back(color);

        compOperator = KnobChoice::create(KnobHolderPtr(), tr(kRotoCompOperatorParamLabel), 1, true);
        compOperator->setHintToolTip( tr(kRotoCompOperatorHint) );
        compOperator->setName(kRotoCompOperatorParam);
        compOperator->populate();
        knobs.push_back(compOperator);


        translate = KnobDouble::create(KnobHolderPtr(), tr(kRotoDrawableItemTranslateParamLabel), 2, true);
        translate->setName(kRotoDrawableItemTranslateParam);
        translate->setHintToolTip( tr(kRotoDrawableItemTranslateParamHint) );
        translate->populate();
        knobs.push_back(translate);

        rotate = KnobDouble::create(KnobHolderPtr(), tr(kRotoDrawableItemRotateParamLabel), 1, true);
        rotate->setName(kRotoDrawableItemRotateParam);
        rotate->setHintToolTip( tr(kRotoDrawableItemRotateParamHint) );
        rotate->populate();
        knobs.push_back(rotate);

        scale = KnobDouble::create(KnobHolderPtr(), tr(kRotoDrawableItemScaleParamLabel), 2, true);
        scale->setName(kRotoDrawableItemScaleParam);
        scale->setHintToolTip( tr(kRotoDrawableItemScaleParamHint) );
        scale->populate();
        scale->setDefaultValue(1, 0);
        scale->setDefaultValue(1, 1);
        knobs.push_back(scale);

        scaleUniform = KnobBool::create(KnobHolderPtr(), tr(kRotoDrawableItemScaleUniformParamLabel), 1, true);
        scaleUniform->setName(kRotoDrawableItemScaleUniformParam);
        scaleUniform->setHintToolTip( tr(kRotoDrawableItemScaleUniformParamHint) );
        scaleUniform->populate();
        scaleUniform->setDefaultValue(true);
        knobs.push_back(scaleUniform);

        skewX = KnobDouble::create(KnobHolderPtr(), tr(kRotoDrawableItemSkewXParamLabel), 1, true);
        skewX->setName(kRotoDrawableItemSkewXParam);
        skewX->setHintToolTip( tr(kRotoDrawableItemSkewXParamHint) );
        skewX->populate();
        knobs.push_back(skewX);

        skewY = KnobDouble::create(KnobHolderPtr(), tr(kRotoDrawableItemSkewYParamLabel), 1, true);
        skewY->setName(kRotoDrawableItemSkewYParam);
        skewY->setHintToolTip( tr(kRotoDrawableItemSkewYParamHint) );
        skewY->populate();
        knobs.push_back(skewY);

        skewOrder = KnobChoice::create(KnobHolderPtr(), tr(kRotoDrawableItemSkewOrderParamLabel), 1, true);
        skewOrder->setName(kRotoDrawableItemSkewOrderParam);
        skewOrder->setHintToolTip( tr(kRotoDrawableItemSkewOrderParamHint) );
        skewOrder->populate();
        knobs.push_back(skewOrder);
        {
            std::vector<std::string> choices;
            choices.push_back("XY");
            choices.push_back("YX");
            skewOrder->populateChoices(choices);
        }

        center = KnobDouble::create(KnobHolderPtr(), tr(kRotoDrawableItemCenterParamLabel), 2, true);
        center->setName(kRotoDrawableItemCenterParam);
        center->setHintToolTip( tr(kRotoDrawableItemCenterParamHint) );
        center->populate();
        knobs.push_back(center);

        extraMatrix = KnobDouble::create(KnobHolderPtr(), tr(kRotoDrawableItemExtraMatrixParamLabel), 9, true);
        extraMatrix->setName(kRotoDrawableItemExtraMatrixParam);
        extraMatrix->setHintToolTip( tr(kRotoDrawableItemExtraMatrixParamHint) );
        extraMatrix->populate();
        extraMatrix->setDefaultValue(1, 0);
        extraMatrix->setDefaultValue(1, 4);
        extraMatrix->setDefaultValue(1, 8);
        knobs.push_back(extraMatrix);


        brushSize->setName(kRotoBrushSizeParam);
        brushSize->setHintToolTip( tr(kRotoBrushSizeParamHint) );
        brushSize->populate();
        brushSize->setDefaultValue(25);
        brushSize->setMinimum(1);
        brushSize->setMaximum(1000);
        knobs.push_back(brushSize);

        brushSpacing->setName(kRotoBrushSpacingParam);
        brushSpacing->setHintToolTip( tr(kRotoBrushSpacingParamHint) );
        brushSpacing->populate();
        brushSpacing->setDefaultValue(0.1);
        brushSpacing->setMinimum(0);
        brushSpacing->setMaximum(1);
        knobs.push_back(brushSpacing);

        brushHardness->setName(kRotoBrushHardnessParam);
        brushHardness->setHintToolTip( tr(kRotoBrushHardnessParamHint) );
        brushHardness->populate();
        brushHardness->setDefaultValue(0.2);
        brushHardness->setMinimum(0);
        brushHardness->setMaximum(1);
        knobs.push_back(brushHardness);

        effectStrength->setName(kRotoBrushEffectParam);
        effectStrength->setHintToolTip( tr(kRotoBrushEffectParamHint) );
        effectStrength->populate();
        effectStrength->setDefaultValue(15);
        effectStrength->setMinimum(0);
        effectStrength->setMaximum(100);
        knobs.push_back(effectStrength);

        pressureOpacity->setName(kRotoBrushPressureOpacityParam);
        pressureOpacity->setHintToolTip( tr(kRotoBrushPressureOpacityParamHint) );
        pressureOpacity->populate();
        pressureOpacity->setAnimationEnabled(false);
        pressureOpacity->setDefaultValue(true);
        knobs.push_back(pressureOpacity);

        pressureSize->setName(kRotoBrushPressureSizeParam);
        pressureSize->populate();
        pressureSize->setHintToolTip( tr(kRotoBrushPressureSizeParamHint) );
        pressureSize->setAnimationEnabled(false);
        pressureSize->setDefaultValue(false);
        knobs.push_back(pressureSize);


        pressureHardness->setName(kRotoBrushPressureHardnessParam);
        pressureHardness->populate();
        pressureHardness->setHintToolTip( tr(kRotoBrushPressureHardnessParamHint) );
        pressureHardness->setAnimationEnabled(false);
        pressureHardness->setDefaultValue(false);
        knobs.push_back(pressureHardness);

        buildUp->setName(kRotoBrushBuildupParam);
        buildUp->populate();
        buildUp->setHintToolTip( tr(kRotoBrushBuildupParamHint) );
        buildUp->setDefaultValue(false);
        buildUp->setAnimationEnabled(false);
        buildUp->setDefaultValue(true);
        knobs.push_back(buildUp);


        visiblePortion->setName(kRotoBrushVisiblePortionParam);
        visiblePortion->setHintToolTip( tr(kRotoBrushVisiblePortionParamHint) );
        visiblePortion->populate();
        visiblePortion->setDefaultValue(0, 0);
        visiblePortion->setDefaultValue(1, 1);
        std::vector<double> mins, maxs;
        mins.push_back(0);
        mins.push_back(0);
        maxs.push_back(1);
        maxs.push_back(1);
        visiblePortion->setMinimumsAndMaximums(mins, maxs);
        knobs.push_back(visiblePortion);

        cloneTranslate->setName(kRotoBrushTranslateParam);
        cloneTranslate->setHintToolTip( tr(kRotoBrushTranslateParamHint) );
        cloneTranslate->populate();
        knobs.push_back(cloneTranslate);

        cloneRotate->setName(kRotoBrushRotateParam);
        cloneRotate->setHintToolTip( tr(kRotoBrushRotateParamHint) );
        cloneRotate->populate();
        knobs.push_back(cloneRotate);

        cloneScale->setName(kRotoBrushScaleParam);
        cloneScale->setHintToolTip( tr(kRotoBrushScaleParamHint) );
        cloneScale->populate();
        cloneScale->setDefaultValue(1, 0);
        cloneScale->setDefaultValue(1, 1);
        knobs.push_back(cloneScale);

        cloneScaleUniform->setName(kRotoBrushScaleUniformParam);
        cloneScaleUniform->setHintToolTip( tr(kRotoBrushScaleUniformParamHint) );
        cloneScaleUniform->populate();
        cloneScaleUniform->setDefaultValue(true);
        knobs.push_back(cloneScaleUniform);

        cloneSkewX->setName(kRotoBrushSkewXParam);
        cloneSkewX->setHintToolTip( tr(kRotoBrushSkewXParamHint) );
        cloneSkewX->populate();
        knobs.push_back(cloneSkewX);

        cloneSkewY->setName(kRotoBrushSkewYParam);
        cloneSkewY->setHintToolTip( tr(kRotoBrushSkewYParamHint) );
        cloneSkewY->populate();
        knobs.push_back(cloneSkewY);

        cloneSkewOrder->setName(kRotoBrushSkewOrderParam);
        cloneSkewOrder->setHintToolTip( tr(kRotoBrushSkewOrderParamHint) );
        cloneSkewOrder->populate();
        knobs.push_back(cloneSkewOrder);
        {
            std::vector<std::string> choices;
            choices.push_back("XY");
            choices.push_back("YX");
            cloneSkewOrder->populateChoices(choices);
        }

        cloneCenter->setName(kRotoBrushCenterParam);
        cloneCenter->setHintToolTip( tr(kRotoBrushCenterParamHint) );
        cloneCenter->populate();
        knobs.push_back(cloneCenter);


        cloneFilter->setName(kRotoBrushFilterParam);
        cloneFilter->setHintToolTip( tr(kRotoBrushFilterParamHint) );
        cloneFilter->populate();
        {
            std::vector<std::string> choices, helps;

            choices.push_back(kFilterImpulse);
            helps.push_back(kFilterImpulseHint);
            choices.push_back(kFilterBilinear);
            helps.push_back(kFilterBilinearHint);
            choices.push_back(kFilterCubic);
            helps.push_back(kFilterCubicHint);
            choices.push_back(kFilterKeys);
            helps.push_back(kFilterKeysHint);
            choices.push_back(kFilterSimon);
            helps.push_back(kFilterSimonHint);
            choices.push_back(kFilterRifman);
            helps.push_back(kFilterRifmanHint);
            choices.push_back(kFilterMitchell);
            helps.push_back(kFilterMitchellHint);
            choices.push_back(kFilterParzen);
            helps.push_back(kFilterParzenHint);
            choices.push_back(kFilterNotch);
            helps.push_back(kFilterNotchHint);
            cloneFilter->populateChoices(choices);
        }
        cloneFilter->setDefaultValue(2);
        knobs.push_back(cloneFilter);


        cloneBlackOutside->setName(kRotoBrushBlackOutsideParam);
        cloneBlackOutside->setHintToolTip( tr(kRotoBrushBlackOutsideParamHint) );
        cloneBlackOutside->populate();
        cloneBlackOutside->setDefaultValue(true);
        knobs.push_back(cloneBlackOutside);

        sourceColor->setName(kRotoBrushSourceColor);
        sourceColor->setHintToolTip( tr(kRotoBrushSizeParamHint) );
        sourceColor->populate();
        sourceColor->setDefaultValue(1);
        {
            std::vector<std::string> choices;
            choices.push_back("foreground");
            choices.push_back("background");
            for (int i = 1; i < 10; ++i) {
                std::stringstream ss;
                ss << "background " << i + 1;
                choices.push_back( ss.str() );
            }
            sourceColor->populateChoices(choices);
        }
        knobs.push_back(sourceColor);

        timeOffset->setName(kRotoBrushTimeOffsetParam);
        timeOffset->setHintToolTip( tr(kRotoBrushTimeOffsetParamHint) );
        timeOffset->populate();
        timeOffset->setDisplayMinimum(-100);
        timeOffset->setDisplayMaximum(100);
        knobs.push_back(timeOffset);

        timeOffsetMode->setName(kRotoBrushTimeOffsetModeParam);
        timeOffsetMode->setHintToolTip( tr(kRotoBrushTimeOffsetModeParamHint) );
        timeOffsetMode->populate();
        {
            std::vector<std::string> modes;
            modes.push_back("Relative");
            modes.push_back("Absolute");
            timeOffsetMode->populateChoices(modes);
        }
        knobs.push_back(timeOffsetMode);

#ifdef NATRON_ROTO_ENABLE_MOTION_BLUR
        motionBlur = KnobDouble::create(NULL, tr(kRotoMotionBlurParamLabel), 1, true) );
        motionBlur->setName(kRotoPerShapeMotionBlurParam);
        motionBlur->setHintToolTip( tr(kRotoMotionBlurParamHint) );
        motionBlur->populate();
        motionBlur->setDefaultValue(0);
        motionBlur->setMinimum(0);
        motionBlur->setDisplayMinimum(0);
        motionBlur->setDisplayMaximum(4);
        motionBlur->setMaximum(4);
        knobs.push_back(motionBlur);

        shutter = KnobDouble::create(NULL, tr(kRotoShutterParamLabel), 1, true) );
        shutter->setName(kRotoPerShapeShutterParam);
        shutter->setHintToolTip( tr(kRotoShutterParamHint) );
        shutter->populate();
        shutter->setDefaultValue(0.5);
        shutter->setMinimum(0);
        shutter->setDisplayMinimum(0);
        shutter->setDisplayMaximum(2);
        shutter->setMaximum(2);
        knobs.push_back(shutter);

        shutterType = KnobChoice::create(NULL, tr(kRotoShutterOffsetTypeParamLabel), 1, true) );
        shutterType->setName(kRotoPerShapeShutterOffsetTypeParam);
        shutterType->setHintToolTip( tr(kRotoShutterOffsetTypeParamHint) );
        shutterType->populate();
        shutterType->setDefaultValue(0);
        {
            std::vector<std::string> options, helps;
            options.push_back("Centered");
            helps.push_back(kRotoShutterOffsetCenteredHint);
            options.push_back("Start");
            helps.push_back(kRotoShutterOffsetStartHint);
            options.push_back("End");
            helps.push_back(kRotoShutterOffsetEndHint);
            options.push_back("Custom");
            helps.push_back(kRotoShutterOffsetCustomHint);
            shutterType->populateChoices(options, helps);
        }
        knobs.push_back(shutterType);

        customOffset = KnobDouble::create(NULL, tr(kRotoShutterCustomOffsetParamLabel), 1, true) );
        customOffset->setName(kRotoPerShapeShutterCustomOffsetParam);
        customOffset->setHintToolTip( tr(kRotoShutterCustomOffsetParamHint) );
        customOffset->populate();
        customOffset->setDefaultValue(0);
        knobs.push_back(customOffset);
#endif

        overlayColor[0] = 0.85164;
        overlayColor[1] = 0.196936;
        overlayColor[2] = 0.196936;
        overlayColor[3] = 1.;
    }

    ~RotoDrawableItemPrivate()
    {
    }
};

struct RotoStrokeItemPrivate
{
    RotoStrokeType type;
    bool finished;
    struct StrokeCurves
    {
        CurvePtr xCurve, yCurve, pressureCurve;
    };

    /**
     * @brief A list of all storkes contained in this item. Basically each time penUp() is called it makes a new stroke
     **/
    std::vector<StrokeCurves> strokes;
    double curveT0; // timestamp of the first point in curve
    double lastTimestamp;
    RectD bbox;
    RectD wholeStrokeBboxWhilePainting;
    mutable QMutex strokeDotPatternsMutex;
    std::vector<cairo_pattern_t*> strokeDotPatterns;

    OSGLContextWPtr drawingGlCpuContext,drawingGlGpuContext;

    RotoStrokeItemPrivate(RotoStrokeType type)
        : type(type)
        , finished(false)
        , strokes()
        , curveT0(0)
        , lastTimestamp(0)
        , bbox()
        , wholeStrokeBboxWhilePainting()
        , strokeDotPatternsMutex()
        , strokeDotPatterns()
        , drawingGlCpuContext()
        , drawingGlGpuContext()
    {
        bbox.x1 = std::numeric_limits<double>::infinity();
        bbox.x2 = -std::numeric_limits<double>::infinity();
        bbox.y1 = std::numeric_limits<double>::infinity();
        bbox.y2 = -std::numeric_limits<double>::infinity();
    }
};

struct RotoContextPrivate
{
    Q_DECLARE_TR_FUNCTIONS(RotoContext)

public:
    mutable QMutex rotoContextMutex;

    /*
     * We have chosen to disable rotopainting and roto shapes from the same RotoContext because the rendering techniques are
     * very much differents. The rotopainting systems requires an entire compositing tree held inside whereas the rotoshapes
     * are rendered and optimized by Cairo internally.
     */
    bool isPaintNode;
    std::list< RotoLayerPtr > layers;
    bool autoKeying;
    bool rippleEdit;
    bool featherLink;
    bool isCurrentlyLoading;
    NodeWPtr node;
    U64 age;

    ///These are knobs that take the value of the selected splines info.
    ///Their value changes when selection changes.
    KnobDoubleWPtr opacity;
    KnobDoubleWPtr feather;
    KnobDoubleWPtr featherFallOff;
    KnobChoiceWPtr fallOffType;
    KnobChoiceWPtr lifeTime;
    KnobBoolWPtr activated; //<allows to disable a shape on a specific frame range
    KnobIntWPtr lifeTimeFrame;

#ifdef NATRON_ROTO_INVERTIBLE
    KnobBoolWPtr inverted;
#endif
    KnobColorWPtr colorKnob;
    KnobDoubleWPtr brushSizeKnob;
    KnobDoubleWPtr brushSpacingKnob;
    KnobDoubleWPtr brushHardnessKnob;
    KnobDoubleWPtr brushEffectKnob;
    KnobSeparatorWPtr pressureLabelKnob;
    KnobBoolWPtr pressureOpacityKnob;
    KnobBoolWPtr pressureSizeKnob;
    KnobBoolWPtr pressureHardnessKnob;
    KnobBoolWPtr buildUpKnob;
    KnobDoubleWPtr brushVisiblePortionKnob;
    KnobDoubleWPtr cloneTranslateKnob;
    KnobDoubleWPtr cloneRotateKnob;
    KnobDoubleWPtr cloneScaleKnob;
    KnobBoolWPtr cloneUniformKnob;
    KnobDoubleWPtr cloneSkewXKnob;
    KnobDoubleWPtr cloneSkewYKnob;
    KnobChoiceWPtr cloneSkewOrderKnob;
    KnobDoubleWPtr cloneCenterKnob;
    KnobButtonWPtr resetCloneCenterKnob;
    KnobChoiceWPtr cloneFilterKnob;
    KnobBoolWPtr cloneBlackOutsideKnob;
    KnobButtonWPtr resetCloneTransformKnob;
    KnobDoubleWPtr translateKnob;
    KnobDoubleWPtr rotateKnob;
    KnobDoubleWPtr scaleKnob;
    KnobBoolWPtr scaleUniformKnob;
    KnobBoolWPtr transformInteractiveKnob;
    KnobDoubleWPtr skewXKnob;
    KnobDoubleWPtr skewYKnob;
    KnobChoiceWPtr skewOrderKnob;
    KnobDoubleWPtr centerKnob;
    KnobButtonWPtr resetCenterKnob;
    KnobDoubleWPtr extraMatrixKnob;
    KnobButtonWPtr resetTransformKnob;
    KnobChoiceWPtr sourceTypeKnob;
    KnobIntWPtr timeOffsetKnob;
    KnobChoiceWPtr timeOffsetModeKnob;

#ifdef NATRON_ROTO_ENABLE_MOTION_BLUR
    KnobChoiceWPtr motionBlurTypeKnob;
    KnobDoubleWPtr motionBlurKnob, globalMotionBlurKnob;
    KnobDoubleWPtr shutterKnob, globalShutterKnob;
    KnobChoiceWPtr shutterTypeKnob, globalShutterTypeKnob;
    KnobDoubleWPtr customOffsetKnob, globalCustomOffsetKnob;
#endif

    std::list<KnobIWPtr > knobs; //< list for easy access to all knobs
    std::list<KnobIWPtr > cloneKnobs;
    std::list<KnobIWPtr > strokeKnobs;
    std::list<KnobIWPtr > shapeKnobs;

    ///This keeps track  of the items linked to the context knobs
    std::list<RotoItemPtr > selectedItems;
    RotoItemPtr lastInsertedItem;
    RotoItemPtr lastLockedItem;


    /*
     * A merge node (or more if there are more than 64 items) used when all items share the same compositing operator to make the rotopaint tree shallow
     */
    NodesList globalMergeNodes;

    RotoContextPrivate(const NodePtr& n )
        : rotoContextMutex()
        , isPaintNode(false)
        , layers()
        , autoKeying(true)
        , rippleEdit(false)
        , featherLink(true)
        , isCurrentlyLoading(false)
        , node(n)
        , age(0)
        , globalMergeNodes()
    {
        EffectInstancePtr effect = n->getEffectInstance();
        RotoPaint* isRotoNode = dynamic_cast<RotoPaint*>( effect.get() );

        if (isRotoNode) {
            isPaintNode = isRotoNode->isDefaultBehaviourPaintContext();
        } else {
            isPaintNode = false;
        }

        assert( n && n->getEffectInstance() );

        KnobPagePtr shapePage, strokePage, generalPage, clonePage, transformPage;

        generalPage = AppManager::createKnob<KnobPage>(effect, tr("General"), 1, true);
        shapePage = AppManager::createKnob<KnobPage>(effect, tr("Shape"), 1, true);
        strokePage = AppManager::createKnob<KnobPage>(effect, tr("Stroke"), 1, true);
        clonePage = AppManager::createKnob<KnobPage>(effect, tr("Clone"), 1, true);
        transformPage = AppManager::createKnob<KnobPage>(effect, tr("Transform"), 1, true);

        KnobDoublePtr opacityKnob = AppManager::createKnob<KnobDouble>(effect, tr(kRotoOpacityParamLabel), 1, true);
        opacityKnob->setHintToolTip( tr(kRotoOpacityHint) );
        opacityKnob->setName(kRotoOpacityParam);
        opacityKnob->setMinimum(0.);
        opacityKnob->setMaximum(1.);
        opacityKnob->setDisplayMinimum(0.);
        opacityKnob->setDisplayMaximum(1.);
        opacityKnob->setDefaultValue(ROTO_DEFAULT_OPACITY);
        opacityKnob->setDefaultAllDimensionsEnabled(false);
        opacityKnob->setIsPersistent(false);
        generalPage->addKnob(opacityKnob);
        knobs.push_back(opacityKnob);
        opacity = opacityKnob;

        KnobColorPtr ck = AppManager::createKnob<KnobColor>(effect, tr(kRotoColorParamLabel), 3, true);
        ck->setHintToolTip( tr(kRotoColorHint) );
        ck->setName(kRotoColorParam);
        ck->setDefaultValue(ROTO_DEFAULT_COLOR_R, 0);
        ck->setDefaultValue(ROTO_DEFAULT_COLOR_G, 1);
        ck->setDefaultValue(ROTO_DEFAULT_COLOR_B, 2);
        ck->setDefaultAllDimensionsEnabled(false);
        generalPage->addKnob(ck);
        ck->setIsPersistent(false);
        knobs.push_back(ck);
        colorKnob = ck;

        KnobChoicePtr lifeTimeKnob = AppManager::createKnob<KnobChoice>(effect, tr(kRotoDrawableItemLifeTimeParamLabel), 1, true);
        lifeTimeKnob->setHintToolTip( tr(kRotoDrawableItemLifeTimeParamHint) );
        lifeTimeKnob->setName(kRotoDrawableItemLifeTimeParam);
        lifeTimeKnob->setAddNewLine(false);
        lifeTimeKnob->setIsPersistent(false);
        lifeTimeKnob->setDefaultAllDimensionsEnabled(false);
        lifeTimeKnob->setAnimationEnabled(false);
        {
            std::vector<std::string> choices, helps;
            choices.push_back(kRotoDrawableItemLifeTimeSingle);
            helps.push_back( tr(kRotoDrawableItemLifeTimeSingleHelp).toStdString() );
            choices.push_back(kRotoDrawableItemLifeTimeFromStart);
            helps.push_back( tr(kRotoDrawableItemLifeTimeFromStartHelp).toStdString() );
            choices.push_back(kRotoDrawableItemLifeTimeToEnd);
            helps.push_back( tr(kRotoDrawableItemLifeTimeToEndHelp).toStdString() );
            choices.push_back(kRotoDrawableItemLifeTimeCustom);
            helps.push_back( tr(kRotoDrawableItemLifeTimeCustomHelp).toStdString() );
            lifeTimeKnob->populateChoices(choices, helps);
        }
        lifeTimeKnob->setDefaultValue(isPaintNode ? 0 : 3);
        generalPage->addKnob(lifeTimeKnob);
        knobs.push_back(lifeTimeKnob);
        lifeTime = lifeTimeKnob;

        KnobIntPtr lifeTimeFrameKnob = AppManager::createKnob<KnobInt>(effect, tr(kRotoDrawableItemLifeTimeFrameParamLabel), 1, true);
        lifeTimeFrameKnob->setHintToolTip( tr(kRotoDrawableItemLifeTimeFrameParamHint) );
        lifeTimeFrameKnob->setName(kRotoDrawableItemLifeTimeFrameParam);
        lifeTimeFrameKnob->setSecretByDefault(!isPaintNode);
        lifeTimeFrameKnob->setDefaultAllDimensionsEnabled(false);
        lifeTimeFrameKnob->setAddNewLine(false);
        lifeTimeFrameKnob->setAnimationEnabled(false);
        generalPage->addKnob(lifeTimeFrameKnob);
        knobs.push_back(lifeTimeFrameKnob);
        lifeTimeFrame = lifeTimeFrameKnob;

        KnobBoolPtr activatedKnob = AppManager::createKnob<KnobBool>(effect, tr(kRotoActivatedParamLabel), 1, true);
        activatedKnob->setHintToolTip( tr(kRotoActivatedHint) );
        activatedKnob->setName(kRotoActivatedParam);
        activatedKnob->setAddNewLine(true);
        activatedKnob->setSecretByDefault(isPaintNode);
        activatedKnob->setDefaultValue(true);
        activatedKnob->setDefaultAllDimensionsEnabled(false);
        generalPage->addKnob(activatedKnob);
        activatedKnob->setIsPersistent(false);
        knobs.push_back(activatedKnob);
        activated = activatedKnob;

#ifdef NATRON_ROTO_INVERTIBLE
        KnobBoolPtr invertedKnob = AppManager::createKnob<KnobBool>(effect, tr(kRotoInvertedParamLabel), 1, true);
        invertedKnob->setHintToolTip( tr(kRotoInvertedHint) );
        invertedKnob->setName(kRotoInvertedParam);
        invertedKnob->setDefaultValue(false);
        invertedKnob->setDefaultAllDimensionsEnabled(false);
        invertedKnob->setIsPersistent(false);
        generalPage->addKnob(invertedKnob);
        knobs.push_back(invertedKnob);
        inverted = invertedKnob;
#endif

        KnobDoublePtr featherKnob = AppManager::createKnob<KnobDouble>(effect, tr(kRotoFeatherParamLabel), 1, true);
        featherKnob->setHintToolTip( tr(kRotoFeatherHint) );
        featherKnob->setName(kRotoFeatherParam);
        featherKnob->setMinimum(0);
        featherKnob->setDisplayMinimum(0);
        featherKnob->setDisplayMaximum(500);
        featherKnob->setDefaultValue(ROTO_DEFAULT_FEATHER);
        featherKnob->setDefaultAllDimensionsEnabled(false);
        featherKnob->setIsPersistent(false);
        shapePage->addKnob(featherKnob);
        knobs.push_back(featherKnob);
        shapeKnobs.push_back(featherKnob);
        feather = featherKnob;

        KnobDoublePtr featherFallOffKnob = AppManager::createKnob<KnobDouble>(effect, tr(kRotoFeatherFallOffParamLabel), 1, true);
        featherFallOffKnob->setHintToolTip( tr(kRotoFeatherFallOffHint) );
        featherFallOffKnob->setName(kRotoFeatherFallOffParam);
        featherFallOffKnob->setMinimum(0.001);
        featherFallOffKnob->setMaximum(5.);
        featherFallOffKnob->setDisplayMinimum(0.2);
        featherFallOffKnob->setDisplayMaximum(5.);
        featherFallOffKnob->setDefaultValue(ROTO_DEFAULT_FEATHERFALLOFF);
        featherFallOffKnob->setDefaultAllDimensionsEnabled(false);
        featherFallOffKnob->setIsPersistent(false);
        featherFallOffKnob->setAddNewLine(false);
        shapePage->addKnob(featherFallOffKnob);
        knobs.push_back(featherFallOffKnob);
        shapeKnobs.push_back(featherFallOffKnob);
        featherFallOff = featherFallOffKnob;


        KnobChoicePtr fallOffRampTypeKnob = AppManager::createKnob<KnobChoice>(effect, tr(kRotoFeatherFallOffTypeLabel), 1, true);
        fallOffRampTypeKnob->setHintToolTip( tr(kRotoFeatherFallOffTypeHint) );
        fallOffRampTypeKnob->setName(kRotoFeatherFallOffType);
        fallOffRampTypeKnob->populate();
        fallOffRampTypeKnob->setDefaultAllDimensionsEnabled(false);
        fallOffRampTypeKnob->setIsPersistent(false);
        {
            std::vector<std::string> entries,helps;
            entries.push_back(kRotoFeatherFallOffTypeLinear);
            helps.push_back(kRotoFeatherFallOffTypeLinearHint);
            entries.push_back(kRotoFeatherFallOffTypePLinear);
            helps.push_back(kRotoFeatherFallOffTypePLinearHint);
            entries.push_back(kRotoFeatherFallOffTypeEaseIn);
            helps.push_back(kRotoFeatherFallOffTypeEaseInHint);
            entries.push_back(kRotoFeatherFallOffTypeEaseOut);
            helps.push_back(kRotoFeatherFallOffTypeEaseOutHint);
            entries.push_back(kRotoFeatherFallOffTypeSmooth);
            helps.push_back(kRotoFeatherFallOffTypeSmoothHint);
            fallOffRampTypeKnob->populateChoices(entries, helps);
        }
        shapePage->addKnob(fallOffRampTypeKnob);
        fallOffType = fallOffRampTypeKnob;
        shapeKnobs.push_back(fallOffRampTypeKnob);
        knobs.push_back(fallOffRampTypeKnob);


        {

            KnobChoicePtr sourceType = AppManager::createKnob<KnobChoice>(effect, tr(kRotoBrushSourceColorLabel), 1, true);
            sourceType->setName(kRotoBrushSourceColor);
            sourceType->setHintToolTip( tr(kRotoBrushSourceColorHint) );
            sourceType->setDefaultValue(1);
            {
                std::vector<std::string> choices;
                choices.push_back("foreground");
                choices.push_back("background");
                for (int i = 1; i < 10; ++i) {
                    std::stringstream ss;
                    ss << "background " << i + 1;
                    choices.push_back( ss.str() );
                }
                sourceType->populateChoices(choices);
            }
            sourceType->setDefaultAllDimensionsEnabled(false);
            clonePage->addKnob(sourceType);
            knobs.push_back(sourceType);
            cloneKnobs.push_back(sourceType);
            sourceTypeKnob = sourceType;

            KnobDoublePtr translate = AppManager::createKnob<KnobDouble>(effect, tr(kRotoBrushTranslateParamLabel), 2, true);
            translate->setName(kRotoBrushTranslateParam);
            translate->setHintToolTip( tr(kRotoBrushTranslateParamHint) );
            translate->setDefaultAllDimensionsEnabled(false);
            translate->setIncrement(10);
            clonePage->addKnob(translate);
            knobs.push_back(translate);
            cloneKnobs.push_back(translate);
            cloneTranslateKnob = translate;

            KnobDoublePtr rotate = AppManager::createKnob<KnobDouble>(effect, tr(kRotoBrushRotateParamLabel), 1, true);
            rotate->setName(kRotoBrushRotateParam);
            rotate->setHintToolTip( tr(kRotoBrushRotateParamHint) );
            rotate->setDefaultAllDimensionsEnabled(false);
            rotate->setDisplayMinimum(-180);
            rotate->setDisplayMaximum(180);
            clonePage->addKnob(rotate);
            knobs.push_back(rotate);
            cloneKnobs.push_back(rotate);
            cloneRotateKnob = rotate;

            KnobDoublePtr scale = AppManager::createKnob<KnobDouble>(effect, tr(kRotoBrushScaleParamLabel), 2, true);
            scale->setName(kRotoBrushScaleParam);
            scale->setHintToolTip( tr(kRotoBrushScaleParamHint) );
            scale->setDefaultValue(1, 0);
            scale->setDefaultValue(1, 1);
            scale->setDisplayMinimum(0.1, 0);
            scale->setDisplayMinimum(0.1, 1);
            scale->setDisplayMaximum(10, 0);
            scale->setDisplayMaximum(10, 1);
            scale->setAddNewLine(false);
            scale->setDefaultAllDimensionsEnabled(false);
            clonePage->addKnob(scale);
            cloneKnobs.push_back(scale);
            knobs.push_back(scale);
            cloneScaleKnob = scale;

            KnobBoolPtr scaleUniform = AppManager::createKnob<KnobBool>(effect, tr(kRotoBrushScaleUniformParamLabel), 1, true);
            scaleUniform->setName(kRotoBrushScaleUniformParam);
            scaleUniform->setHintToolTip( tr(kRotoBrushScaleUniformParamHint) );
            scaleUniform->setDefaultValue(true);
            scaleUniform->setDefaultAllDimensionsEnabled(false);
            scaleUniform->setAnimationEnabled(false);
            clonePage->addKnob(scaleUniform);
            cloneKnobs.push_back(scaleUniform);
            knobs.push_back(scaleUniform);
            cloneUniformKnob = scaleUniform;

            KnobDoublePtr skewX = AppManager::createKnob<KnobDouble>(effect, tr(kRotoBrushSkewXParamLabel), 1, true);
            skewX->setName(kRotoBrushSkewXParam);
            skewX->setHintToolTip( tr(kRotoBrushSkewXParamHint) );
            skewX->setDefaultAllDimensionsEnabled(false);
            skewX->setDisplayMinimum(-1, 0);
            skewX->setDisplayMaximum(1, 0);
            cloneKnobs.push_back(skewX);
            clonePage->addKnob(skewX);
            knobs.push_back(skewX);
            cloneSkewXKnob = skewX;

            KnobDoublePtr skewY = AppManager::createKnob<KnobDouble>(effect, tr(kRotoBrushSkewYParamLabel), 1, true);
            skewY->setName(kRotoBrushSkewYParam);
            skewY->setHintToolTip( tr(kRotoBrushSkewYParamHint) );
            skewY->setDefaultAllDimensionsEnabled(false);
            skewY->setDisplayMinimum(-1, 0);
            skewY->setDisplayMaximum(1, 0);
            clonePage->addKnob(skewY);
            cloneKnobs.push_back(skewY);
            knobs.push_back(skewY);
            cloneSkewYKnob = skewY;

            KnobChoicePtr skewOrder = AppManager::createKnob<KnobChoice>(effect, tr(kRotoBrushSkewOrderParamLabel), 1, true);
            skewOrder->setName(kRotoBrushSkewOrderParam);
            skewOrder->setHintToolTip( tr(kRotoBrushSkewOrderParamHint) );
            skewOrder->setDefaultValue(0);
            {
                std::vector<std::string> choices;
                choices.push_back("XY");
                choices.push_back("YX");
                skewOrder->populateChoices(choices);
            }
            skewOrder->setDefaultAllDimensionsEnabled(false);
            skewOrder->setAnimationEnabled(false);
            clonePage->addKnob(skewOrder);
            cloneKnobs.push_back(skewOrder);
            knobs.push_back(skewOrder);
            cloneSkewOrderKnob = skewOrder;

            KnobDoublePtr center = AppManager::createKnob<KnobDouble>(effect, tr(kRotoBrushCenterParamLabel), 2, true);
            center->setName(kRotoBrushCenterParam);
            center->setHintToolTip( tr(kRotoBrushCenterParamHint) );
            center->setDefaultAllDimensionsEnabled(false);
            center->setDefaultValuesAreNormalized(true);
            center->setAddNewLine(false);
            center->setDefaultValue(0.5, 0);
            center->setDefaultValue(0.5, 1);
            clonePage->addKnob(center);
            cloneKnobs.push_back(center);
            knobs.push_back(center);
            cloneCenterKnob = center;

            KnobButtonPtr resetCloneCenter = AppManager::createKnob<KnobButton>(effect, tr(kRotoResetCloneCenterParamLabel), 1, true);
            resetCloneCenter->setName(kRotoResetCloneCenterParam);
            resetCloneCenter->setHintToolTip( tr(kRotoResetCloneCenterParamHint) );
            resetCloneCenter->setAllDimensionsEnabled(false);
            clonePage->addKnob(resetCloneCenter);
            cloneKnobs.push_back(resetCloneCenter);
            knobs.push_back(resetCloneCenter);
            resetCloneCenterKnob = resetCloneCenter;


            KnobButtonPtr resetCloneTransform = AppManager::createKnob<KnobButton>(effect, tr(kRotoResetCloneTransformParamLabel), 1, true);
            resetCloneTransform->setName(kRotoResetCloneTransformParam);
            resetCloneTransform->setHintToolTip( tr(kRotoResetCloneTransformParamHint) );
            resetCloneTransform->setAllDimensionsEnabled(false);
            clonePage->addKnob(resetCloneTransform);
            cloneKnobs.push_back(resetCloneTransform);
            knobs.push_back(resetCloneTransform);
            resetCloneTransformKnob = resetCloneTransform;

            node.lock()->addTransformInteract(translate, scale, scaleUniform, rotate, skewX, skewY, skewOrder, center,
                                              KnobBoolPtr() /*invert*/,
                                              KnobBoolPtr() /*interactive*/);

            KnobChoicePtr filter = AppManager::createKnob<KnobChoice>(effect, tr(kRotoBrushFilterParamLabel), 1, true);
            filter->setName(kRotoBrushFilterParam);
            filter->setHintToolTip( tr(kRotoBrushFilterParamHint) );
            {
                std::vector<std::string> choices, helps;

                choices.push_back(kFilterImpulse);
                helps.push_back(kFilterImpulseHint);
                choices.push_back(kFilterBilinear);
                helps.push_back(kFilterBilinearHint);
                choices.push_back(kFilterCubic);
                helps.push_back(kFilterCubicHint);
                choices.push_back(kFilterKeys);
                helps.push_back(kFilterKeysHint);
                choices.push_back(kFilterSimon);
                helps.push_back(kFilterSimonHint);
                choices.push_back(kFilterRifman);
                helps.push_back(kFilterRifmanHint);
                choices.push_back(kFilterMitchell);
                helps.push_back(kFilterMitchellHint);
                choices.push_back(kFilterParzen);
                helps.push_back(kFilterParzenHint);
                choices.push_back(kFilterNotch);
                helps.push_back(kFilterNotchHint);
                filter->populateChoices(choices);
            }
            filter->setDefaultValue(2);
            filter->setDefaultAllDimensionsEnabled(false);
            filter->setAddNewLine(false);
            clonePage->addKnob(filter);
            cloneKnobs.push_back(filter);
            knobs.push_back(filter);
            cloneFilterKnob = filter;

            KnobBoolPtr blackOutside = AppManager::createKnob<KnobBool>(effect, tr(kRotoBrushBlackOutsideParamLabel), 1, true);
            blackOutside->setName(kRotoBrushBlackOutsideParam);
            blackOutside->setHintToolTip( tr(kRotoBrushBlackOutsideParamHint) );
            blackOutside->setDefaultValue(true);
            blackOutside->setDefaultAllDimensionsEnabled(false);
            clonePage->addKnob(blackOutside);
            knobs.push_back(blackOutside);
            cloneKnobs.push_back(blackOutside);
            cloneBlackOutsideKnob = blackOutside;

            KnobIntPtr timeOffset = AppManager::createKnob<KnobInt>(effect, tr(kRotoBrushTimeOffsetParamLabel), 1, true);
            timeOffset->setName(kRotoBrushTimeOffsetParam);
            timeOffset->setHintToolTip( tr(kRotoBrushTimeOffsetParamHint) );
            timeOffset->setDisplayMinimum(-100);
            timeOffset->setDisplayMaximum(100);
            timeOffset->setDefaultAllDimensionsEnabled(false);
            timeOffset->setIsPersistent(false);
            timeOffset->setAddNewLine(false);
            clonePage->addKnob(timeOffset);
            cloneKnobs.push_back(timeOffset);
            knobs.push_back(timeOffset);
            timeOffsetKnob = timeOffset;

            KnobChoicePtr timeOffsetMode = AppManager::createKnob<KnobChoice>(effect, tr(kRotoBrushTimeOffsetModeParamLabel), 1, true);
            timeOffsetMode->setName(kRotoBrushTimeOffsetModeParam);
            timeOffsetMode->setHintToolTip( tr(kRotoBrushTimeOffsetModeParamHint) );
            {
                std::vector<std::string> modes;
                modes.push_back("Relative");
                modes.push_back("Absolute");
                timeOffsetMode->populateChoices(modes);
            }
            timeOffsetMode->setDefaultAllDimensionsEnabled(false);
            timeOffsetMode->setIsPersistent(false);
            clonePage->addKnob(timeOffsetMode);
            knobs.push_back(timeOffsetMode);
            cloneKnobs.push_back(timeOffsetMode);
            timeOffsetModeKnob = timeOffsetMode;

            KnobDoublePtr brushSize = AppManager::createKnob<KnobDouble>(effect, tr(kRotoBrushSizeParamLabel), 1, true);
            brushSize->setName(kRotoBrushSizeParam);
            brushSize->setHintToolTip( tr(kRotoBrushSizeParamHint) );
            brushSize->setDefaultValue(25);
            brushSize->setMinimum(1.);
            brushSize->setMaximum(1000);
            brushSize->setDefaultAllDimensionsEnabled(false);
            brushSize->setIsPersistent(false);
            strokePage->addKnob(brushSize);
            knobs.push_back(brushSize);
            strokeKnobs.push_back(brushSize);
            brushSizeKnob = brushSize;

            KnobDoublePtr brushSpacing = AppManager::createKnob<KnobDouble>(effect, tr(kRotoBrushSpacingParamLabel), 1, true);
            brushSpacing->setName(kRotoBrushSpacingParam);
            brushSpacing->setHintToolTip( tr(kRotoBrushSpacingParamHint) );
            brushSpacing->setDefaultValue(0.1);
            brushSpacing->setMinimum(0.);
            brushSpacing->setMaximum(1.);
            brushSpacing->setDefaultAllDimensionsEnabled(false);
            brushSpacing->setIsPersistent(false);
            strokePage->addKnob(brushSpacing);
            knobs.push_back(brushSpacing);
            strokeKnobs.push_back(brushSpacing);
            brushSpacingKnob = brushSpacing;

            KnobDoublePtr brushHardness = AppManager::createKnob<KnobDouble>(effect, tr(kRotoBrushHardnessParamLabel), 1, true);
            brushHardness->setName(kRotoBrushHardnessParam);
            brushHardness->setHintToolTip( tr(kRotoBrushHardnessParamHint) );
            brushHardness->setDefaultValue(0.2);
            brushHardness->setMinimum(0.);
            brushHardness->setMaximum(1.);
            brushHardness->setDefaultAllDimensionsEnabled(false);
            brushHardness->setIsPersistent(false);
            strokePage->addKnob(brushHardness);
            knobs.push_back(brushHardness);
            strokeKnobs.push_back(brushHardness);
            brushHardnessKnob = brushHardness;

            KnobDoublePtr effectStrength = AppManager::createKnob<KnobDouble>(effect, tr(kRotoBrushEffectParamLabel), 1, true);
            effectStrength->setName(kRotoBrushEffectParam);
            effectStrength->setHintToolTip( tr(kRotoBrushEffectParamHint) );
            effectStrength->setDefaultValue(15);
            effectStrength->setMinimum(0.);
            effectStrength->setMaximum(100.);
            effectStrength->setDefaultAllDimensionsEnabled(false);
            effectStrength->setIsPersistent(false);
            strokePage->addKnob(effectStrength);
            knobs.push_back(effectStrength);
            strokeKnobs.push_back(effectStrength);
            brushEffectKnob = effectStrength;

            KnobSeparatorPtr pressureLabel = AppManager::createKnob<KnobSeparator>( effect, tr(kRotoBrushPressureLabelParamLabel) );
            pressureLabel->setName(kRotoBrushPressureLabelParam);
            pressureLabel->setHintToolTip( tr(kRotoBrushPressureLabelParamHint) );
            strokePage->addKnob(pressureLabel);
            knobs.push_back(pressureLabel);
            strokeKnobs.push_back(pressureLabel);
            pressureLabelKnob = pressureLabel;

            KnobBoolPtr pressureOpacity = AppManager::createKnob<KnobBool>( effect, tr(kRotoBrushPressureOpacityParamLabel) );
            pressureOpacity->setName(kRotoBrushPressureOpacityParam);
            pressureOpacity->setHintToolTip( tr(kRotoBrushPressureOpacityParamHint) );
            pressureOpacity->setAnimationEnabled(false);
            pressureOpacity->setDefaultValue(true);
            pressureOpacity->setAddNewLine(false);
            pressureOpacity->setDefaultAllDimensionsEnabled(false);
            pressureOpacity->setIsPersistent(false);
            strokePage->addKnob(pressureOpacity);
            knobs.push_back(pressureOpacity);
            strokeKnobs.push_back(pressureOpacity);
            pressureOpacityKnob = pressureOpacity;

            KnobBoolPtr pressureSize = AppManager::createKnob<KnobBool>( effect, tr(kRotoBrushPressureSizeParamLabel) );
            pressureSize->setName(kRotoBrushPressureSizeParam);
            pressureSize->setHintToolTip( tr(kRotoBrushPressureSizeParamHint) );
            pressureSize->setAnimationEnabled(false);
            pressureSize->setDefaultValue(false);
            pressureSize->setAddNewLine(false);
            pressureSize->setDefaultAllDimensionsEnabled(false);
            pressureSize->setIsPersistent(false);
            knobs.push_back(pressureSize);
            strokeKnobs.push_back(pressureSize);
            strokePage->addKnob(pressureSize);
            pressureSizeKnob = pressureSize;

            KnobBoolPtr pressureHardness = AppManager::createKnob<KnobBool>( effect, tr(kRotoBrushPressureHardnessParamLabel) );
            pressureHardness->setName(kRotoBrushPressureHardnessParam);
            pressureHardness->setHintToolTip( tr(kRotoBrushPressureHardnessParamHint) );
            pressureHardness->setAnimationEnabled(false);
            pressureHardness->setDefaultValue(false);
            pressureHardness->setAddNewLine(true);
            pressureHardness->setDefaultAllDimensionsEnabled(false);
            pressureHardness->setIsPersistent(false);
            knobs.push_back(pressureHardness);
            strokeKnobs.push_back(pressureHardness);
            strokePage->addKnob(pressureHardness);
            pressureHardnessKnob = pressureHardness;

            KnobBoolPtr buildUp = AppManager::createKnob<KnobBool>( effect, tr(kRotoBrushBuildupParamLabel) );
            buildUp->setName(kRotoBrushBuildupParam);
            buildUp->setHintToolTip( tr(kRotoBrushBuildupParamHint) );
            buildUp->setAnimationEnabled(false);
            buildUp->setDefaultValue(false);
            buildUp->setAddNewLine(true);
            buildUp->setDefaultAllDimensionsEnabled(false);
            buildUp->setIsPersistent(false);
            knobs.push_back(buildUp);
            strokeKnobs.push_back(buildUp);
            strokePage->addKnob(buildUp);
            buildUpKnob = buildUp;

            KnobDoublePtr visiblePortion = AppManager::createKnob<KnobDouble>(effect, tr(kRotoBrushVisiblePortionParamLabel), 2, true);
            visiblePortion->setName(kRotoBrushVisiblePortionParam);
            visiblePortion->setHintToolTip( tr(kRotoBrushVisiblePortionParamHint) );
            visiblePortion->setDefaultValue(0, 0);
            visiblePortion->setDefaultValue(1, 1);
            std::vector<double> mins, maxs;
            mins.push_back(0);
            mins.push_back(0);
            maxs.push_back(1);
            maxs.push_back(1);
            visiblePortion->setMinimumsAndMaximums(mins, maxs);
            visiblePortion->setDefaultAllDimensionsEnabled(false);
            visiblePortion->setIsPersistent(false);
            strokePage->addKnob(visiblePortion);
            visiblePortion->setDimensionName(0, "start");
            visiblePortion->setDimensionName(1, "end");
            knobs.push_back(visiblePortion);
            strokeKnobs.push_back(visiblePortion);
            brushVisiblePortionKnob = visiblePortion;
        }

        KnobDoublePtr translate = AppManager::createKnob<KnobDouble>(effect, tr(kRotoDrawableItemTranslateParamLabel), 2, true);
        translate->setName(kRotoDrawableItemTranslateParam);
        translate->setHintToolTip( tr(kRotoDrawableItemTranslateParamHint) );
        translate->setDefaultAllDimensionsEnabled(false);
        translate->setIncrement(10);
        transformPage->addKnob(translate);
        knobs.push_back(translate);
        translateKnob = translate;

        KnobDoublePtr rotate = AppManager::createKnob<KnobDouble>(effect, tr(kRotoDrawableItemRotateParamLabel), 1, true);
        rotate->setName(kRotoDrawableItemRotateParam);
        rotate->setHintToolTip( tr(kRotoDrawableItemRotateParamHint) );
        rotate->setDefaultAllDimensionsEnabled(false);
        rotate->setDisplayMinimum(-180);
        rotate->setDisplayMaximum(180);
        transformPage->addKnob(rotate);
        knobs.push_back(rotate);
        rotateKnob = rotate;

        KnobDoublePtr scale = AppManager::createKnob<KnobDouble>(effect, tr(kRotoDrawableItemScaleParamLabel), 2, true);
        scale->setName(kRotoDrawableItemScaleParam);
        scale->setHintToolTip( tr(kRotoDrawableItemScaleParamHint) );
        scale->setDefaultValue(1, 0);
        scale->setDefaultValue(1, 1);
        scale->setDisplayMinimum(0.1, 0);
        scale->setDisplayMinimum(0.1, 1);
        scale->setDisplayMaximum(10, 0);
        scale->setDisplayMaximum(10, 1);
        scale->setAddNewLine(false);
        scale->setDefaultAllDimensionsEnabled(false);
        transformPage->addKnob(scale);
        knobs.push_back(scale);
        scaleKnob = scale;

        KnobBoolPtr scaleUniform = AppManager::createKnob<KnobBool>(effect, tr(kRotoDrawableItemScaleUniformParamLabel), 1, true);
        scaleUniform->setName(kRotoDrawableItemScaleUniformParam);
        scaleUniform->setHintToolTip( tr(kRotoDrawableItemScaleUniformParamHint) );
        scaleUniform->setDefaultValue(true);
        scaleUniform->setDefaultAllDimensionsEnabled(false);
        scaleUniform->setAnimationEnabled(false);
        transformPage->addKnob(scaleUniform);
        knobs.push_back(scaleUniform);
        scaleUniformKnob = scaleUniform;

        KnobDoublePtr skewX = AppManager::createKnob<KnobDouble>(effect, tr(kRotoDrawableItemSkewXParamLabel), 1, true);
        skewX->setName(kRotoDrawableItemSkewXParam);
        skewX->setHintToolTip( tr(kRotoDrawableItemSkewXParamHint) );
        skewX->setDefaultAllDimensionsEnabled(false);
        skewX->setDisplayMinimum(-1, 0);
        skewX->setDisplayMaximum(1, 0);
        transformPage->addKnob(skewX);
        knobs.push_back(skewX);
        skewXKnob = skewX;

        KnobDoublePtr skewY = AppManager::createKnob<KnobDouble>(effect, tr(kRotoDrawableItemSkewYParamLabel), 1, true);
        skewY->setName(kRotoDrawableItemSkewYParam);
        skewY->setHintToolTip( tr(kRotoDrawableItemSkewYParamHint) );
        skewY->setDefaultAllDimensionsEnabled(false);
        skewY->setDisplayMinimum(-1, 0);
        skewY->setDisplayMaximum(1, 0);
        transformPage->addKnob(skewY);
        knobs.push_back(skewY);
        skewYKnob = skewY;

        KnobChoicePtr skewOrder = AppManager::createKnob<KnobChoice>(effect, tr(kRotoDrawableItemSkewOrderParamLabel), 1, true);
        skewOrder->setName(kRotoDrawableItemSkewOrderParam);
        skewOrder->setHintToolTip( tr(kRotoDrawableItemSkewOrderParamHint) );
        skewOrder->setDefaultValue(0);
        {
            std::vector<std::string> choices;
            choices.push_back("XY");
            choices.push_back("YX");
            skewOrder->populateChoices(choices);
        }
        skewOrder->setDefaultAllDimensionsEnabled(false);
        skewOrder->setAnimationEnabled(false);
        transformPage->addKnob(skewOrder);
        knobs.push_back(skewOrder);
        skewOrderKnob = skewOrder;

        KnobDoublePtr center = AppManager::createKnob<KnobDouble>(effect, tr(kRotoDrawableItemCenterParamLabel), 2, true);
        center->setName(kRotoDrawableItemCenterParam);
        center->setHintToolTip( tr(kRotoDrawableItemCenterParamHint) );
        center->setDefaultAllDimensionsEnabled(false);
        center->setDefaultValuesAreNormalized(true);
        center->setAddNewLine(false);
        center->setDefaultValue(0.5, 0);
        center->setDefaultValue(0.5, 1);
        transformPage->addKnob(center);
        knobs.push_back(center);
        centerKnob = center;

        KnobButtonPtr resetCenter = AppManager::createKnob<KnobButton>(effect, tr(kRotoResetCenterParamLabel), 1, true);
        resetCenter->setName(kRotoResetCenterParam);
        resetCenter->setHintToolTip( tr(kRotoResetCenterParamHint) );
        resetCenter->setAllDimensionsEnabled(false);
        transformPage->addKnob(resetCenter);
        knobs.push_back(resetCenter);
        resetCenterKnob = resetCenter;

        KnobBoolPtr transformInteractive = AppManager::createKnob<KnobBool>(effect, tr(kRotoTransformInteractiveLabel), 1, true);
        transformInteractive->setName(kRotoTransformInteractive);
        transformInteractive->setHintToolTip(tr(kRotoTransformInteractiveHint));
        transformInteractive->setDefaultValue(true);
        transformInteractive->setAllDimensionsEnabled(false);
        transformPage->addKnob(transformInteractive);
        knobs.push_back(transformInteractive);
        transformInteractiveKnob = transformInteractive;



        KnobDoublePtr extraMatrix = AppManager::createKnob<KnobDouble>(effect, tr(kRotoDrawableItemExtraMatrixParamLabel), 9, true);
        extraMatrix->setName(kRotoDrawableItemExtraMatrixParam);
        extraMatrix->setHintToolTip( tr(kRotoDrawableItemExtraMatrixParamHint) );
        extraMatrix->setDefaultAllDimensionsEnabled(false);
        // Set to identity
        extraMatrix->setDefaultValue(1, 0);
        extraMatrix->setDefaultValue(1, 4);
        extraMatrix->setDefaultValue(1, 8);
        transformPage->addKnob(extraMatrix);
        knobs.push_back(extraMatrix);
        extraMatrixKnob = extraMatrix;

        KnobButtonPtr resetTransform = AppManager::createKnob<KnobButton>(effect, tr(kRotoResetTransformParamLabel), 1, true);
        resetTransform->setName(kRotoResetTransformParam);
        resetTransform->setHintToolTip( tr(kRotoResetTransformParamHint) );
        resetTransform->setAllDimensionsEnabled(false);
        transformPage->addKnob(resetTransform);
        knobs.push_back(resetTransform);
        resetTransformKnob = resetTransform;

        node.lock()->addTransformInteract(translate, scale, scaleUniform, rotate, skewX, skewY, skewOrder, center,
                                          KnobBoolPtr() /*invert*/,
                                          transformInteractive /*interactive*/);


#ifdef NATRON_ROTO_ENABLE_MOTION_BLUR
        KnobPagePtr mbPage = AppManager::createKnob<KnobPage>(effect, tr("Motion Blur"), 1, true);
        KnobChoicePtr motionBlurType = AppManager::createKnob<KnobChoice>(effect, tr(kRotoMotionBlurModeParamLabel), 1, true);
        motionBlurType->setName(kRotoMotionBlurModeParam);
        motionBlurType->setHintToolTip( tr(kRotoMotionBlurModeParamHint) );
        motionBlurType->setAnimationEnabled(false);
        {
            std::vector<std::string> entries;
            entries.push_back("Per-Shape");
            entries.push_back("Global");
            motionBlurType->populateChoices(entries);
        }
        mbPage->addKnob(motionBlurType);
        motionBlurTypeKnob = motionBlurType;
        knobs.push_back(motionBlurType);


        //////Per shape motion blur parameters
        KnobDoublePtr motionBlur = AppManager::createKnob<KnobDouble>(effect, tr(kRotoMotionBlurParamLabel), 1, true);
        motionBlur->setName(kRotoPerShapeMotionBlurParam);
        motionBlur->setHintToolTip( tr(kRotoMotionBlurParamHint) );
        motionBlur->setDefaultValue(0);
        motionBlur->setMinimum(0);
        motionBlur->setDisplayMinimum(0);
        motionBlur->setDisplayMaximum(4);
        motionBlur->setAllDimensionsEnabled(false);
        motionBlur->setIsPersistent(false);
        motionBlur->setMaximum(4);
        shapeKnobs.push_back(motionBlur);
        mbPage->addKnob(motionBlur);
        motionBlurKnob = motionBlur;
        knobs.push_back(motionBlur);

        KnobDoublePtr shutter = AppManager::createKnob<KnobDouble>(effect, tr(kRotoShutterParamLabel), 1, true);
        shutter->setName(kRotoPerShapeShutterParam);
        shutter->setHintToolTip( tr(kRotoShutterParamHint) );
        shutter->setDefaultValue(0.5);
        shutter->setMinimum(0);
        shutter->setDisplayMinimum(0);
        shutter->setDisplayMaximum(2);
        shutter->setMaximum(2);
        shutter->setAllDimensionsEnabled(false);
        shutter->setIsPersistent(false);
        shapeKnobs.push_back(shutter);
        mbPage->addKnob(shutter);
        shutterKnob = shutter;
        knobs.push_back(shutter);

        KnobChoicePtr shutterType = AppManager::createKnob<KnobChoice>(effect, tr(kRotoShutterOffsetTypeParamLabel), 1, true);
        shutterType->setName(kRotoPerShapeShutterOffsetTypeParam);
        shutterType->setHintToolTip( tr(kRotoShutterOffsetTypeParamHint) );
        shutterType->setDefaultValue(0);
        {
            std::vector<std::string> options, helps;
            options.push_back("Centered");
            helps.push_back(kRotoShutterOffsetCenteredHint);
            options.push_back("Start");
            helps.push_back(kRotoShutterOffsetStartHint);
            options.push_back("End");
            helps.push_back(kRotoShutterOffsetEndHint);
            options.push_back("Custom");
            helps.push_back(kRotoShutterOffsetCustomHint);
            shutterType->populateChoices(options, helps);
        }
        shutterType->setAllDimensionsEnabled(false);
        shutterType->setAddNewLine(false);
        shutterType->setIsPersistent(false);
        mbPage->addKnob(shutterType);
        shutterTypeKnob = shutterType;
        shapeKnobs.push_back(shutterType);
        knobs.push_back(shutterType);

        KnobDoublePtr customOffset = AppManager::createKnob<KnobDouble>(effect, tr(kRotoShutterCustomOffsetParamLabel), 1, true);
        customOffset->setName(kRotoPerShapeShutterCustomOffsetParam);
        customOffset->setHintToolTip( tr(kRotoShutterCustomOffsetParamHint) );
        customOffset->setDefaultValue(0);
        customOffset->setAllDimensionsEnabled(false);
        customOffset->setIsPersistent(false);
        mbPage->addKnob(customOffset);
        customOffsetKnob = customOffset;
        shapeKnobs.push_back(customOffset);
        knobs.push_back(customOffset);

        //////Global motion blur parameters
        KnobDoublePtr globalMotionBlur = AppManager::createKnob<KnobDouble>(effect, tr(kRotoMotionBlurParamLabel), 1, true);
        globalMotionBlur->setName(kRotoGlobalMotionBlurParam);
        globalMotionBlur->setHintToolTip( tr(kRotoMotionBlurParamHint) );
        globalMotionBlur->setDefaultValue(0);
        globalMotionBlur->setMinimum(0);
        globalMotionBlur->setDisplayMinimum(0);
        globalMotionBlur->setDisplayMaximum(4);
        globalMotionBlur->setMaximum(4);
        globalMotionBlur->setSecretByDefault(true);
        mbPage->addKnob(globalMotionBlur);
        globalMotionBlurKnob = globalMotionBlur;
        knobs.push_back(globalMotionBlur);

        KnobDoublePtr globalShutter = AppManager::createKnob<KnobDouble>(effect, tr(kRotoShutterParamLabel), 1, true);
        globalShutter->setName(kRotoGlobalShutterParam);
        globalShutter->setHintToolTip( tr(kRotoShutterParamHint) );
        globalShutter->setDefaultValue(0.5);
        globalShutter->setMinimum(0);
        globalShutter->setDisplayMinimum(0);
        globalShutter->setDisplayMaximum(2);
        globalShutter->setMaximum(2);
        globalShutter->setSecretByDefault(true);
        mbPage->addKnob(globalShutter);
        globalShutterKnob = globalShutter;
        knobs.push_back(globalShutter);

        KnobChoicePtr globalShutterType = AppManager::createKnob<KnobChoice>(effect, tr(kRotoShutterOffsetTypeParamLabel), 1, true);
        globalShutterType->setName(kRotoGlobalShutterOffsetTypeParam);
        globalShutterType->setHintToolTip( tr(kRotoShutterOffsetTypeParamHint) );
        globalShutterType->setDefaultValue(0);
        {
            std::vector<std::string> options, helps;
            options.push_back("Centered");
            helps.push_back(kRotoShutterOffsetCenteredHint);
            options.push_back("Start");
            helps.push_back(kRotoShutterOffsetStartHint);
            options.push_back("End");
            helps.push_back(kRotoShutterOffsetEndHint);
            options.push_back("Custom");
            helps.push_back(kRotoShutterOffsetCustomHint);
            globalShutterType->populateChoices(options, helps);
        }
        globalShutterType->setAddNewLine(false);
        globalShutterType->setSecretByDefault(true);
        mbPage->addKnob(globalShutterType);
        globalShutterTypeKnob = globalShutterType;
        knobs.push_back(globalShutterType);

        KnobDoublePtr globalCustomOffset = AppManager::createKnob<KnobDouble>(effect, tr(kRotoShutterCustomOffsetParamLabel), 1, true);
        globalCustomOffset->setName(kRotoPerShapeShutterCustomOffsetParam);
        globalCustomOffset->setHintToolTip( tr(kRotoShutterCustomOffsetParamHint) );
        globalCustomOffset->setDefaultValue(0);
        globalCustomOffset->setSecretByDefault(true);
        mbPage->addKnob(globalCustomOffset);
        globalCustomOffsetKnob = globalCustomOffset;
        knobs.push_back(globalCustomOffset);

#endif // ifdef NATRON_ROTO_ENABLE_MOTION_BLUR
    }

    /**
     * @brief Call this after any change to notify the mask has changed for the cache.
     **/
    void incrementRotoAge()
    {
        ///MT-safe: only called on the main-thread
        assert( QThread::currentThread() == qApp->thread() );

        QMutexLocker l(&rotoContextMutex);
        ++age;
    }

    RotoLayerPtr findDeepestSelectedLayer() const
    {
        assert( !rotoContextMutex.tryLock() );

        int minLevel = -1;
        RotoLayerPtr minLayer;
        for (std::list< RotoItemPtr >::const_iterator it = selectedItems.begin();
             it != selectedItems.end(); ++it) {
            int lvl = (*it)->getHierarchyLevel();
            if (lvl > minLevel) {
                RotoLayerPtr isLayer = toRotoLayer(*it);
                if (isLayer) {
                    minLayer = isLayer;
                } else {
                    minLayer = (*it)->getParentLayer();
                }
                minLevel = lvl;
            }
        }

        return minLayer;
    }

    

};

NATRON_NAMESPACE_EXIT;

#endif // ROTOCONTEXTPRIVATE_H
