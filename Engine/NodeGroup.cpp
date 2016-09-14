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

#include "NodeGroup.h"

#include <set>
#include <locale>
#include <cfloat>
#include <algorithm> // min, max
#include <cassert>
#include <stdexcept>

#include <QtCore/QCoreApplication>
#include <QtCore/QTextStream>

#include "Engine/AppInstance.h"
#include "Engine/Bezier.h"
#include "Engine/BezierCP.h"
#include "Engine/Curve.h"
#include "Engine/GroupInput.h"
#include "Engine/GroupOutput.h"
#include "Engine/Image.h"
#include "Engine/KnobFile.h"
#include "Engine/KnobTypes.h"
#include "Engine/Node.h"
#include "Engine/NodeGraphI.h"
#include "Engine/NodeGuiI.h"
#include "Engine/OutputSchedulerThread.h"
#include "Engine/Plugin.h"
#include "Engine/Project.h"
#include "Engine/PrecompNode.h"
#include "Engine/RotoContext.h"
#include "Engine/RotoLayer.h"
#include "Engine/Settings.h"
#include "Engine/TimeLine.h"
#include "Engine/ViewIdx.h"
#include "Engine/ViewerInstance.h"

#define NATRON_PYPLUG_EXPORTER_VERSION 10

NATRON_NAMESPACE_ENTER;

struct NodeCollectionPrivate
{
    AppInstanceWPtr app;
    NodeGraphI* graph;
    mutable QMutex nodesMutex;
    NodesList nodes;

    NodeCollectionPrivate(const AppInstancePtr& app)
        : app(app)
        , graph(0)
        , nodesMutex()
        , nodes()
    {
    }

    NodePtr findNodeInternal(const std::string& name, const std::string& recurseName) const;
};

NodeCollection::NodeCollection(const AppInstancePtr& app)
    : _imp( new NodeCollectionPrivate(app) )
{
}

NodeCollection::~NodeCollection()
{
}

AppInstancePtr
NodeCollection::getApplication() const
{
    return _imp->app.lock();
}

void
NodeCollection::setNodeGraphPointer(NodeGraphI* graph)
{
    _imp->graph = graph;
}

void
NodeCollection::discardNodeGraphPointer()
{
    _imp->graph = 0;
}

NodeGraphI*
NodeCollection::getNodeGraph() const
{
    return _imp->graph;
}

NodesList
NodeCollection::getNodes() const
{
    QMutexLocker k(&_imp->nodesMutex);

    return _imp->nodes;
}

void
NodeCollection::getNodes_recursive(NodesList& nodes,
                                   bool onlyActive) const
{
    std::list<NodeGroupPtr> groupToRecurse;

    {
        QMutexLocker k(&_imp->nodesMutex);
        for (NodesList::const_iterator it = _imp->nodes.begin(); it != _imp->nodes.end(); ++it) {
            if ( onlyActive && !(*it)->isActivated() ) {
                continue;
            }
            nodes.push_back(*it);
            NodeGroupPtr isGrp = (*it)->isEffectNodeGroup();
            if (isGrp) {
                groupToRecurse.push_back(isGrp);
            }
        }
    }

    for (std::list<NodeGroupPtr>::const_iterator it = groupToRecurse.begin(); it != groupToRecurse.end(); ++it) {
        (*it)->getNodes_recursive(nodes, onlyActive);
    }
}

void
NodeCollection::addNode(const NodePtr& node)
{
    {
        QMutexLocker k(&_imp->nodesMutex);
        _imp->nodes.push_back(node);
    }
}

void
NodeCollection::removeNode(const NodePtr& node)
{
    QMutexLocker k(&_imp->nodesMutex);
    NodesList::iterator found = std::find(_imp->nodes.begin(), _imp->nodes.end(), node);

    if ( found != _imp->nodes.end() ) {
        _imp->nodes.erase(found);
    }
}

NodePtr
NodeCollection::getLastNode(const std::string& pluginID) const
{
    QMutexLocker k(&_imp->nodesMutex);

    for (NodesList::reverse_iterator it = _imp->nodes.rbegin(); it != _imp->nodes.rend(); ++it) {
        if ( (*it)->getPluginID() == pluginID ) {
            return *it;
        }
    }

    return NodePtr();
}

bool
NodeCollection::hasNodes() const
{
    QMutexLocker k(&_imp->nodesMutex);

    return _imp->nodes.size() > 0;
}

void
NodeCollection::getActiveNodes(NodesList* nodes) const
{
    QMutexLocker k(&_imp->nodesMutex);

    for (NodesList::iterator it = _imp->nodes.begin(); it != _imp->nodes.end(); ++it) {
        if ( (*it)->isActivated() ) {
            nodes->push_back(*it);
        }
    }
}

void
NodeCollection::getActiveNodesExpandGroups(NodesList* nodes) const
{
    QMutexLocker k(&_imp->nodesMutex);

    for (NodesList::iterator it = _imp->nodes.begin(); it != _imp->nodes.end(); ++it) {
        if ( (*it)->isActivated() ) {
            nodes->push_back(*it);
            NodeGroupPtr isGrp = (*it)->isEffectNodeGroup();
            if (isGrp) {
                isGrp->getActiveNodesExpandGroups(nodes);
            }
        }
    }
}

void
NodeCollection::getViewers(std::list<ViewerInstancePtr>* viewers) const
{
    QMutexLocker k(&_imp->nodesMutex);

    for (NodesList::iterator it = _imp->nodes.begin(); it != _imp->nodes.end(); ++it) {
        ViewerInstancePtr isViewer = (*it)->isEffectViewerInstance();
        if (isViewer) {
            viewers->push_back(isViewer);
        }
        NodeGroupPtr isGrp = (*it)->isEffectNodeGroup();
        if (isGrp) {
            isGrp->getViewers(viewers);
        }
    }
}

void
NodeCollection::getWriters(std::list<OutputEffectInstancePtr>* writers) const
{
    QMutexLocker k(&_imp->nodesMutex);

    for (NodesList::iterator it = _imp->nodes.begin(); it != _imp->nodes.end(); ++it) {
        if ( (*it)->getGroup() && (*it)->isActivated() && (*it)->getEffectInstance()->isWriter() && (*it)->isPartOfProject() ) {
            OutputEffectInstancePtr out = (*it)->isEffectOutput();
            assert(out);
            writers->push_back(out);
        }
        NodeGroupPtr isGrp = (*it)->isEffectNodeGroup();
        if (isGrp) {
            isGrp->getWriters(writers);
        }
    }
}

void
NodeCollection::quitAnyProcessingInternal()
{
    NodesList nodes = getNodes();

    for (NodesList::iterator it = nodes.begin(); it != nodes.end(); ++it) {
        (*it)->quitAnyProcessing_non_blocking();
        NodeGroupPtr isGrp = (*it)->isEffectNodeGroup();
        if (isGrp) {
            isGrp->quitAnyProcessingInternal();
        }
        PrecompNodePtr isPrecomp = (*it)->isEffectPrecompNode();
        if (isPrecomp) {
            isPrecomp->getPrecompApp()->getProject()->quitAnyProcessingInternal();
        }
    }
}

void
NodeCollection::quitAnyProcessingForAllNodes_non_blocking()
{
    quitAnyProcessingInternal();
}

bool
NodeCollection::isCacheIDAlreadyTaken(const std::string& name) const
{
    QMutexLocker k(&_imp->nodesMutex);

    for (NodesList::iterator it = _imp->nodes.begin(); it != _imp->nodes.end(); ++it) {
        if ( (*it)->getCacheID() == name ) {
            return true;
        }
    }

    return false;
}

bool
NodeCollection::hasNodeRendering() const
{
    QMutexLocker k(&_imp->nodesMutex);

    for (NodesList::iterator it = _imp->nodes.begin(); it != _imp->nodes.end(); ++it) {
        if ( (*it)->isOutputNode() ) {
            NodeGroupPtr isGrp = (*it)->isEffectNodeGroup();
            PrecompNodePtr isPrecomp = (*it)->isEffectPrecompNode();
            if (isGrp) {
                if ( isGrp->hasNodeRendering() ) {
                    return true;
                }
            } else if (isPrecomp) {
                if ( isPrecomp->getPrecompApp()->getProject()->hasNodeRendering() ) {
                    return true;
                }
            } else {
                OutputEffectInstancePtr effect = toOutputEffectInstance( (*it)->getEffectInstance() );
                if ( effect && effect->getRenderEngine()->hasThreadsWorking() ) {
                    return true;
                }
            }
        }
    }

    return false;
}

void
NodeCollection::refreshViewersAndPreviews()
{
    assert( QThread::currentThread() == qApp->thread() );

    if ( !getApplication()->isBackground() ) {
        NodesList nodes = getNodes();
        for (NodesList::iterator it = nodes.begin(); it != nodes.end(); ++it) {
            assert(*it);
            (*it)->refreshPreviewsAfterProjectLoad();
            NodeGroupPtr isGrp = (*it)->isEffectNodeGroup();
            if (isGrp) {
                isGrp->refreshViewersAndPreviews();
            } else {
                ViewerInstancePtr n = (*it)->isEffectViewerInstance();
                if (n) {
                    n->renderCurrentFrame(true);
                }
            }
        }
    }
}

void
NodeCollection::refreshPreviews()
{
    if ( getApplication()->isBackground() ) {
        return;
    }
    double time = getApplication()->getTimeLine()->currentFrame();
    NodesList nodes;
    getActiveNodes(&nodes);
    for (NodesList::iterator it = nodes.begin(); it != nodes.end(); ++it) {
        if ( (*it)->isPreviewEnabled() ) {
            (*it)->refreshPreviewImage(time);
        }
        NodeGroupPtr isGrp = (*it)->isEffectNodeGroup();
        if (isGrp) {
            isGrp->refreshPreviews();
        }
    }
}

void
NodeCollection::forceRefreshPreviews()
{
    if ( getApplication()->isBackground() ) {
        return;
    }
    double time = getApplication()->getTimeLine()->currentFrame();
    NodesList nodes;
    getActiveNodes(&nodes);
    for (NodesList::iterator it = nodes.begin(); it != nodes.end(); ++it) {
        if ( (*it)->isPreviewEnabled() ) {
            (*it)->computePreviewImage(time);
        }
        NodeGroupPtr isGrp = (*it)->isEffectNodeGroup();
        if (isGrp) {
            isGrp->forceRefreshPreviews();
        }
    }
}

void
NodeCollection::clearNodes(bool emitSignal)
{
    NodesList nodesToDelete;
    {
        QMutexLocker l(&_imp->nodesMutex);
        nodesToDelete = _imp->nodes;
    }

    ///Clear recursively containers inside this group
    for (NodesList::iterator it = nodesToDelete.begin(); it != nodesToDelete.end(); ++it) {
        // You should have called quitAnyProcessing before!
        assert( !(*it)->isNodeRendering() );

        NodeGroupPtr isGrp = (*it)->isEffectNodeGroup();
        if (isGrp) {
            isGrp->clearNodes(emitSignal);
        }
        PrecompNodePtr isPrecomp = (*it)->isEffectPrecompNode();
        if (isPrecomp) {
            isPrecomp->getPrecompApp()->getProject()->clearNodes(emitSignal);
        }
    }

    ///Kill effects

    for (NodesList::iterator it = nodesToDelete.begin(); it != nodesToDelete.end(); ++it) {
        (*it)->destroyNode(false);
    }


    if (emitSignal) {
        if (_imp->graph) {
            _imp->graph->onNodesCleared();
        }
    }

    {
        QMutexLocker l(&_imp->nodesMutex);
        _imp->nodes.clear();
    }

    nodesToDelete.clear();
}

void
NodeCollection::checkNodeName(const NodeConstPtr& node,
                              const std::string& baseName,
                              bool appendDigit,
                              bool errorIfExists,
                              std::string* nodeName)
{
    if ( baseName.empty() ) {
        throw std::runtime_error( tr("Invalid script-name.").toStdString() );

        return;
    }
    ///Remove any non alpha-numeric characters from the baseName
    std::string cpy = NATRON_PYTHON_NAMESPACE::makeNameScriptFriendly(baseName);
    if ( cpy.empty() ) {
        throw std::runtime_error( tr("Invalid script-name.").toStdString() );

        return;
    }

    ///If this is a group and one of its parameter has the same script-name as the script-name of one of the node inside
    ///the python attribute will be overwritten. Try to prevent this situation.
    NodeGroup* isGroup = dynamic_cast<NodeGroup*>(this);
    if (isGroup) {
        const KnobsVec&  knobs = isGroup->getKnobs();
        for (KnobsVec::const_iterator it = knobs.begin(); it != knobs.end(); ++it) {
            if ( (*it)->getName() == cpy ) {
                throw std::runtime_error( tr("A node within a group cannot have the same script-name (%1) as a parameter on the group for scripting purposes.").arg( QString::fromUtf8( cpy.c_str() ) ).toStdString() );

                return;
            }
        }
    }
    bool foundNodeWithName = false;
    int no = 1;

    {
        std::stringstream ss;
        ss << cpy;
        if (appendDigit) {
            ss << no;
        }
        *nodeName = ss.str();
    }
    do {
        foundNodeWithName = false;
        QMutexLocker l(&_imp->nodesMutex);
        for (NodesList::iterator it = _imp->nodes.begin(); it != _imp->nodes.end(); ++it) {
            if ( (*it != node) && ( (*it)->getScriptName_mt_safe() == *nodeName ) ) {
                foundNodeWithName = true;
                break;
            }
        }
        if (foundNodeWithName) {
            if (errorIfExists || !appendDigit) {
                throw std::runtime_error( tr("A node with the script-name %1 already exists.").arg( QString::fromUtf8( nodeName->c_str() ) ).toStdString() );

                return;
            }
            ++no;
            {
                std::stringstream ss;
                ss << cpy << no;
                *nodeName = ss.str();
            }
        }
    } while (foundNodeWithName);
} // NodeCollection::checkNodeName

