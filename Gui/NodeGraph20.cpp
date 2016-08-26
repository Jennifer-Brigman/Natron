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

#include "NodeGraph.h"
#include "NodeGraphPrivate.h"

#include <cmath> // abs
#include <stdexcept>

GCC_DIAG_UNUSED_PRIVATE_FIELD_OFF
CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QMouseEvent>
#include <QToolTip>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)
GCC_DIAG_UNUSED_PRIVATE_FIELD_ON

#include "Engine/EffectInstance.h"
#include "Engine/Node.h"
#include "Engine/NodeGroup.h"
#include "Engine/Settings.h"
#include "Engine/Utils.h" // convertFromPlainText

#include "Gui/BackdropGui.h"
#include "Gui/Edge.h"
#include "Gui/GuiApplicationManager.h"
#include "Gui/GuiMacros.h"
#include "Gui/NodeGui.h"

#include "Global/QtCompat.h"

NATRON_NAMESPACE_ENTER;
void
NodeGraph::checkForHints(bool shiftdown,
                         bool controlDown,
                         const NodeGuiPtr& selectedNode,
                         const QRectF& visibleSceneR)
{
    NodePtr internalNode = selectedNode->getNode();
    bool doMergeHints = shiftdown && controlDown;
    bool doConnectionHints = appPTR->getCurrentSettings()->isConnectionHintEnabled();

    //Ignore hints for backdrops
    BackdropGuiPtr isBd = toBackdropGui( selectedNode );

    if (isBd) {
        return;
    }

    if (!doMergeHints) {
        ///for nodes already connected don't show hint
        if ( ( internalNode->getMaxInputCount() == 0) && internalNode->hasOutputConnected() ) {
            doConnectionHints = false;
        } else if ( ( internalNode->getMaxInputCount() > 0) && internalNode->hasAllInputsConnected() && internalNode->hasOutputConnected() ) {
            doConnectionHints = false;
        }
    }

    if (!doConnectionHints) {
        return;
    }

    QRectF selectedNodeBbox = selectedNode->boundingRectWithEdges(); //selectedNode->mapToParent( selectedNode->boundingRect() ).boundingRect();
    double tolerance = 10;
    selectedNodeBbox.adjust(-tolerance, -tolerance, tolerance, tolerance);

    NodeGuiPtr nodeToShowMergeRect;
    NodePtr selectedNodeInternalNode = selectedNode->getNode();
    bool selectedNodeIsReader = selectedNodeInternalNode->getEffectInstance()->isReader() || selectedNodeInternalNode->getMaxInputCount() == 0;
    Edge* edge = 0;
    std::set<NodeGuiPtr> nodesWithinRect;
    getNodesWithinViewportRect(visibleWidgetRect(), &nodesWithinRect);

    {
        for (std::set<NodeGuiPtr>::iterator it = nodesWithinRect.begin(); it != nodesWithinRect.end(); ++it) {
            bool isAlreadyAnOutput = false;
            const NodesWList& outputs = internalNode->getGuiOutputs();
            for (NodesWList::const_iterator it2 = outputs.begin(); it2 != outputs.end(); ++it2) {
                NodePtr node = it2->lock();
                if (!node) {
                    continue;
                }
                if ( node == (*it)->getNode() ) {
                    isAlreadyAnOutput = true;
                    break;
                }
            }
            if (isAlreadyAnOutput) {
                continue;
            }
            QRectF nodeBbox = (*it)->boundingRectWithEdges();
            if ( ( (*it) != selectedNode ) && (*it)->isVisible() && nodeBbox.intersects(visibleSceneR) ) {
                if (doMergeHints) {
                    //QRectF nodeRect = (*it)->mapToParent((*it)->boundingRect()).boundingRect();

                    NodePtr internalNode = (*it)->getNode();


                    if ( !internalNode->isOutputNode() && nodeBbox.intersects(selectedNodeBbox) ) {
                        bool nHasInput = internalNode->hasInputConnected();
                        int nMaxInput = internalNode->getMaxInputCount();
                        bool selectedHasInput = selectedNodeInternalNode->hasInputConnected();
                        int selectedMaxInput = selectedNodeInternalNode->getMaxInputCount();
                        double nPAR = internalNode->getEffectInstance()->getAspectRatio(-1);
                        double selectedPAR = selectedNodeInternalNode->getEffectInstance()->getAspectRatio(-1);
                        double nFPS = internalNode->getEffectInstance()->getFrameRate();
                        double selectedFPS = selectedNodeInternalNode->getEffectInstance()->getFrameRate();
                        bool isValid = true;

                        if ( (selectedPAR != nPAR) || (std::abs(nFPS - selectedFPS) > 0.01) ) {
                            if (nHasInput || selectedHasInput) {
                                isValid = false;
                            } else if ( !nHasInput && (nMaxInput == 0) && !selectedHasInput && (selectedMaxInput == 0) ) {
                                isValid = false;
                            }
                        }
                        if (isValid) {
                            nodeToShowMergeRect = (*it)->shared_from_this();
                        }
                    } else {
                        (*it)->setMergeHintActive(false);
                    }
                } else { //!doMergeHints
                    edge = (*it)->hasEdgeNearbyRect(selectedNodeBbox);

                    ///if the edge input is the selected node don't continue
                    if ( edge && ( edge->getSource() == selectedNode) ) {
                        edge = 0;
                    }

                    if ( edge && edge->isOutputEdge() ) {
                        if (selectedNodeIsReader) {
                            continue;
                        }
                        int prefInput = selectedNodeInternalNode->getPreferredInputForConnection();
                        if (prefInput == -1) {
                            edge = 0;
                        } else {
                            Node::CanConnectInputReturnValue ret = selectedNodeInternalNode->canConnectInput(edge->getSource()->getNode(),
                                                                                                             prefInput);
                            if (ret != Node::eCanConnectInput_ok) {
                                edge = 0;
                            }
                        }
                    }

                    if ( edge && !edge->isOutputEdge() ) {
                        if ( (*it)->getNode()->getEffectInstance()->isReader() ||
                             ( (*it)->getNode()->getMaxInputCount() == 0 ) ) {
                            edge = 0;
                            continue;
                        }

                        //Check that the edge can connect to the selected node
                        {
                            Node::CanConnectInputReturnValue ret = edge->getDest()->getNode()->canConnectInput( selectedNodeInternalNode, edge->getInputNumber() );
                            if ( (ret == Node::eCanConnectInput_inputAlreadyConnected) &&
                                 !selectedNodeIsReader ) {
                                ret = Node::eCanConnectInput_ok;
                            }

                            if (ret != Node::eCanConnectInput_ok) {
                                edge = 0;
                            }
                        }

                        //Check that the selected node can connect to the input of the edge

                        if (edge) {
                            NodeGuiPtr edgeHasSource = edge->getSource();
                            if (edgeHasSource) {
                                int prefInput = selectedNodeInternalNode->getPreferredInputForConnection();
                                if (prefInput != -1) {
                                    Node::CanConnectInputReturnValue ret = selectedNodeInternalNode->canConnectInput(edgeHasSource->getNode(), prefInput);
                                    if ( (ret == Node::eCanConnectInput_inputAlreadyConnected) &&
                                         !selectedNodeIsReader ) {
                                        ret = Node::eCanConnectInput_ok;
                                    }

                                    if (ret != Node::eCanConnectInput_ok) {
                                        edge = 0;
                                    }
                                }
                            }
                        }
                    }

                    if (edge) {
                        edge->setUseHighlight(true);
                        break;
                    }
                } // doMergeHints
            }
        }
    } // QMutexLocker l(&_imp->_nodesMutex);

    if ( _imp->_highLightedEdge && ( _imp->_highLightedEdge != edge) ) {
        _imp->_highLightedEdge->setUseHighlight(false);
        _imp->_hintInputEdge->hide();
        _imp->_hintOutputEdge->hide();
    }

    _imp->_highLightedEdge = edge;

    if ( edge && edge->getSource() && edge->getDest() ) {
        ///setup the hints edge

        ///find out if the node is already connected to what the edge is connected
        bool alreadyConnected = false;
        const std::vector<NodeWPtr > & inpNodes = selectedNode->getNode()->getGuiInputs();
        for (std::size_t i = 0; i < inpNodes.size(); ++i) {
            if ( inpNodes[i].lock() == edge->getSource()->getNode() ) {
                alreadyConnected = true;
                break;
            }
        }

        if ( !_imp->_hintInputEdge->isVisible() ) {
            if (!alreadyConnected) {
                int prefInput = selectedNode->getNode()->getPreferredInputForConnection();
                _imp->_hintInputEdge->setInputNumber(prefInput != -1 ? prefInput : 0);
                _imp->_hintInputEdge->setSourceAndDestination(edge->getSource(), selectedNode);
                _imp->_hintInputEdge->setVisible(true);
            }
            _imp->_hintOutputEdge->setInputNumber( edge->getInputNumber() );
            _imp->_hintOutputEdge->setSourceAndDestination( selectedNode, edge->getDest() );
            _imp->_hintOutputEdge->setVisible(true);
        } else {
            if (!alreadyConnected) {
                _imp->_hintInputEdge->initLine();
            }
            _imp->_hintOutputEdge->initLine();
        }
    } else if (edge) {
        ///setup only 1 of the hints edge

        if ( _imp->_highLightedEdge && !_imp->_hintInputEdge->isVisible() ) {
            if ( edge->isOutputEdge() ) {
                int prefInput = selectedNode->getNode()->getPreferredInputForConnection();
                if (prefInput != -1) {
                    _imp->_hintInputEdge->setInputNumber(prefInput);
                    _imp->_hintInputEdge->setSourceAndDestination(edge->getSource(), selectedNode);
                    _imp->_hintInputEdge->setVisible(true);
                }
            } else {
                _imp->_hintInputEdge->setInputNumber( edge->getInputNumber() );
                _imp->_hintInputEdge->setSourceAndDestination( selectedNode, edge->getDest() );
                _imp->_hintInputEdge->setVisible(true);
            }
        } else if ( _imp->_highLightedEdge && _imp->_hintInputEdge->isVisible() ) {
            _imp->_hintInputEdge->initLine();
        }
    } else if (nodeToShowMergeRect) {
        nodeToShowMergeRect->setMergeHintActive(true);
        selectedNode->setMergeHintActive(true);
        _imp->_mergeHintNode = nodeToShowMergeRect;
    } else {
        selectedNode->setMergeHintActive(false);
        _imp->_mergeHintNode.reset();
    }
} // NodeGraph::checkForHints

