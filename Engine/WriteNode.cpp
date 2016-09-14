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

#include "WriteNode.h"

#include "Global/QtCompat.h"

#if !defined(SBK_RUN) && !defined(Q_MOC_RUN)
GCC_DIAG_UNUSED_LOCAL_TYPEDEFS_OFF
#include <boost/algorithm/string/predicate.hpp> // iequals
GCC_DIAG_UNUSED_LOCAL_TYPEDEFS_ON
#endif

#if !defined(Q_MOC_RUN) && !defined(SBK_RUN)
GCC_DIAG_UNUSED_LOCAL_TYPEDEFS_OFF
GCC_DIAG_OFF(unused-parameter)
// /opt/local/include/boost/serialization/smart_cast.hpp:254:25: warning: unused parameter 'u' [-Wunused-parameter]
#include <boost/archive/xml_iarchive.hpp>
#include <boost/archive/xml_oarchive.hpp>
// /usr/local/include/boost/serialization/shared_ptr.hpp:112:5: warning: unused typedef 'boost_static_assert_typedef_112' [-Wunused-local-typedef]
#include <boost/serialization/split_member.hpp>
#include <boost/serialization/version.hpp>
GCC_DIAG_UNUSED_LOCAL_TYPEDEFS_ON
GCC_DIAG_ON(unused-parameter)
#endif

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QtCore/QCoreApplication>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include <ofxNatron.h>

#include "Engine/AppInstance.h"
#include "Engine/AppManager.h"
#include "Engine/CreateNodeArgs.h"
#include "Engine/Node.h"
#include "Engine/KnobTypes.h"
#include "Engine/KnobFile.h"
#include "Engine/NodeSerialization.h"
#include "Engine/KnobSerialization.h" // createDefaultValueForParam
#include "Engine/OutputSchedulerThread.h"
#include "Engine/Plugin.h"
#include "Engine/Project.h"
#include "Engine/ReadNode.h"
#include "Engine/Settings.h"

//The plug-in that is instanciated whenever this node is created and doesn't point to any valid or known extension
#define WRITE_NODE_DEFAULT_WRITER PLUGINID_OFX_WRITEOIIO
#define kPluginSelectorParamEntryDefault "Default"

NATRON_NAMESPACE_ENTER;

//Generic Writer
#define kParamFilename kOfxImageEffectFileParamName
#define kParamOutputFormat kNatronParamFormatChoice
#define kParamFormatType "formatType"
#define kParamFormatSize kNatronParamFormatSize
#define kParamFormatPar kNatronParamFormatPar
#define kParamFrameRange "frameRange"
#define kParamFirstFrame "firstFrame"
#define kParamLastFrame "lastFrame"
#define kParamInputPremult "inputPremult"
#define kParamClipInfo "clipInfo"
#define kParamOutputSpaceLabel "File Colorspace"
#define kParamClipToProject "clipToProject"
#define kNatronOfxParamProcessR      "NatronOfxParamProcessR"
#define kNatronOfxParamProcessG      "NatronOfxParamProcessG"
#define kNatronOfxParamProcessB      "NatronOfxParamProcessB"
#define kNatronOfxParamProcessA      "NatronOfxParamProcessA"

//Generic OCIO
#define kOCIOParamConfigFile "ocioConfigFile"
#define kOCIOParamInputSpace "ocioInputSpace"
#define kOCIOParamOutputSpace "ocioOutputSpace"
#define kOCIOParamInputSpaceChoice "ocioInputSpaceIndex"
#define kOCIOParamOutputSpaceChoice "ocioOutputSpaceIndex"
#define kOCIOHelpButton "ocioHelp"
#define kOCIOHelpLooksButton "ocioHelpLooks"
#define kOCIOHelpDisplaysButton "ocioHelpDisplays"
#define kOCIOParamContext "Context"

/*
   These are names of knobs that are defined in GenericWriter and that should stay on the interface
   no matter what the internal Reader is.
 */
struct GenericKnob
{
    const char* scriptName;
    bool mustKeepValue;
};

static GenericKnob genericWriterKnobNames[] =
{
    {kParamFilename, false},
    {kParamOutputFormat, true},
    {kParamFormatType, true},
    {kParamFormatSize, true},
    {kParamFormatPar, true},
    {kParamFrameRange, true},
    {kParamFirstFrame, true},
    {kParamLastFrame, true},
    {kParamInputPremult, true},
    {kParamClipInfo, false},
    {kParamOutputSpaceLabel, false},
    {kParamClipToProject, true},
    {kNatronOfxParamProcessR, true},
    {kNatronOfxParamProcessG, true},
    {kNatronOfxParamProcessB, true},
    {kNatronOfxParamProcessA, true},


    {kOCIOParamConfigFile, true},
    {kOCIOParamInputSpace, false},
    {kOCIOParamOutputSpace, false},
    {kOCIOParamInputSpaceChoice, false},
    {kOCIOParamOutputSpaceChoice, false},
    {kOCIOHelpButton, false},
    {kOCIOHelpLooksButton, false},
    {kOCIOHelpDisplaysButton, false},
    {kOCIOParamContext, false},
    {0, false}
};
static bool
isGenericKnob(const std::string& knobName,
              bool *mustSerialize)
{
    int i = 0;

    while (genericWriterKnobNames[i].scriptName) {
        if (genericWriterKnobNames[i].scriptName == knobName) {
            *mustSerialize = genericWriterKnobNames[i].mustKeepValue;

            return true;
        }
        ++i;
    }

    return false;
}

