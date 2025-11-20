#include "AbcExport.h"
#include <maya/MFnPlugin.h>


MStatus initializePlugin(MObject obj)
{
    MStatus status;
    MFnPlugin plugin(obj, "Alembic", ABCEXPORT_VERSION, "Any");

    status = plugin.registerCommand(
        AbcExport::AbcExportNewCommandName.c_str(), AbcExport::creator,
        AbcExport::createSyntax);

    if (!status)
    {
        status.perror("registerCommand");
    }

    MString info = "AbcExport v";
    info += ABCEXPORT_VERSION;
    info += " using ";
    info += Alembic::Abc::GetLibraryVersion().c_str();
    MGlobal::displayInfo(info);

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

    MGlobal::executeCommandOnIdle("AlembicDeleteUI");

    return status;
}
