#include "zone_geometry.h"
#include <cmath>
namespace body_slots::detail {
namespace {
bool Finite3(const float* v) { return std::isfinite(v[0])&&std::isfinite(v[1])&&std::isfinite(v[2]); }
bool Zero3(const float* v) { return v[0]==0&&v[1]==0&&v[2]==0; }
bool Bounded3(const float* v) { return Finite3(v)&&std::abs(v[0])<=1000000&&std::abs(v[1])<=1000000&&std::abs(v[2])<=1000000; }
}
Status ValidateZoneShape(const ZoneShape& s) noexcept {
    if(s.structSize!=sizeof(s)||s.reserved[0]||s.reserved[1]||!Bounded3(s.center)||!Bounded3(s.end)||!Bounded3(s.halfExtents)||!std::isfinite(s.radius))return Status::InvalidArgument;
    switch(static_cast<ZoneShapeKind>(s.kind)) {
        case ZoneShapeKind::Sphere: return Zero3(s.end)&&Zero3(s.halfExtents)&&s.radius>0&&s.radius<=1000000?Status::Ok:Status::InvalidArgument;
        case ZoneShapeKind::Capsule: return Zero3(s.halfExtents)&&s.radius>0&&s.radius<=1000000?Status::Ok:Status::InvalidArgument;
        case ZoneShapeKind::Box: return Zero3(s.end)&&s.radius==0&&s.halfExtents[0]>0&&s.halfExtents[1]>0&&s.halfExtents[2]>0?Status::Ok:Status::InvalidArgument;
        default:return Status::UnsupportedVersion;
    }
}
ZoneShape DefaultZoneShape(const Definition& d) noexcept {
    ZoneShape s{};s.structSize=sizeof(s);s.kind=static_cast<std::uint32_t>(ZoneShapeKind::Sphere);
    for(unsigned i=0;i<3;++i)s.center[i]=d.regionCenter[i];s.radius=d.radius;return s;
}
bool ZoneContainsPoint(const ZoneShape& s,const RuntimeTransform& world,const float point[3]) noexcept {
    if(!point||ValidateZoneShape(s)!=Status::Ok||!Finite3(point)||!Finite3(world.position)||!std::isfinite(world.scale)||world.scale<=0)return false;
    double p[3]{};
    // Transform the point back to slot-local coordinates. Use doubles so large
    // finite world values cannot overflow the distance calculation.
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j) {
        if(!std::isfinite(world.rotation[j*3+i]))return false;
        p[i]+=(double(point[j])-world.position[j])*world.rotation[j*3+i]/world.scale;
    }
    if(s.kind==static_cast<std::uint32_t>(ZoneShapeKind::Box)) {
        for(unsigned i=0;i<3;++i)if(std::abs(p[i]-s.center[i])>s.halfExtents[i])return false;return true;
    }
    double t=0,len=0;
    if(s.kind==static_cast<std::uint32_t>(ZoneShapeKind::Capsule)) {
        for(unsigned i=0;i<3;++i){const double a=double(s.end[i])-s.center[i];t+=(p[i]-s.center[i])*a;len+=a*a;}
        t=len>0?t/len:0;if(t<0)t=0;if(t>1)t=1;
    }
    double distance=0;
    for(unsigned i=0;i<3;++i){const double nearest=s.center[i]+t*(double(s.end[i])-s.center[i]);const double v=p[i]-nearest;distance+=v*v;}
    return distance<=double(s.radius)*s.radius;
}
} // namespace body_slots::detail