void
NodeGraph::moveSelectedNodesBy(bool shiftdown,
                               bool controlDown,
                               const QPointF& lastMousePosScene,
                               const QPointF& newPos,
                               const QRectF& visibleSceneR,
                               bool userEdit)
{
    if ( _imp->_selection.empty() ) {
        return;
    }


    //Get the nodes to move, taking into account the backdrops
    bool ignoreMagnet = false;
    std::set<NodeGuiPtr> nodesToMove;
    for (NodesGuiList::iterator it = _imp->_selection.begin();
         it != _imp->_selection.end(); ++it) {
        const NodeGuiPtr& node = *it;
        nodesToMove.insert(node);

        std::map<NodeGuiPtr, NodesGuiList>::iterator foundBd = _imp->_nodesWithinBDAtPenDown.find(*it);
        if ( !controlDown && ( foundBd != _imp->_nodesWithinBDAtPenDown.end() ) ) {
            ignoreMagnet = true; // we move a backdrop, ignore magnet
            for (NodesGuiList::iterator it2 = foundBd->second.begin();
                 it2 != foundBd->second.end(); ++it2) {
                ///add it only if it's not already in the list
                nodesToMove.insert(*it2);

            }
        }
    }

    if (!ignoreMagnet && nodesToMove.size() > 1) {
        ignoreMagnet = true;
    }

    //The delta
    double dxScene = newPos.x() - lastMousePosScene.x();
    double dyScene = newPos.y() - lastMousePosScene.y();


    //Move all nodes
    bool deltaSet = false;
    for (std::set<NodeGuiPtr>::iterator it = nodesToMove.begin();
         it != nodesToMove.end(); ++it) {
        //The current position
        QPointF pos = (*it)->getPos_mt_safe();

        //if ignoreMagnet == true, we do not snap nodes to horizontal/vertical positions
        (*it)->refreshPosition(pos.x() + dxScene, pos.y() + dyScene, ignoreMagnet, newPos);

        //The new position
        QPointF newNodePos = (*it)->getPos_mt_safe();
        if (!ignoreMagnet) {
            //Magnet only works when selection is only for a single node
            //Adjust the delta since mouse press by the new position after snapping
            assert(nodesToMove.size() == 1);
            _imp->_deltaSinceMousePress.rx() += newNodePos.x() - pos.x();
            _imp->_deltaSinceMousePress.ry() += newNodePos.y() - pos.y();
            deltaSet = true;
        }
    }

    if (!deltaSet) {
        _imp->_deltaSinceMousePress.rx() += dxScene;
        _imp->_deltaSinceMousePress.ry() += dyScene;
    }

    if (!userEdit) {
        //For !userEdit do not do auto-scroll and connections hints
        return;
    }

    //Start auto-scolling if nearby the edges
    checkAndStartAutoScrollTimer(newPos);

    //Set the hand cursor
    _imp->cursorSet = true;
    setCursor(Qt::ClosedHandCursor);

    //The lines below are trying to
    if (_imp->_selection.size() != 1) {
        return;
    }

    checkForHints(shiftdown, controlDown, _imp->_selection.front(), visibleSceneR);
} // NodeGraph::moveSelectedNodesBy

