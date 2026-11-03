#include "zone_geometry.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace body_slots;
using namespace body_slots::detail;
namespace {
unsigned checks=0;
#define CHECK(x) do { ++checks; if(!(x)) throw std::runtime_error("line "+std::to_string(__LINE__)+": " #x); } while(false)
RuntimeTransform Identity() { RuntimeTransform t{}; t.rotation[0]=t.rotation[4]=t.rotation[8]=t.scale=1; return t; }
bool Near(double x,double y) { return std::abs(x-y)<1e-6; }
}
int main() { try {
    const auto identity=Identity();
    ZoneShape shape{sizeof(ZoneShape),4,{0,0,0},{0,0,4},{},2,{}};
    float p[]{0.f,0.f,0.f};
    CHECK(ValidateZoneShape(shape)==Status::Ok);
    CHECK(ZoneContainsPoint(shape,identity,p));
    p[2]=4; CHECK(ZoneContainsPoint(shape,identity,p));
    p[2]=4.01f; CHECK(!ZoneContainsPoint(shape,identity,p));
    CHECK(ZonePointSignedDistance(shape,identity,p)>0);
    // A capsule includes these points; a finite cylinder must reject them.
    p[2]=-1; CHECK(!ZoneContainsPoint(shape,identity,p));
    auto capsule=shape; capsule.kind=2; CHECK(ZoneContainsPoint(capsule,identity,p));
    p[0]=2;p[2]=2;CHECK(ZoneContainsPoint(shape,identity,p));
    p[0]=2.01f;CHECK(!ZoneContainsPoint(shape,identity,p));
    p[0]=0;CHECK(Near(ZonePointSignedDistance(shape,identity,p),-2));
    p[0]=5;p[2]=8;CHECK(Near(ZonePointSignedDistance(shape,identity,p),5));
    auto transform=Identity();
    transform.position[0]=10;transform.scale=2;
    transform.rotation[0]=0;transform.rotation[2]=1;
    transform.rotation[6]=-1;transform.rotation[8]=0;
    p[0]=14;p[1]=4;p[2]=0;
    CHECK(ZoneContainsPoint(shape,transform,p));
    p[0]=18.01f;p[1]=0;CHECK(!ZoneContainsPoint(shape,transform,p));
    p[0]=20;CHECK(Near(ZonePointSignedDistance(shape,transform,p),1));
    auto bad=shape;bad.end[2]=0;CHECK(ValidateZoneShape(bad)==Status::InvalidArgument);
    bad=shape;bad.halfExtents[0]=1;CHECK(ValidateZoneShape(bad)==Status::InvalidArgument);
    bad=shape;bad.reserved[0]=1;CHECK(ValidateZoneShape(bad)==Status::InvalidArgument);
    for(unsigned i=0;i<3;++i){auto malformed=identity;malformed.rotation[i*3+i]=-1;
        CHECK(std::isnan(ZonePointSignedDistance(shape,malformed,p)));
        CHECK(!ZoneContainsPoint(shape,malformed,p));}
    auto malformed=identity;malformed.rotation[1]=0.5f;
    CHECK(std::isnan(ZonePointSignedDistance(shape,malformed,p)));
    malformed=identity;malformed.scale=0;CHECK(!ZoneContainsPoint(shape,malformed,p));
    p[1]=std::numeric_limits<float>::quiet_NaN();CHECK(!ZoneContainsPoint(shape,identity,p));
    CHECK(std::isnan(ZonePointSignedDistance(shape,identity,nullptr)));
    // Signed distances support one geometric hysteresis calculation for every primitive.
    p[0]=3;p[1]=0;p[2]=0;
    ZoneShape sphere{sizeof(ZoneShape),1,{},{},{},2,{}};
    CHECK(Near(ZonePointSignedDistance(sphere,identity,p),1));
    ZoneShape box{sizeof(ZoneShape),3,{},{},{2,2,2},0,{}};
    CHECK(Near(ZonePointSignedDistance(box,identity,p),1));
    p[0]=3;p[1]=3;CHECK(Near(ZonePointSignedDistance(box,identity,p),std::sqrt(2.0)));
    p[0]=0;p[1]=0;CHECK(Near(ZonePointSignedDistance(box,identity,p),-2));
    std::cout<<"PASS "<<checks<<" signed geometry/cylinder checks\n";return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; } }
