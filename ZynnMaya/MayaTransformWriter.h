#pragma once
#include "Foundation.h"

#include <Alembic/AbcGeom/OXform.h>
#include <Alembic/AbcGeom/XformOp.h>
#include "MayaUtility.h"

// AnimChan contains what animated plugs to get as a double, and the helper
// info about what operation and which channel to set in mSample
struct AnimChan
{
    MPlug plug;

    // extra value to multiply the data off of the plug by, used to invert
    // certain operations, and convert radians to degrees
    double scale;

    std::size_t opNum;
    Alembic::Util::uint32_t channelNum;
};

// Writes an MFnTransform
class MayaTransformWriter
{
public:
    MayaTransformWriter(Alembic::Abc::OObject & iParent, MDagPath & iDag, 
        Alembic::Util::uint32_t iTimeIndex, const JobArgs & iArgs);

    MayaTransformWriter(MayaTransformWriter & iParent, MDagPath & iDag,
        Alembic::Util::uint32_t iTimeIndex, const JobArgs & iArgs);

    ~MayaTransformWriter();
    void write();
    bool isAnimated() const;
    Alembic::Abc::OObject getObject() {return mSchema.getObject();};

private:

    Alembic::AbcGeom::OXformSchema mSchema;

    void pushTransformStack(const MFnTransform & iTrans, bool iForceStatic);

    void pushTransformStack(const MFnIkJoint & iTrans, bool iForceStatic);

    Alembic::AbcGeom::XformSample mSample;

    std::vector < AnimChan > mAnimChanList;
    MPlug mInheritsPlug;

    size_t mJointOrientOpIndex[3];
    size_t mRotateOpIndex[3];
    size_t mRotateAxisOpIndex[3];

    bool mFilterEulerRotations;
    MEulerRotation mPrevJointOrientSolution;
    MEulerRotation mPrevRotateSolution;
    MEulerRotation mPrevRotateAxisSolution;
};

typedef Alembic::Util::shared_ptr < MayaTransformWriter >
    MayaTransformWriterPtr;