void
NodeCollection::initNodeName(const std::string& pluginLabel,
                             std::string* nodeName)
{
    std::string baseName(pluginLabel);

    if ( (baseName.size() > 3) &&
         ( baseName[baseName.size() - 1] == 'X') &&
         ( baseName[baseName.size() - 2] == 'F') &&
         ( baseName[baseName.size() - 3] == 'O') ) {
        baseName = baseName.substr(0, baseName.size() - 3);
    }

    checkNodeName(NodeConstPtr(), baseName, true, false, nodeName);
}

bool
NodeCollection::connectNodes(int inputNumber,
                             const NodePtr& input,
                             const NodePtr& output,
                             bool force)
{
    ////Only called by the main-thread
    assert( QThread::currentThread() == qApp->thread() );

    NodePtr existingInput = output->getRealInput(inputNumber);
    if (force && existingInput) {
        bool ok = disconnectNodes(existingInput, output);
        if (!ok) {
            return false;
        }
        if ( input && (input->getMaxInputCount() > 0) ) {
            ok = connectNodes(input->getPreferredInputForConnection(), existingInput, input);
            if (!ok) {
                return false;
            }
        }
    }

    if (!input) {
        return true;
    }

    Node::CanConnectInputReturnValue ret = output->canConnectInput(input, inputNumber);
    bool connectionOk = ret == Node::eCanConnectInput_ok ||
                        ret == Node::eCanConnectInput_differentFPS ||
                        ret == Node::eCanConnectInput_differentPars ||
                        ret == Node::eCanConnectInput_multiResNotSupported;

    if (ret == Node::eCanConnectInput_multiResNotSupported) {
        LogEntry::LogEntryColor c;
        if (output->getColor(&c.r, &c.g, &c.b)) {
            c.colorSet = true;
        }

        QString err = tr("Warning: %1 does not support inputs of different sizes but its inputs produce different output size. Please check this.").arg( QString::fromUtf8( output->getScriptName().c_str() ) );
        appPTR->writeToErrorLog_mt_safe(QString::fromUtf8( output->getScriptName().c_str() ) , QDateTime::currentDateTime(), err, false, c);

    }

    if ( !connectionOk || !output->connectInput(input, inputNumber) ) {
        return false;
    }

    return true;
}

bool
NodeCollection::connectNodes(int inputNumber,
                             const std::string & inputName,
                             const NodePtr& output)
{
    NodesList nodes = getNodes();

    for (NodesList::iterator it = nodes.begin(); it != nodes.end(); ++it) {
        assert(*it);
        if ( (*it)->getScriptName() == inputName ) {
            return connectNodes(inputNumber, *it, output);
        }
    }

    return false;
}

bool
NodeCollection::disconnectNodes(const NodePtr& input,
                                const NodePtr& output,
                                bool autoReconnect)
{
    NodePtr inputToReconnectTo;
    int indexOfInput = output->inputIndex( input );

    if (indexOfInput == -1) {
        return false;
    }

    int inputsCount = input->getMaxInputCount();
    if (inputsCount == 1) {
        inputToReconnectTo = input->getInput(0);
    }


    if (output->disconnectInput( input ) < 0) {
        return false;
    }

    if (autoReconnect && inputToReconnectTo) {
        connectNodes(indexOfInput, inputToReconnectTo, output);
    }

    return true;
}

bool
NodeCollection::autoConnectNodes(const NodePtr& selected,
                                 const NodePtr& created)
{
    ///We follow this rule:
    //        1) selected is output
    //          a) created is output --> fail
    //          b) created is input --> connect input
    //          c) created is regular --> connect input
    //        2) selected is input
    //          a) created is output --> connect output
    //          b) created is input --> fail
    //          c) created is regular --> connect output
    //        3) selected is regular
    //          a) created is output--> connect output
    //          b) created is input --> connect input
    //          c) created is regular --> connect output

    ///if true if will connect 'created' as input of 'selected',
    ///otherwise as output.
    bool connectAsInput = false;

    ///cannot connect 2 input nodes together: case 2-b)
    if ( (selected->getMaxInputCount() == 0) && (created->getMaxInputCount() == 0) ) {
        return false;
    }
    ///cannot connect 2 output nodes together: case 1-a)
    if ( selected->isOutputNode() && created->isOutputNode() ) {
        return false;
    }

    ///1)
    if ( selected->isOutputNode() ) {
        ///assert we're not in 1-a)
        assert( !created->isOutputNode() );

        ///for either cases 1-b) or 1-c) we just connect the created node as input of the selected node.
        connectAsInput = true;
    }
    ///2) and 3) are similar exceptfor case b)
    else {
        ///case 2 or 3- a): connect the created node as output of the selected node.
        if ( created->isOutputNode() ) {
            connectAsInput = false;
        }
        ///case b)
        else if (created->getMaxInputCount() == 0) {
            assert(selected->getMaxInputCount() != 0);
            ///case 3-b): connect the created node as input of the selected node
            connectAsInput = true;
        }
        ///case c) connect created as output of the selected node
        else {
            connectAsInput = false;
        }
    }

    bool ret = false;
    if (connectAsInput) {
        ///connect it to the first input
        int selectedInput = selected->getPreferredInputForConnection();
        if (selectedInput != -1) {
            bool ok = connectNodes(selectedInput, created, selected, true);
            assert(ok);
            Q_UNUSED(ok);
            ret = true;
        } else {
            ret = false;
        }
    } else {
        if ( !created->isOutputNode() ) {
            ///we find all the nodes that were previously connected to the selected node,
            ///and connect them to the created node instead.
            std::map<NodePtr, int> outputsConnectedToSelectedNode;
            selected->getOutputsConnectedToThisNode(&outputsConnectedToSelectedNode);
            for (std::map<NodePtr, int>::iterator it = outputsConnectedToSelectedNode.begin();
                 it != outputsConnectedToSelectedNode.end(); ++it) {
                if ( it->first->getParentMultiInstanceName().empty() ) {
                    bool ok = disconnectNodes(selected, it->first);
                    assert(ok);
                    ok = connectNodes(it->second, created, it->first);
                    Q_UNUSED(ok);
                    //assert(ok); Might not be ok if the disconnectNodes() action above was queued
                }
            }
        }
        ///finally we connect the created node to the selected node
        int createdInput = created->getPreferredInputForConnection();
        if (createdInput != -1) {
            bool ok = connectNodes(createdInput, selected, created);
            assert(ok);
            Q_UNUSED(ok);
            ret = true;
        } else {
            ret = false;
        }
    }

    ///update the render trees
    std::list<ViewerInstancePtr> viewers;
    created->hasViewersConnected(&viewers);
    for (std::list<ViewerInstancePtr>::iterator it = viewers.begin(); it != viewers.end(); ++it) {
        (*it)->renderCurrentFrame(true);
    }

    return ret;
} // autoConnectNodes

NodePtr
NodeCollectionPrivate::findNodeInternal(const std::string& name,
                                        const std::string& recurseName) const
{
    QMutexLocker k(&nodesMutex);

    for (NodesList::const_iterator it = nodes.begin(); it != nodes.end(); ++it) {
        if ( (*it)->getScriptName_mt_safe() == name ) {
            if ( !recurseName.empty() ) {
                NodeGroupPtr isGrp = (*it)->isEffectNodeGroup();
                if (isGrp) {
                    return isGrp->getNodeByFullySpecifiedName(recurseName);
                } else {
                    NodesList children;
                    (*it)->getChildrenMultiInstance(&children);
                    for (NodesList::iterator it2 = children.begin(); it2 != children.end(); ++it2) {
                        if ( (*it2)->getScriptName_mt_safe() == recurseName ) {
                            return *it2;
                        }
                    }
                }
            } else {
                return *it;
            }
        }
    }

    return NodePtr();
}

NodePtr
NodeCollection::getNodeByName(const std::string & name) const
{
    return _imp->findNodeInternal( name, std::string() );
}

void
NodeCollection::getNodeNameAndRemainder_LeftToRight(const std::string& fullySpecifiedName,
                                                    std::string& name,
                                                    std::string& remainder)
{
    std::size_t foundDot = fullySpecifiedName.find_first_of('.');

    if (foundDot != std::string::npos) {
        name = fullySpecifiedName.substr(0, foundDot);
        if ( foundDot + 1 < fullySpecifiedName.size() ) {
            remainder = fullySpecifiedName.substr(foundDot + 1, std::string::npos);
        }
    } else {
        name = fullySpecifiedName;
    }
}

void
NodeCollection::getNodeNameAndRemainder_RightToLeft(const std::string& fullySpecifiedName,
                                                    std::string& name,
                                                    std::string& remainder)
{
    std::size_t foundDot = fullySpecifiedName.find_last_of('.');

    if (foundDot != std::string::npos) {
        name = fullySpecifiedName.substr(foundDot + 1, std::string::npos);
        if (foundDot > 0) {
            remainder = fullySpecifiedName.substr(0, foundDot - 1);
        }
    } else {
        name = fullySpecifiedName;
    }
}

NodePtr
NodeCollection::getNodeByFullySpecifiedName(const std::string& fullySpecifiedName) const
{
    std::string toFind;
    std::string recurseName;

    getNodeNameAndRemainder_LeftToRight(fullySpecifiedName, toFind, recurseName);

    return _imp->findNodeInternal(toFind, recurseName);
}

void
NodeCollection::fixRelativeFilePaths(const std::string& projectPathName,
                                     const std::string& newProjectPath,
                                     bool blockEval)
{
    NodesList nodes = getNodes();
    ProjectPtr project = getApplication()->getProject();

    for (NodesList::iterator it = nodes.begin(); it != nodes.end(); ++it) {
        if ( (*it)->isActivated() ) {
            (*it)->getEffectInstance()->beginChanges();

            const KnobsVec& knobs = (*it)->getKnobs();
            for (U32 j = 0; j < knobs.size(); ++j) {
                KnobStringBasePtr isString = toKnobStringBase(knobs[j]);
                KnobStringPtr isStringKnob = toKnobString(isString);
                if ( !isString || isStringKnob || ( knobs[j] == project->getEnvVarKnob() ) ) {
                    continue;
                }

                std::string filepath = isString->getValue();

                if ( !filepath.empty() ) {
                    if ( project->fixFilePath(projectPathName, newProjectPath, filepath) ) {
                        isString->setValue(filepath);
                    }
                }
            }
            (*it)->getEffectInstance()->endChanges(blockEval);


            NodeGroupPtr isGrp = (*it)->isEffectNodeGroup();
            if (isGrp) {
                isGrp->fixRelativeFilePaths(projectPathName, newProjectPath, blockEval);
            }
        }
    }
}

void
NodeCollection::fixPathName(const std::string& oldName,
                            const std::string& newName)
{
    NodesList nodes = getNodes();
    ProjectPtr project = getApplication()->getProject();

    for (NodesList::iterator it = nodes.begin(); it != nodes.end(); ++it) {
        if ( (*it)->isActivated() ) {
            const KnobsVec& knobs = (*it)->getKnobs();
            for (U32 j = 0; j < knobs.size(); ++j) {
                KnobStringBasePtr isString = toKnobStringBase(knobs[j]);
                KnobStringPtr isStringKnob = toKnobString(isString);
                if ( !isString || isStringKnob || ( knobs[j] == project->getEnvVarKnob() ) ) {
                    continue;
                }

                std::string filepath = isString->getValue();

                if ( ( filepath.size() >= (oldName.size() + 2) ) &&
                     ( filepath[0] == '[') &&
                     ( filepath[oldName.size() + 1] == ']') &&
                     ( filepath.substr( 1, oldName.size() ) == oldName) ) {
                    filepath.replace(1, oldName.size(), newName);
                    isString->setValue(filepath);
                }
            }

            NodeGroupPtr isGrp = (*it)->isEffectNodeGroup();
            if (isGrp) {
                isGrp->fixPathName(oldName, newName);
            }
        }
    }
}

bool
NodeCollection::checkIfNodeLabelExists(const std::string & n,
                                       const NodeConstPtr& caller) const
{
    QMutexLocker k(&_imp->nodesMutex);

    for (NodesList::const_iterator it = _imp->nodes.begin(); it != _imp->nodes.end(); ++it) {
        if ( (*it != caller) && ( (*it)->getLabel_mt_safe() == n ) ) {
            return true;
        }
    }

    return false;
}

bool
NodeCollection::checkIfNodeNameExists(const std::string & n,
                                      const NodeConstPtr& caller) const
{
    QMutexLocker k(&_imp->nodesMutex);

    for (NodesList::const_iterator it = _imp->nodes.begin(); it != _imp->nodes.end(); ++it) {
        if ( (*it != caller) && ( (*it)->getScriptName_mt_safe() == n ) ) {
            return true;
        }
    }

    return false;
}

void
NodeCollection::recomputeFrameRangeForAllReadersInternal(int* firstFrame,
                                                         int* lastFrame,
                                                         bool setFrameRange)
{
    NodesList nodes = getNodes();

    for (NodesList::iterator it = nodes.begin(); it != nodes.end(); ++it) {
        if ( (*it)->isActivated() ) {
            if ( (*it)->getEffectInstance()->isReader() ) {
                double thisFirst, thislast;
                (*it)->getEffectInstance()->getFrameRange_public( (*it)->getHashValue(), &thisFirst, &thislast );
                if (thisFirst != INT_MIN) {
                    *firstFrame = setFrameRange ? thisFirst : std::min(*firstFrame, (int)thisFirst);
                }
                if (thislast != INT_MAX) {
                    *lastFrame = setFrameRange ? thislast : std::max(*lastFrame, (int)thislast);
                }
            } else {
                NodeGroupPtr isGrp = (*it)->isEffectNodeGroup();
                if (isGrp) {
                    isGrp->recomputeFrameRangeForAllReadersInternal(firstFrame, lastFrame, false);
                }
            }
        }
    }
}

