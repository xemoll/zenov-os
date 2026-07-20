VerifiedSet verify_set(const std::filesystem::path&dir,std::uint64_t now){
    Envelope root1(read_all(dir/"root-bootstrap.zrm"));Envelope root2(read_all(dir/"root.zrm"));auto trust=bootstrap_root(root1);trust=update_root(root2,trust);
    Envelope ts(read_all(dir/"timestamp.zrm"));verify_role(ts,trust,Timestamp,now);const Meta*sref=find_meta(ts,"snapshot.zrm");if(!sref)throw Error("timestamp omits snapshot");
    Envelope snap(read_all(dir/"snapshot.zrm"));verify_reference(*sref,snap);verify_role(snap,trust,Snapshot,now);
    const Meta*tref=find_meta(snap,"targets.zrm");const Meta*dref=find_meta(snap,"native-apps.zrm");if(!tref||!dref)throw Error("snapshot omits targets metadata");
    Envelope targets(read_all(dir/"targets.zrm"));verify_reference(*tref,targets);verify_role(targets,trust,Targets,now);
    Envelope delegated(read_all(dir/"native-apps.zrm"));verify_reference(*dref,delegated);verify_role(delegated,trust,Delegated,now);
    if(targets.header->body_size!=sizeof(DelegHead)+sizeof(Delegation))throw Error("unexpected targets delegation body");
    const auto*dh=reinterpret_cast<const DelegHead*>(targets.body);const auto*del=reinterpret_cast<const Delegation*>(targets.body+sizeof(DelegHead));
    if(dh->target_count||dh->delegation_count!=1||text(del->role_name,sizeof(del->role_name))!="native-apps"||text(del->prefix,sizeof(del->prefix))!="native/"||!del->terminating||del->threshold!=1)throw Error("invalid native delegation");
    const auto*role=find_role(trust,Delegated);if(!role||role->key_count!=1||std::memcmp(role->key_ids,del->key_id,8)!=0)throw Error("delegation key mismatch");
    if (delegated.header->body_size < sizeof(TargetsHead)) throw Error("delegated targets truncated");
    const auto* th = reinterpret_cast<const TargetsHead*>(delegated.body);
    if (th->reserved || th->target_count == 0 || th->target_count > 16 ||
        sizeof(TargetsHead) + th->target_count * sizeof(Target) != delegated.header->body_size) {
        throw Error("invalid delegated target table");
    }
    const auto* tr = reinterpret_cast<const Target*>(delegated.body + sizeof(TargetsHead));
    std::vector<Target> list(tr, tr + th->target_count);
    for(std::size_t i=0;i<list.size();++i){
        const auto path=text(list[i].path,sizeof(list[i].path));
        const auto read_scope=text(list[i].read_scope,sizeof(list[i].read_scope));
        const auto write_scope=text(list[i].write_scope,sizeof(list[i].write_scope));
        if(path.rfind("native/",0)!=0||list[i].package_length==0||list[i].payload_type==0)throw Error("invalid delegated target");
        const bool needs_read=(list[i].syscall_mask&(1U<<3U|1U<<4U))!=0U;
        const bool needs_write=(list[i].syscall_mask&(1U<<5U))!=0U;
        if(needs_read!=(!read_scope.empty())||needs_write!=(!write_scope.empty()))throw Error("invalid target syscall scopes");
        for(std::size_t j=0;j<i;++j)if(text(list[j].path,sizeof(list[j].path))==path)throw Error("duplicate delegated target");
    }
    return VerifiedSet{std::move(trust),std::move(ts),std::move(snap),std::move(targets),std::move(delegated),std::move(list)};
}
State read_state(const std::filesystem::path&p){State s{};if(!std::filesystem::exists(p))return s;const auto b=read_all(p);if(b.size()!=sizeof(State))throw Error("repository state size invalid");std::memcpy(&s,b.data(),sizeof(s));if(std::memcmp(s.magic,"ZRST1",5)||s.checksum!=fnv(reinterpret_cast<const std::uint8_t*>(&s),sizeof(s)-sizeof(s.checksum)))throw Error("repository state corrupt");return s;}
void enforce_state(const State&s,const VerifiedSet&v){if(!s.root)return;if(v.root.version<s.root||v.targets.header->version<s.targets||v.delegated.header->version<s.delegated||v.snapshot.header->version<s.snapshot||v.timestamp.header->version<s.timestamp)throw Error("repository rollback detected");}
void write_state(const std::filesystem::path&p,const VerifiedSet&v){State s{};std::memcpy(s.magic,"ZRST1",5);s.root=v.root.version;s.targets=v.targets.header->version;s.delegated=v.delegated.header->version;s.snapshot=v.snapshot.header->version;s.timestamp=v.timestamp.header->version;s.checksum=fnv(reinterpret_cast<const std::uint8_t*>(&s),sizeof(s)-sizeof(s.checksum));std::ofstream out(p,std::ios::binary|std::ios::trunc);if(!out)throw Error("cannot write repository state");out.write(reinterpret_cast<const char*>(&s),sizeof(s));if(!out)throw Error("cannot commit repository state");}

void usage(){std::cerr<<"usage:\n  zenrepo verify --metadata DIR --time EPOCH [--state FILE]\n  zenrepo inspect FILE\n";}
std::string role_name(std::uint16_t r){switch(r){case Root:return"root";case Targets:return"targets";case Snapshot:return"snapshot";case Timestamp:return"timestamp";case Delegated:return"delegated";default:return"unknown";}}
}
