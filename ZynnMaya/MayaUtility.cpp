#include "MayaUtility.h"

#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MFnSet.h>
#include <maya/MSelectionList.h>
#include <maya/MItSelectionList.h>
#include <maya/MFloatArray.h>
#include <maya/MItDependencyGraph.h>
#include <maya/MPointArray.h>
#include <maya/MFloatPointArray.h>
#include <maya/MFnPluginData.h>
#include <maya/MPxData.h>
#include <maya/MThreadPool.h>

#include "json.hpp"
#include <fstream>
#include <qtzlib/zlib.h>


// this struct is used in function "bool util::isAnimated(MObject & object, bool checkParent)"
struct NodesToCheckStruct
{
    MObject node;
    bool    checkParent;
};

// return seconds per frame
double util::spf()
{
    static const MTime sec(1.0, MTime::kSeconds);
    return 1.0 / sec.as(MTime::uiUnit());
}

bool util::isAncestorDescendentRelationship(const MDagPath & path1,
    const MDagPath & path2)
{
    unsigned int length1 = path1.length();
    unsigned int length2 = path2.length();
    unsigned int diff;

    if (length1 == length2 && !(path1 == path2))
        return false;

    MDagPath ancestor, descendent;
    if (length1 > length2)
    {
        ancestor = path2;
        descendent = path1;
        diff = length1 - length2;
    }
    else
    {
        ancestor = path1;
        descendent = path2;
        diff = length2 - length1;
    }

    descendent.pop(diff);

    bool ret = (ancestor == descendent);

    if (ret)
    {
        MString err = path1.fullPathName() + " and ";
        err += path2.fullPathName() + " have parenting relationships";
        MGlobal::displayError(err);
    }
    return ret;
}



struct BlockAddr
{
    uint64_t start;
    uint64_t end;
    uint32_t typeCode;
};

struct DecompressThreadData
{
    int threadNo;
    std::vector<unsigned char> dataOutput;
    std::vector<unsigned char> dataInput;
};

struct DecompressTaskData
{
    int taskCount;
    std::vector<DecompressThreadData> decompressedThreadData;
};

struct AddressInfo
{
    int group;
    int index;
};




using json = nlohmann::json;
typedef std::vector<unsigned char> uint8Array;


std::vector<unsigned char> decompressData(const std::vector<unsigned char>& compressedData)
{
    z_stream zs = {};
    if (inflateInit(&zs) != Z_OK)
    {
        throw std::runtime_error("inflateInit failed");
    }

    zs.next_in = (Bytef*)compressedData.data();
    zs.avail_in = compressedData.size();

    int ret;
    std::vector<unsigned char> output;
    char* outBuffer = new char[32768];

    do
    {
        zs.next_out = reinterpret_cast<Bytef*>(outBuffer);
        zs.avail_out = sizeof(outBuffer);

        ret = inflate(&zs, 0);

        if (output.size() < zs.total_out)
        {
            output.insert(output.end(), outBuffer, outBuffer + (zs.total_out - output.size()));
        }

    } while (ret == Z_OK);



    inflateEnd(&zs);

    if (ret != Z_STREAM_END)
    {
        throw std::runtime_error("Failed to decompress data");
    }
    delete[] outBuffer;



    return output;
}



std::vector<BlockAddr> GetBlocks(const std::vector<unsigned char>& data)
{
    std::vector<BlockAddr> infos;
    uint64_t address = 0;
    while (address < data.size() - 1)
    {
        uint64_t size = *(uint64_t*)(data.data() + address + 8);
        uint32_t typeCode = *(uint32_t*)(data.data() + address);
        BlockAddr info = { address + 16,address + 16 + size,typeCode };
        infos.push_back(info);
        address = address + size + 16;
    }
    return infos;
}


static MThreadRetVal WorkerFunForDecompress(void* data)
{
    DecompressThreadData* threadData = (DecompressThreadData*)data;
    threadData->dataOutput = decompressData(threadData->dataInput);
    return (MThreadRetVal)0;
}


static void TaskDecomposerForDecompress(void* data, MThreadRootTask* root)
{
    DecompressTaskData* taskData = (DecompressTaskData*)data;

    for (int i = 0; i < taskData->taskCount; ++i)
    {
        MThreadPool::createTask(WorkerFunForDecompress, taskData->decompressedThreadData.data() + i, root);
    }
    MThreadPool::executeAndJoin(root);
}


