//maya
#include <maya/MGlobal.h>
#include <maya/MTime.h>
#include <maya/MItSelectionList.h>
#include <maya/MDagPath.h>
#include <maya/MFnMesh.h>
#include <maya/MItMeshPolygon.h>
#include <maya/MPointArray.h>
#include <maya/MBoundingBox.h>
//alembic
#include <Alembic/Abc/All.h>
#include <Alembic/AbcGeom/All.h>
#include <Alembic/AbcCoreAbstract/All.h>
#include <Alembic/AbcCoreOgawa/All.h>
//
#include <vector>
//custom 
#include "AbcExportCommand.h"


MStatus get_uvs(const MFnMesh& mesh,std::vector<float>& outuvs)
{
	MStatus status;

	MFloatArray uArray;
	MFloatArray vArray;
	status = mesh.getUVs(uArray, vArray);

	if (!status)
	{
		return status;
	}
	outuvs.reserve(uArray.length()*2);
	for (int i = 0; i < uArray.length(); i++) 
	{
		outuvs.push_back(uArray[i]);
		outuvs.push_back(vArray[i]);
	}
	return MS::kSuccess;
}

MStatus get_normals(const MFnMesh& mesh, std::vector<float>& outNormals)
{
	MStatus status;

	int polygonCount = mesh.numPolygons(&status);
	if (!status)
		return status;



	for (int i=0;i<polygonCount;i++) 
	{
		MFloatVectorArray normals;
		mesh.getFaceVertexNormals(i, normals);
		for (int j = 0; j < normals.length(); j++) 
		{
			outNormals.push_back(normals[j].x);
			outNormals.push_back(normals[j].y);
			outNormals.push_back(normals[j].z);
		}

	}
	return MS::kSuccess;
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

static MString MEL_COMMAND = "abcExport";

AbcExportCommand::AbcExportCommand()
{
}

AbcExportCommand::~AbcExportCommand()
{
}

MStatus AbcExportCommand::doIt(const MArgList& args)
{
	MStatus status;

	MSelectionList selectionList;

	status = MGlobal::getActiveSelectionList(selectionList);
	if (!status) {
		MGlobal::displayError("Failed to get selection list");
		return status;
	}
	MDagPath dagPath;
	status = selectionList.getDagPath(0, dagPath);

	if (!status) {
		MGlobal::displayError("Failed to get dag path");
		return status;
	}

	if (!dagPath.hasFn(MFn::kMesh)) {
		MGlobal::displayWarning("Selected oject is not a mesh");
		return MS::kFailure;
	}

	MFnMesh mayaMesh(dagPath, &status);



	std::vector<float> uvs;

	status = get_uvs(mayaMesh, uvs);

	if (!status || uvs.empty())
	{
		MGlobal::displayError("Failed to get uv");
		return status;
	}

	//uv
	Alembic::AbcGeom::V2fArraySample uvArraySample((const Imath::V2f*)&uvs.front(), uvs.size() / 2);
	Alembic::AbcGeom::OV2fGeomParam::Sample uvSample(uvArraySample, Alembic::AbcGeom::kFacevaryingScope);

	//normal
	std::vector<float> normals;
	status = get_normals(mayaMesh, normals);
	if (!status || normals.empty()) {
		MGlobal::displayError("Failed to get normal");
		return status;
	}
	Alembic::AbcGeom::V3fArraySample normalArray((const Imath::V3f*)&normals.front(), normals.size() / 3);
	Alembic::AbcGeom::ON3fGeomParam::Sample normalSample(normalArray, Alembic::AbcGeom::kFacevaryingScope);

	//vertex

	std::vector<float> vertices;
	status = get_vertices(mayaMesh, vertices);
	if (!status || vertices.empty()) {
		MGlobal::displayError("Failed to get Points");
		return status;
	}
	Alembic::AbcGeom::P3fArraySample vertexArray((const Imath::V3f*)&vertices.front(), vertices.size() / 3);

	//index & cout
	std::vector<int> indexList, countList;

	MItMeshPolygon mayaMeshPoly(dagPath);
	while (!mayaMeshPoly.isDone())
	{
		MIntArray indices;
		status = mayaMeshPoly.getVertices(indices);
		if (!status) 
		{
			MGlobal::displayError("Failed to get index");
			return status;
		}
		countList.push_back(indices.length());
		for (int i = 0; i < indices.length(); i++) {
			indexList.push_back(indices[i]);
		}
		mayaMeshPoly.next();
	}
	Alembic::AbcGeom::Int32ArraySample indexArray(&indexList.front(), indexList.size());
	Alembic::AbcGeom::Int32ArraySample countArray(&countList.front(), countList.size());


	Alembic::AbcGeom::OPolyMeshSchema::Sample meshSample(vertexArray,indexArray,countArray,uvSample,normalSample);

	Alembic::Abc::OArchive archive(Alembic::AbcCoreOgawa::WriteArchive(), "d:/Desktop/testss.abc");

	Alembic::AbcGeom::OPolyMesh mesh = Alembic::AbcGeom::OPolyMesh(archive.getTop(), "meshy");
	Alembic::AbcGeom::OPolyMeshSchema meshSchema = mesh.getSchema();
	meshSchema.set(meshSample);

	//АќЮЇКа

	MPoint boudingMax = mayaMesh.boundingBox().max();
	MPoint boudingMin = mayaMesh.boundingBox().min();

	Imath::Box3d cbox;
	cbox.extendBy(Imath::V3d(boudingMax.x, boudingMax.y, boudingMax.z));
	cbox.extendBy(Imath::V3d(boudingMin.x, boudingMin.y, boudingMin.z));

	meshSchema.getChildBoundsProperty().set(cbox);


	return status;
}

void* AbcExportCommand::Creator()
{
	return(new AbcExportCommand());
}

MString AbcExportCommand::CommandName()
{
	return MEL_COMMAND;
}
