#pragma once

#include "Foundation.h"

class AbcExport : public MPxCommand
{
  public:
    AbcExport();
    ~AbcExport() override;
    MStatus doIt(const MArgList& args) override;

    static MSyntax  createSyntax();
    static void* creator();

    static std::string AbcExportNewCommandName;
};

