#pragma once
//maya
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
//
#include <fstream>
#include "json.hpp"
#include <memory>
#include <qtzlib/zlib.h>
#include <chrono>



using json = nlohmann::json;
typedef std::vector<unsigned char> uint8Array;




struct mObjectCmp
{
	bool operator()(const MObject& o1, const MObject& o2) const
	{
		return strcmp(MFnDependencyNode(o1).name().asChar(), MFnDependencyNode(o2).name().asChar()) < 0;
	}
};
typedef std::map <MObject, MSelectionList, mObjectCmp> GetMembersMap;


MStatus getSetComponents(const MDagPath& dagPath, const MObject& SG, MObject& compObj)
{
	const MString instObjGroupsAttrName("instObjGroups");
	GetMembersMap gmMap;

	// Check if SG is really a shading engine
	if (SG.hasFn(MFn::kShadingEngine) != true)
	{
		MFnDependencyNode fnDepNode(SG);
		MString message;
		message.format("Node ^1s is not a valid shading engine...", fnDepNode.name());
		MGlobal::displayError(message);

		return MS::kFailure;
	}

	// get the instObjGroups iog plug
	MStatus status;
	MFnDependencyNode depNode(dagPath.node());
	MPlug iogPlug(depNode.findPlug(instObjGroupsAttrName, false, &status));
	if (status == MS::kFailure)
		return MS::kFailure;

	// if there are no elements,  this shading group is not connected as a face set
	if (iogPlug.numElements() <= 0)
		return MS::kFailure;

	// the first element should always be connected as a source
	MPlugArray iogConnections;
	iogPlug.elementByLogicalIndex(0, &status).connectedTo(iogConnections, false, true, &status);
	if (status == MS::kFailure)
		return MS::kFailure;

	// Function set for the shading engine
	MFnSet fnSet(SG);

	// Retrieve members
	MSelectionList selList;
	GetMembersMap::iterator it = gmMap.find(SG);
	if (it != gmMap.end())
		selList = it->second;
	else
	{
		fnSet.getMembers(selList, false);
		gmMap[SG] = selList;
	}

	// Iteration through the list
	MDagPath            curDagPath;
	MItSelectionList    itSelList(selList);
	for (; itSelList.isDone() != true; itSelList.next())
	{
		// Test if it's a face mapping
		if (itSelList.hasComponents() == true)
		{
			itSelList.getDagPath(curDagPath, compObj);

			// Test if component object is valid and if it's the right object
			if ((compObj.isNull() == false) && (curDagPath == dagPath))
			{
				return MS::kSuccess;
			}
		}
	}

	// SG is a shading engine but has no components connected to the dagPath.
	// This means we have a whole object mapping!
	return MS::kFailure;
}


MObjectArray get_out_connected_SG(const MDagPath& shapeDPath)
{
	MStatus status;

	MObjectArray connSG;
	MObject obj(shapeDPath.node());

	MItDependencyGraph itDG(
		obj,
		MFn::kShadingEngine,
		MItDependencyGraph::kDownstream,
		MItDependencyGraph::kBreadthFirst,
		MItDependencyGraph::kNodeLevel,
		&status);

	if (!status)
		return connSG;

	itDG.enablePruningOnFilter();

	for (; itDG.isDone() != true; itDG.next()) {
		connSG.append(itDG.thisNode());
	}
}

MStatus get_uvs(const MFnMesh& mesh, std::vector<float>& outuvs, std::vector<Alembic::Util::uint32_t>& indices)
{
	MStatus status;

	MFloatArray uArray;
	MFloatArray vArray;
	status = mesh.getUVs(uArray, vArray);

	if (!status)
	{
		return status;
	}
	outuvs.reserve(uArray.length() * 2);
	for (unsigned int i = 0; i < uArray.length(); i++)
	{
		outuvs.push_back(uArray[i]);
		outuvs.push_back(vArray[i]);
	}

	indices.reserve(mesh.numFaceVertices(&status));
	int faceCount = mesh.numPolygons(&status);
	if (!status)
		return status;
	int uvId = 0;
	for (int f = 0; f < faceCount; f++) {
		int len = mesh.polygonVertexCount(f, &status);
		if (!status)
			return status;
		for (int i = len - 1; i >= 0; i--) {
			status = mesh.getPolygonUVid(f, i, uvId);
			if (!status)
				return status;
			indices.push_back(uvId);
		}
	}

	return MS::kSuccess;
}