bool
WriteNode::isBundledWriter(const std::string& pluginID,
                           bool wasProjectCreatedWithLowerCaseIDs)
{
    if (wasProjectCreatedWithLowerCaseIDs) {
        // Natron 1.x has plugin ids stored in lowercase
        return boost::iequals(pluginID, PLUGINID_OFX_WRITEOIIO) ||
        boost::iequals(pluginID, PLUGINID_OFX_WRITEFFMPEG) ||
        boost::iequals(pluginID, PLUGINID_OFX_WRITEPFM) ||
        boost::iequals(pluginID, PLUGINID_OFX_WRITEPNG);
    }

    return pluginID == PLUGINID_OFX_WRITEOIIO ||
    pluginID == PLUGINID_OFX_WRITEFFMPEG ||
    pluginID == PLUGINID_OFX_WRITEPFM ||
    pluginID == PLUGINID_OFX_WRITEPNG;
}

bool
WriteNode::isBundledWriter(const std::string& pluginID)
{
    return isBundledWriter( pluginID, getApp()->wasProjectCreatedWithLowerCaseIDs() );
}

struct WriteNodePrivate
{
    Q_DECLARE_TR_FUNCTIONS(WriteNode)

public:
    WriteNode* _publicInterface; // can not be a smart ptr
    NodeWPtr embeddedPlugin, readBackNode, inputNode, outputNode;
    std::list<KnobSerializationPtr> genericKnobsSerialization;
    KnobOutputFileWPtr outputFileKnob;

    //Thiese are knobs owned by the ReadNode and not the Reader
    KnobIntWPtr frameIncrKnob;
    KnobBoolWPtr readBackKnob;
    KnobChoiceWPtr pluginSelectorKnob;
    KnobStringWPtr pluginIDStringKnob;
    KnobSeparatorWPtr separatorKnob;
    KnobButtonWPtr renderButtonKnob;
    std::list<KnobIWPtr > writeNodeKnobs;

    //MT only
    int creatingWriteNode;


    WriteNodePrivate(WriteNode* publicInterface)
        : _publicInterface(publicInterface)
        , embeddedPlugin()
        , readBackNode()
        , inputNode()
        , outputNode()
        , genericKnobsSerialization()
        , outputFileKnob()
        , frameIncrKnob()
        , pluginSelectorKnob()
        , pluginIDStringKnob()
        , separatorKnob()
        , renderButtonKnob()
        , writeNodeKnobs()
        , creatingWriteNode(0)
    {
    }

    void placeWriteNodeKnobsInPage();

    void createReadNodeAndConnectGraph(const std::string& filename);

    void createWriteNode(bool throwErrors, const std::string& filename, const NodeSerializationPtr& serialization);

    void destroyWriteNode();

    void cloneGenericKnobs();

    void refreshPluginSelectorKnob();

    void createDefaultWriteNode();

    bool checkEncoderCreated(double time, ViewIdx view);

    void setReadNodeOriginalFrameRange();
};


class SetCreatingWriterRAIIFlag
{
    WriteNodePrivate* _p;

public:

    SetCreatingWriterRAIIFlag(WriteNodePrivate* p)
        : _p(p)
    {
        ++p->creatingWriteNode;
    }

    ~SetCreatingWriterRAIIFlag()
    {
        --_p->creatingWriteNode;
    }
};


WriteNode::WriteNode(const NodePtr& n)
    : NodeGroup(n)
    , _imp( new WriteNodePrivate(this) )
{
    setSupportsRenderScaleMaybe(eSupportsYes);
}

WriteNode::~WriteNode()
{
}

NodePtr
WriteNode::getEmbeddedWriter() const
{
    return _imp->embeddedPlugin.lock();
}

void
WriteNode::setEmbeddedWriter(const NodePtr& node)
{
    _imp->embeddedPlugin = node;
}

void
WriteNodePrivate::placeWriteNodeKnobsInPage()
{
    KnobIPtr pageKnob = _publicInterface->getKnobByName("Controls");
    KnobPagePtr isPage = toKnobPage(pageKnob);

    if (!isPage) {
        return;
    }
    for (std::list<KnobIWPtr >::iterator it = writeNodeKnobs.begin(); it != writeNodeKnobs.end(); ++it) {
        KnobIPtr knob = it->lock();
        knob->setParentKnob( KnobIPtr() );
        isPage->removeKnob(knob);
    }
    KnobsVec children = isPage->getChildren();
    int index = -1;
    for (std::size_t i = 0; i < children.size(); ++i) {
        if (children[i]->getName() == kParamLastFrame) {
            index = i;
            break;
        }
    }
    if (index != -1) {
        ++index;
        for (std::list<KnobIWPtr >::iterator it = writeNodeKnobs.begin(); it != writeNodeKnobs.end(); ++it) {
            KnobIPtr knob = it->lock();
            isPage->insertKnob(index, knob);
            ++index;
        }
    }

    // Find the separatorKnob in the page and if the next parameter is also a separator, hide it
    int foundSep = -1;
    for (std::size_t i = 0; i < children.size(); ++i) {
        if (children[i]== separatorKnob.lock()) {
            foundSep = i;
            break;
        }
    }
    if (foundSep != -1) {
        ++foundSep;
        if (foundSep < (int)children.size()) {
            bool isSecret = children[foundSep]->getIsSecret();
            while (isSecret && foundSep < (int)children.size()) {
                ++foundSep;
                isSecret = children[foundSep]->getIsSecret();
            }
            if (foundSep < (int)children.size()) {
                separatorKnob.lock()->setSecret(toKnobSeparator(children[foundSep]));
            } else {
                separatorKnob.lock()->setSecret(true);
            }

        } else {
            separatorKnob.lock()->setSecret(true);
        }
    }

    //Set the render button as the last knob
    KnobButtonPtr renderB = renderButtonKnob.lock();
    if (renderB) {
        renderB->setParentKnob( KnobIPtr() );
        isPage->removeKnob( renderB );
        isPage->addKnob(renderB);
    }
}

