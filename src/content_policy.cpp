#include "content_policy.h"
namespace body_slots::detail {
Status ValidateContentPolicy(const SlotContentPolicy& p) noexcept {
    if(p.structSize!=sizeof(p)||p.reserved||p.reserved2||p.unresolved>1||p.baseFormCount>kMaxPolicyForms||p.allKeywordCount>kMaxPolicyForms||p.anyKeywordCount>kMaxPolicyForms||p.formListCount>kMaxPolicyForms)return Status::InvalidArgument;
    const std::uint32_t* arrays[]{p.baseForms,p.allKeywords,p.anyKeywords,p.formLists};const std::uint32_t counts[]{p.baseFormCount,p.allKeywordCount,p.anyKeywordCount,p.formListCount};
    for(unsigned a=0;a<4;++a)for(unsigned i=0;i<kMaxPolicyForms;++i){if((i<counts[a])!=bool(arrays[a][i]))return Status::InvalidArgument;for(unsigned j=0;j<i&&i<counts[a];++j)if(arrays[a][i]==arrays[a][j])return Status::InvalidArgument;}return Status::Ok;
}
bool EvaluateContentPolicy(const SlotContentPolicy& p,const ContentPolicyResolver& r,bool& known) noexcept {
    known=false;if(ValidateContentPolicy(p)!=Status::Ok||p.unresolved||!r.IsBaseForm||!r.HasKeyword||!r.IsInFormList||!r.DependencyAvailable)return false;
    try{
        for(std::uint32_t i=0;i<p.baseFormCount;++i)if(!r.DependencyAvailable(r.context,p.baseForms[i],1))return false;
        for(std::uint32_t i=0;i<p.allKeywordCount;++i)if(!r.DependencyAvailable(r.context,p.allKeywords[i],2))return false;
        for(std::uint32_t i=0;i<p.anyKeywordCount;++i)if(!r.DependencyAvailable(r.context,p.anyKeywords[i],2))return false;
        for(std::uint32_t i=0;i<p.formListCount;++i)if(!r.DependencyAvailable(r.context,p.formLists[i],3))return false;
        known=true;bool formAllowed=!p.baseFormCount&&!p.formListCount;
        for(std::uint32_t i=0;i<p.baseFormCount;++i)if(r.IsBaseForm(r.context,p.baseForms[i]))formAllowed=true;
        for(std::uint32_t i=0;i<p.formListCount;++i)if(r.IsInFormList(r.context,p.formLists[i]))formAllowed=true;
        if(!formAllowed)return false;
        for(std::uint32_t i=0;i<p.allKeywordCount;++i)if(!r.HasKeyword(r.context,p.allKeywords[i]))return false;
        bool anyAllowed=!p.anyKeywordCount;for(std::uint32_t i=0;i<p.anyKeywordCount;++i)if(r.HasKeyword(r.context,p.anyKeywords[i]))anyAllowed=true;
        return anyAllowed;
    }catch(...){known=false;return false;}
}
} // namespace body_slots::detail