void ReadItems(json items, std::unordered_map<std::string, std::vector<AddressInfo>>& Items)
{

    for (auto it = items.begin(); it != items.end(); ++it)
    {

        if (!it.value().is_number_integer())
            continue;//跳过非数字的类型

        int64_t value = it.value().get<int64_t>();
        if (value < 100)
        {
            Items[it.key()].push_back({ 0,it.value() });
        }
        else
        {
            int group = value >> 32;
            int index = value & 0xFFFFFFFF;
            Items[it.key()].push_back({ group,index });
        }
    }

}

bool util::GetSplineData(
    const MFnDependencyNode& node,
    std::vector<std::vector<uint64_t>>& PrimitiveInfosList,
    std::vector<std::vector<float>>& PositionsList,
    std::vector<std::vector<float>>& WidthDataList,
    bool writeDebugFile)
{
    //从xg节点中获取交互式毛发曲线的二进制数据
    MPlug plug = node.findPlug("outSplineData", false);
    MObject handle = plug.asMObject();
    MFnPluginData pluginData(handle);
    MPxData* data = pluginData.data();
    ////可以输出调试文件
    //if (writeDebugFile) {
    //    std::ofstream outPutFile("D:/HairOutput.txt", std::ios::binary);
    //    data->writeBinary(outPutFile);
    //}
    std::ostringstream buffer(std::ios::binary);
    data->writeBinary(buffer);
    std::string binary_string = buffer.str();
    uint8Array originalData(binary_string.data(), binary_string.data() + binary_string.size());

    //获取数据块信息
    std::vector<BlockAddr> blockAddrs = GetBlocks(originalData);
    //提取头部信息
    BlockAddr infoBlockAddr = blockAddrs[0];
    blockAddrs.erase(blockAddrs.begin());//删除第一个块,这个块表示信息,剩下的块表示毛发数据,刚好可以对应到头信息中的毛发组数量

    json infoJson = json::parse(originalData.data() + infoBlockAddr.start, originalData.data() + infoBlockAddr.end);
    json headerJson = infoJson["Header"];
    //如果有未知的编码格式则返回假
    if (headerJson["GroupBase64"])
        return false;
    size_t dataGroupCount = headerJson["GroupCount"];

    DecompressTaskData taskData;
    //仅在数据需要解压时解压
    if (headerJson["GroupDeflate"])
    {
        taskData.taskCount = dataGroupCount;
        taskData.decompressedThreadData.resize(dataGroupCount);

        //多线程解压数据
        if (MThreadPool::init() != MStatus::kSuccess)
        {
            MGlobal::displayError("Failed to init Muti Thread Pool");
            return false;
        }

        for (size_t groupIndex = 0; groupIndex < dataGroupCount; groupIndex++)
        {
            taskData.decompressedThreadData[groupIndex].dataInput = std::vector<unsigned char>(originalData.begin() + blockAddrs[groupIndex].start + 32, originalData.begin() + blockAddrs[groupIndex].end);
        }
        MThreadPool::newParallelRegion(TaskDecomposerForDecompress, &taskData);

        MThreadPool::release();
        MThreadPool::release();

    }


    //分析解压后的数据的数据块
    std::vector<std::vector<uint8Array>> decompressedDataBlocks;
    for (size_t groupIndex = 0; groupIndex < dataGroupCount; groupIndex++)
    {
        std::vector<unsigned char> validData;

        if (headerJson["GroupDeflate"])
            validData = taskData.decompressedThreadData[groupIndex].dataOutput;
        else
            memcpy(validData.data(), originalData.data() + blockAddrs[groupIndex].start + 32, blockAddrs[groupIndex].end - blockAddrs[groupIndex].start - 32);

        std::vector<BlockAddr> subBlocks = GetBlocks(validData);
        std::vector<uint8Array> subDatas;
        for (const BlockAddr& info : subBlocks)
        {
            subDatas.push_back(uint8Array(validData.begin() + info.start, validData.begin() + info.end));
        }
        decompressedDataBlocks.push_back(subDatas);
    }


    //收集所有的毛发信息
    std::unordered_map<std::string, std::vector<AddressInfo>> Items;
    for (auto& info : infoJson["Items"])
        ReadItems(info, Items);
    for (auto& info : infoJson["RefMeshArray"])
        ReadItems(info, Items);


    //遍历所有毛发信息获取需要的毛发信息
    for (auto pair : Items)
    {
        if (pair.first == "PrimitiveInfos")
        {
            for (AddressInfo addr : pair.second)
            {
                uint8Array decompressedData = decompressedDataBlocks[addr.group][addr.index];
                std::vector<uint64_t> PrimitiveInfos;
                for (int i = 0; i < decompressedData.size(); i += 12)
                {
                    uint32_t* value1 = (uint32_t*)(decompressedData.data() + i);
                    uint64_t* value2 = (uint64_t*)(decompressedData.data() + i + 4);
                    PrimitiveInfos.push_back(*value1);
                    PrimitiveInfos.push_back(*value2);
                }
                PrimitiveInfosList.push_back(PrimitiveInfos);
            }
        }
        else if (pair.first == "Positions")
        {
            for (AddressInfo addr : pair.second)
            {
                uint8Array decompressedData = decompressedDataBlocks[addr.group][addr.index];
                std::vector<float> pos((float*)decompressedData.data(), (float*)decompressedData.data() + decompressedData.size() / 4);
                PositionsList.push_back(pos);
            }
        }
        else if (pair.first == "WIDTH_CV")
        {
            for (AddressInfo addr : pair.second)
            {
                uint8Array decompressedData = decompressedDataBlocks[addr.group][addr.index];
                std::vector<float> widthData((float*)decompressedData.data(), (float*)decompressedData.data() + decompressedData.size() / 4);
                WidthDataList.push_back(widthData);
            }
        }
    }

    return true;
}

