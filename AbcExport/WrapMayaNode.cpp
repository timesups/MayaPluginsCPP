#include "WrapMayaNode.h"
#include <maya/MFnTransform.h>
#include <maya/MMatrix.h>
#include <maya/MGlobal.h>
#include <maya/MFnMesh.h>
#include <maya/MFloatArray.h>
#include <maya/MFloatVectorArray.h>
#include <maya/MPointArray.h>
#include <maya/MItMeshPolygon.h>
#include <maya/MPoint.h>
#include <maya/MBoundingBox.h>
#include <maya/MObjectArray.h>
#include <maya/MItDependencyGraph.h>
#include <maya/MFnSet.h>
#include <maya/MSelectionList.h>
#include <maya/MItSelectionList.h>
#include <maya/MIntArray.h>
#include <maya/MFnSingleIndexedComponent.h>
#include <maya/MStringArray.h>
#include <maya/MFloatPointArray.h>


void fillTopology(
	const MFnMesh&lMesh,
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


MString stripNamespaces(const MString& iNodeName, unsigned int iDepth=1)
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

MStatus get_conn_shader(const MObject& sg,MFnDependencyNode& shader)
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

struct mObjectCmp
{
	bool operator()(const MObject& o1, const MObject& o2) const
	{
		return strcmp(MFnDependencyNode(o1).name().asChar(), MFnDependencyNode(o2).name().asChar()) < 0;
	}
};
typedef std::map <MObject, MSelectionList, mObjectCmp> GetMembersMap;

MStatus getSetComponents(const MDagPath& dagPath, const MObject& SG,MObject& compObj)
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
	for (int i = 0; i < uArray.length(); i++)
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
			status = mesh.getPolygonUVid(f, i,uvId);
			if (!status)
				return status;
			indices.push_back(uvId);
		}
	}

	return MS::kSuccess;
}

void get_normals(const MFnMesh& lMesh,std::vector<float>& oNormals)
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

MStatus get_vertices(const MFnMesh& mesh, std::vector<float>& outVertices)
{
	MStatus status;

	int numVertices = mesh.numVertices(&status);
	if (!status)
		return status;

	outVertices.reserve(numVertices * 3);

	MPointArray points;

	status = mesh.getPoints(points);
	if (!status)
		return status;
	for (int i = 0; i < points.length(); i++)
	{
		const MPoint point = points[i];
		outVertices.push_back(point.x);
		outVertices.push_back(point.y);
		outVertices.push_back(point.z);
	}


	return MS::kSuccess;
}


Transform::Transform(
	const MDagPath& inDagPath, 
	const std::shared_ptr<RootNode> inParent,
	bool anim, 
	const uint32_t timeIndex):RootNode(inDagPath)
{
	MFnTransform transform(dagPath);

	if (anim)
		abcObject = std::make_shared<Alembic::AbcGeom::OXform>(Alembic::AbcGeom::OXform(*(inParent->abcObject), std::string(transform.name().asChar()), timeIndex));
	else
		abcObject = std::make_shared<Alembic::AbcGeom::OXform>(Alembic::AbcGeom::OXform(*(inParent->abcObject), std::string(transform.name().asChar())));
}

MStatus Transform::write_to_alembic_file()
{
	MStatus status;
	MFnTransform transform(dagPath);
	double mat[4][4];
	status = transform.transformationMatrix().get(mat);
	if (!status) {
		MGlobal::displayError("Failed to get matrix");
		return status;
	}

	Alembic::AbcGeom::XformOp op(Alembic::AbcGeom::XformOperationType::kMatrixOperation, 0);
	Alembic::AbcGeom::XformSample sample;
	sample.addOp(op, Imath::M44d(mat));
	std::shared_ptr<Alembic::AbcGeom::OXform> oxform = std::static_pointer_cast<Alembic::AbcGeom::OXform>(abcObject);
	oxform->getSchema().set(sample);
	return status;
}


Mesh::Mesh(
	const MDagPath& inDagPath,
	const std::shared_ptr<RootNode> inParent,
	bool anim, const uint32_t timeIndex) :RootNode(inDagPath)
{
	MFnMesh mesh(dagPath);

	if (anim)
		abcObject = std::make_shared<Alembic::AbcGeom::OPolyMesh>(Alembic::AbcGeom::OPolyMesh(*(inParent->abcObject), std::string(mesh.name().asChar()), timeIndex));
	else
		abcObject = std::make_shared<Alembic::AbcGeom::OPolyMesh>(Alembic::AbcGeom::OPolyMesh(*(inParent->abcObject), std::string(mesh.name().asChar())));
	
	write_faceset(mesh);

}

bool Mesh::is_intermediate_objet()
{
	MFnMesh mesh(dagPath);
	return mesh.isIntermediateObject();
}

