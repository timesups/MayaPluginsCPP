#pragma once
#include <string>

#include "WrapMayaNode.h"


class OAlembicFile {
public:
	OAlembicFile(std::string filePath, bool anim, Alembic::AbcCoreAbstract::TimeSampling timeSamping);
	~OAlembicFile();
	void write_alembic_data(std::vector<MDagPath>& dags);
	void recursion_collect_object(const MDagPath* inDagPath, RootNode* parent);
private:
	std::vector<RootNode*> objects;
	Alembic::Abc::OArchive* archive = nullptr;
	uint32_t timeIndex;
	bool anim;
};