// returns 0 if static, 1 if sampled, and 2 if a curve
int util::getSampledType(const MPlug& iPlug)
{
    MPlugArray conns;

    iPlug.connectedTo(conns, true, false);

    // it's possible that only some element of an array plug or
    // some component of a compound plus is connected
    if (conns.length() == 0)
    {
        if (iPlug.isArray())
        {
            unsigned int numConnectedElements = iPlug.numConnectedElements();
            for (unsigned int e = 0; e < numConnectedElements; e++)
            {
                int retVal = getSampledType(iPlug.connectionByPhysicalIndex(e));
                if (retVal > 0)
                    return retVal;
            }
        }
        else if (iPlug.isCompound() && iPlug.numConnectedChildren() > 0)
        {
            unsigned int numChildren = iPlug.numChildren();
            for (unsigned int c = 0; c < numChildren; c++)
            {
                int retVal = getSampledType(iPlug.child(c));
                if (retVal > 0)
                    return retVal;
            }
        }
        return 0;
    }

    MObject ob;
    MFnDependencyNode nodeFn;
    for (unsigned i = 0; i < conns.length(); i++)
    {
        ob = conns[i].node();
        MFn::Type type = ob.apiType();

        switch (type)
        {
            case MFn::kAnimCurveTimeToAngular:
            case MFn::kAnimCurveTimeToDistance:
            case MFn::kAnimCurveTimeToTime:
            case MFn::kAnimCurveTimeToUnitless:
            {
                nodeFn.setObject(ob);
                MPlug incoming = nodeFn.findPlug("i", true);

                // sampled
                if (incoming.isConnected())
                    return 1;

                // curve
                else
                    return 2;
            }
            break;

            case MFn::kMute:
            {
                nodeFn.setObject(ob);
                MPlug mutePlug = nodeFn.findPlug("mute", true);

                // static
                if (mutePlug.asBool())
                    return 0;
                // curve
                else
                   return 2;
            }
            break;

            default:
            break;
        }
    }

    return 1;
}