void
WriteNodePrivate::cloneGenericKnobs()
{
    const KnobsVec& knobs = _publicInterface->getKnobs();

    for (std::list<KnobSerializationPtr>::iterator it = genericKnobsSerialization.begin(); it != genericKnobsSerialization.end(); ++it) {
        KnobIPtr serializedKnob = (*it)->getKnob();
        for (KnobsVec::const_iterator it2 = knobs.begin(); it2 != knobs.end(); ++it2) {
            if ( (*it2)->getName() == serializedKnob->getName() ) {
                KnobChoicePtr isChoice = toKnobChoice(*it2);
                KnobChoicePtr serializedIsChoice = toKnobChoice( serializedKnob );;
                if (isChoice && serializedIsChoice) {
                    const ChoiceExtraData* extraData = dynamic_cast<const ChoiceExtraData*>( (*it)->getExtraData() );
                    assert(extraData);
                    if (extraData) {
                        isChoice->choiceRestoration(serializedIsChoice, extraData);
                    }
                } else {
                    (*it2)->clone( serializedKnob );
                }
                (*it2)->setSecret( serializedKnob->getIsSecret() );
                if ( (*it2)->getDimension() == serializedKnob->getDimension() ) {
                    for (int i = 0; i < (*it2)->getDimension(); ++i) {
                        (*it2)->setEnabled( i, serializedKnob->isEnabled(i) );
                    }
                }

                break;
            }
        }
    }
}

void
WriteNodePrivate::destroyWriteNode()
{
    assert( QThread::currentThread() == qApp->thread() );
    if (!embeddedPlugin.lock()) {
        return;
    }
    KnobsVec knobs = _publicInterface->getKnobs();

    genericKnobsSerialization.clear();

    std::string serializationString;
    try {
        std::ostringstream ss;
        boost::archive::xml_oarchive oArchive(ss);
        std::list<KnobSerializationPtr> serialized;
        for (KnobsVec::iterator it = knobs.begin(); it != knobs.end(); ++it) {
            if ( !(*it)->isDeclaredByPlugin() ) {
                continue;
            }

            //If it is a knob of this WriteNode, do not destroy it
            bool isWriteNodeKnob = false;
            for (std::list<KnobIWPtr >::iterator it2 = writeNodeKnobs.begin(); it2 != writeNodeKnobs.end(); ++it2) {
                if (it2->lock() == *it) {
                    isWriteNodeKnob = true;
                    break;
                }
            }
            if (isWriteNodeKnob) {
                continue;
            }

            //Keep pages around they will be re-used
            KnobPagePtr isPage = toKnobPage(*it);
            if (isPage) {
                continue;
            }

            //This is a knob of the Writer plug-in

            //Serialize generic knobs and keep them around until we create a new Writer plug-in
            bool mustSerializeKnob;
            bool isGeneric = isGenericKnob( (*it)->getName(), &mustSerializeKnob );
            if (!isGeneric || mustSerializeKnob) {

                if (!isGeneric && !(*it)->getDefaultIsSecret()) {
                    // Don't save the secret state otherwise some knobs could be invisible when cloning the serialization even if we change format
                    (*it)->setSecret(false);
                }
                KnobSerializationPtr s( new KnobSerialization(*it) );
                serialized.push_back(s);
            }
            if (!isGeneric) {
                try {
                    _publicInterface->deleteKnob(*it, false);
                } catch (...) {
                    
                }
            }
        }

        int n = (int)serialized.size();
        oArchive << boost::serialization::make_nvp("numItems", n);
        for (std::list<KnobSerializationPtr>::const_iterator it = serialized.begin(); it!= serialized.end(); ++it) {
            oArchive << boost::serialization::make_nvp("item", **it);

        }
        serializationString = ss.str();

    } catch (...) {
        assert(false);
    }

    try {
        std::stringstream ss(serializationString);
        boost::archive::xml_iarchive iArchive(ss);
        int n ;
        iArchive >> boost::serialization::make_nvp("numItems", n);
        for (int i = 0; i < n; ++i) {
            KnobSerializationPtr s(new KnobSerialization);
            iArchive >> boost::serialization::make_nvp("item", *s);
            genericKnobsSerialization.push_back(s);

        }
    } catch (const std::exception& e) {
        qDebug() << e.what();
        assert(false);
    }
    

    //This will remove the GUI of non generic parameters
    _publicInterface->recreateKnobs(true);

    embeddedPlugin.reset();
    readBackNode.reset();
} // WriteNodePrivate::destroyWriteNode