void
NodeCollection::recomputeFrameRangeForAllReaders(int* firstFrame,
                                                 int* lastFrame)
{
    recomputeFrameRangeForAllReadersInternal(firstFrame, lastFrame, true);
}

void
NodeCollection::forceComputeInputDependentDataOnAllTrees()
{
    NodesList nodes;

    getNodes_recursive(nodes, true);
    std::list<Project::NodesTree> trees;
    Project::extractTreesFromNodes(nodes, trees);

    for (NodesList::iterator it = nodes.begin(); it != nodes.end(); ++it) {
        (*it)->markAllInputRelatedDataDirty();
    }

    std::list<NodePtr> markedNodes;
    for (std::list<Project::NodesTree>::iterator it = trees.begin(); it != trees.end(); ++it) {
        it->output.node->forceRefreshAllInputRelatedData();
    }
}

void
NodeCollection::getParallelRenderArgs(std::map<NodePtr, ParallelRenderArgsPtr >& argsMap) const
{
    NodesList nodes = getNodes();

    for (NodesList::iterator it = nodes.begin(); it != nodes.end(); ++it) {
        if ( !(*it)->isActivated() ) {
            continue;
        }
        ParallelRenderArgsPtr args = (*it)->getEffectInstance()->getParallelRenderArgsTLS();
        if (args) {
            argsMap.insert( std::make_pair(*it, args) );
        }

        if ( (*it)->isMultiInstance() ) {
            ///If the node has children, set the thread-local storage on them too, even if they do not render, it can be useful for expressions
            ///on parameters.
            NodesList children;
            (*it)->getChildrenMultiInstance(&children);
            for (NodesList::iterator it2 = children.begin(); it2 != children.end(); ++it2) {
                ParallelRenderArgsPtr childArgs = (*it2)->getEffectInstance()->getParallelRenderArgsTLS();
                if (childArgs) {
                    argsMap.insert( std::make_pair(*it2, childArgs) );
                }
            }
        }

        //If the node has an attached stroke, that means it belongs to the roto paint tree, hence it is not in the project.
        RotoContextPtr rotoContext = (*it)->getRotoContext();
        if (args && rotoContext) {
            for (NodesList::const_iterator it2 = args->rotoPaintNodes.begin(); it2 != args->rotoPaintNodes.end(); ++it2) {
                ParallelRenderArgsPtr args2 = (*it2)->getEffectInstance()->getParallelRenderArgsTLS();
                if (args2) {
                    argsMap.insert( std::make_pair(*it2, args2) );
                }
            }
        }


        const NodeGroupPtr isGrp = (*it)->isEffectNodeGroup();
        if (isGrp) {
            isGrp->getParallelRenderArgs(argsMap);
        }

        const PrecompNodePtr isPrecomp = (*it)->isEffectPrecompNode();
        if (isPrecomp) {
            isPrecomp->getPrecompApp()->getProject()->getParallelRenderArgs(argsMap);
        }
    }
}

struct NodeGroupPrivate
{
    mutable QMutex nodesLock; // protects inputs & outputs
    std::vector<NodeWPtr > inputs, guiInputs;
    NodesWList outputs, guiOutputs;
    bool isDeactivatingGroup;
    bool isActivatingGroup;
    bool isEditable;
    KnobButtonPtr exportAsTemplate;

    NodeGroupPrivate()
        : nodesLock(QMutex::Recursive)
        , inputs()
        , guiInputs()
        , outputs()
        , guiOutputs()
        , isDeactivatingGroup(false)
        , isActivatingGroup(false)
        , isEditable(true)
        , exportAsTemplate()
    {
    }
};

NodeGroup::NodeGroup(const NodePtr &node)
    : OutputEffectInstance(node)
    , NodeCollection( node ? node->getApp() : AppInstancePtr() )
    , _imp( new NodeGroupPrivate() )
{
    setSupportsRenderScaleMaybe(EffectInstance::eSupportsYes);
}

bool
NodeGroup::getIsDeactivatingGroup() const
{
    assert( QThread::currentThread() == qApp->thread() );

    return _imp->isDeactivatingGroup;
}

void
NodeGroup::setIsDeactivatingGroup(bool b)
{
    assert( QThread::currentThread() == qApp->thread() );
    _imp->isDeactivatingGroup = b;
}

bool
NodeGroup::getIsActivatingGroup() const
{
    assert( QThread::currentThread() == qApp->thread() );

    return _imp->isActivatingGroup;
}

void
NodeGroup::setIsActivatingGroup(bool b)
{
    assert( QThread::currentThread() == qApp->thread() );
    _imp->isActivatingGroup = b;
}

NodeGroup::~NodeGroup()
{
}

std::string
NodeGroup::getPluginDescription() const
{
    return "Use this to nest multiple nodes into a single node. The original nodes will be replaced by the Group node and its "
           "content is available in a separate NodeGraph tab. You can add user parameters to the Group node which can drive "
           "parameters of nodes nested within the Group. To specify the outputs and inputs of the Group node, you may add multiple "
           "Input node within the group and exactly 1 Output node.";
}

void
NodeGroup::addAcceptedComponents(int /*inputNb*/,
                                 std::list<ImageComponents>* comps)
{
    comps->push_back( ImageComponents::getRGBAComponents() );
    comps->push_back( ImageComponents::getRGBComponents() );
    comps->push_back( ImageComponents::getAlphaComponents() );
}

void
NodeGroup::addSupportedBitDepth(std::list<ImageBitDepthEnum>* depths) const
{
    depths->push_back(eImageBitDepthByte);
    depths->push_back(eImageBitDepthShort);
    depths->push_back(eImageBitDepthFloat);
}

int
NodeGroup::getMaxInputCount() const
{
    return (int)_imp->inputs.size();
}

std::string
NodeGroup::getInputLabel(int inputNb) const
{
    NodePtr input;
    {
        QMutexLocker k(&_imp->nodesLock);
        if ( ( inputNb >= (int)_imp->inputs.size() ) || (inputNb < 0) ) {
            return std::string();
        }

        ///If the input name starts with "input" remove it, otherwise keep the full name

        input = _imp->inputs[inputNb].lock();
        if (!input) {
            return std::string();
        }
    }
    QString inputName = QString::fromUtf8( input->getLabel_mt_safe().c_str() );

    if ( inputName.startsWith(QString::fromUtf8("input"), Qt::CaseInsensitive) ) {
        inputName.remove(0, 5);
    }

    return inputName.toStdString();
}

double
NodeGroup::getCurrentTime() const
{
    NodePtr node = getOutputNodeInput(false);

    if (node) {
        return node->getEffectInstance()->getCurrentTime();
    }

    return EffectInstance::getCurrentTime();
}

ViewIdx
NodeGroup::getCurrentView() const
{
    NodePtr node = getOutputNodeInput(false);

    if (node) {
        return node->getEffectInstance()->getCurrentView();
    }

    return EffectInstance::getCurrentView();
}

bool
NodeGroup::isInputOptional(int inputNb) const
{
    NodePtr n;

    {
        QMutexLocker k(&_imp->nodesLock);

        if ( ( inputNb >= (int)_imp->inputs.size() ) || (inputNb < 0) ) {
            return false;
        }


        n = _imp->inputs[inputNb].lock();
        if (!n) {
            return false;
        }
    }
    GroupInputPtr input = n->isEffectGroupInput();

    assert(input);
    if (!input) {
        return false;
    }
    KnobIPtr knob = input->getKnobByName(kNatronGroupInputIsOptionalParamName);
    assert(knob);
    if (!knob) {
        return false;
    }
    KnobBoolPtr isBool = toKnobBool(knob);
    assert(isBool);

    return isBool ? isBool->getValue() : false;
}

bool
NodeGroup::isHostChannelSelectorSupported(bool* /*defaultR*/,
                                          bool* /*defaultG*/,
                                          bool* /*defaultB*/,
                                          bool* /*defaultA*/) const
{
    return false;
}

bool
NodeGroup::isInputMask(int inputNb) const
{
    NodePtr n;

    {
        QMutexLocker k(&_imp->nodesLock);

        if ( ( inputNb >= (int)_imp->inputs.size() ) || (inputNb < 0) ) {
            return false;
        }


        n = _imp->inputs[inputNb].lock();
        if (!n) {
            return false;
        }
    }
    GroupInputPtr input = n->isEffectGroupInput();

    assert(input);
    if (!input) {
        return false;
    }
    KnobIPtr knob = input->getKnobByName(kNatronGroupInputIsMaskParamName);
    assert(knob);
    if (!knob) {
        return false;
    }
    KnobBoolPtr isBool = toKnobBool(knob);
    assert(isBool);

    return isBool ? isBool->getValue() : false;
}

void
NodeGroup::initializeKnobs()
{
    KnobIPtr nodePage = getKnobByName(NATRON_PARAMETER_PAGE_NAME_EXTRA);

    assert(nodePage);
    KnobPagePtr isPage = toKnobPage(nodePage);
    assert(isPage);
    _imp->exportAsTemplate = AppManager::createKnob<KnobButton>( shared_from_this(), tr("Export as PyPlug") );
    _imp->exportAsTemplate->setName("exportAsPyPlug");
    _imp->exportAsTemplate->setHintToolTip( tr("Export this group as a Python group script (PyPlug) that can be shared and/or later "
                                               "on re-used as a plug-in.") );
    if (isPage) {
        isPage->addKnob(_imp->exportAsTemplate);
    }
}

void
NodeGroup::notifyNodeDeactivated(const NodePtr& node)
{
    if ( getIsDeactivatingGroup() ) {
        return;
    }
    NodePtr thisNode = getNode();

    {
        QMutexLocker k(&_imp->nodesLock);
        GroupInputPtr isInput = node->isEffectGroupInput();
        if (isInput) {
            for (U32 i = 0; i < _imp->inputs.size(); ++i) {
                NodePtr input = _imp->inputs[i].lock();
                if (node == input) {
                    ///Also disconnect the real input

                    thisNode->disconnectInput(i);


                    _imp->inputs.erase(_imp->inputs.begin() + i);
                    thisNode->initializeInputs();

                    return;
                }
            }
            ///The input must have been tracked before
            assert(false);
        }
        GroupOutputPtr isOutput = toGroupOutput( node->getEffectInstance() );
        if (isOutput) {
            for (NodesWList::iterator it = _imp->outputs.begin(); it != _imp->outputs.end(); ++it) {
                if (it->lock()->getEffectInstance() == isOutput) {
                    _imp->outputs.erase(it);
                    break;
                }
            }
        }

        ///Sync gui inputs/outputs
        _imp->guiInputs = _imp->inputs;
        _imp->guiOutputs = _imp->outputs;
    }

    ///Notify outputs of the group nodes that their inputs may have changed
    const NodesWList& outputs = thisNode->getOutputs();
    for (NodesWList::const_iterator it = outputs.begin(); it != outputs.end(); ++it) {
        NodePtr output = it->lock();
        if (!output) {
            continue;
        }
        int idx = output->getInputIndex(thisNode);
        assert(idx != -1);
        output->onInputChanged(idx);
    }
} // NodeGroup::notifyNodeDeactivated

void
NodeGroup::notifyNodeActivated(const NodePtr& node)
{
    if ( getIsActivatingGroup() ) {
        return;
    }

    NodePtr thisNode = getNode();

    {
        QMutexLocker k(&_imp->nodesLock);
        GroupInputPtr isInput = node->isEffectGroupInput();
        if (isInput) {
            _imp->inputs.push_back(node);
            _imp->guiInputs.push_back(node);
            thisNode->initializeInputs();
        }
        GroupOutputPtr isOutput = toGroupOutput( node->getEffectInstance() );
        if (isOutput) {
            _imp->outputs.push_back(node);
            _imp->guiOutputs.push_back(node);
        }
    }
    ///Notify outputs of the group nodes that their inputs may have changed
    const NodesWList& outputs = thisNode->getOutputs();
    for (NodesWList::const_iterator it = outputs.begin(); it != outputs.end(); ++it) {
        NodePtr output = it->lock();
        if (!output) {
            continue;
        }
        int idx = output->getInputIndex(thisNode);
        assert(idx != -1);
        output->onInputChanged(idx);
    }
}

void
NodeGroup::notifyInputOptionalStateChanged(const NodePtr& /*node*/)
{
    getNode()->initializeInputs();
}

void
NodeGroup::notifyInputMaskStateChanged(const NodePtr& /*node*/)
{
    getNode()->initializeInputs();
}

void
NodeGroup::notifyNodeNameChanged(const NodePtr& node)
{
    GroupInputPtr isInput = node->isEffectGroupInput();

    if (isInput) {
        getNode()->initializeInputs();
    }
}

void
NodeGroup::dequeueConnexions()
{
    QMutexLocker k(&_imp->nodesLock);

    _imp->inputs = _imp->guiInputs;
    _imp->outputs = _imp->guiOutputs;
}

NodePtr
NodeGroup::getOutputNode(bool useGuiConnexions) const
{
    QMutexLocker k(&_imp->nodesLock);

    ///A group can only have a single output.
    if ( ( !useGuiConnexions && _imp->outputs.empty() ) || ( useGuiConnexions && _imp->guiOutputs.empty() ) ) {
        return NodePtr();
    }

    return useGuiConnexions ? _imp->guiOutputs.front().lock() : _imp->outputs.front().lock();
}