bool util::getRotOrder(MTransformationMatrix::RotationOrder iOrder,
    unsigned int & oXAxis, unsigned int & oYAxis, unsigned int & oZAxis)
{
    switch (iOrder)
    {
        case MTransformationMatrix::kXYZ:
        {
            oXAxis = 0;
            oYAxis = 1;
            oZAxis = 2;
        }
        break;

        case MTransformationMatrix::kYZX:
        {
            oXAxis = 1;
            oYAxis = 2;
            oZAxis = 0;
        }
        break;

        case MTransformationMatrix::kZXY:
        {
            oXAxis = 2;
            oYAxis = 0;
            oZAxis = 1;
        }
        break;

        case MTransformationMatrix::kXZY:
        {
            oXAxis = 0;
            oYAxis = 2;
            oZAxis = 1;
        }
        break;

        case MTransformationMatrix::kYXZ:
        {
            oXAxis = 1;
            oYAxis = 0;
            oZAxis = 2;
        }
        break;

        case MTransformationMatrix::kZYX:
        {
            oXAxis = 2;
            oYAxis = 1;
            oZAxis = 0;
        }
        break;

        default:
        {
            return false;
        }
    }
    return true;
}

// 0 dont write, 1 write static 0, 2 write anim 0, 3 write anim -1
int util::getVisibilityType(const MPlug & iPlug)
{
    int type = getSampledType(iPlug);

    // static case
    if (type == 0)
    {
        // dont write anything
        if (iPlug.asBool())
            return 0;

        // write static 0
        return 1;
    }
    else
    {
        // anim write -1
        if (iPlug.asBool())
            return 3;

        // write anim 0
        return 2;
    }
}

// does this cover all cases?
bool util::isAnimated(MObject & object, bool checkParent)
{
    MStatus stat;
    MItDependencyGraph iter(object, MFn::kInvalid,
        MItDependencyGraph::kUpstream,
        MItDependencyGraph::kDepthFirst,
        MItDependencyGraph::kPlugLevel,
        &stat);

    if (stat!= MS::kSuccess)
    {
        MGlobal::displayError("Unable to create DG iterator ");
    }

    // MAnimUtil::isAnimated(node) will search the history of the node
    // for any animation curve nodes. It will return true for those nodes
    // that have animation curve in their history.
    // The average time complexity is O(n^2) where n is the number of history
    // nodes. But we can improve the best case by split the loop into two.
    std::vector<NodesToCheckStruct> nodesToCheckAnimCurve;

    NodesToCheckStruct nodeStruct;
    for (; !iter.isDone(); iter.next())
    {
        MObject node = iter.thisNode();

        if (node.hasFn(MFn::kPluginDependNode) ||
                node.hasFn( MFn::kConstraint ) ||
                node.hasFn(MFn::kPointConstraint) ||
                node.hasFn(MFn::kAimConstraint) ||
                node.hasFn(MFn::kOrientConstraint) ||
                node.hasFn(MFn::kScaleConstraint) ||
                node.hasFn(MFn::kGeometryConstraint) ||
                node.hasFn(MFn::kNormalConstraint) ||
                node.hasFn(MFn::kTangentConstraint) ||
                node.hasFn(MFn::kParentConstraint) ||
                node.hasFn(MFn::kPoleVectorConstraint) ||
                node.hasFn(MFn::kParentConstraint) ||
                node.hasFn(MFn::kTime) ||
                node.hasFn(MFn::kJoint) ||
                node.hasFn(MFn::kGeometryFilt) ||
                node.hasFn(MFn::kTweak) ||
                node.hasFn(MFn::kPolyTweak) ||
                node.hasFn(MFn::kSubdTweak) ||
                node.hasFn(MFn::kCluster) ||
                node.hasFn(MFn::kFluid) ||
                node.hasFn(MFn::kPolyBoolOp))
        {
            return true;
        }

        if (node.hasFn(MFn::kExpression))
        {
            MFnExpression fn(node, &stat);
            if (stat == MS::kSuccess && fn.isAnimated())
            {
                return true;
            }
        }

        // skip shading nodes
        if (!node.hasFn(MFn::kShadingEngine))
        {
            MPlug plug = iter.thisPlug();
            MFnAttribute attr(plug.attribute(), &stat);
            bool checkNodeParent = false;
            if (stat == MS::kSuccess && attr.isWorldSpace())
            {
                checkNodeParent = true;
            }

            nodeStruct.node = node;
            nodeStruct.checkParent = checkParent || checkNodeParent;
            nodesToCheckAnimCurve.push_back(nodeStruct);
        }
        else
        {
            // and don't traverse the rest of their subgraph
            iter.prune();
        }
    }

    for (size_t i = 0; i < nodesToCheckAnimCurve.size(); i++)
    {
        if (MAnimUtil::isAnimated(nodesToCheckAnimCurve[i].node, nodesToCheckAnimCurve[i].checkParent))
        {
            return true;
        }
    }

    return false;
}

