#pragma once
#include <string>
#include <memory>
#include "WrapMayaNode.h"


class OAlembicFile {
public:
	OAlembicFile(std::string filePath, bool anim, Alembic::AbcCoreAbstract::TimeSampling timeSamping);
	~OAlembicFile();
	void write_alembic_data(std::vector<MDagPath>& dags);
private:
	void recursion_collect_object(const MDagPath& inDagPath, std::shared_ptr<RootNode> parent);
private:
	std::vector<std::shared_ptr<RootNode>> objects;
	std::shared_ptr<Alembic::Abc::OArchive> archive = nullptr;
	uint32_t timeIndex;
	bool anim;
};