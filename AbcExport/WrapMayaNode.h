#pragma once
//maya
#include <maya/MDagPath.h>
//alembic
#include <Alembic/Abc/All.h>
#include <Alembic/AbcGeom/All.h>
#include <Alembic/AbcCoreAbstract/All.h>
#include <Alembic/AbcCoreOgawa/All.h>


class RootNode {
public:
	RootNode(const MDagPath* inDagPath):
		dagPath(inDagPath){}
	~RootNode()
	{
		if (abcObject) {
			delete abcObject;
			abcObject = nullptr;
		}
	}
	virtual MStatus write_to_alembic_file() { return MS::kSuccess; }
	virtual bool is_intermediate_objet() 
	{
		return false;
	}
public:
	//此处是指向常量的指针,因为不需要修改这些量的值
	const MDagPath *dagPath = nullptr;
	Alembic::Abc::OObject* abcObject = nullptr;
};


class Transform : public RootNode
{
public:
	Transform(const MDagPath* inDagPath, const RootNode* inParent, bool anim = false, const uint32_t timeIndex = 0);
	virtual MStatus write_to_alembic_file() override;
};


class Mesh : public RootNode
{
public:
	Mesh(const MDagPath* inDagPath, const RootNode* inParent, bool anim = false, const uint32_t timeIndex = 0);
	virtual bool is_intermediate_objet() override;
	virtual MStatus write_to_alembic_file() override;
private:
	Alembic::AbcGeom::OV2fGeomParam::Sample uvSample;
};