bool util::isDrivenByFBIK(const MFnIkJoint & iJoint)
{
    // check joints that are driven by Maya FBIK
    // Maya FBIK has no connection to joints' TRS plugs
    // but TRS of joints are driven by FBIK, they are not static
    // Maya 2012's new HumanIK has connections to joints.
    // FBIK is a special case.
    MStatus status = MS::kSuccess;
    if (iJoint.hikJointName(&status).length() > 0 && status) {
        return true;
    }
    return false;
}

bool util::isDrivenBySplineIK(const MFnIkJoint & iJoint)
{
    // spline IK can drive the starting joint's translate channel but
    // it has no connection to the translate plug.
    // we treat the joint as animated in this case.
    // find the ikHandle node.
    MPlug msgPlug = iJoint.findPlug("message", false);
    MPlugArray msgPlugDst;
    msgPlug.connectedTo(msgPlugDst, false, true);
    for (unsigned int i = 0; i < msgPlugDst.length(); i++) {
        MFnDependencyNode ikHandle(msgPlugDst[i].node());
        if (!ikHandle.object().hasFn(MFn::kIkHandle)) continue;

        // find the ikSolver node.
        MPlug ikSolverPlug = ikHandle.findPlug("ikSolver", true);
        MPlugArray ikSolverDst;
        ikSolverPlug.connectedTo(ikSolverDst, true, false);
        for (unsigned int j = 0; j < ikSolverDst.length(); j++) {

            // return true if the ikSolver is a spline solver.
            if (ikSolverDst[j].node().hasFn(MFn::kSplineSolver)) {
                return true;
            }
        }
    }

    return false;
}

bool util::isIntermediate(const MObject & object)
{
    MStatus stat;
    MFnDagNode mFn(object);

    MPlug plug = mFn.findPlug("intermediateObject", false, &stat);
    if (stat == MS::kSuccess && plug.asBool())
        return true;
    else
        return false;
}

bool util::isRenderable(const MObject & object)
{
    MStatus stat;
    MFnDagNode mFn(object);

    // templated turned on?  return false
    MPlug plug = mFn.findPlug("template", false, &stat);
    if (stat == MS::kSuccess && plug.asBool())
        return false;

    // visibility or lodVisibility off?  return false
    plug = mFn.findPlug("visibility", false, &stat);
    if (stat == MS::kSuccess && !plug.asBool())
    {
        // the value is off. let's check if it has any in-connection,
        // otherwise, it means it is not animated.
        MPlugArray arrayIn;
        plug.connectedTo(arrayIn, true, false, &stat);

        if (stat == MS::kSuccess && arrayIn.length() == 0)
        {
            return false;
        }
    }

    plug = mFn.findPlug("lodVisibility", false, &stat);
    if (stat == MS::kSuccess && !plug.asBool())
    {
        MPlugArray arrayIn;
        plug.connectedTo(arrayIn, true, false, &stat);

        if (stat == MS::kSuccess && arrayIn.length() == 0)
        {
            return false;
        }
    }

    // this shape is renderable
    return true;
}

MString util::stripNamespaces(const MString & iNodeName, unsigned int iDepth)
{
    if (iDepth == 0)
    {
        return iNodeName;
    }

    MStringArray strArray;
    if (iNodeName.split(':', strArray) == MS::kSuccess)
    {
        unsigned int len = strArray.length();

        // we want to strip off more namespaces than what we have
        // so we just return the last name
        if (len == 0)
        {
            return iNodeName;
        }
        else if (len <= iDepth + 1)
        {
            return strArray[len-1];
        }

        MString name;
        for (unsigned int i = iDepth; i < len - 1; ++i)
        {
            name += strArray[i];
            name += ":";
        }
        name += strArray[len-1];
        return name;
    }

    return iNodeName;
}

