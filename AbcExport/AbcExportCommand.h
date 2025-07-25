#pragma once
#include <maya/MPxCommand.h>




class AbcExportCommand :public MPxCommand
{
public:
	AbcExportCommand();

	virtual ~AbcExportCommand() override;
	virtual MStatus doIt(const MArgList& args) override;



	// static methods
	static void* Creator();

	static MString CommandName();
};