#pragma once

#include "descriptor.hpp"

namespace zenuniverse {

const Descriptor* latest(const std::vector<Descriptor>& records,const std::string& id){const Descriptor* best=nullptr;for(const auto&d:records)if(d.id==id&&(!best||version_less(*best,d)))best=&d;return best;}

struct Plan { std::vector<const Descriptor*> order; std::vector<std::string> blocked; bool user_asset=false; };

void resolve_capability(const std::string& cap,const std::vector<Descriptor>& records,const std::set<std::string>& host,std::set<std::string>& active,std::set<std::string>& emitted,Plan& plan){
    if (host.count(cap) || emitted.count(cap)) return;
    if (!active.insert(cap).second) {
        plan.blocked.push_back("dependency cycle at " + cap);
        return;
    }
    const Descriptor* provider=nullptr; for(const auto&d:records) if(std::find(d.provides.begin(),d.provides.end(),cap)!=d.provides.end() && (!provider||version_less(*provider,d))) provider=&d;
    if(!provider){plan.blocked.push_back("missing capability provider: "+cap);active.erase(cap);return;}
    if(provider->availability!="available") plan.blocked.push_back("provider not available yet: "+provider->id+" ("+provider->availability+")");
    for(const auto&r:provider->requirements)resolve_capability(r,records,host,active,emitted,plan);
    if(!emitted.count(cap)){plan.order.push_back(provider);for(const auto&p:provider->provides)emitted.insert(p);} active.erase(cap);
}

Plan make_plan(const Descriptor& app,const std::vector<Descriptor>& records,const std::string& host_arch,const std::set<std::string>& host){
    Plan plan; if(app.architecture!="any"&&app.architecture!=host_arch)plan.blocked.push_back("architecture mismatch: package="+app.architecture+" host="+host_arch);
    std::set<std::string> active,emitted=host; const auto runtime=runtime_capability(app); if(!runtime.empty())resolve_capability(runtime,records,host,active,emitted,plan); for(const auto&r:app.requirements)resolve_capability(r,records,host,active,emitted,plan); plan.order.push_back(&app); plan.user_asset=app.delivery=="user-supplied"; return plan;
}

std::string compile_catalog(const std::vector<Descriptor>& records){
    std::vector<const Descriptor*> sorted;for(const auto&d:records)sorted.push_back(&d);std::sort(sorted.begin(),sorted.end(),[](auto*a,auto*b){return std::tie(a->id,a->version)<std::tie(b->id,b->version);});
    std::ostringstream body; for(auto*d:sorted){const auto c=canonical(*d);body<<"record-sha256="<<sha256(c)<<'\n'<<c<<".\n";} const auto payload=body.str();
    std::ostringstream out;out<<"ZENUNIVERSE1\ncount="<<sorted.size()<<"\npayload-sha256="<<sha256(payload)<<"\n\n"<<payload;return out.str();
}

} // namespace zenuniverse