void
WriteNodePrivate::createDefaultWriteNode()
{
    NodeGroupPtr isGrp = toNodeGroup( _publicInterface->shared_from_this() );
    CreateNodeArgs args( WRITE_NODE_DEFAULT_WRITER, isGrp );
    args.setProperty(kCreateNodeArgsPropNoNodeGUI, true);
    args.setProperty(kCreateNodeArgsPropOutOfProject, true);
    args.setProperty(kCreateNodeArgsPropSilent, true);
    args.setProperty(kCreateNodeArgsPropMetaNodeContainer, _publicInterface->getNode());
    args.setProperty<std::string>(kCreateNodeArgsPropNodeInitialName, "defaultWriteNodeWriter");
    args.setProperty<bool>(kCreateNodeArgsPropAllowNonUserCreatablePlugins, true);
    //args.paramValues.push_back(createDefaultValueForParam<std::string>(kOfxImageEffectFileParamName, filePattern));
    embeddedPlugin = _publicInterface->getApp()->createNode(args);

    if ( !embeddedPlugin.lock() ) {
        QString error = tr("The IO.ofx.bundle OpenFX plug-in is required to use this node, make sure it is installed.");
        throw std::runtime_error( error.toStdString() );
    }


    //We need to explcitly refresh the Python knobs since we attached the embedded node knobs into this node.
    _publicInterface->getNode()->declarePythonFields();

    //Destroy it to keep the default parameters
    destroyWriteNode();
    placeWriteNodeKnobsInPage();
    separatorKnob.lock()->setSecret(true);
}

bool
WriteNodePrivate::checkEncoderCreated(double time,
                                      ViewIdx view)
{
    KnobOutputFilePtr fileKnob = outputFileKnob.lock();

    assert(fileKnob);
    std::string pattern = fileKnob->generateFileNameAtTime( std::floor(time + 0.5), ViewSpec( view.value() ) ).toStdString();
    if ( pattern.empty() ) {
        _publicInterface->setPersistentMessage( eMessageTypeError, tr("Filename is empty.").toStdString() );

        return false;
    }
    if ( !embeddedPlugin.lock() ) {
        QString s = tr("Encoder was not created for %1. Check that the file exists and its format is supported.")
                    .arg( QString::fromUtf8( pattern.c_str() ) );
        _publicInterface->setPersistentMessage( eMessageTypeError, s.toStdString() );

        return false;
    }

    return true;
}

static std::string
getFileNameFromSerialization(const std::list<KnobSerializationPtr>& serializations)
{
    std::string filePattern;

    for (std::list<KnobSerializationPtr>::const_iterator it = serializations.begin(); it != serializations.end(); ++it) {
        if ( (*it)->getKnob()->getName() == kOfxImageEffectFileParamName ) {
            KnobStringBasePtr isString = toKnobStringBase( (*it)->getKnob() );
            assert(isString);
            if (isString) {
                filePattern = isString->getValue();
            }
            break;
        }
    }

    return filePattern;
}

void
WriteNodePrivate::setReadNodeOriginalFrameRange()
{
    NodePtr readNode = readBackNode.lock();

    if (!readNode) {
        return;
    }
    NodePtr writeNode = embeddedPlugin.lock();
    if (!writeNode) {
        return;
    }
    double first, last;
    writeNode->getEffectInstance()->getFrameRange_public(writeNode->getEffectInstance()->getHash(), &first, &last);

    {
        KnobIPtr originalFrameRangeKnob = readNode->getKnobByName(kReaderParamNameOriginalFrameRange);
        assert(originalFrameRangeKnob);
        KnobIntPtr originalFrameRange = toKnobInt( originalFrameRangeKnob );
        if (originalFrameRange) {
            originalFrameRange->setValues(first, last, ViewSpec::all(), eValueChangedReasonNatronInternalEdited);
        }
    }
    {
        KnobIPtr firstFrameKnob = readNode->getKnobByName(kParamFirstFrame);
        assert(firstFrameKnob);
        KnobIntPtr firstFrame = toKnobInt( firstFrameKnob );
        if (firstFrame) {
            firstFrame->setValue(first);
        }
    }
    {
        KnobIPtr lastFrameKnob = readNode->getKnobByName(kParamLastFrame);
        assert(lastFrameKnob);
        KnobIntPtr lastFrame = toKnobInt( lastFrameKnob );
        if (lastFrame) {
            lastFrame->setValue(last);
        }
    }
}

void
WriteNodePrivate::createReadNodeAndConnectGraph(const std::string& filename)
{
    QString qpattern = QString::fromUtf8( filename.c_str() );
    std::string ext = QtCompat::removeFileExtension(qpattern).toLower().toStdString();
    NodeGroupPtr isGrp = toNodeGroup( _publicInterface->shared_from_this() );
    std::string readerPluginID = appPTR->getReaderPluginIDForFileType(ext);
    NodePtr writeNode = embeddedPlugin.lock();

    readBackNode.reset();
    if ( !readerPluginID.empty() ) {
        CreateNodeArgs args(readerPluginID, isGrp );
        args.setProperty(kCreateNodeArgsPropNoNodeGUI, true);
        args.setProperty(kCreateNodeArgsPropOutOfProject, true);
        args.setProperty<std::string>(kCreateNodeArgsPropNodeInitialName, "internalDecoderNode");
        args.setProperty<bool>(kCreateNodeArgsPropAllowNonUserCreatablePlugins, true);

        //Set a pre-value for the inputfile knob only if it did not exist
        if ( !filename.empty() ) {
            args.addParamDefaultValue<std::string>(kOfxImageEffectFileParamName, filename);
        }

        if (writeNode) {
            double first, last;
            writeNode->getEffectInstance()->getFrameRange_public(writeNode->getEffectInstance()->getHash(), &first, &last);
            std::vector<int> originalRange(2);
            originalRange[0] = (int)first;
            originalRange[1] = (int)last;
            args.addParamDefaultValueN<int>(kReaderParamNameOriginalFrameRange, originalRange);
            args.addParamDefaultValue<int>(kParamFirstFrame, (int)first);
            args.addParamDefaultValue<int>(kParamFirstFrame, (int)last);
        }


        readBackNode = _publicInterface->getApp()->createNode(args);
    }

    NodePtr input = inputNode.lock(), output = outputNode.lock();
    assert(input && output);
    if (writeNode) {
        writeNode->replaceInput(input, 0);
        NodePtr readNode = readBackNode.lock();
        if (readNode) {
            output->replaceInput(readNode, 0);
            readNode->replaceInput(writeNode, 0);
            // sync the output colorspace of the reader from input colorspace of the writer

            KnobIPtr outputWriteColorSpace = writeNode->getKnobByName(kOCIOParamOutputSpace);
            KnobIPtr inputReadColorSpace = readNode->getKnobByName(kNatronReadNodeOCIOParamInputSpace);
            if (inputReadColorSpace && outputWriteColorSpace) {
                inputReadColorSpace->slaveTo(0, outputWriteColorSpace, 0);
            }
        } else {
            output->replaceInput(writeNode, 0);
        }
    } else {
        output->replaceInput(input, 0);
    }
} // WriteNodePrivate::createReadNodeAndConnectGraph