void
NodeGraph::mouseMoveEvent(QMouseEvent* e)
{
    QPointF newPos = mapToScene( e->pos() );
    QPointF lastMousePosScene = mapToScene( _imp->_lastMousePos.x(), _imp->_lastMousePos.y() );
    double dx, dy;
    {
        QPointF newPosRoot = _imp->_root->mapFromScene(newPos);
        QPointF lastMousePosRoot = _imp->_root->mapFromScene(lastMousePosScene);
        dx = newPosRoot.x() - lastMousePosRoot.x();
        dy = newPosRoot.y() - lastMousePosRoot.y();
    }

    _imp->_hasMovedOnce = true;

    bool mustUpdate = true;
    NodeCollectionPtr collection = getGroup();
    NodeGroupPtr isGroup = toNodeGroup(collection);
    bool isGroupEditable = true;
    bool groupEdited = true;
    if (isGroup) {
        isGroupEditable = isGroup->isSubGraphEditable();
        groupEdited = isGroup->getNode()->hasPyPlugBeenEdited();
    }
    if (!groupEdited && isGroupEditable) {
        ///check if user is nearby unlock
        int iw = _imp->unlockIcon.width();
        int ih = _imp->unlockIcon.height();
        int w = width();
        if ( ( e->x() >= (w - iw - 10 - 15) ) && ( e->x() <= (w - 10 + 15) ) &&
             ( e->y() >= (10 - 15) ) && ( e->y() <= (10 + ih + 15) ) ) {
            assert(isGroup);
            QPoint pos = mapToGlobal( e->pos() );
            QToolTip::showText( pos, NATRON_NAMESPACE::convertFromPlainText(QCoreApplication::translate("NodeGraph", "Clicking the unlock button will convert the PyPlug to a regular group saved in the project and dettach it from the script.\n"
                                                                                                "Any modification will not be written to the Python script. Subsequent loading of the project will no longer load this group from the python script."), NATRON_NAMESPACE::WhiteSpaceNormal) );
        }
    }

    QRectF sceneR = visibleSceneRect();
    if ( groupEdited && (_imp->_evtState != eEventStateSelectionRect) && (_imp->_evtState != eEventStateDraggingArrow) ) {
        // Set cursor

        std::set<NodeGuiPtr> visibleNodes;
        getNodesWithinViewportRect(visibleWidgetRect(), &visibleNodes);

        NodeGuiPtr selected;
        Edge* selectedEdge = 0;
        bool optionalInputsAutoHidden = areOptionalInputsAutoHidden();

        for (std::set<NodeGuiPtr>::iterator it = visibleNodes.begin(); it != visibleNodes.end(); ++it) {
            QPointF evpt = (*it)->mapFromScene(newPos);
            QRectF bbox = (*it)->mapToScene( (*it)->boundingRect() ).boundingRect();
            if ( (*it)->isActive() && bbox.intersects(sceneR) ) {
                if ( (*it)->contains(evpt) ) {
                    selected = (*it)->shared_from_this();
                    if (optionalInputsAutoHidden) {
                        (*it)->refreshEdgesVisility(true);
                    } else {
                        break;
                    }
                } else {
                    Edge* edge = (*it)->hasEdgeNearbyPoint(newPos);
                    if (edge) {
                        selectedEdge = edge;
                        if (!optionalInputsAutoHidden) {
                            break;
                        }
                    } else if ( optionalInputsAutoHidden && !(*it)->getIsSelected() ) {
                        (*it)->refreshEdgesVisility(false);
                    }
                }
            }
        }
        if (selected) {
            _imp->cursorSet = true;
            setCursor( QCursor(Qt::OpenHandCursor) );
        } else if (selectedEdge) {
        } else if (!selectedEdge && !selected) {
            if (_imp->cursorSet) {
                _imp->cursorSet = false;
                unsetCursor();
            }
        }
    }

    bool mustUpdateNavigator = false;
    ///Apply actions
    switch (_imp->_evtState) {
    case eEventStateDraggingArrow: {
        QPointF np = _imp->_arrowSelected->mapFromScene(newPos);
        if ( _imp->_arrowSelected->isOutputEdge() ) {
            _imp->_arrowSelected->dragDest(np);
        } else {
            _imp->_arrowSelected->dragSource(np);
        }
        checkAndStartAutoScrollTimer(newPos);
        mustUpdate = true;
        break;
    }
    case eEventStateDraggingNode: {
        mustUpdate = true;
        mustUpdateNavigator = true;
        bool controlDown = modifierHasControl(e);
        bool shiftdown = modifierHasShift(e);
        moveSelectedNodesBy(shiftdown, controlDown, lastMousePosScene, newPos, sceneR, true);
        break;
    }
    case eEventStateMovingArea: {
        mustUpdateNavigator = true;
        moveRootInternal(dx, dy);
        _imp->cursorSet = true;
        setCursor( QCursor(Qt::SizeAllCursor) );
        mustUpdate = true;
        break;
    }
    case eEventStateResizingBackdrop: {
        mustUpdateNavigator = true;
        assert(_imp->_backdropResized);
        QPointF p = _imp->_backdropResized->scenePos();
        int w = newPos.x() - p.x();
        int h = newPos.y() - p.y();
        checkAndStartAutoScrollTimer(newPos);
        mustUpdate = true;
        pushUndoCommand( new ResizeBackdropCommand(_imp->_backdropResized, w, h) );
        break;
    }
    case eEventStateSelectionRect: {
        QPointF startDrag = _imp->_lastSelectionStartPointScene;
        QPointF cur = newPos;
        double xmin = std::min( cur.x(), startDrag.x() );
        double xmax = std::max( cur.x(), startDrag.x() );
        double ymin = std::min( cur.y(), startDrag.y() );
        double ymax = std::max( cur.y(), startDrag.y() );
        checkAndStartAutoScrollTimer(newPos);
        QRectF selRect(xmin, ymin, xmax - xmin, ymax - ymin);
        _imp->_selectionRect = selRect;
        mustUpdate = true;
        break;
    }
    case eEventStateDraggingNavigator: {
        QPointF mousePosSceneCoordinates;
        bool insideNavigator = isNearbyNavigator(e->pos(), mousePosSceneCoordinates);
        if (insideNavigator) {
            _imp->_refreshOverlays = true;
            centerOn(mousePosSceneCoordinates);
            _imp->_lastMousePos = e->pos();
            update();

            return;
        }
        break;
    }
    case eEventStateZoomingArea: {
        int delta = 2 * ( ( e->x() - _imp->_lastMousePos.x() ) - ( e->y() - _imp->_lastMousePos.y() ) );
        setTransformationAnchor(QGraphicsView::AnchorViewCenter);
        wheelEventInternal(modCASIsControl(e), delta);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        mustUpdate = true;
        break;
    }
    default:
        mustUpdate = false;
        break;
    } // switch


    _imp->_lastMousePos = e->pos();


    if (mustUpdateNavigator) {
        _imp->_refreshOverlays = true;
        mustUpdate = true;
    }

    if (mustUpdate) {
        update();
    }
    QGraphicsView::mouseMoveEvent(e);
} // mouseMoveEvent

NATRON_NAMESPACE_EXIT;