MStatus Mesh::write_to_alembic_file()
{
	MStatus status;


	MFnMesh mesh(dagPath);

	std::shared_ptr<Alembic::AbcGeom::OPolyMesh> polyMesh = std::static_pointer_cast<Alembic::AbcGeom::OPolyMesh>(abcObject);
	Alembic::AbcGeom::OPolyMeshSchema::Sample meshSample;

	//write uv
	std::vector<float> uvs;
	std::vector<Alembic::Util::uint32_t> uvsIndex;
	status = get_uvs(mesh, uvs, uvsIndex);
	if(status && !uvs.empty() && !uvsIndex.empty())
	{
		Alembic::AbcGeom::V2fArraySample uvArraySample((const Imath::V2f*)&uvs.front(), uvs.size() / 2);
		Alembic::AbcGeom::OV2fGeomParam::Sample uvSample(uvArraySample, Alembic::AbcGeom::kFacevaryingScope);
		uvSample.setIndices(Alembic::Abc::UInt32ArraySample(&uvsIndex.front(), uvsIndex.size()));
		meshSample.setUVs(uvSample);
	}
	else
	{
		MString message;
		message.format("Failed to get uv from mesh :", mesh.name());
		MGlobal::displayWarning(message);
	}
	
	//write normal;
	std::vector<float> normals;
	get_normals(mesh, normals);
	Alembic::AbcGeom::ON3fGeomParam::Sample normalSample;
	if (!normals.empty())
	{
		normalSample.setScope(Alembic::AbcGeom::kFacevaryingScope);
		normalSample.setVals(Alembic::AbcGeom::N3fArraySample(
			(const Imath::V3f*)&normals.front(), normals.size() / 3));
		meshSample.setNormals(normalSample);
	}
	else
	{
		MString message;
		message.format("Failed to get normal from mesh :^1s", mesh.name());
		MGlobal::displayInfo(message);
	}

	//write mesh info
	std::vector<float> points;
	std::vector<Alembic::Util::int32_t> facePoints;
	std::vector<Alembic::Util::int32_t> pointCounts;
	fillTopology(mesh, points, facePoints, pointCounts);
	meshSample.setPositions(Alembic::Abc::V3fArraySample(
		(const Imath::V3f*)&points.front(), points.size() / 3));
	meshSample.setFaceIndices(Alembic::Abc::Int32ArraySample(facePoints));
	meshSample.setFaceCounts(Alembic::Abc::Int32ArraySample(pointCounts));



	//load mesh sample
	polyMesh->getSchema().set(meshSample);


	//write bounding box
	MPoint boudingMax = mesh.boundingBox().max();
	MPoint boudingMin = mesh.boundingBox().min();
	Imath::Box3d cbox;
	cbox.extendBy(Imath::V3d(boudingMax.x, boudingMax.y, boudingMax.z));
	cbox.extendBy(Imath::V3d(boudingMin.x, boudingMin.y, boudingMin.z));
	polyMesh->getSchema().getChildBoundsProperty().set(cbox);
	
	

	return MS::kSuccess;
}

void Mesh::write_faceset(const MFnMesh&mesh)
{
	std::shared_ptr<Alembic::AbcGeom::OPolyMesh> polyMesh = std::static_pointer_cast<Alembic::AbcGeom::OPolyMesh>(abcObject);
	MObjectArray connSGObjs(get_out_connected_SG(dagPath));
	const unsigned int sgCount = connSGObjs.length();
	MStatus status;
	for (unsigned int i = 0; i < sgCount; i++)
	{
		MObject connSGObj, compObj;
		connSGObj = connSGObjs[i];

		MFnDependencyNode fnDepNode(connSGObj);
		MFnDependencyNode shader;
		MString connSgObjName;

		status = get_conn_shader(connSGObj, shader);
		if (status)
			connSgObjName = shader.name();
		else
			connSgObjName = fnDepNode.name();

		std::vector<Alembic::Util::int32_t> faceIndices;

		status = getSetComponents(dagPath, connSGObj, compObj);

		//如果一个模型上只有一种材质,则将整个模型的面都设置为同一种faceset,否则按照原有的方式设置faceSet
		if (status)
		{
			// retrieve the face indices
			MIntArray indices;
			MFnSingleIndexedComponent compFn;
			compFn.setObject(compObj);
			compFn.getElements(indices);
			const unsigned int numData = indices.length();

			// encountered the whole object mapping. skip it.
			if (numData == 0)
				continue;
			faceIndices.resize(numData);
			for (unsigned int j = 0; j < numData; ++j)
			{
				faceIndices[j] = indices[j];
			}
		}
		else
		{
			int numPolygons = mesh.numPolygons(&status);
			if (!status)
				continue;
			faceIndices.reserve(numPolygons);
			for (int i = 0; i < numPolygons; i++) {
				faceIndices.push_back(i);
			}
		}

		connSgObjName = stripNamespaces(connSgObjName);

		Alembic::AbcGeom::OFaceSet faceSet;
		std::string faceSetName(connSgObjName.asChar());

		MPlug abcFacesetNamePlug = fnDepNode.findPlug("AbcFacesetName", true);
		if (!abcFacesetNamePlug.isNull())
		{
			faceSetName = abcFacesetNamePlug.asString().asChar();
		}

		if (polyMesh->getSchema().valid())
		{
			if (polyMesh->getSchema().hasFaceSet(faceSetName))
			{
				faceSet = polyMesh->getSchema().getFaceSet(faceSetName);
			}
			else
			{
				faceSet = polyMesh->getSchema().createFaceSet(faceSetName);
			}
		}
		Alembic::AbcGeom::OFaceSetSchema::Sample samp;
		samp.setFaces(Alembic::Abc::Int32ArraySample(faceIndices));

		Alembic::AbcGeom::OFaceSetSchema faceSetSchema = faceSet.getSchema();
		faceSetSchema.set(samp);
		faceSetSchema.setFaceExclusivity(Alembic::AbcGeom::kFaceSetExclusive);
	}
}