void
WriteNodePrivate::createWriteNode(bool throwErrors,
                                  const std::string& filename,
                                  const NodeSerializationPtr& serialization)
{
    if (creatingWriteNode) {
        return;
    }

    NodeGroupPtr isGrp = toNodeGroup( _publicInterface->shared_from_this() );
    NodePtr input = inputNode.lock(), output = outputNode.lock();
    //NodePtr maskInput;
    assert( (input && output) || (!input && !output) );
    if (!output) {
        CreateNodeArgs args(PLUGINID_NATRON_OUTPUT, isGrp);
        args.setProperty(kCreateNodeArgsPropNoNodeGUI, true);
        args.setProperty(kCreateNodeArgsPropOutOfProject, true);
        args.setProperty<bool>(kCreateNodeArgsPropAllowNonUserCreatablePlugins, true);
        output = _publicInterface->getApp()->createNode(args);
        try {
            output->setScriptName("Output");
        } catch (...) {
        }

        assert(output);
        outputNode = output;
    }
    if (!input) {
        CreateNodeArgs args(PLUGINID_NATRON_INPUT, isGrp);
        args.setProperty(kCreateNodeArgsPropNoNodeGUI, true);
        args.setProperty(kCreateNodeArgsPropOutOfProject, true);
        args.setProperty<std::string>(kCreateNodeArgsPropNodeInitialName, "Source");
        args.setProperty<bool>(kCreateNodeArgsPropAllowNonUserCreatablePlugins, true);
        input = _publicInterface->getApp()->createNode(args);
        assert(input);
        inputNode = input;
    }

    SetCreatingWriterRAIIFlag creatingNode__(this);
    QString qpattern = QString::fromUtf8( filename.c_str() );
    std::string ext = QtCompat::removeFileExtension(qpattern).toLower().toStdString();
    KnobStringPtr pluginIDKnob = pluginIDStringKnob.lock();
    std::string writerPluginID = pluginIDKnob->getValue();

    if ( writerPluginID.empty() ) {
        KnobChoicePtr pluginChoiceKnob = pluginSelectorKnob.lock();
        int pluginChoice_i = pluginChoiceKnob->getValue();
        if (pluginChoice_i == 0) {
            //Use default
            writerPluginID = appPTR->getWriterPluginIDForFileType(ext);
        } else {
            std::vector<std::string> entries = pluginChoiceKnob->getEntries_mt_safe();
            if ( (pluginChoice_i >= 0) && ( pluginChoice_i < (int)entries.size() ) ) {
                writerPluginID = entries[pluginChoice_i];
            }
        }
    }


    //Destroy any previous reader
    //This will store the serialization of the generic knobs
    destroyWriteNode();

    bool defaultFallback = false;

    //Find the appropriate reader
    if (writerPluginID.empty() && !serialization) {
        //Couldn't find any reader
        if ( !ext.empty() ) {
            QString message = tr("No plugin capable of encoding %1 was found.").arg( QString::fromUtf8( ext.c_str() ) );
            //Dialogs::errorDialog(tr("Read").toStdString(), message.toStdString(), false);
            if (throwErrors) {
                throw std::runtime_error( message.toStdString() );
            }
        }
        defaultFallback = true;
    } else {
        if ( writerPluginID.empty() ) {
            writerPluginID = WRITE_NODE_DEFAULT_WRITER;
        }
        CreateNodeArgs args(writerPluginID, isGrp );
        args.setProperty(kCreateNodeArgsPropNoNodeGUI, true);
        args.setProperty(kCreateNodeArgsPropOutOfProject, true);
        args.setProperty<std::string>(kCreateNodeArgsPropNodeInitialName, "internalEncoderNode");
        args.setProperty<NodeSerializationPtr >(kCreateNodeArgsPropNodeSerialization, serialization);
        args.setProperty<NodePtr>(kCreateNodeArgsPropMetaNodeContainer, _publicInterface->getNode());
        args.setProperty<bool>(kCreateNodeArgsPropAllowNonUserCreatablePlugins, true);
        //Set a pre-value for the inputfile knob only if it did not exist
        if (!filename.empty() && !serialization) {
            args.addParamDefaultValue<std::string>(kOfxImageEffectFileParamName, filename);
        }
        if (serialization) {
            args.setProperty<bool>(kCreateNodeArgsPropSilent, true);
        }

        embeddedPlugin = _publicInterface->getApp()->createNode(args);
        if (pluginIDKnob) {
            pluginIDKnob->setValue(writerPluginID);
        }
        placeWriteNodeKnobsInPage();
        separatorKnob.lock()->setSecret(false);


        //We need to explcitly refresh the Python knobs since we attached the embedded node knobs into this node.
        _publicInterface->getNode()->declarePythonFields();
    }


    if ( !embeddedPlugin.lock() ) {
        defaultFallback = true;
    }

    if (defaultFallback) {
        createDefaultWriteNode();
    }

    // Make the write node be a pass-through while we do not render
    NodePtr writeNode = embeddedPlugin.lock();
    if (writeNode) {
        writeNode->setNodeDisabled(true);
    }

    bool readFromFile = readBackKnob.lock()->getValue();
    if (readFromFile) {
        createReadNodeAndConnectGraph(filename);
    } else {
        NodePtr input = inputNode.lock(), output = outputNode.lock();
        if (writeNode) {
            output->replaceInput(writeNode, 0);
            writeNode->replaceInput(input, 0);
        } else {
            output->replaceInput(input, 0);
        }
    }

    _publicInterface->getNode()->findPluginFormatKnobs();

    //Clone the old values of the generic knobs
    cloneGenericKnobs();


    NodePtr thisNode = _publicInterface->getNode();
    //Refresh accepted bitdepths on the node
    thisNode->refreshAcceptedBitDepths();

    //Refresh accepted components
    thisNode->initializeInputs();

    //This will refresh the GUI with this Reader specific parameters
    _publicInterface->recreateKnobs(true);

    KnobIPtr knob = writeNode ? writeNode->getKnobByName(kOfxImageEffectFileParamName) : _publicInterface->getKnobByName(kOfxImageEffectFileParamName);
    if (knob) {
        outputFileKnob = toKnobOutputFile(knob);
    }
} // WriteNodePrivate::createWriteNode

