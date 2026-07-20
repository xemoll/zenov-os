// SPDX-License-Identifier: BSD-2-Clause
#include "resolver.hpp"

#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace zenuniverse {

struct Args{std::vector<std::string> pos;std::map<std::string,std::vector<std::string>>opt;};
Args parse_args(int argc,char**argv,int start){Args a;for(int i=start;i<argc;++i){std::string t=argv[i];if(t.rfind("--",0)==0){auto k=t.substr(2);if(i+1>=argc)throw Error("option requires value: "+t);a.opt[k].push_back(argv[++i]);}else a.pos.push_back(t);}return a;}
std::string one(const Args&a,const std::string&k){auto it=a.opt.find(k);if(it==a.opt.end()||it->second.size()!=1U)throw Error("exactly one --"+k+" required");return it->second[0];}
std::vector<std::string> many(const Args&a,const std::string&k){auto it=a.opt.find(k);return it==a.opt.end()?std::vector<std::string>{}:it->second;}
void unknown(const Args&a,const std::set<std::string>&ok){for(const auto&[k,v]:a.opt){(void)v;if(!ok.count(k))throw Error("unknown option --"+k);}}

void usage(){std::cout<<"zenuniverse - deterministic universal package catalog and runtime resolver\n\n"
"  zenuniverse validate FILE.zsource [...]\n"
"  zenuniverse compile --input DIR --output CATALOG.zuc\n"
"  zenuniverse resolve --input DIR --package ID --host-arch ARCH [--capability CAP ...]\n"
"  zenuniverse fetch-plan --input DIR --package ID\n"
"  zenuniverse self-test\n";}

int command(const std::string& cmd,const Args&a){
    if (cmd == "self-test") {
        unknown(a, {});
        if (!a.pos.empty()) throw Error("self-test takes no arguments");
        if (sha256("abc") != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") {
            throw Error("SHA-256 known-answer test failed");
        }
        std::cout << "ZENUNIVERSE_SELF_TEST_OK\n";
        return 0;
    }
    if(cmd=="validate"){unknown(a,{});if(a.pos.empty())throw Error("validate requires descriptors");for(const auto&p:a.pos){auto d=parse_descriptor(p);validate_descriptor(d);std::cout<<"VALID "<<d.id<<'@'<<d.version<<'\n';}std::cout<<"ZENUNIVERSE_VALIDATE_OK count="<<a.pos.size()<<'\n';return 0;}
    if(cmd=="compile"){unknown(a,{"input","output"});if(!a.pos.empty())throw Error("compile takes no positional arguments");auto records=load_directory(one(a,"input"));auto catalog=compile_catalog(records);write_atomic(one(a,"output"),catalog);std::cout<<"ZENUNIVERSE_COMPILE_OK packages="<<records.size()<<" sha256="<<sha256(catalog)<<'\n';return 0;}
    if(cmd=="resolve"){unknown(a,{"input","package","host-arch","capability"});if(!a.pos.empty())throw Error("resolve takes no positional arguments");auto records=load_directory(one(a,"input"));auto id=one(a,"package"),arch=one(a,"host-arch");if(!architectures.count(arch))throw Error("unsupported host architecture");std::set<std::string> caps;for(auto&v:many(a,"capability")){if(!safe_id(v))throw Error("unsafe capability");caps.insert(v);}auto app=latest(records,id);if(!app)throw Error("package not found: "+id);auto plan=make_plan(*app,records,arch,caps);for(auto*d:plan.order)std::cout<<"install "<<d->id<<'@'<<d->version<<" availability="<<d->availability<<" delivery="<<d->delivery<<" runtime="<<d->runtime<<'\n';for(auto&r:plan.blocked)std::cout<<"blocked: "<<r<<'\n';if(plan.user_asset)std::cout<<"asset: user-supplied; ZenovOS must not download or redistribute proprietary content\n";if(plan.blocked.empty()){std::cout<<"ZENUNIVERSE_RESOLVE_OK package="<<id<<'\n';return 0;}std::cout<<"ZENUNIVERSE_RESOLVE_BLOCKED package="<<id<<" reasons="<<plan.blocked.size()<<'\n';return 3;}
    if(cmd=="fetch-plan"){unknown(a,{"input","package"});if(!a.pos.empty())throw Error("fetch-plan takes no positional arguments");auto records=load_directory(one(a,"input"));auto id=one(a,"package");auto d=latest(records,id);if(!d)throw Error("package not found: "+id);std::cout<<"package="<<d->id<<'@'<<d->version<<'\n'<<"delivery="<<d->delivery<<'\n'<<"bytes="<<d->bytes<<'\n'<<"sha256="<<d->sha<<'\n';for(const auto&m:d->mirrors)std::cout<<"mirror="<<m<<'\n';std::cout<<"runtime="<<d->runtime<<'\n'<<"artifact="<<d->artifact<<'\n';if(d->delivery=="https")std::cout<<"ZENUNIVERSE_FETCH_READY verified-https=yes atomic-temp=yes resume-policy=range-if-server-supports\n";else if(d->delivery=="user-supplied")std::cout<<"ZENUNIVERSE_FETCH_USER_SUPPLIED\n";else std::cout<<"ZENUNIVERSE_FETCH_NO_NETWORK\n";return 0;}
    throw Error("unknown command: "+cmd);
}

} // namespace zenuniverse

int main(int argc, char** argv) {
    try {
        if (argc < 2 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "help") {
            zenuniverse::usage();
            return argc < 2 ? 2 : 0;
        }
        return zenuniverse::command(argv[1], zenuniverse::parse_args(argc, argv, 2));
    } catch (const zenuniverse::Error& error) {
        std::cerr << "zenuniverse: error: " << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "zenuniverse: fatal: " << error.what() << '\n';
        return 2;
    }
}