NodePtr
NodeGroup::getOutputNodeInput(bool useGuiConnexions) const
{
    NodePtr output = getOutputNode(useGuiConnexions);

    if (output) {
        return useGuiConnexions ? output->getGuiInput(0) : output->getInput(0);
    }

    return NodePtr();
}

NodePtr
NodeGroup::getRealInputForInput(bool useGuiConnexions,
                                const NodePtr& input) const
{
    {
        QMutexLocker k(&_imp->nodesLock);
        if (!useGuiConnexions) {
            for (U32 i = 0; i < _imp->inputs.size(); ++i) {
                if (_imp->inputs[i].lock() == input) {
                    return getNode()->getInput(i);
                }
            }
        } else {
            for (U32 i = 0; i < _imp->guiInputs.size(); ++i) {
                if (_imp->guiInputs[i].lock() == input) {
                    return getNode()->getGuiInput(i);
                }
            }
        }
    }

    return NodePtr();
}

void
NodeGroup::getInputsOutputs(NodesList* nodes,
                            bool useGuiConnexions) const
{
    QMutexLocker k(&_imp->nodesLock);

    if (!useGuiConnexions) {
        for (U32 i = 0; i < _imp->inputs.size(); ++i) {
            NodesWList outputs;
            NodePtr input = _imp->inputs[i].lock();
            if (!input) {
                continue;
            }
            input->getOutputs_mt_safe(outputs);
            for (NodesWList::iterator it = outputs.begin(); it != outputs.end(); ++it) {
                NodePtr node = it->lock();
                if (node) {
                    nodes->push_back(node);
                }
            }
        }
    } else {
        for (U32 i = 0; i < _imp->guiInputs.size(); ++i) {
            NodesWList outputs;
            NodePtr input = _imp->guiInputs[i].lock();
            if (!input) {
                continue;
            }
            input->getOutputs_mt_safe(outputs);
            for (NodesWList::iterator it = outputs.begin(); it != outputs.end(); ++it) {
                NodePtr node = it->lock();
                if (node) {
                    nodes->push_back(node);
                }
            }
        }
    }
}

void
NodeGroup::getInputs(std::vector<NodePtr >* inputs,
                     bool useGuiConnexions) const
{
    QMutexLocker k(&_imp->nodesLock);

    if (!useGuiConnexions) {
        for (U32 i = 0; i < _imp->inputs.size(); ++i) {
            NodePtr input = _imp->inputs[i].lock();
            if (!input) {
                continue;
            }
            inputs->push_back(input);
        }
    } else {
        for (U32 i = 0; i < _imp->guiInputs.size(); ++i) {
            NodePtr input = _imp->guiInputs[i].lock();
            if (!input) {
                continue;
            }
            inputs->push_back(input);
        }
    }
}

void
NodeGroup::purgeCaches()
{
    NodesList nodes = getNodes();

    for (NodesList::iterator it = nodes.begin(); it != nodes.end(); ++it) {
        (*it)->getEffectInstance()->purgeCaches();
    }
}

bool
NodeGroup::knobChanged(const KnobIPtr& k,
                       ValueChangedReasonEnum /*reason*/,
                       ViewSpec /*view*/,
                       double /*time*/,
                       bool /*originatedFromMainThread*/)
{
    bool ret = true;

    if (k == _imp->exportAsTemplate) {
        NodeGuiIPtr gui_i = getNode()->getNodeGui();
        if (gui_i) {
            gui_i->exportGroupAsPythonScript();
        }
    } else {
        ret = false;
    }

    return ret;
}

void
NodeGroup::setSubGraphEditable(bool editable)
{
    assert( QThread::currentThread() == qApp->thread() );
    _imp->isEditable = editable;
    Q_EMIT graphEditableChanged(editable);
}

bool
NodeGroup::isSubGraphEditable() const
{
    assert( QThread::currentThread() == qApp->thread() );

    return _imp->isEditable;
}

static QString
escapeString(const QString& str)
{
    QString ret;

    for (int i = 0; i < str.size(); ++i) {
        if ( (i == 0) || ( str[i - 1] != QLatin1Char('\\') ) ) {
            if ( str[i] == QLatin1Char('\\') ) {
                ret.append( QLatin1Char('\\') );
                ret.append( QLatin1Char('\\') );
            } else if ( str[i] == QLatin1Char('"') ) {
                ret.append( QLatin1Char('\\') );
                ret.append( QLatin1Char('\"') );
            } else if ( str[i] == QLatin1Char('\'') ) {
                ret.append( QLatin1Char('\\') );
                ret.append( QLatin1Char('\'') );
            } else if ( str[i] == QLatin1Char('\n') ) {
                ret.append( QLatin1Char('\\') );
                ret.append( QLatin1Char('n') );
            } else if ( str[i] == QLatin1Char('\t') ) {
                ret.append( QLatin1Char('\\') );
                ret.append( QLatin1Char('t') );
            } else if ( str[i] == QLatin1Char('\r') ) {
                ret.append( QLatin1Char('\\') );
                ret.append( QLatin1Char('r') );
            } else {
                ret.append(str[i]);
            }
        } else {
            ret.append(str[i]);
        }
    }
    ret.prepend( QLatin1Char('"') );
    ret.append( QLatin1Char('"') );

    return ret;
}

static QString
escapeString(const std::string& str)
{
    QString s = QString::fromUtf8( str.c_str() );

    return escapeString(s);
}

#define ESC(s) escapeString(s)


/* *INDENT-OFF* */

#define WRITE_STATIC_LINE(line) ts << line "\n"
#define WRITE_INDENT(x)                                 \
    for (int _i = 0; _i < x; ++_i) { ts << "    "; }
#define WRITE_STRING(str) ts << str << "\n"
//#define NUM(n) QString::number(n)
#define NUM_INT(n) QString::number(n, 10)
#define NUM_COLOR(n) QString::number(n, 'g', 4)
#define NUM_PIXEL(n) QString::number(n, 'f', 0)
#define NUM_VALUE(n) QString::number(n, 'g', 16)
#define NUM_TIME(n) QString::number(n, 'g', 16)

/* *INDENT-ON* */

static bool
exportKnobValues(int indentLevel,
                 const KnobIPtr knob,
                 const QString& paramFullName,
                 bool mustDefineParam,
                 QTextStream& ts)
{
    bool hasExportedValue = false;

    KnobStringBasePtr isStr = toKnobStringBase(knob);
    AnimatingKnobStringHelperPtr isAnimatedStr = boost::dynamic_pointer_cast<AnimatingKnobStringHelper>(knob);
    KnobDoubleBasePtr isDouble = toKnobDoubleBase(knob);
    KnobIntBasePtr isInt = toKnobIntBase(knob);
    KnobBoolBasePtr isBool = toKnobBoolBase(knob);
    KnobParametricPtr isParametric = toKnobParametric(knob);
    KnobChoicePtr isChoice = toKnobChoice(knob);
    KnobGroupPtr isGrp = toKnobGroup(knob);
    KnobStringPtr isStringKnob = toKnobString(knob);

    ///Don't export this kind of parameter. Mainly this is the html label of the node which is 99% of times empty
    if ( isStringKnob &&
         isStringKnob->isMultiLine() &&
         isStringKnob->usesRichText() &&
         !isStringKnob->hasContentWithoutHtmlTags() &&
         !isStringKnob->isAnimationEnabled() &&
         isStringKnob->getExpression(0).empty() ) {
        return false;
    }

    EffectInstancePtr holderIsEffect = toEffectInstance( knob->getHolder() );

    if (isChoice && holderIsEffect) {
        //Do not serialize mask channel selector if the mask is not enabled
        int maskInputNb = holderIsEffect->getNode()->isMaskChannelKnob(isChoice);
        if (maskInputNb != -1) {
            if ( !holderIsEffect->getNode()->isMaskEnabled(maskInputNb) ) {
                return false;
            }
        }
    }

    int innerIdent = mustDefineParam ? 2 : 1;

    for (int i = 0; i < knob->getDimension(); ++i) {
        if (isParametric) {
            if (!hasExportedValue) {
                hasExportedValue = true;
                if (mustDefineParam) {
                    WRITE_INDENT(indentLevel); WRITE_STRING(QString::fromUtf8("param = ") + paramFullName);
                    WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("if param is not None:") );
                }
            }
            CurvePtr curve = isParametric->getParametricCurve(i);
            double r, g, b;
            isParametric->getCurveColor(i, &r, &g, &b);
            WRITE_INDENT(innerIdent); WRITE_STRING( QString::fromUtf8("param.setCurveColor(") + NUM_INT(i) + QString::fromUtf8(", ") +
                                                    NUM_COLOR(r) + QString::fromUtf8(", ") + NUM_COLOR(g) + QString::fromUtf8(", ") + NUM_COLOR(b) + QString::fromUtf8(")") );
            if (curve) {
                KeyFrameSet keys = curve->getKeyFrames_mt_safe();
                int c = 0;
                if ( !keys.empty() ) {
                    WRITE_INDENT(innerIdent); WRITE_STRING( QString::fromUtf8("param.deleteAllControlPoints(") + NUM_INT(i) + QString::fromUtf8(")") );
                }
                for (KeyFrameSet::iterator it3 = keys.begin(); it3 != keys.end(); ++it3, ++c) {
                    QString interpStr;
                    switch ( it3->getInterpolation() ) {
                    case eKeyframeTypeNone:
                        interpStr = QString::fromUtf8("NatronEngine.Natron.KeyframeTypeEnum.eKeyframeTypeNone");
                        break;
                    case eKeyframeTypeSmooth:
                        interpStr = QString::fromUtf8("NatronEngine.Natron.KeyframeTypeEnum.eKeyframeTypeSmooth");
                        break;
                    case eKeyframeTypeBroken:
                        interpStr = QString::fromUtf8("NatronEngine.Natron.KeyframeTypeEnum.eKeyframeTypeBroken");
                        break;
                    case eKeyframeTypeCatmullRom:
                        interpStr = QString::fromUtf8("NatronEngine.Natron.KeyframeTypeEnum.eKeyframeTypeCatmullRom");
                        break;
                    case eKeyframeTypeConstant:
                        interpStr = QString::fromUtf8("NatronEngine.Natron.KeyframeTypeEnum.eKeyframeTypeConstant");
                        break;
                    case eKeyframeTypeCubic:
                        interpStr = QString::fromUtf8("NatronEngine.Natron.KeyframeTypeEnum.eKeyframeTypeCubic");
                        break;
                    case eKeyframeTypeFree:
                        interpStr = QString::fromUtf8("NatronEngine.Natron.KeyframeTypeEnum.eKeyframeTypeFree");
                        break;
                    case eKeyframeTypeHorizontal:
                        interpStr = QString::fromUtf8("NatronEngine.Natron.KeyframeTypeEnum.eKeyframeTypeHorizontal");
                        break;
                    case eKeyframeTypeLinear:
                        interpStr = QString::fromUtf8("NatronEngine.Natron.KeyframeTypeEnum.eKeyframeTypeLinear");
                        break;
                    default:
                        break;
                    }


                    WRITE_INDENT(innerIdent); WRITE_STRING( QString::fromUtf8("param.addControlPoint(") + NUM_INT(i) + QString::fromUtf8(", ") +
                                                            NUM_TIME( it3->getTime() ) + QString::fromUtf8(", ") +
                                                            NUM_VALUE( it3->getValue() ) + QString::fromUtf8(", ") + NUM_VALUE( it3->getLeftDerivative() )
                                                            + QString::fromUtf8(", ") + NUM_VALUE( it3->getRightDerivative() ) + QString::fromUtf8(", ") + interpStr + QString::fromUtf8(")") );
                }
            }
        } else { // !isParametric
            CurvePtr curve = knob->getCurve(ViewIdx(0), i, true);
            if (curve) {
                KeyFrameSet keys = curve->getKeyFrames_mt_safe();

                if ( !keys.empty() ) {
                    if (!hasExportedValue) {
                        hasExportedValue = true;
                        if (mustDefineParam) {
                            WRITE_INDENT(indentLevel); WRITE_STRING(QString::fromUtf8("param = ") + paramFullName);
                            WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("if param is not None:") );
                        }
                    }
                }

                for (KeyFrameSet::iterator it3 = keys.begin(); it3 != keys.end(); ++it3) {
                    if (isAnimatedStr) {
                        std::string value = isAnimatedStr->getValueAtTime(it3->getTime(), i, ViewIdx(0), true);
                        WRITE_INDENT(innerIdent); WRITE_STRING( QString::fromUtf8("param.setValueAtTime(") + ESC(value) + QString::fromUtf8(", ")
                                                                + NUM_TIME( it3->getTime() ) + QChar::fromLatin1(')') );
                    } else if (isBool) {
                        int v = std::min( 1., std::max( 0., std::floor(it3->getValue() + 0.5) ) );
                        QString vStr = v ? QString::fromUtf8("True") : QString::fromUtf8("False");
                        WRITE_INDENT(innerIdent); WRITE_STRING( QString::fromUtf8("param.setValueAtTime(") + vStr + QString::fromUtf8(", ")
                                                                + NUM_TIME( it3->getTime() )  + QLatin1Char(')') );
                    } else if (isChoice) {
                        WRITE_INDENT(innerIdent); WRITE_STRING( QString::fromUtf8("param.setValueAtTime(") + NUM_INT( (int)it3->getValue() ) + QString::fromUtf8(", ")
                                                                + NUM_TIME( it3->getTime() ) + QLatin1Char(')') );
                    } else {
                        WRITE_INDENT(innerIdent); WRITE_STRING( QString::fromUtf8("param.setValueAtTime(") + NUM_VALUE( it3->getValue() ) + QString::fromUtf8(", ")
                                                                + NUM_TIME( it3->getTime() ) + QString::fromUtf8(", ") + NUM_INT(i) + QLatin1Char(')') );
                    }
                }
            }

            if ( ( !curve || (curve->getKeyFramesCount() == 0) ) && knob->hasModifications(i) ) {
                if (!hasExportedValue) {
                    hasExportedValue = true;
                    if (mustDefineParam) {
                        WRITE_INDENT(indentLevel); WRITE_STRING(QString::fromUtf8("param = ") + paramFullName);
                        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("if param is not None:") );
                    }
                }

                if (isGrp) {
                    int v = std::min( 1., std::max( 0., std::floor(isGrp->getValue(i, ViewIdx(0), true) + 0.5) ) );
                    QString vStr = v ? QString::fromUtf8("True") : QString::fromUtf8("False");
                    WRITE_INDENT(innerIdent); WRITE_STRING( QString::fromUtf8("param.setOpened(") + vStr + QString::fromUtf8(")") );
                } else if (isStr) {
                    std::string v = isStr->getValue(i, ViewIdx(0), true);
                    WRITE_INDENT(innerIdent); WRITE_STRING( QString::fromUtf8("param.setValue(") + ESC(v)  + QString::fromUtf8(")") );
                } else if (isDouble) {
                    double v = isDouble->getValue(i, ViewIdx(0), true);
                    WRITE_INDENT(innerIdent); WRITE_STRING( QString::fromUtf8("param.setValue(") + NUM_VALUE(v) + QString::fromUtf8(", ") + NUM_INT(i) + QString::fromUtf8(")") );
                } else if (isChoice) {
                    WRITE_INDENT(innerIdent); WRITE_STRING( QString::fromUtf8("param.set(") + ESC( isChoice->getActiveEntryText_mt_safe() ) + QString::fromUtf8(")") );
                } else if (isInt) {
                    int v = isInt->getValue(i, ViewIdx(0), true);
                    WRITE_INDENT(innerIdent); WRITE_STRING( QString::fromUtf8("param.setValue(") + NUM_INT(v) + QString::fromUtf8(", ") + NUM_INT(i) + QString::fromUtf8(")") );
                } else if (isBool) {
                    int v = std::min( 1., std::max( 0., std::floor(isBool->getValue(i, ViewIdx(0), true) + 0.5) ) );
                    QString vStr = v ? QString::fromUtf8("True") : QString::fromUtf8("False");
                    WRITE_INDENT(innerIdent); WRITE_STRING( QString::fromUtf8("param.setValue(") + vStr + QString::fromUtf8(")") );
                }
            } // if ((!curve || curve->getKeyFramesCount() == 0) && knob->hasModifications(i)) {
        } // if (isParametric) {
    } // for (int i = 0; i < (*it2)->getDimension(); ++i)

    bool isSecretByDefault = knob->getDefaultIsSecret();
    if (knob->isUserKnob() && isSecretByDefault) {
        if (!hasExportedValue) {
            hasExportedValue = true;
            if (mustDefineParam) {
                WRITE_INDENT(indentLevel); WRITE_STRING(QString::fromUtf8("param = ") + paramFullName);
                WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("if param is not None:") );
            }
        }

        WRITE_INDENT(innerIdent); WRITE_STRING( QString::fromUtf8("param.setVisibleByDefault(False)") );
    }

    if ( knob->isUserKnob() ) {
        bool isSecret = knob->getIsSecret();
        if (isSecret != isSecretByDefault) {
            if (!hasExportedValue) {
                hasExportedValue = true;
                if (mustDefineParam) {
                    WRITE_INDENT(indentLevel); WRITE_STRING(QString::fromUtf8("param = ") + paramFullName);
                    WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("if param is not None:") );
                }
            }

            QString str = QString::fromUtf8("param.setVisible(");
            if (isSecret) {
                str += QString::fromUtf8("False");
            } else {
                str += QString::fromUtf8("True");
            }
            str += QString::fromUtf8(")");
            WRITE_INDENT(innerIdent); WRITE_STRING(str);
        }

        bool enabledByDefault = knob->isDefaultEnabled(0);
        if (!enabledByDefault) {
            if (!hasExportedValue) {
                hasExportedValue = true;
                if (mustDefineParam) {
                    WRITE_INDENT(indentLevel); WRITE_STRING(QString::fromUtf8("param = ") + paramFullName);
                    WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("if param is not None:") );
                }
            }

            WRITE_INDENT(innerIdent); WRITE_STRING( QString::fromUtf8("param.setEnabledByDefault(False)") );
        }

        for (int i = 0; i < knob->getDimension(); ++i) {
            bool isEnabled = knob->isEnabled(i);
            if (isEnabled != enabledByDefault) {
                if (!hasExportedValue) {
                    hasExportedValue = true;
                    if (mustDefineParam) {
                        WRITE_INDENT(indentLevel); WRITE_STRING(QString::fromUtf8("param = ") + paramFullName);
                        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("if param is not None:") );
                    }
                }

                QString str = QString::fromUtf8("param.setEnabled(");
                if (isEnabled) {
                    str += QString::fromUtf8("True");
                } else {
                    str += QString::fromUtf8("False");
                }
                str += QString::fromUtf8(", ");
                str += NUM_INT(i);
                str += QLatin1Char(')');
                WRITE_INDENT(innerIdent); WRITE_STRING(str);
            }
        }
    } // isuserknob

    if (mustDefineParam && hasExportedValue) {
        WRITE_INDENT(innerIdent); WRITE_STRING("del param");
    }

    return hasExportedValue;
} // exportKnobValues