void
WriteNodePrivate::refreshPluginSelectorKnob()
{
    KnobOutputFilePtr fileKnob = outputFileKnob.lock();

    assert(fileKnob);
    std::string filePattern = fileKnob->getValue();
    std::vector<std::string> entries, help;
    entries.push_back(kPluginSelectorParamEntryDefault);
    help.push_back("Use the default plug-in chosen from the Preferences to write this file format");

    QString qpattern = QString::fromUtf8( filePattern.c_str() );
    std::string ext = QtCompat::removeFileExtension(qpattern).toLower().toStdString();
    std::string pluginID;
    if ( !ext.empty() ) {
        pluginID = appPTR->getWriterPluginIDForFileType(ext);
        IOPluginSetForFormat writersForFormat;
        appPTR->getWritersForFormat(ext, &writersForFormat);

        // Reverse it so that we sort them by decreasing score order
        for (IOPluginSetForFormat::reverse_iterator it = writersForFormat.rbegin(); it != writersForFormat.rend(); ++it) {
            Plugin* plugin = appPTR->getPluginBinary(QString::fromUtf8( it->pluginID.c_str() ), -1, -1, false);
            entries.push_back( plugin->getPluginID().toStdString() );
            std::stringstream ss;
            ss << "Use " << plugin->getPluginLabel().toStdString() << " version ";
            ss << plugin->getMajorVersion() << "." << plugin->getMinorVersion();
            ss << " to write this file format";
            help.push_back( ss.str() );
        }
    }

    KnobChoicePtr pluginChoice = pluginSelectorKnob.lock();

    pluginChoice->populateChoices(entries, help);
    pluginChoice->blockValueChanges();
    pluginChoice->resetToDefaultValue(0);
    pluginChoice->unblockValueChanges();
    if (entries.size() <= 2) {
        pluginChoice->setSecret(true);
    } else {
        pluginChoice->setSecret(false);
    }

    KnobStringPtr pluginIDKnob = pluginIDStringKnob.lock();
    pluginIDKnob->blockValueChanges();
    pluginIDKnob->setValue(pluginID);
    pluginIDKnob->unblockValueChanges();
}

bool
WriteNode::isWriter() const
{
    return true;
}

bool
WriteNode::isVideoWriter() const
{
    NodePtr p = _imp->embeddedPlugin.lock();

    return p ? p->getPluginID() == PLUGINID_OFX_WRITEFFMPEG : false;
}

bool
WriteNode::isGenerator() const
{
    return false;
}

bool
WriteNode::isOutput() const
{
    return true;
}

bool
WriteNode::getCreateChannelSelectorKnob() const
{
    return false;
}

bool
WriteNode::isHostChannelSelectorSupported(bool* /*defaultR*/,
                                          bool* /*defaultG*/,
                                          bool* /*defaultB*/,
                                          bool* /*defaultA*/) const
{
    return false;
}

int
WriteNode::getMajorVersion() const
{ return 1; }

int
WriteNode::getMinorVersion() const
{ return 0; }

std::string
WriteNode::getPluginID() const
{ return PLUGINID_NATRON_WRITE; }

std::string
WriteNode::getPluginLabel() const
{ return "Write"; }

std::string
WriteNode::getPluginDescription() const
{
    return "Node used to write images or videos on disk. The image/video is identified by its filename and "
           "its extension. Given the extension, the Writer selected from the Preferences to encode that specific format will be used. ";
}

void
WriteNode::getPluginGrouping(std::list<std::string>* grouping) const
{
    grouping->push_back(PLUGIN_GROUP_IMAGE);
}

