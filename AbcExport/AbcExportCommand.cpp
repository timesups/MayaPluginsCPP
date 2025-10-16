#include <maya/MTime.h>
#include <maya/MGlobal.h>
#include <maya/MSelectionList.h>
#include <maya/MArgList.h>
#include <maya/MStringArray.h>

#include "AbcExportCommand.h"
#include "OAlembicFile.h"
#include "Marco.h"



static MString MEL_COMMAND = "abcExport";

AbcExportCommand::AbcExportCommand()
{
}

AbcExportCommand::~AbcExportCommand()
{
}

MStatus AbcExportCommand::doIt(const MArgList& args)
{
	TIMESTART
	MStatus status;

	//get args
	unsigned int argIndex = 0;
	MStringArray objNames = args.asStringArray(argIndex,&status);
	if (!status) {
		MGlobal::displayError("Faied to get arg 1(objNames)");
		return status;
	}


	MString filePath = args.asString(1, &status);
	if (!status) {
		MGlobal::displayError("Faied to get arg 2(filePath)");
		return status;
	}

	int frameStart = args.asInt(2, &status);
	if (!status) {
		MGlobal::displayError("Faied to get arg 3(frameStart) set to default");
		frameStart = 0;
	}

	int frameEnd = args.asInt(3, &status);
	if (!status) {
		MGlobal::displayError("Faied to get arg 4(frameEnd) set to default");
		frameEnd = 0;
	}

	int frameStartExpend = args.asInt(4, &status);
	if(!status)
	{
		frameStartExpend = 0;
	}

	int frameEndExpend = args.asInt(5, &status);
	if (!status)
	{
		frameEndExpend = 0;
	}
	//~arg get over


	bool anim = false;
	if ((frameEnd - frameStart) > 0) {
		anim = true;
	}

	//calc time sampling
	MTime sec(1, MTime::kSeconds);
	float spf = 1.0 / sec.asUnits(MTime::uiUnit());
	Alembic::AbcCoreAbstract::TimeSampling timeSampling(spf, spf * frameStart);
	OAlembicFile abcFile(filePath.asChar(), anim, timeSampling);

	//gather dag paths;
	std::vector<MDagPath> dagPaths;
	MSelectionList slList;
	dagPaths.reserve(objNames.length());

	for (int i = 0; i < objNames.length(); i++) {
		slList.clear();
		status = MGlobal::getSelectionListByName(objNames[i], slList);
		if (!status)
			continue;
		MDagPath dpath;
		status = slList.getDagPath(0, dpath);
		if (!status)
			continue;
		dagPaths.push_back(dpath);
	}

	if (dagPaths.size() == 0) {
		MGlobal::displayWarning("Nothing to export");
		return MS::kFailure;
	}


	//export
	if (anim) 
	{

		for (int f = frameStart - frameStartExpend; f <= frameEnd + frameEndExpend; f++) 
		{
			if(f<frameStart)
			{
				MGlobal::viewFrame(frameStart);
				abcFile.write_alembic_data(dagPaths);
			}
			else if(f >= frameStart && f <= frameEnd)
			{
				MGlobal::viewFrame(f);
				abcFile.write_alembic_data(dagPaths);
			}
			else
			{
				MGlobal::viewFrame(frameEnd);
				abcFile.write_alembic_data(dagPaths);
			}
		}
	}
	else
	{
		abcFile.write_alembic_data(dagPaths);
	}

	MGlobal::displayInfo("Export successful");

	TIMEEND
	return MS::kSuccess;
}

void* AbcExportCommand::Creator()
{
	return(new AbcExportCommand());
}

MString AbcExportCommand::CommandName()
{
	return MEL_COMMAND;
}
