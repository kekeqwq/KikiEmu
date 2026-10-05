// SPDX-License-Identifier: GPL-2.0-or-later
#include "ab.hpp"
#include "boot.hpp"
#include "process.hpp"
#include <fstream>
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace kiki {
namespace {
uint32_t word(const unsigned char* p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
void seal(std::array<unsigned char,32>& c){auto v=crc32(c.data(),28);for(int n=0;n<4;++n)c[28+n]=v>>(8*n);}
void validate(const std::array<unsigned char,32>& c){
 if(word(c.data()+4)!=0x42414342||c[8]!=1||c[9]!=2||c[10]!=0||c[11]!=0||word(c.data()+28)!=crc32(c.data(),28))throw std::runtime_error("Native A/B boot-control identity/CRC invalid; refusing to guess or format userdata.");
 if((c[0]!='_'|| (c[1]!='a'&&c[1]!='b'))||c[2]||c[3])throw std::runtime_error("Native A/B suffix invalid.");
 for(int n=16;n<28;++n)if(c[n])throw std::runtime_error("Unknown A/B reserved data.");
 if(c[13]>1||c[15]>1)throw std::runtime_error("Unknown A/B slot flags.");
}
const nlohmann::json& partition(const nlohmann::json& layout,const std::string& name){for(const auto& p:layout.at("partitions"))if(p.at("name")==name)return p;throw std::runtime_error("Native A/B partition missing.");}
void tool(const nlohmann::json& b,const fs::path& cwd,const std::vector<std::wstring>& a,const wchar_t* exe=L"qemu-img.exe"){
 verify_qemu_binding(b);auto r=image_tool(fs::path(utf16(b.at("binDirectory").get<std::string>()))/exe,cwd,a);if(r.exitCode)throw std::runtime_error("Owned A/B image tool failed: "+r.output);
}
void extract(const nlohmann::json& b,const fs::path& d,const fs::path& out,uint64_t offset,uint64_t bytes){
 if(fs::exists(out)||offset%512||bytes%512)throw std::runtime_error("A/B extraction requires new aligned file.");
 tool(b,d,{L"dd",L"-f",L"qcow2",L"-O",L"raw",L"if=phone.qcow2",L"of="+out.wstring(),L"bs=512",L"skip="+std::to_wstring(offset/512),L"count="+std::to_wstring((offset+bytes)/512)});
 if(fs::file_size(out)!=bytes)throw std::runtime_error("A/B bounded read length mismatch.");
}
std::array<unsigned char,512> read_control(const nlohmann::json& b,const fs::path& d,const nlohmann::json& l,const fs::path& scratch){
 extract(b,d,scratch,partition(l,"misc").at("offsetBytes").get<uint64_t>()+2048,512);
 std::array<unsigned char,512> bytes{};std::ifstream in(scratch,std::ios::binary);if(!in.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("A/B control truncated.");in.close();fs::remove(scratch);return bytes;
}
void write_control(const nlohmann::json& b,const fs::path& d,const nlohmann::json& l,const fs::path& file,const std::array<unsigned char,512>& bytes){
 if(fs::exists(file))throw std::runtime_error("A/B control write file exists.");std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());out.close();if(!out)throw std::runtime_error("Could not stage A/B control sector.");
 auto off=partition(l,"misc").at("offsetBytes").get<uint64_t>()+2048;
 tool(b,d,{L"-f",L"qcow2",L"-c",L"write -q -s \""+fs::relative(file,d).generic_wstring()+L"\" "+std::to_wstring(off)+L" 512",L"phone.qcow2"},L"qemu-io.exe");
 auto actual=read_control(b,d,l,file.parent_path()/"bcb-readback.bin");if(actual!=bytes)throw std::runtime_error("A/B control write verification failed.");fs::remove(file);
}
}
int select_ab_slot(std::array<unsigned char,32>& c){
 validate(c);int selected=-1;unsigned priority=0;
 for(int n=0;n<2;++n){auto flags=c[12+2*n];unsigned p=flags&15;auto tries=(flags>>4)&7;bool success=flags&128;
  if(p&&!(c[13+2*n]&1)&&(success||tries)&&p>priority){selected=n;priority=p;}}
 if(selected<0)throw std::runtime_error("No bootable native A/B slot; retain disk/data for recovery.");
 auto& flags=c[12+2*selected];if(!(flags&128))flags-=16;
 c[0]='_';c[1]='a'+selected;c[2]=c[3]=0;seal(c);return selected;
}
nlohmann::json prepare_ab_boot(const nlohmann::json& b,const fs::path& d,const nlohmann::json& l,const fs::path& log){
 if(l.at("layoutVersion")!="gpt-ab-v1")return nlohmann::json::object();
 auto stage=log/"ab-boot";if(fs::exists(stage)||!fs::create_directory(stage))throw std::runtime_error("A/B boot staging must be new.");
 for(int attempt=0;attempt<2;++attempt){
  auto trial=stage/utf16("trial-"+std::to_string(attempt));if(!fs::create_directory(trial))throw std::runtime_error("A/B trial directory exists.");
  auto sector=read_control(b,d,l,trial/"bcb.bin");std::array<unsigned char,32> c{};std::copy_n(sector.begin(),32,c.begin());int slot=select_ab_slot(c);
  auto& p=partition(l,slot==0?"boot_a":"boot_b");auto off=p.at("offsetBytes").get<uint64_t>();
  extract(b,d,trial/"header.bin",off,4096);std::array<unsigned char,4096> header{};std::ifstream h(trial/"header.bin",std::ios::binary);h.read(reinterpret_cast<char*>(header.data()),header.size());h.close();
  auto align=[](uint64_t v){return (v+4095)/4096*4096;};uint64_t bytes=4096+align(word(header.data()+8))+align(word(header.data()+12));
  try{
   if(bytes>p.at("lengthBytes").get<uint64_t>())throw std::runtime_error("Native slot boot image exceeds capacity.");parse_boot_header(header,bytes);
  }catch(const std::exception& e){std::ofstream error(trial/"invalid-boot.txt");error<<e.what();error.close();fail_ab_slot(b,d,l,slot,trial);continue;}
  extract(b,d,trial/"installed-boot.img",off,bytes);
  try{validate_boot_payload(trial/"installed-boot.img");}catch(const std::exception& e){std::ofstream error(trial/"invalid-image.txt");error<<e.what();error.close();fail_ab_slot(b,d,l,slot,trial);continue;}
  auto cache=derive_boot_cache(trial/"installed-boot.img",stage/"boot");
  std::copy(c.begin(),c.end(),sector.begin());write_control(b,d,l,trial/"bcb-write.bin",sector);fs::remove(trial/"installed-boot.img");
  nlohmann::json result={{"slot",slot},{"suffix",slot==0?"_a":"_b"},{"bootUuid",p.at("uuid")},{"cache",cache},{"directory",utf8(stage.wstring())}};
  std::ofstream out(stage/"selection.json");out<<result.dump(2)<<'\n';if(!out)throw std::runtime_error("Could not record A/B boot selection.");return result;
 }
 throw std::runtime_error("Both native slot boot images invalid; disk/data retained for recovery.");
}
void fail_ab_slot(const nlohmann::json& b,const fs::path& d,const nlohmann::json& l,int slot,const fs::path& log){
 if(slot<0||slot>1)throw std::runtime_error("Invalid failed slot.");auto sector=read_control(b,d,l,log/"failed-bcb-read.bin");std::array<unsigned char,32> c{};std::copy_n(sector.begin(),32,c.begin());validate(c);
 c[12+2*slot]=0;c[13+2*slot]=0;seal(c);std::copy(c.begin(),c.end(),sector.begin());write_control(b,d,l,log/"failed-bcb-write.bin",sector);
}
}
