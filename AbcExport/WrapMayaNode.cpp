//maya
#include <maya/MFnDependencyNode.h>
#include <maya/MFnTransform.h>
#include <maya/MGlobal.h>
#include <maya/MFnMesh.h>
#include <maya/MFnSingleIndexedComponent.h>
#include <maya/MMatrix.h>
#include <maya/MBoundingBox.h>
#include <maya/MFnNurbsCurve.h>
#include <maya/MItDag.h>

//
#include "WrapMayaNode.h"
#include "util.h"


using namespace Alembic;
using namespace std;

//Transform
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

//Mesh
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

//curve
Curve::Curve(const MDagPath& inDagPath, const std::shared_ptr<RootNode> inParent, bool anim, const uint32_t timeIndex) :RootNode(inDagPath)
{
	MFnNurbsCurve curve(dagPath);
	if (anim)
		abcObject = std::make_shared<Alembic::AbcGeom::OCurves>(Alembic::AbcGeom::OCurves(*(inParent->abcObject), std::string(curve.name().asChar()), timeIndex));
	else
		abcObject = std::make_shared<Alembic::AbcGeom::OCurves>(Alembic::AbcGeom::OCurves(*(inParent->abcObject), std::string(curve.name().asChar())));

}

MStatus Curve::write_to_alembic_file()
{
	MStatus status;
	MFnNurbsCurve curve(dagPath);
	shared_ptr<AbcGeom::OCurves> abcCurve = std::static_pointer_cast<AbcGeom::OCurves>(abcObject);

	AbcGeom::OCurvesSchema::Sample curveSample;


	//basis
	curveSample.setBasis(AbcGeom::BasisType::kBsplineBasis);
	curveSample.setWrap(AbcGeom::CurvePeriodicity::kNonPeriodic);

	
	//type
	if (curve.degree() == 3)
		curveSample.setType(AbcGeom::CurveType::kCubic);
	else if (curve.degree() == 1)
		curveSample.setType(AbcGeom::CurveType::kLinear);
	else
		curveSample.setType(AbcGeom::CurveType::kVariableOrder);



	uint8_t order = curve.degree() + 1;
	int nVertex = curve.numCVs();


	std::vector< uint8_t> orders = { order };
	std::vector<int> vertices = { nVertex };

	curveSample.setCurvesNumVertices(Alembic::Abc::Int32ArraySample(vertices));

	curveSample.setOrders(Abc::UcharArraySample(orders));
	//knots
	std::vector<float> knots;

	MDoubleArray knotsArray;
	curve.getKnots(knotsArray);
	knots.reserve(knotsArray.length() + 2);
	if (knotsArray.length() > 1)
	{
		unsigned int knotsLength = knotsArray.length();
		if (knotsArray[0] == knotsArray[knotsLength - 1] ||
			knotsArray[0] == knotsArray[1])
		{
			knots.push_back(static_cast<float>(knotsArray[0]));
		}
		else
		{
			knots.push_back(static_cast<float>(2 * knotsArray[0] - knotsArray[1]));
		}
		for (unsigned int j = 0; j < knotsLength; ++j)
		{
			knots.push_back(static_cast<float>(knotsArray[j]));
		}
		if (knotsArray[0] == knotsArray[knotsLength - 1] ||
			knotsArray[knotsLength - 1] == knotsArray[knotsLength - 2])
		{
			knots.push_back(static_cast<float>((knotsArray[knotsLength - 1])));
		}
		else
		{
			knots.push_back(static_cast<float>(2 * knotsArray[knotsLength - 1] -
				knotsArray[knotsLength - 2]));
		}
	}
	curveSample.setKnots(Abc::FloatArraySample(knots));

	//positions
	std::vector<float> Positions;
	MPointArray cvArray;
	curve.getCVs(cvArray);
	for (int i = 0; i < nVertex; i++) 
	{
		Positions.push_back(cvArray[i].x);
		Positions.push_back(cvArray[i].y);
		Positions.push_back(cvArray[i].z);
	}
	curveSample.setPositions(Abc::P3fArraySample((const Imath::V3f*)Positions.data(), Positions.size() / 3));
	//load sample
	abcCurve->getSchema().set(curveSample);


	return MS::kSuccess;
}

void Curve::write_group_name(const std::string& groupName)
{
}