static void
exportUserKnob(int indentLevel,
               const KnobIPtr& knob,
               const QString& fullyQualifiedNodeName,
               const KnobGroupPtr& group,
               KnobPagePtr page,
               QTextStream& ts)
{
    KnobIntPtr isInt = toKnobInt(knob);
    KnobDoublePtr isDouble = toKnobDouble(knob);
    KnobBoolPtr isBool = toKnobBool(knob);
    KnobChoicePtr isChoice = toKnobChoice(knob);
    KnobColorPtr isColor = toKnobColor(knob);
    KnobStringPtr isStr = toKnobString(knob);
    KnobFilePtr isFile = toKnobFile(knob);
    KnobOutputFilePtr isOutFile = toKnobOutputFile(knob);
    KnobPathPtr isPath = toKnobPath(knob);
    KnobGroupPtr isGrp = toKnobGroup(knob);
    KnobButtonPtr isButton = toKnobButton(knob);
    KnobSeparatorPtr isSep = boost::dynamic_pointer_cast<KnobSeparator>(knob);
    KnobParametricPtr isParametric = toKnobParametric(knob);
    boost::shared_ptr<KnobI > aliasedParam;
    {
        KnobI::ListenerDimsMap listeners;
        knob->getListeners(listeners);
        if ( !listeners.empty() ) {
            KnobIPtr listener = listeners.begin()->first.lock();
            if ( listener && (listener->getAliasMaster() == knob) ) {
                aliasedParam = listener;
            }
        }
    }

    if (isInt) {
        QString createToken;
        switch ( isInt->getDimension() ) {
        case 1:
            createToken = QString::fromUtf8(".createIntParam(");
            break;
        case 2:
            createToken = QString::fromUtf8(".createInt2DParam(");
            break;
        case 3:
            createToken = QString::fromUtf8(".createInt3DParam(");
            break;
        default:
            assert(false);
            createToken = QString::fromUtf8(".createIntParam(");
            break;
        }
        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param = ") + fullyQualifiedNodeName + createToken + ESC( isInt->getName() ) +
                                                 QString::fromUtf8(", ") + ESC( isInt->getLabel() ) + QString::fromUtf8(")") );


        std::vector<int> defaultValues = isInt->getDefaultValues_mt_safe();


        assert( (int)defaultValues.size() == isInt->getDimension() );
        for (int i = 0; i < isInt->getDimension(); ++i) {
            int min = isInt->getMinimum(i);
            int max = isInt->getMaximum(i);
            int dMin = isInt->getDisplayMinimum(i);
            int dMax = isInt->getDisplayMaximum(i);
            if (min != INT_MIN) {
                WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setMinimum(") + NUM_INT(min) + QString::fromUtf8(", ") +
                                                         NUM_INT(i) + QString::fromUtf8(")") );
            }
            if (max != INT_MAX) {
                WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setMaximum(") + NUM_INT(max) + QString::fromUtf8(", ") +
                                                         NUM_INT(i) + QString::fromUtf8(")") );
            }
            if (dMin != INT_MIN) {
                WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setDisplayMinimum(") + NUM_INT(dMin) + QString::fromUtf8(", ") +
                                                         NUM_INT(i) + QString::fromUtf8(")") );
            }
            if (dMax != INT_MAX) {
                WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setDisplayMaximum(") + NUM_INT(dMax) + QString::fromUtf8(", ") +
                                                         NUM_INT(i) + QString::fromUtf8(")") );
            }
            WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setDefaultValue(") + NUM_INT(defaultValues[i]) + QString::fromUtf8(", ") + NUM_INT(i) + QString::fromUtf8(")") );
            WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.restoreDefaultValue(") + NUM_INT(i) + QString::fromUtf8(")") );
        }
    } else if (isDouble) {
        QString createToken;
        switch ( isDouble->getDimension() ) {
        case 1:
            createToken = QString::fromUtf8(".createDoubleParam(");
            break;
        case 2:
            createToken = QString::fromUtf8(".createDouble2DParam(");
            break;
        case 3:
            createToken = QString::fromUtf8(".createDouble3DParam(");
            break;
        default:
            assert(false);
            createToken = QString::fromUtf8(".createDoubleParam(");
            break;
        }
        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param = ") + fullyQualifiedNodeName + createToken + ESC( isDouble->getName() ) +
                                                 QString::fromUtf8(", ") + ESC( isDouble->getLabel() ) + QString::fromUtf8(")") );

        std::vector<double> defaultValues = isDouble->getDefaultValues_mt_safe();
        assert( (int)defaultValues.size() == isDouble->getDimension() );
        for (int i = 0; i < isDouble->getDimension(); ++i) {
            double min = isDouble->getMinimum(i);
            double max = isDouble->getMaximum(i);
            double dMin = isDouble->getDisplayMinimum(i);
            double dMax = isDouble->getDisplayMaximum(i);
            if (min != -DBL_MAX) {
                WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setMinimum(") + NUM_VALUE(min) + QString::fromUtf8(", ") +
                                                         NUM_INT(i) + QString::fromUtf8(")") );
            }
            if (max != DBL_MAX) {
                WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setMaximum(") + NUM_VALUE(max) + QString::fromUtf8(", ") +
                                                         NUM_INT(i) + QString::fromUtf8(")") );
            }
            if (dMin != -DBL_MAX) {
                WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setDisplayMinimum(") + NUM_VALUE(dMin) + QString::fromUtf8(", ") +
                                                         NUM_INT(i) + QString::fromUtf8(")") );
            }
            if (dMax != DBL_MAX) {
                WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setDisplayMaximum(") + NUM_VALUE(dMax) + QString::fromUtf8(", ") +
                                                         NUM_INT(i) + QString::fromUtf8(")") );
            }
            if (defaultValues[i] != 0.) {
                WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setDefaultValue(") + NUM_VALUE(defaultValues[i]) + QString::fromUtf8(", ") + NUM_INT(i) + QString::fromUtf8(")") );
                WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.restoreDefaultValue(") + NUM_INT(i) + QString::fromUtf8(")") );
            }
        }
    } else if (isBool) {
        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param = ") + fullyQualifiedNodeName + QString::fromUtf8(".createBooleanParam(") + ESC( isBool->getName() ) +
                                                 QString::fromUtf8(", ") + ESC( isBool->getLabel() ) + QString::fromUtf8(")") );

        std::vector<bool> defaultValues = isBool->getDefaultValues_mt_safe();
        assert( (int)defaultValues.size() == isBool->getDimension() );

        if (defaultValues[0]) {
            WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setDefaultValue(True)") );
            WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.restoreDefaultValue()") );
        }
    } else if (isChoice) {
        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param = ") + fullyQualifiedNodeName + QString::fromUtf8(".createChoiceParam(") +
                                                 ESC( isChoice->getName() ) +
                                                 QString::fromUtf8(", ") + ESC( isChoice->getLabel() ) + QString::fromUtf8(")") );

        KnobChoicePtr aliasedIsChoice = toKnobChoice(aliasedParam);

        if (!aliasedIsChoice) {
            std::vector<std::string> entries = isChoice->getEntries_mt_safe();
            std::vector<std::string> helps = isChoice->getEntriesHelp_mt_safe();
            if (entries.size() > 0) {
                if ( helps.empty() ) {
                    for (U32 i = 0; i < entries.size(); ++i) {
                        helps.push_back("");
                    }
                }
                WRITE_INDENT(indentLevel); ts << "entries = [ (" << ESC(entries[0]) << ", " << ESC(helps[0]) << "),\n";
                for (U32 i = 1; i < entries.size(); ++i) {
                    QString endToken = (i == entries.size() - 1) ? QString::fromUtf8(")]") : QString::fromUtf8("),");
                    WRITE_INDENT(indentLevel); WRITE_STRING(QString::fromUtf8("(") + ESC(entries[i]) + QString::fromUtf8(", ") + ESC(helps[i]) + endToken);
                }
                WRITE_INDENT(indentLevel); WRITE_STATIC_LINE("param.setOptions(entries)");
                WRITE_INDENT(indentLevel); WRITE_STATIC_LINE("del entries");
            }
            std::vector<int> defaultValues = isChoice->getDefaultValues_mt_safe();
            assert( (int)defaultValues.size() == isChoice->getDimension() );
            if (defaultValues[0] != 0) {
                std::string entryStr = isChoice->getEntry(defaultValues[0]);
                WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setDefaultValue(") + ESC(entryStr) + QString::fromUtf8(")") );
                WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.restoreDefaultValue()") );
            }
        } else {
            std::vector<int> defaultValues = isChoice->getDefaultValues_mt_safe();
            assert( (int)defaultValues.size() == isChoice->getDimension() );
            if (defaultValues[0] != 0) {
                WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setDefaultValue(") + NUM_INT(defaultValues[0]) + QString::fromUtf8(")") );
                WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.restoreDefaultValue()"));
            }
        }
    } else if (isColor) {
        QString hasAlphaStr = (isColor->getDimension() == 4) ? QString::fromUtf8("True") : QString::fromUtf8("False");
        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param = ") + fullyQualifiedNodeName + QString::fromUtf8(".createColorParam(") + ESC( isColor->getName() ) +
                                                 QString::fromUtf8(", ") + ESC( isColor->getLabel() ) + QString::fromUtf8(", ") + hasAlphaStr +  QString::fromUtf8(")") );


        std::vector<double> defaultValues = isColor->getDefaultValues_mt_safe();
        assert( (int)defaultValues.size() == isColor->getDimension() );

        for (int i = 0; i < isColor->getDimension(); ++i) {
            double min = isColor->getMinimum(i);
            double max = isColor->getMaximum(i);
            double dMin = isColor->getDisplayMinimum(i);
            double dMax = isColor->getDisplayMaximum(i);
            if (min != -DBL_MAX) {
                WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setMinimum(") + NUM_VALUE(min) + QString::fromUtf8(", ") +
                                                         NUM_INT(i) + QString::fromUtf8(")") );
            }
            if (max != DBL_MAX) {
                WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setMaximum(") + NUM_VALUE(max) + QString::fromUtf8(", ") +
                                                         NUM_INT(i) + QString::fromUtf8(")") );
            }
            if (dMin != -DBL_MAX) {
                WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setDisplayMinimum(") + NUM_VALUE(dMin) + QString::fromUtf8(", ") +
                                                         NUM_INT(i) + QString::fromUtf8(")") );
            }
            if (dMax != DBL_MAX) {
                WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setDisplayMaximum(") + NUM_VALUE(dMax) + QString::fromUtf8(", ") +
                                                         NUM_INT(i) + QString::fromUtf8(")") );
            }
            if (defaultValues[i] != 0.) {
                WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setDefaultValue(") + NUM_VALUE(defaultValues[i]) + QString::fromUtf8(", ") + NUM_INT(i) + QString::fromUtf8(")") );
                WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.restoreDefaultValue(") + NUM_INT(i) + QString::fromUtf8(")") );
            }
        }
    } else if (isButton) {
        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param = ") + fullyQualifiedNodeName + QString::fromUtf8(".createButtonParam(") +
                                                 ESC( isButton->getName() ) +
                                                 QString::fromUtf8(", ") + ESC( isButton->getLabel() ) + QString::fromUtf8(")") );
    } else if (isSep) {
        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param = ") + fullyQualifiedNodeName + QString::fromUtf8(".createSeparatorParam(") +
                                                 ESC( isSep->getName() ) +
                                                 QString::fromUtf8(", ") + ESC( isSep->getLabel() ) + QString::fromUtf8(")") );
    } else if (isStr) {
        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param = ") + fullyQualifiedNodeName + QString::fromUtf8(".createStringParam(") +
                                                 ESC( isStr->getName() ) +
                                                 QString::fromUtf8(", ") + ESC( isStr->getLabel() ) + QString::fromUtf8(")") );
        QString typeStr;
        if ( isStr->isLabel() ) {
            typeStr = QString::fromUtf8("eStringTypeLabel");
        } else if ( isStr->isMultiLine() ) {
            if ( isStr->usesRichText() ) {
                typeStr = QString::fromUtf8("eStringTypeRichTextMultiLine");
            } else {
                typeStr = QString::fromUtf8("eStringTypeMultiLine");
            }
        } else if ( isStr->isCustomKnob() ) {
            typeStr = QString::fromUtf8("eStringTypeCustom");
        } else {
            typeStr = QString::fromUtf8("eStringTypeDefault");
        }
        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setType(NatronEngine.StringParam.TypeEnum.") + typeStr + QString::fromUtf8(")") );

        std::vector<std::string> defaultValues = isStr->getDefaultValues_mt_safe();
        assert( (int)defaultValues.size() == isStr->getDimension() );
        QString def = QString::fromUtf8( defaultValues[0].c_str() );
        if ( !def.isEmpty() ) {
            WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setDefaultValue(") + ESC(def) + QString::fromUtf8(")") );
            WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.restoreDefaultValue()") );
        }
    } else if (isFile) {
        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param = ") + fullyQualifiedNodeName + QString::fromUtf8(".createFileParam(") + ESC( isFile->getName() ) +
                                                 QString::fromUtf8(", ") + ESC( isFile->getLabel() ) + QString::fromUtf8(")") );
        QString seqStr = isFile->isInputImageFile() ? QString::fromUtf8("True") : QString::fromUtf8("False");
        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setSequenceEnabled(") + seqStr + QString::fromUtf8(")") );

        std::vector<std::string> defaultValues = isFile->getDefaultValues_mt_safe();
        assert( (int)defaultValues.size() == isFile->getDimension() );
        QString def = QString::fromUtf8( defaultValues[0].c_str() );
        if ( !def.isEmpty() ) {
            WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setDefaultValue()") + def + QString::fromUtf8(")") );
            WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.restoreDefaultValue()") );
        }
    } else if (isOutFile) {
        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param = ") + fullyQualifiedNodeName + QString::fromUtf8(".createOutputFileParam(") +
                                                 ESC( isOutFile->getName() ) +
                                                 QString::fromUtf8(", ") + ESC( isOutFile->getLabel() ) + QString::fromUtf8(")") );
        assert(isOutFile);
        QString seqStr = isOutFile->isOutputImageFile() ? QString::fromUtf8("True") : QString::fromUtf8("False");
        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setSequenceEnabled(") + seqStr + QString::fromUtf8(")") );

        std::vector<std::string> defaultValues = isOutFile->getDefaultValues_mt_safe();
        assert( (int)defaultValues.size() == isOutFile->getDimension() );
        QString def = QString::fromUtf8( defaultValues[0].c_str() );
        if ( !def.isEmpty() ) {
            WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setDefaultValue(") + ESC(def) + QString::fromUtf8(")") );
            WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.restoreDefaultValue()") );
        }
    } else if (isPath) {
        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param = ") + fullyQualifiedNodeName + QString::fromUtf8(".createPathParam(") +
                                                 ESC( isPath->getName() ) +
                                                 QString::fromUtf8(", ") + ESC( isPath->getLabel() ) + QString::fromUtf8(")") );
        if ( isPath->isMultiPath() ) {
            WRITE_INDENT(indentLevel); WRITE_STRING("param.setAsMultiPathTable()");
        }

        std::vector<std::string> defaultValues = isPath->getDefaultValues_mt_safe();
        assert( (int)defaultValues.size() == isPath->getDimension() );
        QString def = QString::fromUtf8( defaultValues[0].c_str() );
        if ( !def.isEmpty() ) {
            WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setDefaultValue(") + ESC(def) + QString::fromUtf8(")") );
            WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.restoreDefaultValue()") );
        }
    } else if (isGrp) {
        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param = ") + fullyQualifiedNodeName + QString::fromUtf8(".createGroupParam(") +
                                                 ESC( isGrp->getName() ) +
                                                 QString::fromUtf8(", ") + ESC( isGrp->getLabel() ) + QString::fromUtf8(")") );
        if ( isGrp->isTab() ) {
            WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setAsTab()") );
        }
    } else if (isParametric) {
        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param = ") + fullyQualifiedNodeName + QString::fromUtf8(".createParametricParam(") +
                                                 ESC( isParametric->getName() ) +
                                                 QString::fromUtf8(", ") + ESC( isParametric->getLabel() ) +  QString::fromUtf8(", ") +
                                                 NUM_INT( isParametric->getDimension() ) + QString::fromUtf8(")") );
    }

    WRITE_STATIC_LINE("");

    if (group) {
        QString grpFullName = fullyQualifiedNodeName + QString::fromUtf8(".") + QString::fromUtf8( group->getName().c_str() );
        WRITE_INDENT(indentLevel); WRITE_STATIC_LINE("# Add the param to the group, no need to add it to the page");
        WRITE_INDENT(indentLevel); WRITE_STRING( grpFullName + QString::fromUtf8(".addParam(param)") );
    } else {
        assert(page);
        QString pageFullName = fullyQualifiedNodeName + QString::fromUtf8(".") + QString::fromUtf8( page->getName().c_str() );
        WRITE_INDENT(indentLevel); WRITE_STATIC_LINE("# Add the param to the page");
        WRITE_INDENT(indentLevel); WRITE_STRING( pageFullName + QString::fromUtf8(".addParam(param)") );
    }

    WRITE_STATIC_LINE("");
    WRITE_INDENT(indentLevel); WRITE_STATIC_LINE("# Set param properties");

    QString help = QString::fromUtf8( knob->getHintToolTip().c_str() );
    if ( !aliasedParam || ( aliasedParam->getHintToolTip() != knob->getHintToolTip() ) ) {
        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setHelp(") + ESC(help) + QString::fromUtf8(")") );
    }


    bool previousHasNewLineActivated = true;
    KnobsVec children;
    if (group) {
        children = group->getChildren();
    } else if (page) {
        children = page->getChildren();
    }
    for (U32 i = 0; i < children.size(); ++i) {
        if (children[i] == knob) {
            if (i > 0) {
                previousHasNewLineActivated = children[i - 1]->isNewLineActivated();
            }
            break;
        }
    }

    if (previousHasNewLineActivated) {
        WRITE_INDENT(indentLevel); WRITE_STRING("param.setAddNewLine(True)");
    } else {
        WRITE_INDENT(indentLevel); WRITE_STRING("param.setAddNewLine(False)");
    }

    if ( !knob->getIsPersistent() ) {
        WRITE_INDENT(indentLevel); WRITE_STRING("param.setPersistent(False)");
    }

    if ( !knob->getEvaluateOnChange() ) {
        WRITE_INDENT(indentLevel); WRITE_STRING("param.setEvaluateOnChange(False)");
    }

    if ( knob->canAnimate() ) {
        QString animStr = knob->isAnimationEnabled() ? QString::fromUtf8("True") : QString::fromUtf8("False");
        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setAnimationEnabled(") + animStr + QString::fromUtf8(")") );
    }

    exportKnobValues(indentLevel, knob, QString(), false, ts);
    WRITE_INDENT(indentLevel); WRITE_STRING( fullyQualifiedNodeName + QString::fromUtf8(".") + QString::fromUtf8( knob->getName().c_str() ) + QString::fromUtf8(" = param") );
    WRITE_INDENT(indentLevel); WRITE_STATIC_LINE("del param");

    WRITE_STATIC_LINE("");

    if (isGrp) {
        KnobsVec children =  isGrp->getChildren();
        for (KnobsVec::const_iterator it3 = children.begin(); it3 != children.end(); ++it3) {
            exportUserKnob(indentLevel, *it3, fullyQualifiedNodeName, isGrp, page, ts);
        }
    }
} // exportUserKnob

