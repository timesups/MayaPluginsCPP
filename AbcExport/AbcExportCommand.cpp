#include "AbcExportCommand.h"
#include "OAlembicFile.h"

#include <maya/MTime.h>
#include <maya/MGlobal.h>
#include <maya/MSelectionList.h>


static MString MEL_COMMAND = "abcExport";

AbcExportCommand::AbcExportCommand()
{
}

AbcExportCommand::~AbcExportCommand()
{
}

MStatus AbcExportCommand::doIt(const MArgList& args)
{
	int frameStart = 0;
	int frameEnd = 100;
	bool anim = true;
	std::string filePath = "d:/testss.abc";




	MStatus status;
	MTime sec(1, MTime::kSeconds);

	float spf = 1.0 / sec.asUnits(MTime::uiUnit());

	Alembic::AbcCoreAbstract::TimeSampling timeSampling(spf, spf * frameStart);

	OAlembicFile abcFile(filePath, anim, timeSampling);

	MSelectionList selections;
	status = MGlobal::getActiveSelectionList(selections);
	if (!status) {
		MGlobal::displayError("Failed to get selections");
		return status;
	}

	std::vector<MDagPath> dagPaths(selections.length());

	for (int i = 0; i < selections.length(); i++)
	{
		MDagPath dagPath;
		status = selections.getDagPath(i, dagPath);
		if (!status)
			continue;
		dagPaths.push_back(dagPath);
	}
		


	if (anim) {
		for (int f = frameStart; f <= frameEnd; f++) {
			MGlobal::viewFrame(f);
			abcFile.write_alembic_data(dagPaths);
		}
	}




	return status;
}

void* AbcExportCommand::Creator()
{
	return(new AbcExportCommand());
}

MString AbcExportCommand::CommandName()
{
	return MEL_COMMAND;
}
