#include "zone_geometry.h"
#include <cmath>
#include <algorithm>
#include <limits>
namespace body_slots::detail {
namespace {
bool Finite3(const float* v) { return std::isfinite(v[0])&&std::isfinite(v[1])&&std::isfinite(v[2]); }
bool Zero3(const float* v) { return v[0]==0&&v[1]==0&&v[2]==0; }
bool Bounded3(const float* v) { return Finite3(v)&&std::abs(v[0])<=1000000&&std::abs(v[1])<=1000000&&std::abs(v[2])<=1000000; }
bool ProperRotation(const float* r) {
    for(unsigned i=0;i<9;++i)if(!std::isfinite(r[i]))return false;
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j){
        double dot=0;for(unsigned k=0;k<3;++k)dot+=double(r[k*3+i])*r[k*3+j];
        if(std::abs(dot-(i==j?1.0:0.0))>0.001)return false;
    }
    const double det=double(r[0])*(double(r[4])*r[8]-double(r[5])*r[7])-
        double(r[1])*(double(r[3])*r[8]-double(r[5])*r[6])+
        double(r[2])*(double(r[3])*r[7]-double(r[4])*r[6]);
    return std::abs(det-1.0)<=0.001;
}
}
Status ValidateZoneShape(const ZoneShape& s) noexcept {
    if(s.structSize!=sizeof(s)||s.reserved[0]||s.reserved[1]||!Bounded3(s.center)||!Bounded3(s.end)||!Bounded3(s.halfExtents)||!std::isfinite(s.radius))return Status::InvalidArgument;
    switch(static_cast<ZoneShapeKind>(s.kind)) {
        case ZoneShapeKind::Sphere: return Zero3(s.end)&&Zero3(s.halfExtents)&&s.radius>0&&s.radius<=1000000?Status::Ok:Status::InvalidArgument;
        case ZoneShapeKind::Capsule: return Zero3(s.halfExtents)&&s.radius>0&&s.radius<=1000000?Status::Ok:Status::InvalidArgument;
        case ZoneShapeKind::Cylinder: return Zero3(s.halfExtents)&&s.radius>0&&s.radius<=1000000&&
            (s.center[0]!=s.end[0]||s.center[1]!=s.end[1]||s.center[2]!=s.end[2])?Status::Ok:Status::InvalidArgument;
        case ZoneShapeKind::Box: return Zero3(s.end)&&s.radius==0&&s.halfExtents[0]>0&&s.halfExtents[1]>0&&s.halfExtents[2]>0?Status::Ok:Status::InvalidArgument;
        default:return Status::UnsupportedVersion;
    }
}
ZoneShape DefaultZoneShape(const Definition& d) noexcept {
    ZoneShape s{};s.structSize=sizeof(s);s.kind=static_cast<std::uint32_t>(ZoneShapeKind::Sphere);
    for(unsigned i=0;i<3;++i)s.center[i]=d.regionCenter[i];s.radius=d.radius;return s;
}
double ZonePointSignedDistance(const ZoneShape& s,const RuntimeTransform& world,const float point[3]) noexcept {
    const auto invalid=std::numeric_limits<double>::quiet_NaN();
    if(!point||ValidateZoneShape(s)!=Status::Ok||!Finite3(point)||!Finite3(world.position)||!std::isfinite(world.scale)||world.scale<=0||!ProperRotation(world.rotation))return invalid;
    double p[3]{};
    // Transform the point back to slot-local coordinates. Use doubles so large
    // finite world values cannot overflow the distance calculation.
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j) {
        p[i]+=(double(point[j])-world.position[j])*world.rotation[j*3+i]/world.scale;
    }
    if(s.kind==static_cast<std::uint32_t>(ZoneShapeKind::Box)) {
        double outside=0,inside=-std::numeric_limits<double>::infinity();
        for(unsigned i=0;i<3;++i){const double d=std::abs(p[i]-s.center[i])-s.halfExtents[i];outside+=std::max(d,0.0)*std::max(d,0.0);inside=std::max(inside,d);}
        return std::sqrt(outside)+std::min(inside,0.0);
    }
    double t=0,len=0;
    if(s.kind==static_cast<std::uint32_t>(ZoneShapeKind::Capsule)||s.kind==static_cast<std::uint32_t>(ZoneShapeKind::Cylinder)) {
        for(unsigned i=0;i<3;++i){const double a=double(s.end[i])-s.center[i];t+=(p[i]-s.center[i])*a;len+=a*a;}
        t=len>0?t/len:0;
        if(s.kind==static_cast<std::uint32_t>(ZoneShapeKind::Cylinder)){
            double radial2=0;
            for(unsigned i=0;i<3;++i){const double delta=p[i]-s.center[i]-t*(double(s.end[i])-s.center[i]);radial2+=delta*delta;}
            const double radial=std::sqrt(radial2)-s.radius;
            const double axial=(std::abs(t-0.5)-0.5)*std::sqrt(len);
            return std::hypot(std::max(radial,0.0),std::max(axial,0.0))+std::min(std::max(radial,axial),0.0);
        }
        if(t<0)t=0;if(t>1)t=1;
    }
    double distance=0;
    for(unsigned i=0;i<3;++i){const double nearest=s.center[i]+t*(double(s.end[i])-s.center[i]);const double v=p[i]-nearest;distance+=v*v;}
    return std::sqrt(distance)-s.radius;
}
bool ZoneContainsPoint(const ZoneShape& s,const RuntimeTransform& world,const float point[3]) noexcept {
    return ZonePointSignedDistance(s,world,point)<=0;
}
} // namespace body_slots::detail