static void
exportBezierPointAtTime(int indentLevel,
                        const BezierCPPtr& point,
                        bool isFeather,
                        double time,
                        int idx,
                        QTextStream& ts)
{
    QString token = isFeather ? QString::fromUtf8("bezier.setFeatherPointAtIndex(") : QString::fromUtf8("bezier.setPointAtIndex(");
    double x, y, lx, ly, rx, ry;

    point->getPositionAtTime(false, time, ViewIdx(0), &x, &y);
    point->getLeftBezierPointAtTime(false, time, ViewIdx(0), &lx, &ly);
    point->getRightBezierPointAtTime(false, time, ViewIdx(0), &rx, &ry);

    WRITE_INDENT(indentLevel); WRITE_STRING( token + NUM_INT(idx) + QString::fromUtf8(", ") +
                                             NUM_TIME(time) + QString::fromUtf8(", ") + NUM_VALUE(x) + QString::fromUtf8(", ") +
                                             NUM_VALUE(y) + QString::fromUtf8(", ") + NUM_VALUE(lx) + QString::fromUtf8(", ") +
                                             NUM_VALUE(ly) + QString::fromUtf8(", ") + NUM_VALUE(rx) + QString::fromUtf8(", ") +
                                             NUM_VALUE(ry) + QString::fromUtf8(")") );
}