void Curve::write_is_guide(bool is_guide)
{
}

void Curve::write_group_id(std::vector<int> group_id)
{
}



//Spline
Spline::Spline(const MDagPath& inDagPath, const std::shared_ptr<RootNode> inParent, bool anim, const uint32_t timeIndex):RootNode(inDagPath)
{
	MFnDependencyNode splineNode(dagPath.node());
	if (anim)
		abcObject = std::make_shared<Alembic::AbcGeom::OCurves>(Alembic::AbcGeom::OCurves(*(inParent->abcObject), std::string(splineNode.name().asChar()), timeIndex));
	else
		abcObject = std::make_shared<Alembic::AbcGeom::OCurves>(Alembic::AbcGeom::OCurves(*(inParent->abcObject), std::string(splineNode.name().asChar())));
}

MStatus Spline::write_to_alembic_file()
{
	MStatus status;
	MFnDependencyNode splineNode(dagPath.node());
	std::vector<std::vector<uint64_t>> PrimitiveInfosList;
	std::vector<std::vector<float>> PositionsList;
	std::vector<std::vector<float>> WidthDataList;
	GetSplineData(splineNode, PrimitiveInfosList, PositionsList, WidthDataList, false);

	shared_ptr<AbcGeom::OCurves> abcCurve = std::static_pointer_cast<AbcGeom::OCurves>(abcObject);
	AbcGeom::OCurvesSchema::Sample curveSample;


	int numCurves = 0;
	uint64_t numCvs = 0;
	for (int i=0;i<PrimitiveInfosList.size();i++)
	{
		numCurves += (PrimitiveInfosList[i].size()/2);

		for(int j=0;j<PrimitiveInfosList[i].size();j+=2)
		{
			numCvs += PrimitiveInfosList[i][j];
		}

	}

	std::vector<uint8_t> orders(numCurves);
	std::vector<int> nVertices(numCurves);


	curveSample.setBasis(AbcGeom::kBsplineBasis);
	curveSample.setWrap(AbcGeom::CurvePeriodicity::kNonPeriodic);
	curveSample.setType(AbcGeom::kCubic);

	int degree = 3;

	std::vector<Imath::V3f> pointArray(numCvs);
	std::vector<float> widthArray(numCvs);


	std::vector<float> knots;

	int curveIndex = 0;
	int cvIndex = 0;
	

	for(int i=0;i<PrimitiveInfosList.size();i++)
	{
		auto primitiveInfos = PrimitiveInfosList[i];
		auto positionData = PositionsList[i];
		auto widthData = WidthDataList[i];

		for (int j = 0; j < primitiveInfos.size(); j += 2)
		{
			uint64_t offset = primitiveInfos[j];
			int length = primitiveInfos[j + 1];

			if (length < 2)
				continue;

			uint64_t startAddr = offset * 3;
			for(int k=0;k<length;k++)
			{
				pointArray[cvIndex].x = positionData[startAddr];
				pointArray[cvIndex].y = positionData[startAddr+1];
				pointArray[cvIndex].z = positionData[startAddr+2];

				widthArray[cvIndex] = widthData[offset + k];
				startAddr += 3;
				cvIndex += 1;
			}

			orders[curveIndex] = degree + 1;
			nVertices[curveIndex] = length;

			int knotsInsideNum = length - degree + 1;

			int knotsLitSize = 2 * degree + knotsInsideNum;
			std::vector<int> knotsList(knotsLitSize);
			std::fill_n(knotsList.begin(), degree, 0);

			for(int n=0;n<knotsInsideNum;n++)
			{
				knotsList[degree + n] = n;
			}
			std::fill_n(knotsList.begin() + degree + knotsInsideNum, degree, knotsInsideNum - 1);
			for (auto value : knotsList)
				knots.push_back(static_cast<float>(value));
			curveIndex += 1;
		}

	}


	curveSample.setCurvesNumVertices(nVertices);
	curveSample.setPositions(pointArray);
	curveSample.setKnots(Abc::FloatArraySample(knots));
	curveSample.setOrders(orders);
	//write width
	curveSample.setWidths(AbcGeom::OFloatGeomParam::Sample(widthArray, AbcGeom::GeometryScope::kVertexScope));

	abcCurve->getSchema().set(curveSample);
	return MS::kSuccess;
}


