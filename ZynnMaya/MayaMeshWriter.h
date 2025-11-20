#pragma once
#include "Foundation.h"
#include "MayaTransformWriter.h"
#include <Alembic/AbcGeom/OPolyMesh.h>
#include <Alembic/AbcGeom/OSubD.h>

// Mechanism to cache the MFnSet::getMembers results
struct mObjectCmp
{
    bool operator()(const MObject& o1, const MObject& o2) const
    {
        return strcmp(MFnDependencyNode(o1).name().asChar(), MFnDependencyNode(o2).name().asChar()) < 0;
    }
};

typedef std::map <MObject, MSelectionList, mObjectCmp> GetMembersMap;

// Writes an MFnMesh as a poly mesh OR a subd mesh
class MayaMeshWriter
{
  public:

    MayaMeshWriter(MDagPath & iDag, Alembic::Abc::OObject & iParent,
        Alembic::Util::uint32_t iTimeIndex, const JobArgs & iArgs,
        GetMembersMap& gmMap);
    void write();
    bool isAnimated() const;
    bool isSubD();
    unsigned int getNumCVs();
    unsigned int getNumFaces();

  private:

    void fillTopology(
        std::vector<float> & oPoints,
        std::vector<Alembic::Util::int32_t> & oFacePoints,
        std::vector<Alembic::Util::int32_t> & oPointCounts);

    void writePoly(const Alembic::AbcGeom::OV2fGeomParam::Sample & iUVs);

    void writeSubD(const Alembic::AbcGeom::OV2fGeomParam::Sample & iUVs);

    void getUVs(std::vector<float> & uvs,
        std::vector<Alembic::Util::uint32_t> & indices,
        std::string & name);

    void getPolyNormals(std::vector<float> & oNormals);
    bool mNoNormals;
    bool mWriteGeometry;
    bool mWriteUVs;
    bool mWriteColorSets;
    bool mWriteUVSets;

    bool mIsGeometryAnimated;
    MDagPath mDagPath;

    Alembic::AbcGeom::OPolyMeshSchema mPolySchema;
    Alembic::AbcGeom::OSubDSchema     mSubDSchema;

    void writeColor();
    std::vector<Alembic::AbcGeom::OC3fGeomParam> mRGBParams;
    std::vector<Alembic::AbcGeom::OC4fGeomParam> mRGBAParams;

    void writeUVSets();
    typedef std::vector<Alembic::AbcGeom::OV2fGeomParam> UVParamsVec;
    UVParamsVec mUVparams;
};
