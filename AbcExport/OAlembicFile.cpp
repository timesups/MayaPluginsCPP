#include "OAlembicFile.h"
#include <maya/MFnDependencyNode.h>
#include <maya/MStatus.h>

OAlembicFile::OAlembicFile(std::string filePath, bool inAnim, Alembic::AbcCoreAbstract::TimeSampling timeSamping)
{
	archive = new Alembic::Abc::OArchive(Alembic::AbcCoreOgawa::WriteArchive(), filePath);
	this->anim = inAnim;
	if (inAnim)
		timeIndex = archive->addTimeSampling(timeSamping);
}

OAlembicFile::~OAlembicFile()
{
	delete archive;
	archive = nullptr;
}

void OAlembicFile::write_alembic_data(std::vector<MDagPath>& dags)
{
	if (objects.empty()) {
		for (auto dagPath : dags) {
			recursion_collect_object(&dagPath, nullptr);
		}
	}

	for (auto obj : objects) {
		obj->write_to_alembic_file();
	}
}

void OAlembicFile::recursion_collect_object(const MDagPath* inDagPath, RootNode* parent)
{
	if (inDagPath->childCount() == 0)
		return;

	if (!parent) {
		parent = new RootNode(nullptr, nullptr);
		parent->abcObject = &archive->getTop();
	}

	Transform transform(inDagPath, parent, anim, timeIndex);

	objects.push_back(&transform);

	for (int i = 0; i < inDagPath->childCount(); i++) {
		MObject childObject = inDagPath->child(i);
		MFnDependencyNode childDpNode(childObject);
		MDagPath childDagPath = MDagPath::getAPathTo(childObject);
		if (childObject.apiType() == MFn::kMesh) {
			Mesh mesh(&childDagPath, &transform, anim, timeIndex);
			if (!mesh.is_intermediate_objet())
				objects.push_back(&mesh);
		}
		else if (childObject.apiType() == MFn::kTransform) {
			recursion_collect_object(&childDagPath, &transform);
		}
	}


}
