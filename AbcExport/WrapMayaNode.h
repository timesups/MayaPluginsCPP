#pragma once
//maya
#include <maya/MDagPath.h>
//alembic
#include <Alembic/Abc/All.h>
#include <Alembic/AbcGeom/All.h>
#include <Alembic/AbcCoreAbstract/All.h>
#include <Alembic/AbcCoreOgawa/All.h>



class RootNode 
{
public:
	RootNode(const MDagPath& inDagPath):
		dagPath(inDagPath){}
	~RootNode() = default;
	virtual MStatus write_to_alembic_file() { return MS::kSuccess; }
	virtual bool is_intermediate_objet() 
	{
		return false;
	}
public:
	//此处是指向常量的指针,因为不需要修改这些量的值
	const MDagPath dagPath;
	std::shared_ptr<Alembic::Abc::OObject> abcObject = nullptr;

};


class Transform : public RootNode
{
public:
	Transform(const MDagPath& inDagPath, const std::shared_ptr<RootNode> inParent, bool anim = false, const uint32_t timeIndex = 0);
	virtual MStatus write_to_alembic_file() override;
};


class Mesh : public RootNode
{
public:
	Mesh(const MDagPath& inDagPath, const std::shared_ptr<RootNode> inParent, bool anim = false, const uint32_t timeIndex = 0);
	virtual bool is_intermediate_objet() override;
	virtual MStatus write_to_alembic_file() override;
private:
	void write_faceset(const MFnMesh& mesh);
};

class Curve : public RootNode
{
public:
	Curve(const MDagPath& inDagPath, const std::shared_ptr<RootNode> inParent, bool anim = false, const uint32_t timeIndex = 0);
	virtual MStatus write_to_alembic_file() override;
	void write_group_name(const std::string& groupName);
	void write_is_guide(bool is_guide);
	void write_group_id(std::vector<int> group_id);

};


class Spline : public RootNode
{
public:
	Spline(const MDagPath& inDagPath, const std::shared_ptr<RootNode> inParent, bool anim = false, const uint32_t timeIndex = 0);
	virtual MStatus write_to_alembic_file() override;
};