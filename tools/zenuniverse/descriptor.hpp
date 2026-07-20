#pragma once

#include "common.hpp"

namespace zenuniverse {

struct Descriptor {
    std::string schema,id,version,kind,platform,architecture,artifact,delivery,runtime,availability,entrypoint,channel,category,license,description,homepage,sha;
    std::uint64_t bytes = 0;
    std::vector<std::string> mirrors,requirements,provides;
    fs::path source;
};

const std::set<std::string> kinds={"application","game","runtime","sdk","toolchain","profile","firmware"};
const std::set<std::string> platforms={"zenov","linux","windows","macos","playstation2","playstation3","xbox","xbox360"};
const std::set<std::string> architectures={"any","x86","x86_64","arm64","ppc64"};
const std::set<std::string> artifacts={"zpk","zex1","elf32","elf64","appimage","flatpak","deb","rpm","tar","exe","msi","msix","appx","dmg","pkg","app","iso","disc-image","rom","runtime-bundle","metadata"};
const std::set<std::string> deliveries={"embedded","https","user-supplied","metadata-only"};
const std::set<std::string> runtimes={"native","linux-abi","wine","proton","qemu-user","qemu-system","darling","pcsx2","rpcs3","xemu","xenia","external"};
const std::set<std::string> availability={"available","planned","external"};

std::uint64_t parse_u64(const std::string& value, const std::string& field) {
    if (value.empty()) throw Error(field + " is empty");
    std::uint64_t n=0;
    for (unsigned char c:value) { if (!std::isdigit(c)) throw Error(field+" must be decimal"); if (n>(std::numeric_limits<std::uint64_t>::max()-(c-'0'))/10U) throw Error(field+" overflows"); n=n*10U+(c-'0'); }
    return n;
}

Descriptor parse_descriptor(const fs::path& path) {
    Descriptor d; d.source=path;
    std::map<std::string,std::string*> scalar={{"schema",&d.schema},{"id",&d.id},{"version",&d.version},{"kind",&d.kind},{"platform",&d.platform},{"architecture",&d.architecture},{"artifact",&d.artifact},{"delivery",&d.delivery},{"runtime",&d.runtime},{"availability",&d.availability},{"entrypoint",&d.entrypoint},{"channel",&d.channel},{"category",&d.category},{"license",&d.license},{"description",&d.description},{"homepage",&d.homepage},{"sha256",&d.sha}};
    std::set<std::string> seen;
    std::istringstream in(read_text(path)); std::string line; std::size_t number=0;
    while (std::getline(in,line)) {
        ++number; if (!line.empty()&&line.back()=='\r') line.pop_back(); line=trim(line); if (line.empty()||line[0]=='#') continue;
        const auto eq=line.find('='); if (eq==std::string::npos||eq==0U) throw Error(path.string()+":"+std::to_string(number)+": expected key=value");
        auto key=trim(line.substr(0,eq)), value=trim(line.substr(eq+1)); if (value.empty()) throw Error(path.string()+":"+std::to_string(number)+": empty value");
        if (key=="mirror") d.mirrors.push_back(value); else if (key=="requires") d.requirements.push_back(value); else if (key=="provides") d.provides.push_back(value); else if (key=="bytes") { if (!seen.insert(key).second) throw Error("duplicate bytes"); d.bytes=parse_u64(value,"bytes"); }
        else { auto it=scalar.find(key); if (it==scalar.end()) throw Error(path.string()+":"+std::to_string(number)+": unknown key "+key); if (!seen.insert(key).second) throw Error("duplicate "+key); *it->second=value; }
    }
    return d;
}

std::string runtime_capability(const Descriptor& d) { return d.runtime=="native" ? std::string{} : "runtime."+d.runtime; }

void validate_descriptor(const Descriptor& d) {
    if (d.schema!="zen-source-1") throw Error("schema must be zen-source-1: "+d.source.string());
    if (!safe_id(d.id)||!safe_id(d.version)) throw Error("unsafe id or version: "+d.source.string());
    if (!kinds.count(d.kind)||!platforms.count(d.platform)||!architectures.count(d.architecture)||!artifacts.count(d.artifact)||!deliveries.count(d.delivery)||!runtimes.count(d.runtime)||!availability.count(d.availability)) throw Error("unsupported enum in "+d.source.string());
    if (!safe_id(d.channel)||!safe_id(d.category)||!printable(d.license,96)||!printable(d.description,240)||!https_url(d.homepage)) throw Error("invalid metadata in "+d.source.string());
    if (d.entrypoint.empty()||d.entrypoint.size()>192U||d.entrypoint.find_first_of("\r\n")!=std::string::npos) throw Error("invalid entrypoint in "+d.source.string());
    std::set<std::string> unique;
    for (const auto& v:d.requirements) { if (!safe_id(v)||!unique.insert(v).second) throw Error("invalid or duplicate requires in "+d.source.string()); }
    unique.clear(); for (const auto& v:d.provides) { if (!safe_id(v)||!unique.insert(v).second) throw Error("invalid or duplicate provides in "+d.source.string()); }
    unique.clear(); for (const auto& v:d.mirrors) { if (!https_url(v)||!unique.insert(v).second) throw Error("invalid or duplicate HTTPS mirror in "+d.source.string()); }
    if (d.delivery=="https") { if (d.mirrors.empty()||!hex64(d.sha)||d.bytes==0U) throw Error("https delivery requires mirror, bytes and lowercase sha256: "+d.source.string()); }
    if (d.delivery=="embedded") { if (!d.mirrors.empty()||!hex64(d.sha)||d.bytes==0U) throw Error("embedded delivery requires bytes/sha256 and no mirrors: "+d.source.string()); }
    if (d.delivery=="metadata-only") { if (!d.mirrors.empty()||d.sha!="-"||d.bytes!=0U) throw Error("metadata-only requires bytes=0 sha256=- and no mirrors: "+d.source.string()); }
    if (d.delivery=="user-supplied") { if (!d.mirrors.empty()||d.sha!="-"||d.bytes!=0U) throw Error("user-supplied content cannot publish mirrors or fixed bytes: "+d.source.string()); }
    if ((d.platform=="playstation2"||d.platform=="playstation3"||d.platform=="xbox"||d.platform=="xbox360") && d.kind=="game" && d.delivery!="user-supplied") throw Error("console game content must be user-supplied: "+d.source.string());
    const std::map<std::string,std::set<std::string>> allowed={{"windows",{"exe","msi","msix","appx","runtime-bundle","metadata"}},{"linux",{"elf32","elf64","appimage","flatpak","deb","rpm","tar","runtime-bundle","metadata"}},{"macos",{"dmg","pkg","app","runtime-bundle","metadata"}},{"playstation2",{"iso","disc-image","metadata"}},{"playstation3",{"iso","disc-image","metadata"}},{"xbox",{"iso","disc-image","rom","metadata"}},{"xbox360",{"iso","disc-image","rom","metadata"}},{"zenov",{"zpk","zex1","elf32","runtime-bundle","metadata"}}};
    if (!allowed.at(d.platform).count(d.artifact)) throw Error("artifact/platform mismatch: "+d.source.string());
    const std::map<std::string,std::set<std::string>> platform_runtime={{"windows",{"wine","proton","qemu-system","external"}},{"linux",{"linux-abi","qemu-user","qemu-system","external"}},{"macos",{"darling","qemu-system","external"}},{"playstation2",{"pcsx2","external"}},{"playstation3",{"rpcs3","external"}},{"xbox",{"xemu","external"}},{"xbox360",{"xenia","external"}},{"zenov",{"native","external"}}};
    if (d.kind!="runtime" && !platform_runtime.at(d.platform).count(d.runtime)) throw Error("runtime/platform mismatch: "+d.source.string());
    if (d.kind=="runtime" && d.runtime!="native") throw Error("runtime provider itself must execute natively: "+d.source.string());
    if (d.availability=="available" && (d.delivery=="metadata-only"||d.delivery=="user-supplied") && d.kind=="runtime") throw Error("available runtime must contain downloadable or embedded bytes: "+d.source.string());
}

std::string canonical(const Descriptor& d) {
    auto mirrors=d.mirrors, req=d.requirements, prov=d.provides; std::sort(mirrors.begin(),mirrors.end()); std::sort(req.begin(),req.end()); std::sort(prov.begin(),prov.end());
    std::ostringstream o;
    o<<"schema="<<d.schema<<'\n'<<"id="<<d.id<<'\n'<<"version="<<d.version<<'\n'<<"kind="<<d.kind<<'\n'<<"platform="<<d.platform<<'\n'<<"architecture="<<d.architecture<<'\n'<<"artifact="<<d.artifact<<'\n'<<"delivery="<<d.delivery<<'\n'<<"runtime="<<d.runtime<<'\n'<<"availability="<<d.availability<<'\n'<<"entrypoint="<<d.entrypoint<<'\n'<<"channel="<<d.channel<<'\n'<<"category="<<d.category<<'\n'<<"license="<<d.license<<'\n'<<"description="<<d.description<<'\n'<<"homepage="<<d.homepage<<'\n'<<"bytes="<<d.bytes<<'\n'<<"sha256="<<d.sha<<'\n';
    for (const auto& v : mirrors) o << "mirror=" << v << '\n';
    for (const auto& v : req) o << "requires=" << v << '\n';
    for (const auto& v : prov) o << "provides=" << v << '\n';
    return o.str();
}

std::vector<std::uint64_t> version_parts(std::string_view value) {
    std::vector<std::uint64_t> out; std::size_t pos=0;
    while (pos<value.size()) { auto end=value.find('.',pos); auto part=value.substr(pos,end==std::string_view::npos?value.size()-pos:end-pos); std::uint64_t n=0; bool any=false; for(unsigned char c:part){ if(!std::isdigit(c)) break; any=true;n=n*10U+(c-'0'); } out.push_back(any?n:0); if(end==std::string_view::npos) break; pos=end+1U; }
    return out;
}

bool version_less(const Descriptor& a,const Descriptor& b){auto x=version_parts(a.version),y=version_parts(b.version);x.resize(std::max(x.size(),y.size()));y.resize(x.size());return x==y?a.version<b.version:std::lexicographical_compare(x.begin(),x.end(),y.begin(),y.end());}

std::vector<Descriptor> load_directory(const fs::path& directory) {
    if (!fs::is_directory(directory)) throw Error("not a directory: "+directory.string());
    std::vector<fs::path> paths; for (const auto& e:fs::directory_iterator(directory)) if(e.is_regular_file()&&e.path().extension()==".zsource") paths.push_back(e.path()); std::sort(paths.begin(),paths.end());
    std::vector<Descriptor> records; std::set<std::string> identities;
    for(const auto&p:paths){auto d=parse_descriptor(p);validate_descriptor(d); if(!identities.insert(d.id+"@"+d.version).second)throw Error("duplicate identity: "+d.id+"@"+d.version);records.push_back(std::move(d));}
    if (records.empty()) throw Error("catalog has no .zsource descriptors");
    return records;
}

} // namespace zenuniverse