void get_normals(const MFnMesh& lMesh, std::vector<float>& oNormals)
{
	MStatus status = MS::kSuccess;
	MPlug plug = lMesh.findPlug("noNormals", true, &status);
	if (status == MS::kSuccess && plug.asBool() == true)
	{
		return;
	}
	else if (status != MS::kSuccess)
	{
		bool userSetNormals = false;

		// go through all per face-vertex normals and verify if any of them
		// has been tweaked by users
		unsigned int numFaces = lMesh.numPolygons();
		for (unsigned int faceIndex = 0; faceIndex < numFaces; faceIndex++)
		{
			MIntArray normals;
			lMesh.getFaceNormalIds(faceIndex, normals);
			unsigned int numNormals = normals.length();
			for (unsigned int n = 0; n < numNormals; n++)
			{
				if (lMesh.isNormalLocked(normals[n]))
				{
					userSetNormals = true;
					break;
				}
			}
		}

		// we looped over all the normals and they were all calculated by Maya
		// so we just need to check to see if any of the edges are hard
		// before we decide not to write the normals.
		if (!userSetNormals)
		{
			bool hasHardEdges = false;

			// go through all edges and verify if any of them is hard edge
			unsigned int numEdges = lMesh.numEdges();
			for (unsigned int edgeIndex = 0; edgeIndex < numEdges; edgeIndex++)
			{
				if (!lMesh.isEdgeSmooth(edgeIndex))
				{
					hasHardEdges = true;
					break;
				}
			}

			// all the edges were smooth, we don't need to write the normals
			if (!hasHardEdges)
			{
				return;
			}
		}
	}


	bool flipNormals = false;
	plug = lMesh.findPlug("flipNormals", true, &status);
	if (status == MS::kSuccess)
		flipNormals = plug.asBool();

	// get the per vertex per face normals (aka vertex)
	unsigned int numFaces = lMesh.numPolygons();

	for (unsigned int faceIndex = 0; faceIndex < numFaces; faceIndex++)
	{
		MIntArray vertexList;
		lMesh.getPolygonVertices(faceIndex, vertexList);

		// re-pack the order of normals in this vector before writing into prop
		// so that Renderman can also use it
		unsigned int numVertices = vertexList.length();
		for (int v = numVertices - 1; v >= 0; v--)
		{
			unsigned int vertexIndex = vertexList[v];
			MVector normal;
			lMesh.getFaceVertexNormal(faceIndex, vertexIndex, normal);

			if (flipNormals)
				normal = -normal;

			oNormals.push_back(static_cast<float>(normal[0]));
			oNormals.push_back(static_cast<float>(normal[1]));
			oNormals.push_back(static_cast<float>(normal[2]));
		}
	}
}

void fillTopology(
	const MFnMesh& lMesh,
	std::vector<float>& oPoints,
	std::vector<Alembic::Util::int32_t>& oFacePoints,
	std::vector<Alembic::Util::int32_t>& oPointCounts)
{
	MStatus status = MS::kSuccess;
	if (!status)
	{
		MGlobal::displayError("MFnMesh() failed for MayaMeshWriter");
	}

	MFloatPointArray pts;

	lMesh.getPoints(pts);

	if (pts.length() < 3 && pts.length() > 0)
	{
		MString err = lMesh.fullPathName() +
			" is not a valid mesh, because it only has ";
		err += pts.length();
		err += " points.";
		MGlobal::displayError(err);
		return;
	}

	unsigned int numPolys = lMesh.numPolygons();

	if (numPolys == 0)
	{
		MGlobal::displayWarning(lMesh.fullPathName() + " has no polygons.");
		return;
	}

	unsigned int i;
	int j;

	oPoints.resize(pts.length() * 3);

	// repack the float
	for (i = 0; i < pts.length(); i++)
	{
		size_t local = i * 3;
		oPoints[local] = pts[i].x;
		oPoints[local + 1] = pts[i].y;
		oPoints[local + 2] = pts[i].z;
	}

	/*
		oPoints -
		oFacePoints - vertex list
		oPointCounts - number of points per polygon
	*/

	MIntArray faceArray;

	for (i = 0; i < numPolys; i++)
	{
		lMesh.getPolygonVertices(i, faceArray);

		if (faceArray.length() < 3)
		{
			MGlobal::displayWarning("Skipping degenerate polygon");
			continue;
		}

		// write backwards cause polygons in Maya are in a different order
		// from Renderman (clockwise vs counter-clockwise?)
		int faceArrayLength = faceArray.length() - 1;
		for (j = faceArrayLength; j > -1; j--)
		{
			oFacePoints.push_back(faceArray[j]);
		}

		oPointCounts.push_back(faceArray.length());
	}
}


MString stripNamespaces(const MString& iNodeName, unsigned int iDepth = 1)
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
			return strArray[len - 1];
		}

		MString name;
		for (unsigned int i = iDepth; i < len - 1; ++i)
		{
			name += strArray[i];
			name += ":";
		}
		name += strArray[len - 1];
		return name;
	}

	return iNodeName;
}

MStatus get_conn_shader(const MObject& sg, MFnDependencyNode& shader)
{
	MStatus status;
	MFnDependencyNode sgNode(sg);
	MPlug plugSurfaceShader = sgNode.findPlug("surfaceShader", &status);
	if (!status)
		return status;
	MObject shaderNode = plugSurfaceShader.source().node();
	shader.setObject(shaderNode);

	return status;
}


#pragma region SplineData


struct BlockAddr
{
	uint64_t start;
	uint64_t end;
	uint32_t typeCode;
};

struct AddressInfo
{
	int group;
	int index;
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
		MThreadPool::createTask(WorkerFunForDecompress, taskData->decompressedThreadData.data()+i, root);
	}
	MThreadPool::executeAndJoin(root);
}

bool GetSplineData(
	const MFnDependencyNode& node,
	std::vector<std::vector<uint64_t>>& PrimitiveInfosList,
	std::vector<std::vector<float>>& PositionsList,
	std::vector<std::vector<float>>& WidthDataList,
	bool writeDebugFile = false)
{
		//从xg节点中获取交互式毛发曲线的二进制数据
	MPlug plug = node.findPlug("outSplineData", false);
	MObject handle = plug.asMObject();
	MFnPluginData pluginData(handle);
	MPxData* data = pluginData.data();
	//可以输出调试文件
	if (writeDebugFile) {
		std::ofstream outPutFile("D:/HairOutput.txt", std::ios::binary);
		data->writeBinary(outPutFile);
	}
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

		if(headerJson["GroupDeflate"])
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



#pragma endregion