MString util::getHelpText()
{
    MString ret =
"AbcExport [options]\n"
"Options:\n"
"-h / -help  Print this message.\n"
"\n"
"-prs / -preRollStartFrame double\n"
"The frame to start scene evaluation at.  This is used to set the\n"
"starting frame for time dependent translations and can be used to evaluate\n"
"run-up that isn't actually translated.\n"
"\n"
"-duf / -dontSkipUnwrittenFrames\n"
"When evaluating multiple translate jobs, the presence of this flag decides\n"
"whether to evaluate frames between jobs when there is a gap in their frame\n"
"ranges.\n"
"\n"
"-v / -verbose\n"
"Prints the current frame that is being evaluated.\n"
"\n"
"-j / -jobArg string REQUIRED\n"
"String which contains flags for writing data to a particular file.\n"
"Multiple jobArgs can be specified.\n"
"\n"
"-jobArg flags:\n"
"\n"
"-a / -attr string\n"
"A specific geometric attribute to write out.\n"
"This flag may occur more than once.\n"
"\n"
"-as / -autoSubd\n"
"If this flag is present and the mesh has crease edges, crease vertices or holes, \n"
"the mesh (OPolyMesh) would now be written out as an OSubD and crease info will be stored in the Alembic \n"
"file. Otherwise, creases info won't be preserved in Alembic file \n"
"unless a custom Boolean attribute SubDivisionMesh has been added to mesh node and its value is true. \n"
"\n"
"-atp / -attrPrefix string (default ABC_)\n"
"Prefix filter for determining which geometric attributes to write out.\n"
"This flag may occur more than once.\n"
"\n"
"-df / -dataFormat string\n"
"The data format to use to write the file.  Can be either HDF or Ogawa.\n"
"The default is Ogawa.\n"
"\n"
"-ef / -eulerFilter\n"
"If this flag is present, apply Euler filter while sampling rotations.\n"
"\n"
"-f / -file string REQUIRED\n"
"File location to write the Alembic data.\n"
"\n"
"-fr / -frameRange double double\n"
"The frame range to write.\n"
"Multiple occurrences of -frameRange are supported within a job. Each\n"
"-frameRange defines a new frame range. -step or -frs will affect the\n"
"current frame range only.\n"
"\n"
"-frs / -frameRelativeSample double\n"
"frame relative sample that will be written out along the frame range.\n"
"This flag may occur more than once.\n"
"\n"
"-nn / -noNormals\n"
"If this flag is present normal data for Alembic poly meshes will not be\n"
"written.\n"
"\n"
"-pr / -preRoll\n"
"If this flag is present, this frame range will not be sampled.\n"
"\n"
"-ro / -renderableOnly\n"
"If this flag is present non-renderable hierarchy (invisible, or templated)\n"
"will not be written out.\n"
"\n"
"-rt / -root\n"
"Maya dag path which will be parented to the root of the Alembic file.\n"
"This flag may occur more than once.  If unspecified, it defaults to '|' which\n"
"means the entire scene will be written out.\n"
"\n"
"-s / -step double (default 1.0)\n"
"The time interval (expressed in frames) at which the frame range is sampled.\n"
"Additional samples around each frame can be specified with -frs.\n"
"\n"
"-sl / -selection\n"
"If this flag is present, write out all all selected nodes from the active\n"
"selection list that are descendents of the roots specified with -root.\n"
"\n"
"-sn / -stripNamespaces (optional int)\n"
"If this flag is present all namespaces will be stripped off of the node before\n"
"being written to Alembic.  If an optional int is specified after the flag\n"
"then that many namespaces will be stripped off of the node name. Be careful\n"
"that the new stripped name does not collide with other sibling node names.\n\n"
"Examples: \n"
"taco:foo:bar would be written as just bar with -sn\n"
"taco:foo:bar would be written as foo:bar with -sn 1\n"
"\n"
"-u / -userAttr string\n"
"A specific user attribute to write out.  This flag may occur more than once.\n"
"\n"
"-uatp / -userAttrPrefix string\n"
"Prefix filter for determining which user attributes to write out.\n"
"This flag may occur more than once.\n"
"\n"
"-uv / -uvWrite\n"
"If this flag is present, uv data for PolyMesh and SubD shapes will be written to\n"
"the Alembic file.  Only the current uv map is used.\n"
"\n"
"-uvo / -uvsOnly\n"
"If this flag is present, only uv data for PolyMesh and SubD shapes will be written\n"
"to the Alembic file.  Only the current uv map is used.\n"
"\n"
"-wcs / -writeColorSets\n"
"Write all color sets on MFnMeshes as color 3 or color 4 indexed geometry \n"
"parameters with face varying scope.\n"
"\n"
"-wfs / -writeFaceSets\n"
"Write all Face sets on MFnMeshes.\n"
"\n"
"-wfg / -wholeFrameGeo\n"
"If this flag is present data for geometry will only be written out on whole\n"
"frames.\n"
"\n"
"-ws / -worldSpace\n"
"If this flag is present, any root nodes will be stored in world space.\n"
"\n"
"-wv / -writeVisibility\n"
"If this flag is present, visibility state will be stored in the Alembic\n"
"file.  Otherwise everything written out is treated as visible.\n"
"\n"
"-wuvs / -writeUVSets\n"
"Write all uv sets on MFnMeshes as vector 2 indexed geometry \n"
"parameters with face varying scope.\n"
"\n"
"-mfc / -melPerFrameCallback string\n"
"When each frame (and the static frame) is evaluated the string specified is\n"
"evaluated as a Mel command. See below for special processing rules.\n"
"\n"
"-mpc / -melPostJobCallback string\n"
"When the translation has finished the string specified is evaluated as a Mel\n"
"command. See below for special processing rules.\n"
"\n"
"-pfc / -pythonPerFrameCallback string\n"
"When each frame (and the static frame) is evaluated the string specified is\n"
"evaluated as a python command. See below for special processing rules.\n"
"\n"
"-ppc / -pythonPostJobCallback string\n"
"When the translation has finished the string specified is evaluated as a\n"
"python command. See below for special processing rules.\n"
"\n"
"Special callback information:\n"
"On the callbacks, special tokens are replaced with other data, these tokens\n"
"and what they are replaced with are as follows:\n"
"\n"
"#FRAME# replaced with the frame number being evaluated.\n"
"#FRAME# is ignored in the post callbacks.\n"
"\n"
"#BOUNDS# replaced with a string holding bounding box values in minX minY minZ\n"
"maxX maxY maxZ space seperated order.\n"
"\n"
"#BOUNDSARRAY# replaced with the bounding box values as above, but in\n"
"array form.\n"
"In Mel: {minX, minY, minZ, maxX, maxY, maxZ}\n"
"In Python: [minX, minY, minZ, maxX, maxY, maxZ]\n"
"\n"
"Examples:\n"
"\n"
"AbcExport -j \"-root |group|foo -root |test|path|bar -file /tmp/test.abc\"\n"
"Writes out everything at foo and below and bar and below to /tmp/test.abc.\n"
"foo and bar are siblings parented to the root of the Alembic scene.\n"
"\n"
"AbcExport -j \"-frameRange 1 5 -step 0.5 -root |group|foo -file /tmp/test.abc\"\n"
"Writes out everything at foo and below to /tmp/test.abc sampling at frames:\n"
"1 1.5 2 2.5 3 3.5 4 4.5 5\n"
"\n"
"AbcExport -j \"-fr 0 10 -frs -0.1 -frs 0.2 -step 5 -file /tmp/test.abc\"\n"
"Writes out everything in the scene to /tmp/test.abc sampling at frames:\n"
"-0.1 0.2 4.9 5.2 9.9 10.2\n"
"\n"
"Note: The difference between your highest and lowest frameRelativeSample can\n"
"not be greater than your step size.\n"
"\n"
"AbcExport -j \"-step 0.25 -frs 0.3 -frs 0.60 -fr 1 5 -root foo -file test.abc\"\n"
"\n"
"Is illegal because the highest and lowest frameRelativeSamples are 0.3 frames\n"
"apart.\n"
"\n"
"AbcExport -j \"-sl -root |group|foo -file /tmp/test.abc\"\n"
"Writes out all selected nodes and it's ancestor nodes including up to foo.\n"
"foo will be parented to the root of the Alembic scene.\n"
"\n";

    return ret;
}
