#include <maya/MFnPlugin.h>
#include <maya/MGlobal.h>
#include "AbcExportCommand.h"




MStatus initializePlugin(MObject pluginObj) 
{
	const char* vender = "zcx";
	const char* version = "0.0.1";
	const char* requireApiVersion = "any";

	MStatus status;

	MFnPlugin pluginfn(pluginObj, vender, version,requireApiVersion, &status);

	if (!status) 
	{
		MGlobal::displayError("Failed to initialize plugin: " + status.errorString());
		return(status);
	}

	status = pluginfn.registerCommand(AbcExportCommand::CommandName(), AbcExportCommand::Creator);

	if (!status) 
	{
		MGlobal::displayError("Failed to register abcExportCommand");
		return(status);
	}

	return(status);
}



MStatus uninitializePlugin(MObject pluginObj)
{

	MStatus status;

	MFnPlugin pluginfn(pluginObj);
	status = pluginfn.deregisterCommand(AbcExportCommand::CommandName());
	if (!status) {
		MGlobal::displayError("Failed to deregister abcExportCommand");
		return status;
	}

	return(status);
}