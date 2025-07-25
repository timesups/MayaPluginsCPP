#include "WrapMayaNode.h"
#include <maya/MFnTransform.h>


Transform::Transform(
	const MDagPath* inDagPath,
	const RootNode* inParent, 
	const Alembic::AbcCoreAbstract::TimeSampling* inTimeSampling = nullptr):RootNode(inDagPath,inParent,inTimeSampling)
{
	MFnTransform transform(*dagPath);
	abcObject = new Alembic::AbcGeom::OXform(parent->abcObject, transform.name(), *inTimeSampling);

}

bool Transform::write_to_alembic_file()
{
	return false;
}