void
WriteNode::initializeKnobs()
{
    KnobPagePtr controlpage = AppManager::createKnob<KnobPage>( shared_from_this(), tr("Controls") );


    ///Find a  "lastFrame" parameter and add it after it
    KnobIntPtr frameIncrKnob = AppManager::createKnob<KnobInt>( shared_from_this(), tr(kNatronWriteParamFrameStepLabel) );

    frameIncrKnob->setName(kNatronWriteParamFrameStep);
    frameIncrKnob->setHintToolTip( tr(kNatronWriteParamFrameStepHint) );
    frameIncrKnob->setAnimationEnabled(false);
    frameIncrKnob->setMinimum(1);
    frameIncrKnob->setDefaultValue(1);
    controlpage->addKnob(frameIncrKnob);
    /*if (mainPage) {
        std::vector< KnobIPtr > children = mainPage->getChildren();
        bool foundLastFrame = false;
        for (std::size_t i = 0; i < children.size(); ++i) {
            if (children[i]->getName() == "lastFrame") {
                mainPage->insertKnob(i + 1, frameIncrKnob);
                foundLastFrame = true;
                break;
            }
        }
        if (!foundLastFrame) {
            mainPage->addKnob(frameIncrKnob);
        }
       }*/
    _imp->frameIncrKnob = frameIncrKnob;
    _imp->writeNodeKnobs.push_back(frameIncrKnob);

    KnobBoolPtr readBack = AppManager::createKnob<KnobBool>( shared_from_this(), tr(kNatronWriteParamReadBackLabel) );
    readBack->setAnimationEnabled(false);
    readBack->setName(kNatronWriteParamReadBack);
    readBack->setHintToolTip( tr(kNatronWriteParamReadBackHint) );
    readBack->setEvaluateOnChange(false);
    readBack->setDefaultValue(false);
    controlpage->addKnob(readBack);
    _imp->readBackKnob = readBack;
    _imp->writeNodeKnobs.push_back(readBack);


    KnobChoicePtr pluginSelector = AppManager::createKnob<KnobChoice>( shared_from_this(), tr("Encoder") );
    pluginSelector->setAnimationEnabled(false);
    pluginSelector->setName(kNatronWriteNodeParamEncodingPluginChoice);
    pluginSelector->setHintToolTip( tr("Select the internal encoder plug-in used for this file format. By default this uses "
                                       "the plug-in selected for this file extension in the Preferences.") );
    pluginSelector->setEvaluateOnChange(false);
    _imp->pluginSelectorKnob = pluginSelector;
    controlpage->addKnob(pluginSelector);

    _imp->writeNodeKnobs.push_back(pluginSelector);

    KnobSeparatorPtr separator = AppManager::createKnob<KnobSeparator>( shared_from_this(), tr("Encoder Options") );
    separator->setName("encoderOptionsSeparator");
    separator->setHintToolTip( tr("Below can be found parameters that are specific to the Writer plug-in.") );
    controlpage->addKnob(separator);
    _imp->separatorKnob = separator;
    _imp->writeNodeKnobs.push_back(separator);

    KnobStringPtr pluginID = AppManager::createKnob<KnobString>( shared_from_this(), tr("PluginID") );
    pluginID->setAnimationEnabled(false);
    pluginID->setName(kNatronWriteNodeParamEncodingPluginID);
    pluginID->setSecretByDefault(true);
    controlpage->addKnob(pluginID);
    _imp->pluginIDStringKnob = pluginID;
    _imp->writeNodeKnobs.push_back(pluginID);
} // WriteNode::initializeKnobs

void
WriteNode::onEffectCreated(bool mayCreateFileDialog,
                           const CreateNodeArgs& args)
{

    RenderEnginePtr engine = getRenderEngine();
    assert(engine);
    QObject::connect(engine.get(), SIGNAL(renderFinished(int)), this, SLOT(onSequenceRenderFinished()));

    if ( !_imp->renderButtonKnob.lock() ) {
        _imp->renderButtonKnob = toKnobButton( getKnobByName("startRender") );
        assert( _imp->renderButtonKnob.lock() );
    }


    //If we already loaded the Writer, do not do anything
    if ( _imp->embeddedPlugin.lock() ) {
        return;
    }
    bool throwErrors = false;
    KnobStringPtr pluginIdParam = _imp->pluginIDStringKnob.lock();
    std::string pattern;

    if (mayCreateFileDialog) {
        bool useDialogForWriters = appPTR->getCurrentSettings()->isFileDialogEnabledForNewWriters();
        if (useDialogForWriters) {
            pattern = getApp()->saveImageFileDialog();
        }

        //The user selected a file, if it fails to read do not create the node
        throwErrors = true;
    } else {

        std::vector<std::string> defaultParamValues = args.getPropertyN<std::string>(kCreateNodeArgsPropNodeInitialParamValues);
        std::vector<std::string>::iterator foundFileName  = std::find(defaultParamValues.begin(), defaultParamValues.end(), std::string(kOfxImageEffectFileParamName));
        if (foundFileName != defaultParamValues.end()) {
            std::string propName(kCreateNodeArgsPropParamValue);
            propName += "_";
            propName += kOfxImageEffectFileParamName;
            pattern = args.getProperty<std::string>(propName);
        }
    }

    _imp->createWriteNode( throwErrors, pattern, NodeSerializationPtr() );
    _imp->refreshPluginSelectorKnob();
}

void
WriteNode::onKnobsAboutToBeLoaded(const NodeSerializationPtr& serialization)
{
    _imp->renderButtonKnob = toKnobButton( getKnobByName("startRender") );
    assert( _imp->renderButtonKnob.lock() );

    assert(serialization);
    NodePtr node = getNode();

    //Load the pluginID to create first.
    node->loadKnob( _imp->pluginIDStringKnob.lock(), serialization->getKnobsValues() );

    std::string filename = getFileNameFromSerialization( serialization->getKnobsValues() );
    //Create the Reader with the serialization
    _imp->createWriteNode(false, filename, serialization);
    _imp->refreshPluginSelectorKnob();
}

