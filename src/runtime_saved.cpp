#include "runtime_saved.h"
#include "instance_configuration.h"
#include "validation.h"
#include "zone_geometry.h"
#include "content_policy.h"
#include <algorithm>
#include <cstring>
#include <memory>
namespace body_slots::detail {
namespace {
bool Empty(const void* p,std::size_t size) {const auto* b=static_cast<const unsigned char*>(p);for(std::size_t i=0;i<size;++i)if(b[i])return false;return true;}
bool Path(const char* p,std::size_t size) {if(!p[0])return Empty(p,size);return ValidString(p,size,false)&&p[0]!='/'&&p[0]!='\\'&&!std::strchr(p,':')&&!std::strstr(p,"..");}
Status Validate(const RuntimeCustomizationData& data) {
    if(data.count>kMaxInstances)return Status::LimitExceeded;
    for(std::uint32_t i=0;i<data.count;++i) {
        const auto& row=data.entries[i];if(ValidateInstanceCustomizationKey(row.key)!=Status::Ok||!row.fieldMask||(row.fieldMask&~15u)||row.reserved)return Status::InvalidArgument;
        if(row.fieldMask&kSavedRuntimeBinding){auto s=ValidateRuntimeBinding(row.binding);if(s!=Status::Ok)return s;}else if(!Empty(&row.binding,sizeof(row.binding)))return Status::InvalidArgument;
        if(row.fieldMask&kSavedRuntimeVisual){auto s=ValidateRuntimeVisual(row.visual);if(s!=Status::Ok)return s;}else if(!Empty(&row.visual,sizeof(row.visual)))return Status::InvalidArgument;
        if(row.fieldMask&kSavedRuntimeShape){auto s=ValidateZoneShape(data.shapes[i]);if(s!=Status::Ok)return s;}else if(!Empty(&data.shapes[i],sizeof(ZoneShape)))return Status::InvalidArgument;
        if(row.fieldMask&kSavedRuntimePolicy){auto s=ValidateContentPolicy(data.policies[i]);if(s!=Status::Ok)return s;}else if(!Empty(&data.policies[i],sizeof(SlotContentPolicy)))return Status::InvalidArgument;
        for(std::uint32_t j=0;j<i;++j)if(SameInstanceCustomizationKey(row.key,data.entries[j].key))return Status::Conflict;
    }return Status::Ok;
}
void U32(std::vector<std::uint8_t>& out,std::uint32_t v){for(unsigned i=0;i<4;++i)out.push_back(static_cast<std::uint8_t>(v>>(8*i)));}
void Float(std::vector<std::uint8_t>& out,float v){std::uint32_t bits;std::memcpy(&bits,&v,4);U32(out,bits);}
void Bytes(std::vector<std::uint8_t>& out,const void* value,std::size_t size){const auto* b=static_cast<const std::uint8_t*>(value);out.insert(out.end(),b,b+size);}
struct Reader {const std::uint8_t* cursor;std::uint32_t U32(){std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::uint32_t(*cursor++)<<(8*i);return v;}float Float(){const auto bits=U32();float v;std::memcpy(&v,&bits,4);return v;}void Bytes(void* out,std::size_t size){std::memcpy(out,cursor,size);cursor+=size;}};
}
Status ValidateRuntimeBinding(const RuntimeBindingSpec& value) noexcept {
    if(value.structSize!=sizeof(value)||value.reserved)return Status::InvalidArgument;
    if(value.kind!=1&&value.kind!=2&&value.kind!=3)return Status::UnsupportedVersion;
    if(value.kind==3&&value.requiredEquipmentFormId)return Status::InvalidArgument;
    if(value.kind==2&&(!value.requiredEquipmentFormId||!value.node[0]))return Status::InvalidArgument;
    return value.node[0]?(ValidString(value.node,sizeof(value.node),false)?Status::Ok:Status::InvalidArgument):(Empty(value.node,sizeof(value.node))?Status::Ok:Status::InvalidArgument);
}
Status ValidateRuntimeVisual(const SlotVisualSpec& value) noexcept {
    return value.structSize==sizeof(value)&&!(value.flags&~1u)&&Path(value.holderModel,sizeof(value.holderModel))&&Path(value.previewModel,sizeof(value.previewModel))?Status::Ok:Status::InvalidArgument;
}
Status EncodeRuntimeCustomizations(const RuntimeCustomizationData& data,std::vector<std::uint8_t>& output) noexcept {
    try {
        const auto status=Validate(data);if(status!=Status::Ok)return status;
        std::array<std::uint32_t,kMaxInstances> order{};for(std::uint32_t i=0;i<data.count;++i)order[i]=i;std::sort(order.begin(),order.begin()+data.count,[&](auto a,auto b){return std::memcmp(&data.entries[a].key,&data.entries[b].key,sizeof(InstanceCustomizationKey))<0;});
        std::vector<std::uint8_t> out;out.reserve(16+data.count*kRuntimeCustomizationSaveRecordBytes);
        U32(out,0x54525342);U32(out,kRuntimeCustomizationSaveVersion);U32(out,kRuntimeCustomizationSaveRecordBytes);U32(out,data.count); // BSRT
        for(std::uint32_t i=0;i<data.count;++i){const auto index=order[i];const auto& r=data.entries[index];U32(out,r.fieldMask);Bytes(out,&r.key,sizeof(r.key));U32(out,r.binding.kind);U32(out,r.binding.requiredEquipmentFormId);Bytes(out,r.binding.node,96);U32(out,r.visual.flags);Bytes(out,r.visual.holderModel,192);Bytes(out,r.visual.previewModel,192);const auto& s=data.shapes[index];U32(out,s.kind);for(float v:s.center)Float(out,v);for(float v:s.end)Float(out,v);for(float v:s.halfExtents)Float(out,v);Float(out,s.radius);U32(out,0);const auto& p=data.policies[index];U32(out,p.baseFormCount);U32(out,p.allKeywordCount);U32(out,p.anyKeywordCount);U32(out,p.formListCount);U32(out,0);for(auto v:p.baseForms)U32(out,v);for(auto v:p.allKeywords)U32(out,v);for(auto v:p.anyKeywords)U32(out,v);for(auto v:p.formLists)U32(out,v);U32(out,p.unresolved);U32(out,0);}
        output.swap(out);return Status::Ok;
    }catch(...){return Status::InternalError;}
}
Status DecodeRuntimeCustomizations(const std::uint8_t* bytes,std::uint32_t size,RuntimeCustomizationData& output) noexcept {
    try {
        if(!bytes||size<16||size>kMaxRuntimeCustomizationSaveBytes)return Status::InvalidArgument;Reader read{bytes};if(read.U32()!=0x54525342)return Status::InvalidArgument;const auto version=read.U32();if(version<1||version>kRuntimeCustomizationSaveVersion)return Status::UnsupportedVersion;const auto recordBytes=read.U32();if(recordBytes!=(version==1?816:version==2?864:kRuntimeCustomizationSaveRecordBytes))return Status::InvalidArgument;
        const auto count=read.U32();if(count>kMaxInstances||size!=16+count*recordBytes)return Status::InvalidArgument;
        auto candidate=std::make_unique<RuntimeCustomizationData>();candidate->count=count;
        for(std::uint32_t i=0;i<count;++i){auto& r=candidate->entries[i];r.fieldMask=read.U32();read.Bytes(&r.key,sizeof(r.key));r.binding.kind=read.U32();r.binding.requiredEquipmentFormId=read.U32();read.Bytes(r.binding.node,96);r.visual.flags=read.U32();read.Bytes(r.visual.holderModel,192);read.Bytes(r.visual.previewModel,192);if(r.fieldMask&kSavedRuntimeBinding)r.binding.structSize=sizeof(RuntimeBindingSpec);if(r.fieldMask&kSavedRuntimeVisual)r.visual.structSize=sizeof(SlotVisualSpec);if((version==1&&(r.fieldMask&~3u))||(version==2&&(r.fieldMask&~7u)))return Status::InvalidArgument;if(version>=2){auto& s=candidate->shapes[i];s.kind=read.U32();for(float& v:s.center)v=read.Float();for(float& v:s.end)v=read.Float();for(float& v:s.halfExtents)v=read.Float();s.radius=read.Float();if(read.U32())return Status::InvalidArgument;if(r.fieldMask&kSavedRuntimeShape)s.structSize=sizeof(s);}if(version>=3){auto& p=candidate->policies[i];p.baseFormCount=read.U32();p.allKeywordCount=read.U32();p.anyKeywordCount=read.U32();p.formListCount=read.U32();p.reserved=read.U32();for(auto& v:p.baseForms)v=read.U32();for(auto& v:p.allKeywords)v=read.U32();for(auto& v:p.anyKeywords)v=read.U32();for(auto& v:p.formLists)v=read.U32();p.unresolved=read.U32();p.reserved2=read.U32();if(r.fieldMask&kSavedRuntimePolicy)p.structSize=sizeof(p);}}
        const auto status=Validate(*candidate);if(status!=Status::Ok)return status;output=*candidate;return Status::Ok;
    }catch(...){return Status::InternalError;}
}
Status RemapRuntimeCustomizationForms(std::vector<std::uint8_t>& bytes,RuntimeFormResolver resolver,void* context) noexcept {
    try {
        if(!resolver||bytes.size()>kMaxRuntimeCustomizationSaveBytes)return Status::InvalidArgument;
        auto data=std::make_unique<RuntimeCustomizationData>();auto status=DecodeRuntimeCustomizations(bytes.data(),static_cast<std::uint32_t>(bytes.size()),*data);if(status!=Status::Ok)return status;
        for(std::uint32_t i=0;i<data->count;++i){auto& r=data->entries[i];if(!(r.fieldMask&kSavedRuntimeBinding)||!r.binding.requiredEquipmentFormId)continue;std::uint32_t resolved=0;if(resolver(r.binding.requiredEquipmentFormId,&resolved,context)&&resolved)r.binding.requiredEquipmentFormId=resolved;else{r.binding.kind=static_cast<std::uint32_t>(RuntimeBindingKind::UnresolvedEquipment);r.binding.requiredEquipmentFormId=0;}}
        for(std::uint32_t i=0;i<data->count;++i){if(!(data->entries[i].fieldMask&kSavedRuntimePolicy))continue;auto& p=data->policies[i];if(p.unresolved)continue;std::uint32_t* arrays[]{p.baseForms,p.allKeywords,p.anyKeywords,p.formLists};const std::uint32_t counts[]{p.baseFormCount,p.allKeywordCount,p.anyKeywordCount,p.formListCount};for(unsigned a=0;a<4;++a)for(std::uint32_t j=0;j<counts[a];++j){std::uint32_t resolved=0;if(resolver(arrays[a][j],&resolved,context)&&resolved)arrays[a][j]=resolved;else p.unresolved=1;}}
        return EncodeRuntimeCustomizations(*data,bytes);
    }catch(...){return Status::InternalError;}
}
} // namespace body_slots::detail