static void
exportRotoLayer(int indentLevel,
                const std::list<RotoItemPtr >& items,
                const RotoLayerPtr& layer,
                QTextStream& ts)
{
    QString parentLayerName = QString::fromUtf8( layer->getScriptName().c_str() ) + QString::fromUtf8("_layer");

    for (std::list<RotoItemPtr >::const_iterator it = items.begin(); it != items.end(); ++it) {
        RotoLayerPtr isLayer = toRotoLayer(*it);
        BezierPtr isBezier = toBezier(*it);

        if (isBezier) {
            double time;
            const std::list<BezierCPPtr >& cps = isBezier->getControlPoints();
            const std::list<BezierCPPtr >& fps = isBezier->getFeatherPoints();

            if ( cps.empty() ) {
                continue;
            }

            time = cps.front()->getKeyframeTime(false, 0);

            WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("bezier = roto.createBezier(0, 0, ") + NUM_TIME(time) + QString::fromUtf8(")") );
            WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("bezier.setScriptName(") + ESC( isBezier->getScriptName() ) + QString::fromUtf8(")") );
            WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("bezier.setLabel(") + ESC( isBezier->getLabel() ) + QString::fromUtf8(")") );
            QString lockedStr = isBezier->getLocked() ? QString::fromUtf8("True") : QString::fromUtf8("False");
            WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("bezier.setLocked(") + lockedStr + QString::fromUtf8(")") );
            QString visibleStr = isBezier->isGloballyActivated() ? QString::fromUtf8("True") : QString::fromUtf8("False");
            WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("bezier.setVisible(") + visibleStr + QString::fromUtf8(")") );

            KnobBoolPtr activatedKnob = isBezier->getActivatedKnob();
            exportKnobValues(indentLevel, activatedKnob, QString::fromUtf8("bezier.getActivatedParam()"), true, ts);

            KnobDoublePtr featherDist = isBezier->getFeatherKnob();
            exportKnobValues(indentLevel, featherDist, QString::fromUtf8("bezier.getFeatherDistanceParam()"), true, ts);

            KnobDoublePtr opacityKnob = isBezier->getOpacityKnob();
            exportKnobValues(indentLevel, opacityKnob, QString::fromUtf8("bezier.getOpacityParam()"), true, ts);

            KnobDoublePtr fallOffKnob = isBezier->getFeatherFallOffKnob();
            exportKnobValues(indentLevel, fallOffKnob, QString::fromUtf8("bezier.getFeatherFallOffParam()"), true, ts);

            KnobColorPtr colorKnob = isBezier->getColorKnob();
            exportKnobValues(indentLevel, colorKnob, QString::fromUtf8("bezier.getColorParam()"), true, ts);

            KnobChoicePtr compositing = isBezier->getOperatorKnob();
            exportKnobValues(indentLevel, compositing, QString::fromUtf8("bezier.getCompositingOperatorParam()"), true, ts);


            WRITE_INDENT(indentLevel); WRITE_STRING( parentLayerName + QString::fromUtf8(".addItem(bezier)") );
            WRITE_INDENT(indentLevel); WRITE_STATIC_LINE("");

            assert( cps.size() == fps.size() );

            std::set<double> kf;
            isBezier->getKeyframeTimes(&kf);

            //the last python call already registered the first control point
            int nbPts = cps.size() - 1;
            WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("for i in range(0, ") + NUM_INT(nbPts) + QString::fromUtf8("):") );
            WRITE_INDENT(2); WRITE_STATIC_LINE("bezier.addControlPoint(0,0)");

            ///Now that all points are created position them
            int idx = 0;
            std::list<BezierCPPtr >::const_iterator fpIt = fps.begin();
            for (std::list<BezierCPPtr >::const_iterator it2 = cps.begin(); it2 != cps.end(); ++it2, ++fpIt, ++idx) {
                for (std::set<double>::iterator it3 = kf.begin(); it3 != kf.end(); ++it3) {
                    exportBezierPointAtTime(indentLevel, *it2, false, *it3, idx, ts);
                    exportBezierPointAtTime(indentLevel, *fpIt, true, *it3, idx, ts);
                }
                if ( kf.empty() ) {
                    exportBezierPointAtTime(indentLevel, *it2, false, time, idx, ts);
                    exportBezierPointAtTime(indentLevel, *fpIt, true, time, idx, ts);
                }
            }
            if ( isBezier->isCurveFinished() ) {
                WRITE_INDENT(indentLevel); WRITE_STRING("bezier.setCurveFinished(True)");
            }

            WRITE_INDENT(indentLevel); WRITE_STATIC_LINE("del bezier");
        } else {
            QString name =  QString::fromUtf8( isLayer->getScriptName().c_str() );
            QString layerName = name + QString::fromUtf8("_layer");
            WRITE_INDENT(indentLevel); WRITE_STRING( name + QString::fromUtf8(" = roto.createLayer()") );
            WRITE_INDENT(indentLevel); WRITE_STRING( layerName +  QString::fromUtf8(".setScriptName(") + ESC(name) + QString::fromUtf8(")") );
            WRITE_INDENT(indentLevel); WRITE_STRING( layerName + QString::fromUtf8(".setLabel(") + ESC( isLayer->getLabel() ) + QString::fromUtf8(")") );
            QString lockedStr = isLayer->getLocked() ? QString::fromUtf8("True") : QString::fromUtf8("False");
            WRITE_INDENT(indentLevel); WRITE_STRING( layerName + QString::fromUtf8(".setLocked()") + lockedStr + QString::fromUtf8(")") );
            QString visibleStr = isLayer->isGloballyActivated() ? QString::fromUtf8("True") : QString::fromUtf8("False");
            WRITE_INDENT(indentLevel); WRITE_STRING( layerName + QString::fromUtf8(".setVisible(") + visibleStr + QString::fromUtf8(")") );

            WRITE_INDENT(indentLevel); WRITE_STRING(parentLayerName + QString::fromUtf8(".addItem(") + layerName);

            const std::list<RotoItemPtr >& items = isLayer->getItems();
            exportRotoLayer(indentLevel, items, isLayer, ts);
            WRITE_INDENT(indentLevel); WRITE_STRING(QString::fromUtf8("del ") + layerName);
        }
        WRITE_STATIC_LINE("");
    }
} // exportRotoLayer

static void
exportAllNodeKnobs(int indentLevel,
                   const NodePtr& node,
                   QTextStream& ts)
{
    const KnobsVec& knobs = node->getKnobs();
    std::list<KnobPagePtr> userPages;

    for (KnobsVec::const_iterator it2 = knobs.begin(); it2 != knobs.end(); ++it2) {
        if ( (*it2)->getIsPersistent() && !(*it2)->isUserKnob() ) {
            QString getParamStr  = QString::fromUtf8("lastNode.getParam(\"");
            const std::string& paramName =  (*it2)->getName();
            if ( paramName.empty() ) {
                continue;
            }
            getParamStr += QString::fromUtf8( paramName.c_str() );
            getParamStr += QString::fromUtf8("\")");
            if ( exportKnobValues(indentLevel, *it2, getParamStr, true, ts) ) {
                WRITE_STATIC_LINE("");
            }
        }

        if ( (*it2)->isUserKnob() ) {
            KnobPagePtr isPage = toKnobPage(*it2);
            if (isPage) {
                userPages.push_back(isPage);
            }
        }
    } // for (KnobsVec::const_iterator it2 = knobs.begin(); it2 != knobs.end(); ++it2)
    if ( !userPages.empty() ) {
        WRITE_STATIC_LINE("");
        WRITE_INDENT(indentLevel); WRITE_STATIC_LINE("# Create the user parameters");
    }
    for (std::list<KnobPagePtr>::iterator it2 = userPages.begin(); it2 != userPages.end(); ++it2) {
        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("lastNode.") + QString::fromUtf8( (*it2)->getName().c_str() ) +
                                                 QString::fromUtf8(" = lastNode.createPageParam(") + ESC( (*it2)->getName() ) + QString::fromUtf8(", ") +
                                                 ESC( (*it2)->getLabel() ) + QString::fromUtf8(")") );
        KnobsVec children =  (*it2)->getChildren();
        for (KnobsVec::const_iterator it3 = children.begin(); it3 != children.end(); ++it3) {
            exportUserKnob(indentLevel, *it3, QString::fromUtf8("lastNode"), KnobGroupPtr(), *it2, ts);
        }
    }

    if ( !userPages.empty() ) {
        WRITE_INDENT(indentLevel); WRITE_STATIC_LINE("# Refresh the GUI with the newly created parameters");
        std::list<std::string> pagesOrdering = node->getPagesOrder();
        if ( !pagesOrdering.empty() ) {
            QString line = QString::fromUtf8("lastNode.setPagesOrder([");
            std::list<std::string>::iterator next = pagesOrdering.begin();
            ++next;
            for (std::list<std::string>::iterator it = pagesOrdering.begin(); it != pagesOrdering.end(); ++it) {
                line += QLatin1Char('\'');
                line += QString::fromUtf8( it->c_str() );
                line += QLatin1Char('\'');
                if ( next != pagesOrdering.end() ) {
                    line += QString::fromUtf8(", ");
                    ++next;
                }
            }
            line += QString::fromUtf8("])");
            WRITE_INDENT(indentLevel); WRITE_STRING(line);
        }
        WRITE_INDENT(indentLevel); WRITE_STATIC_LINE("lastNode.refreshUserParamsGUI()");
    }

    RotoContextPtr roto = node->getRotoContext();
    if (roto) {
        const std::list<RotoLayerPtr >& layers = roto->getLayers();

        if ( !layers.empty() ) {
            WRITE_INDENT(indentLevel); WRITE_STATIC_LINE("# For the roto node, create all layers and beziers");
            WRITE_INDENT(indentLevel); WRITE_STRING("roto = lastNode.getRotoContext()");
            RotoLayerPtr baseLayer = layers.front();
            QString baseLayerName = QString::fromUtf8( baseLayer->getScriptName().c_str() );
            QString baseLayerToken = baseLayerName + QString::fromUtf8("_layer");
            WRITE_INDENT(indentLevel); WRITE_STRING( baseLayerToken + QString::fromUtf8(" = roto.getBaseLayer()") );

            WRITE_INDENT(indentLevel); WRITE_STRING( baseLayerToken + QString::fromUtf8(".setScriptName(") + ESC(baseLayerName) + QString::fromUtf8(")") );
            WRITE_INDENT(indentLevel); WRITE_STRING( baseLayerToken + QString::fromUtf8(".setLabel(") + ESC( baseLayer->getLabel() ) + QString::fromUtf8(")") );
            QString lockedStr = baseLayer->getLocked() ? QString::fromUtf8("True") : QString::fromUtf8("False");
            WRITE_INDENT(indentLevel); WRITE_STRING( baseLayerToken + QString::fromUtf8(".setLocked(") + lockedStr + QString::fromUtf8(")") );
            QString visibleStr = baseLayer->isGloballyActivated() ? QString::fromUtf8("True") : QString::fromUtf8("False");
            WRITE_INDENT(indentLevel); WRITE_STRING( baseLayerToken + QString::fromUtf8(".setVisible(") + visibleStr + QString::fromUtf8(")") );
            exportRotoLayer(indentLevel, baseLayer->getItems(), baseLayer,  ts);
            WRITE_INDENT(indentLevel); WRITE_STRING(QString::fromUtf8("del ") + baseLayerToken);
            WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("del roto") );
        }
    }
} // exportAllNodeKnobs

static bool
exportKnobLinks(int indentLevel,
                const NodePtr& groupNode,
                const NodePtr& node,
                const QString& groupName,
                const QString& nodeName,
                QTextStream& ts)
{
    bool hasExportedLink = false;
    const KnobsVec& knobs = node->getKnobs();

    for (KnobsVec::const_iterator it2 = knobs.begin(); it2 != knobs.end(); ++it2) {
        QString paramName = nodeName + QString::fromUtf8(".getParam(\"") + QString::fromUtf8( (*it2)->getName().c_str() ) + QString::fromUtf8("\")");
        bool hasDefined = false;

        //Check for alias link
        KnobIPtr alias = (*it2)->getAliasMaster();
        if (alias) {
            if (!hasDefined) {
                WRITE_INDENT(indentLevel); WRITE_STRING(QString::fromUtf8("param = ") + paramName);
                hasDefined = true;
            }
            hasExportedLink = true;

            EffectInstancePtr aliasHolder = toEffectInstance( alias->getHolder() );
            assert(aliasHolder);
            if (!aliasHolder) {
                throw std::logic_error("exportKnobLinks");
            }
            QString aliasName;
            if ( aliasHolder == groupNode->getEffectInstance() ) {
                aliasName = groupName;
            } else {
                aliasName = groupName + QString::fromUtf8( aliasHolder->getNode()->getScriptName_mt_safe().c_str() );
            }
            aliasName += QString::fromUtf8(".getParam(");
            aliasName += ESC(QString::fromUtf8( alias->getName().c_str() ));
            aliasName += QString::fromUtf8(")");

            WRITE_INDENT(indentLevel); WRITE_STRING( aliasName + QString::fromUtf8(".setAsAlias(param)") );
        } else {
            for (int i = 0; i < (*it2)->getDimension(); ++i) {
                std::string expr = (*it2)->getExpression(i);
                QString hasRetVar = (*it2)->isExpressionUsingRetVariable(i) ? QString::fromUtf8("True") : QString::fromUtf8("False");
                if ( !expr.empty() ) {
                    if (!hasDefined) {
                        WRITE_INDENT(indentLevel); WRITE_STRING(QString::fromUtf8("param = ") + paramName);
                        hasDefined = true;
                    }
                    hasExportedLink = true;
                    WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.setExpression(") + ESC(expr) + QString::fromUtf8(", ") +
                                                             hasRetVar + QString::fromUtf8(", ") + NUM_INT(i) + QString::fromUtf8(")") );
                }

                std::pair<int, KnobIPtr > master = (*it2)->getMaster(i);
                if (master.second) {
                    if (!hasDefined) {
                        WRITE_INDENT(indentLevel); WRITE_STRING(QString::fromUtf8("param = ") + paramName);
                        hasDefined = true;
                    }
                    hasExportedLink = true;

                    EffectInstancePtr masterHolder = toEffectInstance( master.second->getHolder() );
                    assert(masterHolder);
                    if (!masterHolder) {
                        throw std::logic_error("exportKnobLinks");
                    }
                    QString masterName;
                    if ( masterHolder == groupNode->getEffectInstance() ) {
                        masterName = groupName;
                    } else {
                        masterName = groupName + QString::fromUtf8( masterHolder->getNode()->getScriptName_mt_safe().c_str() );
                    }
                    masterName += QLatin1String(".getParam(");
                    masterName += ESC(QString::fromUtf8( master.second->getName().c_str() ));
                    masterName += QLatin1String(")");


                    WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("param.slaveTo(") +  masterName + QString::fromUtf8(", ") +
                                                             NUM_INT(i) + QString::fromUtf8(", ") + NUM_INT(master.first) + QString::fromUtf8(")") );
                }
            }
        }
        if (hasDefined) {
            WRITE_INDENT(indentLevel); WRITE_STATIC_LINE("del param");
        }
    }

    return hasExportedLink;
} // exportKnobLinks

