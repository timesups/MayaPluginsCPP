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


MStatus get_uvs(const MFnMesh& mesh, std::vector<float>& outuvs)
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
	return MS::kSuccess;
}

MStatus get_normals(const MFnMesh& mesh, std::vector<float>& outNormals)
{
	MStatus status;

	int polygonCount = mesh.numPolygons(&status);
	if (!status)
		return status;



	for (int i = 0; i < polygonCount; i++)
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


Transform::Transform(
	const MDagPath* inDagPath, 
	const RootNode* inParent, 
	bool anim, 
	const uint32_t timeIndex):RootNode(inDagPath)
{
	

	try
	{
		MFnTransform transform(*dagPath);
		if (anim)
			abcObject = new Alembic::AbcGeom::OXform(*(inParent->abcObject), std::string(transform.name().asChar()), timeIndex);
		else
			abcObject = new Alembic::AbcGeom::OXform(*(inParent->abcObject), std::string(transform.name().asChar()));
	}
	catch (const std::exception& e)
	{
		MGlobal::displayError(e.what());
	}

}

MStatus Transform::write_to_alembic_file()
{
	MStatus status;


	MFnTransform transform(*dagPath);
	double mat[4][4];
	status = transform.transformationMatrix().get(mat);
	if (!status) {
		MGlobal::displayError("Failed to get matrix");
		return status;
	}

	Alembic::AbcGeom::XformOp op(Alembic::AbcGeom::XformOperationType::kMatrixOperation, 0);
	Alembic::AbcGeom::XformSample sample;

	sample.addOp(op, Imath::M44d(mat));

	Alembic::AbcGeom::OXform* oxform = (Alembic::AbcGeom::OXform*)abcObject;
	oxform->getSchema().set(sample);

	return status;
}


Mesh::Mesh(
	const MDagPath* inDagPath,
	const RootNode* inParent,
	bool anim, const uint32_t timeIndex) :RootNode(inDagPath)
{
	MFnMesh mesh(*dagPath);

	if (anim)
		abcObject = new Alembic::AbcGeom::OPolyMesh(*inParent->abcObject, std::string(mesh.name().asChar()), timeIndex);
	else
		abcObject = new Alembic::AbcGeom::OPolyMesh(*inParent->abcObject, std::string(mesh.name().asChar()));
	//uv
	std::vector<float> uvs;
	get_uvs(mesh, uvs);
	Alembic::AbcGeom::V2fArraySample uvArraySample((const Imath::V2f*)&uvs.front(), uvs.size() / 2);
	uvSample.setScope(Alembic::AbcGeom::kFacevaryingScope);
	uvSample.setVals(uvArraySample);
}

bool Mesh::is_intermediate_objet()
{
	MFnMesh mesh(*dagPath);
	return mesh.isIntermediateObject();
}

MStatus Mesh::write_to_alembic_file()
{
	MStatus status;
	MFnMesh mesh(*dagPath);

	//normal
	std::vector<float> normals;
	status = get_normals(mesh, normals);
	if (!status || normals.empty()) {
		MGlobal::displayError("Failed to get normal");
		return status;
	}
	Alembic::AbcGeom::V3fArraySample normalArray((const Imath::V3f*)&normals.front(), normals.size() / 3);
	Alembic::AbcGeom::ON3fGeomParam::Sample normalSample(normalArray, Alembic::AbcGeom::kFacevaryingScope);


	//vertex
	std::vector<float> vertices;
	status = get_vertices(mesh, vertices);
	if (!status || vertices.empty()) {
		MGlobal::displayError("Failed to get Points");
		return status;
	}
	Alembic::AbcGeom::P3fArraySample vertexArray((const Imath::V3f*)&vertices.front(), vertices.size() / 3);


	//index & cout
	std::vector<int> indexList, countList;

	MItMeshPolygon mayaMeshPoly(*dagPath);
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

	//mesh sample
	Alembic::AbcGeom::OPolyMeshSchema::Sample meshSample(vertexArray, indexArray, countArray, uvSample, normalSample);

	//bounding box

	MPoint boudingMax = mesh.boundingBox().max();
	MPoint boudingMin = mesh.boundingBox().min();

	Imath::Box3d cbox;
	cbox.extendBy(Imath::V3d(boudingMax.x, boudingMax.y, boudingMax.z));
	cbox.extendBy(Imath::V3d(boudingMin.x, boudingMin.y, boudingMin.z));


	Alembic::AbcGeom::OPolyMesh* polyMesh = (Alembic::AbcGeom::OPolyMesh*)abcObject;
	polyMesh->getSchema().set(meshSample);
	polyMesh->getSchema().getChildBoundsProperty().set(cbox);

	return status;
}