bool
WriteNode::knobChanged(const KnobIPtr& k,
                       ValueChangedReasonEnum reason,
                       ViewSpec view,
                       double time,
                       bool originatedFromMainThread)
{
    bool ret = true;
    NodePtr writer = _imp->embeddedPlugin.lock();

    if ( ( k == _imp->outputFileKnob.lock() ) && (reason != eValueChangedReasonTimeChanged) ) {
        if (_imp->creatingWriteNode) {
            if (writer) {
                writer->getEffectInstance()->knobChanged(k, reason, view, time, originatedFromMainThread);
            }

            return false;
        }

        NodePtr hasMaster = getNode()->getMasterNode();
        if (hasMaster) {
            unslaveAllKnobs();
        }
        try {
            _imp->refreshPluginSelectorKnob();
        } catch (const std::exception& e) {
            setPersistentMessage( eMessageTypeError, e.what() );
        }

        KnobOutputFilePtr fileKnob = _imp->outputFileKnob.lock();
        assert(fileKnob);
        std::string filename = fileKnob->getValue();

        try {
            _imp->createWriteNode( false, filename, NodeSerializationPtr() );
        } catch (const std::exception& e) {
            setPersistentMessage( eMessageTypeError, e.what() );
        }
        if (hasMaster) {
            slaveAllKnobs(hasMaster->getEffectInstance(), false);
        }
    } else if ( k == _imp->pluginSelectorKnob.lock() ) {
        KnobStringPtr pluginIDKnob = _imp->pluginIDStringKnob.lock();
        std::string entry = _imp->pluginSelectorKnob.lock()->getActiveEntryText_mt_safe();
        if ( entry == pluginIDKnob->getValue() ) {
            return false;
        }

        if (entry == "Default") {
            entry.clear();
        }

        pluginIDKnob->setValue(entry);

        KnobOutputFilePtr fileKnob = _imp->outputFileKnob.lock();
        assert(fileKnob);
        std::string filename = fileKnob->getValue();

        try {
            _imp->createWriteNode( false, filename, NodeSerializationPtr() );
        } catch (const std::exception& e) {
            setPersistentMessage( eMessageTypeError, e.what() );
        }
    } else if ( k == _imp->readBackKnob.lock() ) {
        clearPersistentMessage(false);
        bool readFile = _imp->readBackKnob.lock()->getValue();
        KnobButtonPtr button = _imp->renderButtonKnob.lock();
        button->setAllDimensionsEnabled(!readFile);
        if (readFile) {
            KnobOutputFilePtr fileKnob = _imp->outputFileKnob.lock();
            assert(fileKnob);
            std::string filename = fileKnob->getValue();
            _imp->createReadNodeAndConnectGraph(filename);
        } else {
            NodePtr input = _imp->inputNode.lock(), output = _imp->outputNode.lock();
            if (input && writer && output) {
                writer->replaceInput(input, 0);
                output->replaceInput(writer, 0);
            }
        }
    } else if ( (k->getName() == kParamFirstFrame) ||
                ( k->getName() == kParamLastFrame) ||
                ( k->getName() == kParamFrameRange) ) {
        _imp->setReadNodeOriginalFrameRange();
        ret = false;
    } else {
        ret = false;
    }
    if (!ret && writer) {
        EffectInstancePtr effect = writer->getEffectInstance();
        if (effect) {
            ret |= effect->knobChanged(k, reason, view, time, originatedFromMainThread);
        }
    }

    return ret;
} // WriteNode::knobChanged

bool
WriteNode::isViewAware() const
{
    NodePtr writer = _imp->embeddedPlugin.lock();

    if (writer) {
        EffectInstancePtr effect = writer->getEffectInstance();
        if (effect) {
            return effect->isViewAware();
        }
    }
    return false;
}

void
WriteNode::getFrameRange(double *first,
                         double *last)
{
    NodePtr writer = _imp->embeddedPlugin.lock();

    if (writer) {
        EffectInstancePtr effect = writer->getEffectInstance();
        if (effect) {
            effect->getFrameRange(first, last);
            return;
        }
    }
    EffectInstance::getFrameRange(first, last);
    
}


void
WriteNode::onSequenceRenderStarted()
{
    NodePtr writerNodePlugin = _imp->embeddedPlugin.lock();

    if (writerNodePlugin) {
        writerNodePlugin->setNodeDisabled(false);
    }
    bool readFile = _imp->readBackKnob.lock()->getValue();
    NodePtr readNode = _imp->readBackNode.lock();
    // If read from file is checked, temporarily disable the read node
    if (readFile && readNode) {
        readNode->setNodeDisabled(true);
    }
}

void
WriteNode::onSequenceRenderFinished()
{
    NodePtr writerNodePlugin = _imp->embeddedPlugin.lock();

    if (writerNodePlugin) {
        writerNodePlugin->setNodeDisabled(true);
    }
    bool readFile = _imp->readBackKnob.lock()->getValue();
    NodePtr readNode = _imp->readBackNode.lock();
    // If read from file was checked, re-enable
    if (readFile && readNode) {
        readNode->setNodeDisabled(false);
    }
}

NATRON_NAMESPACE_EXIT;

NATRON_NAMESPACE_USING;
#include "moc_WriteNode.cpp"