void
NodeCollection::exportGroupInternal(int indentLevel,
                                    const NodePtr& upperLevelGroupNode,
                                    const QString& upperLevelGroupName,
                                    QTextStream& ts)
{
    WRITE_INDENT(indentLevel); WRITE_STATIC_LINE("# Create all nodes in the group");
    WRITE_STATIC_LINE("");

    NodeGroup* isGroup = dynamic_cast<NodeGroup*>(this);
    NodePtr groupNode;
    if (isGroup) {
        groupNode = isGroup->getNode();
    }

    QString groupName = upperLevelGroupName + QString::fromUtf8("group");

    if (isGroup) {
        WRITE_INDENT(indentLevel); WRITE_STATIC_LINE("# Create the parameters of the group node the same way we did for all internal nodes");
        WRITE_INDENT(indentLevel); WRITE_STRING(QString::fromUtf8("lastNode = ") + groupName);
        exportAllNodeKnobs(indentLevel, isGroup->getNode(), ts);
        WRITE_INDENT(indentLevel); WRITE_STATIC_LINE("del lastNode");
        WRITE_STATIC_LINE("");
    }


    NodesList nodes = getNodes();
    NodesList exportedNodes;

    ///Re-order nodes so we're sure Roto nodes get exported in the end since they may depend on Trackers
    NodesList rotos;
    NodesList newNodes;
    for (NodesList::iterator it = nodes.begin(); it != nodes.end(); ++it) {
        if ( (*it)->isRotoPaintingNode() ) {
            rotos.push_back(*it);
        } else {
            newNodes.push_back(*it);
        }
    }
    newNodes.insert( newNodes.end(), rotos.begin(), rotos.end() );

    for (NodesList::iterator it = newNodes.begin(); it != newNodes.end(); ++it) {
        ///Don't create viewer while exporting
        ViewerInstancePtr isViewer = (*it)->isEffectViewerInstance();
        if (isViewer) {
            continue;
        }
        if ( !(*it)->isActivated() ) {
            continue;
        }

        exportedNodes.push_back(*it);

        ///Let the parent of the multi-instance node create the children
        if ( (*it)->getParentMultiInstance() ) {
            continue;
        }

        QString nodeName = QString::fromUtf8( (*it)->getPluginID().c_str() );

        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("# Start of node ") + ESC( (*it)->getScriptName_mt_safe() ) );
        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("lastNode = app.createNode(") + ESC(nodeName) + QString::fromUtf8(", ") +
                                                 NUM_INT( (*it)->getPlugin()->getMajorVersion() ) + QString::fromUtf8(", ") + groupName +
                                                 QString::fromUtf8(")") );
        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("lastNode.setScriptName(") + ESC( (*it)->getScriptName_mt_safe() ) + QString::fromUtf8(")") );
        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("lastNode.setLabel(") + ESC( (*it)->getLabel_mt_safe() ) + QString::fromUtf8(")") );
        double x, y;
        (*it)->getPosition(&x, &y);
        double w, h;
        (*it)->getSize(&w, &h);
        // a precision of 1 pixel is enough for the position on the nodegraph
        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("lastNode.setPosition(") + NUM_PIXEL(x) + QString::fromUtf8(", ") + NUM_PIXEL(y) + QString::fromUtf8(")") );
        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("lastNode.setSize(") + NUM_PIXEL(w) + QString::fromUtf8(", ") + NUM_PIXEL(h) + QString::fromUtf8(")") );

        double r, g, b;
        (*it)->getColor(&r, &g, &b);
        // a precision of 3 digits is enough for the node coloe
        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("lastNode.setColor(") + NUM_COLOR(r) + QString::fromUtf8(", ") + NUM_COLOR(g) + QString::fromUtf8(", ") + NUM_COLOR(b) +  QString::fromUtf8(")") );

        std::list<ImageComponents> userComps;
        (*it)->getUserCreatedComponents(&userComps);
        for (std::list<ImageComponents>::iterator it2 = userComps.begin(); it2 != userComps.end(); ++it2) {
            const std::vector<std::string>& channels = it2->getComponentsNames();
            QString compStr = QString::fromUtf8("[");
            for (std::size_t i = 0; i < channels.size(); ++i) {
                compStr.append( ESC(channels[i]) );
                if ( i < (channels.size() - 1) ) {
                    compStr.push_back( QLatin1Char(',') );
                }
            }
            compStr.push_back( QLatin1Char(']') );
            WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("lastNode.addUserPlane(") + ESC( it2->getLayerName() ) + QString::fromUtf8(", ") + compStr +  QString::fromUtf8(")") );
        }

        QString nodeNameInScript = groupName + QString::fromUtf8( (*it)->getScriptName_mt_safe().c_str() );
        WRITE_INDENT(indentLevel); WRITE_STRING( nodeNameInScript + QString::fromUtf8(" = lastNode") );
        WRITE_STATIC_LINE("");
        exportAllNodeKnobs(indentLevel, *it, ts);
        WRITE_INDENT(indentLevel); WRITE_STRING("del lastNode");
        WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("# End of node ") + ESC( (*it)->getScriptName_mt_safe() ) );
        WRITE_STATIC_LINE("");

        std::list< NodePtr > children;
        (*it)->getChildrenMultiInstance(&children);
        if ( !children.empty() ) {
            WRITE_INDENT(indentLevel); WRITE_STATIC_LINE("# Create children if the node is a multi-instance such as a tracker");
            for (std::list< NodePtr > ::iterator it2 = children.begin(); it2 != children.end(); ++it2) {
                if ( (*it2)->isActivated() ) {
                    WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("lastNode = ") + nodeNameInScript + QString::fromUtf8(".createChild()") );
                    WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("lastNode.setScriptName(\"") + QString::fromUtf8( (*it2)->getScriptName_mt_safe().c_str() ) + QString::fromUtf8("\")") );
                    WRITE_INDENT(indentLevel); WRITE_STRING( QString::fromUtf8("lastNode.setLabel(\"") + QString::fromUtf8( (*it2)->getLabel_mt_safe().c_str() ) + QString::fromUtf8("\")") );
                    exportAllNodeKnobs(indentLevel, *it2, ts);
                    WRITE_INDENT(indentLevel); WRITE_STRING( nodeNameInScript + QString::fromUtf8(".") + QString::fromUtf8( (*it2)->getScriptName_mt_safe().c_str() ) + QString::fromUtf8(" = lastNode") );
                    WRITE_INDENT(indentLevel); WRITE_STRING("del lastNode");
                }
            }
            WRITE_STATIC_LINE("");
        }

        NodeGroupPtr isGrp = (*it)->isEffectNodeGroup();
        if (isGrp) {
            WRITE_INDENT(indentLevel); WRITE_STRING(groupName + QString::fromUtf8("group = ") + nodeNameInScript);
            isGrp->exportGroupInternal(indentLevel, groupNode, groupName, ts);
            WRITE_STATIC_LINE("");
        }
    }


    WRITE_INDENT(indentLevel); WRITE_STATIC_LINE("# Now that all nodes are created we can connect them together, restore expressions");
    bool hasConnected = false;
    for (NodesList::iterator it = exportedNodes.begin(); it != exportedNodes.end(); ++it) {
        QString nodeQualifiedName( groupName + QString::fromUtf8( (*it)->getScriptName_mt_safe().c_str() ) );

        if ( !(*it)->getParentMultiInstance() ) {
            for (int i = 0; i < (*it)->getMaxInputCount(); ++i) {
                NodePtr inputNode = (*it)->getRealInput(i);
                if (inputNode) {
                    hasConnected = true;
                    QString inputQualifiedName( groupName  + QString::fromUtf8( inputNode->getScriptName_mt_safe().c_str() ) );
                    WRITE_INDENT(indentLevel); WRITE_STRING( nodeQualifiedName + QString::fromUtf8(".connectInput(") + NUM_INT(i) +
                                                             QString::fromUtf8(", ") + inputQualifiedName + QString::fromUtf8(")") );
                }
            }
        }
    }
    if (hasConnected) {
        WRITE_STATIC_LINE("");
    }

    bool hasExported = false;

    for (NodesList::iterator it = exportedNodes.begin(); it != exportedNodes.end(); ++it) {
        QString nodeQualifiedName( groupName + QString::fromUtf8( (*it)->getScriptName_mt_safe().c_str() ) );
        if ( exportKnobLinks(indentLevel, groupNode, *it, groupName, nodeQualifiedName, ts) ) {
            hasExported = true;
        }
    }
    if (hasExported) {
        WRITE_STATIC_LINE("");
    }
    if (isGroup) {
        exportKnobLinks(indentLevel, upperLevelGroupNode ? upperLevelGroupNode : groupNode, groupNode,
                        upperLevelGroupNode ? upperLevelGroupName : groupName, groupName, ts);
    }
} // exportGroupInternal

void
NodeCollection::exportGroupToPython(const QString& pluginID,
                                    const QString& pluginLabel,
                                    const QString& pluginDescription,
                                    const QString& pluginIconPath,
                                    const QString& pluginGrouping,
                                    int version,
                                    QString& output)
{
    QString extModule(pluginLabel);

    extModule.append( QString::fromUtf8("Ext") );

    QTextStream ts(&output);
    // coding must be set in first or second line, see https://www.python.org/dev/peps/pep-0263/
    WRITE_STATIC_LINE("# -*- coding: utf-8 -*-");
    WRITE_STATIC_LINE("# DO NOT EDIT THIS FILE");
    QString descline = QString( QString::fromUtf8("# This file was automatically generated by %1 PyPlug exporter version %2.") ).arg( QString::fromUtf8(NATRON_APPLICATION_NAME) ).arg(NATRON_PYPLUG_EXPORTER_VERSION);
    WRITE_STRING(descline);
    WRITE_STATIC_LINE();
    QString handWrittenStr = QString::fromUtf8("# Hand-written code should be added in a separate file named %1.py").arg(extModule);
    WRITE_STRING(handWrittenStr);
    WRITE_STATIC_LINE("# See http://natron.readthedocs.org/en/master/groups.html#adding-hand-written-code-callbacks-etc");
    WRITE_STATIC_LINE("# Note that Viewers are never exported");
    WRITE_STATIC_LINE();
    WRITE_STATIC_LINE("import " NATRON_ENGINE_PYTHON_MODULE_NAME);
    WRITE_STATIC_LINE("import sys");
    WRITE_STATIC_LINE("");
    WRITE_STATIC_LINE("# Try to import the extensions file where callbacks and hand-written code should be located.");
    WRITE_STATIC_LINE("try:");


    WRITE_INDENT(1); WRITE_STRING( QString::fromUtf8("from ") + extModule + QString::fromUtf8(" import *") );
    WRITE_STRING("except ImportError:");
    WRITE_INDENT(1); WRITE_STRING("pass");
    WRITE_STATIC_LINE("");

    WRITE_STATIC_LINE("def getPluginID():");
    WRITE_INDENT(1); WRITE_STRING( QString::fromUtf8("return \"") + pluginID + QString::fromUtf8("\"") );
    WRITE_STATIC_LINE("");

    WRITE_STATIC_LINE("def getLabel():");
    WRITE_INDENT(1); WRITE_STRING( QString::fromUtf8("return ") + ESC(pluginLabel) );
    WRITE_STATIC_LINE("");

    WRITE_STATIC_LINE("def getVersion():");
    WRITE_INDENT(1); WRITE_STRING(QString::fromUtf8("return ") + NUM_INT(version));
    WRITE_STATIC_LINE("");

    if ( !pluginIconPath.isEmpty() ) {
        WRITE_STATIC_LINE("def getIconPath():");
        WRITE_INDENT(1); WRITE_STRING( QString::fromUtf8("return ") + ESC(pluginIconPath) );
        WRITE_STATIC_LINE("");
    }

    WRITE_STATIC_LINE("def getGrouping():");
    WRITE_INDENT(1); WRITE_STRING( QString::fromUtf8("return \"") + pluginGrouping + QString::fromUtf8("\"") );
    WRITE_STATIC_LINE("");

    if ( !pluginDescription.isEmpty() ) {
        WRITE_STATIC_LINE("def getPluginDescription():");
        WRITE_INDENT(1); WRITE_STRING( QString::fromUtf8("return ") + ESC(pluginDescription) );
        WRITE_STATIC_LINE("");
    }


    WRITE_STATIC_LINE("def createInstance(app,group):");

    exportGroupInternal(1, NodePtr(), QString(), ts);

    ///Import user hand-written code
    WRITE_INDENT(1); WRITE_STATIC_LINE("try:");
    WRITE_INDENT(2); WRITE_STRING( QString::fromUtf8("extModule = sys.modules[") + ESC(extModule) + QString::fromUtf8("]") );
    WRITE_INDENT(1); WRITE_STATIC_LINE("except KeyError:");
    WRITE_INDENT(2); WRITE_STATIC_LINE("extModule = None");

    QString testAttr = QString::fromUtf8("if extModule is not None and hasattr(extModule ,\"createInstanceExt\") and hasattr(extModule.createInstanceExt,\"__call__\"):");
    WRITE_INDENT(1); WRITE_STRING(testAttr);
    WRITE_INDENT(2); WRITE_STRING("extModule.createInstanceExt(app,group)");
} // NodeCollection::exportGroupToPython

NATRON_NAMESPACE_EXIT;

NATRON_NAMESPACE_USING;
#include "moc_NodeGroup.cpp"
