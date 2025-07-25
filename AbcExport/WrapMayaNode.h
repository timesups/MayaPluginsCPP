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
	RootNode(const MDagPath* inDagPath, const RootNode* inParent,const Alembic::AbcCoreAbstract::TimeSampling* inTimeSampling=nullptr):
		dagPath(inDagPath), parent(inParent){}
	~RootNode()
	{
		if (abcObject) {
			delete abcObject;
			abcObject = nullptr;
		}
	}
	virtual bool write_to_alembic_file() = 0;
	virtual bool is_intermediate_objet() 
	{
		return false;
	}
public:
	//此处是指向常量的指针,因为不需要修改这些量的值
	const MDagPath *dagPath = nullptr;
	const RootNode *parent = nullptr;
	Alembic::Abc::OObject* abcObject = nullptr;
};



class Transform : public RootNode
{
	Transform(const MDagPath* inDagPath, const RootNode* inParent, const Alembic::AbcCoreAbstract::TimeSampling* inTimeSampling = nullptr);
public:
	virtual bool write_to_alembic_file() override;
};