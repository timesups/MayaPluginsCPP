#pragma once

#include "Foundation.h"
#include "MayaTransformWriter.h"

#include <Alembic/AbcGeom/OCurves.h>

class MayaNurbsCurveWriter
{
  public:

    MayaNurbsCurveWriter(MDagPath& iDag,
        Alembic::Abc::OObject& iParent, Alembic::Util::uint32_t iTimeIndex,
        bool iIsCurveGr,bool ExportAnim);
    void write();
    bool isAnimated() const;
    unsigned int getNumCVs();
    unsigned int getNumCurves();
    MStatus BakeUV(MDagPath splineDagPath);
    std::vector<MPoint> rootList;
public:
    void WriteGroupName();
    void WriteIsGuide();
    void WriteGroupId(int id);
public:
    //attr in abc file
    std::string groomGroupNameAttrName = "groom_group_name";
    std::string groomGuideAttrName = "groom_guide";
    std::string groomGroupIdAttrName = "groom_group_id";
    std::string groomRootUVAttrName = "groom_root_uv";
    MString attrMeshUVName = "MeshUVName";
    bool isGuide = false;
    std::string groupName;

private:




    bool mIsAnimated;
    MDagPath mRootDagPath;
    MDagPathArray mNurbsCurves;

    Alembic::AbcGeom::OCurvesSchema mSchema;

    bool mIsCurveGrp;
    unsigned int mCVCount;
};
