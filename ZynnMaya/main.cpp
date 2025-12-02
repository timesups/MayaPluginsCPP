#include "AbcExport.h"
#include <maya/MFnPlugin.h>


MStatus initializePlugin(MObject obj)
{
    MStatus status;
    MFnPlugin plugin(obj, "Alembic", ABCEXPORT_VERSION, "Any");

    status = plugin.registerCommand(
        AbcExport::AbcExportNewCommandName.c_str(), AbcExport::creator);

    if (!status)
    {
        status.perror("registerCommand");
    }
    return status;
}

MStatus uninitializePlugin(MObject obj)
{
    MStatus status;
    MFnPlugin plugin(obj);

    status = plugin.deregisterCommand(AbcExport::AbcExportNewCommandName.c_str());

    if (!status)
    {
        status.perror("deregisterCommand");
    }

    return status;
}
