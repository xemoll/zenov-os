#include "crypto.hpp"
#include "metadata.hpp"
#include "verification.hpp"

int main(int argc,char**argv){
    try{
        if(argc>=3&&std::string(argv[1])=="inspect"){Envelope e(read_all(argv[2]));std::cout<<"role="<<role_name(e.header->role)<<"\nexpires="<<e.header->expires<<"\nbody_bytes="<<e.header->body_size<<"\nsignatures="<<e.header->signature_count<<"\nsha256="<<hex(Sha256::hash(e.bytes).data(),32)<<"\n";return 0;}
        if(argc>=2&&std::string(argv[1])=="verify"){std::filesystem::path dir;std::filesystem::path state;std::uint64_t now=0;for(int i=2;i<argc;++i){const std::string a=argv[i];if(a=="--metadata"&&i+1<argc)dir=argv[++i];else if(a=="--time"&&i+1<argc)now=std::stoull(argv[++i]);else if(a=="--state"&&i+1<argc)state=argv[++i];else throw Error("unknown or incomplete option: "+a);}if(dir.empty()||!now)throw Error("--metadata and --time are required");auto verified=verify_set(dir,now);if(!state.empty()){const auto prior=read_state(state);enforce_state(prior,verified);write_state(state,verified);}std::cout<<"ZENREPO_VERIFY_OK trust=verified packages="<<verified.target_list.size()<<"\n";for(const auto&t:verified.target_list)std::cout<<"TARGET "<<text(t.path,sizeof(t.path))<<" "<<text(t.name,sizeof(t.name))<<"@"<<text(t.version,sizeof(t.version))<<" mask=0x"<<std::hex<<t.syscall_mask<<std::dec<<"\n";return 0;}
        usage();return 2;
    }catch(const std::exception&e){std::cerr<<"zenrepo: "<<e.what()<<"\n";return 1;}
}
