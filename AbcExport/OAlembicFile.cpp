#include "OAlembicFile.h"
#include <maya/MFnDependencyNode.h>
#include <maya/MStatus.h>
#include <maya/MGlobal.h>
#include <maya/MFnMesh.h>
#include <maya/MFnNurbsCurve.h>
#include <string>


OAlembicFile::OAlembicFile(std::string filePath, bool inAnim, Alembic::AbcCoreAbstract::TimeSampling timeSamping)
{
	archive = std::make_shared<Alembic::Abc::OArchive>(Alembic::Abc::OArchive(Alembic::AbcCoreOgawa::WriteArchive(), filePath));
	archive->setCompressionHint(0);
	this->anim = inAnim;
	if (inAnim)
		timeIndex = archive->addTimeSampling(timeSamping);
}

OAlembicFile::~OAlembicFile()
{
}

void OAlembicFile::write_alembic_data(std::vector<MDagPath>& dags)
{
	if (objects.empty()) {
		for (auto dagPath : dags) {
			recursion_collect_object(dagPath, nullptr);
		}
	}
	for (auto obj : objects) {
		obj->write_to_alembic_file();
	}
}

void OAlembicFile::recursion_collect_object(const MDagPath& inDagPath, std::shared_ptr<RootNode> parent)
{
	MStatus status;
	unsigned int childCount = inDagPath.childCount(&status);
	if (!status || childCount == 0)
		return;

	if (!parent) 
	{
		parent = std::make_shared<RootNode>(RootNode(MDagPath()));
		parent->abcObject = std::make_shared<Alembic::Abc::OObject>(archive->getTop());
	}
	std::shared_ptr<Transform> transform = std::make_shared<Transform>(Transform(inDagPath,parent,anim,timeIndex));
	objects.push_back(transform);


	for (int i = 0; i < inDagPath.childCount(); i++) 
	{
		MObject childObject = inDagPath.child(i);
		MDagPath childDagPath = MDagPath::getAPathTo(childObject);

		MFnDependencyNode childDepNode(childObject);

		if (childObject.apiType() == MFn::kTransform) 
		{
			recursion_collect_object(childDagPath, transform);
		}
		else if(childObject.apiType() == MFn::kMesh)
		{
			MFnMesh mayaMesh(childDagPath);
			if (mayaMesh.isIntermediateObject())
				continue;
			//因为一个Oxform指定对应一个shape,所以如果重复绑定会丢失父级,导致导入maya后不显示
			std::shared_ptr<Mesh> mesh = std::make_shared<Mesh>(Mesh(childDagPath, transform, anim, timeIndex));
			objects.push_back(mesh);
		}
		else if (childObject.apiType() == MFn::kNurbsCurve)
		{
			MFnNurbsCurve mayaCurve(childDagPath);
			if (mayaCurve.isIntermediateObject())
				continue;

			std::shared_ptr<Curve> curve = std::make_shared<Curve>(Curve(childDagPath, transform, anim, timeIndex));
			objects.push_back(curve);
		}
		else if (childDepNode.typeName() == "xgmSplineDescription")
		{
			std::shared_ptr<Spline> curve = std::make_shared<Spline>(Spline(childDagPath, transform, anim, timeIndex));
			objects.push_back(curve);
		}
	}
}
