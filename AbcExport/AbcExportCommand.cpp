#include "AbcExportCommand.h"
#include "OAlembicFile.h"

#include <maya/MTime.h>
#include <maya/MGlobal.h>
#include <maya/MSelectionList.h>
#include <maya/MArgList.h>
#include <maya/MStringArray.h>


static MString MEL_COMMAND = "abcExport";

AbcExportCommand::AbcExportCommand()
{
}

AbcExportCommand::~AbcExportCommand()
{
}

MStatus AbcExportCommand::doIt(const MArgList& args)
{
	MStatus status;

	//get args
	unsigned int argIndex = 0;
	MStringArray objNames = args.asStringArray(argIndex,&status);
	if (!status) {
		MGlobal::displayError("Faied to get arg 1");
		return status;
	}


	MString filePath = args.asString(1, &status);
	if (!status) {
		MGlobal::displayError("Faied to get arg 2");
		return status;
	}


	int frameStart = args.asInt(2, &status);
	if (!status) {
		MGlobal::displayError("Faied to get arg 3 set to default");
		frameStart = 0;
	}

	int frameEnd = args.asInt(3, &status);
	if (!status) {
		MGlobal::displayError("Faied to get arg 4 set to default");
		frameEnd = 0;
	}

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
		for (int f = frameStart; f <= frameEnd; f++) {
			MGlobal::viewFrame(f);
			abcFile.write_alembic_data(dagPaths);
		}
	}
	else
	{
		abcFile.write_alembic_data(dagPaths);
	}

	MGlobal::displayInfo("Export successful");
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
