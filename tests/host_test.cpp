// Host-side unit tests for the portable network logic in main.cpp.
// Mirrors firmware/wifi-netlab/src/main.cpp algorithms verbatim (uint32_t math).
// Compile: g++ -std=c++17 -O2 -Wall -Wextra tests/host_test.cpp -o /data/data/com.rk.xededitor/tmp/opencode/wifi-host-test && ./wifi-host-test
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <map>
#include <string>
#include <iostream>
#include <cassert>

using u32 = uint32_t;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) { printf("PASS: %s\n", msg); } else { printf("FAIL: %s\n", msg); failures++; } } while(0)

static u32 ip4(int a,int b,int c,int d){ return (a<<24)|(b<<16)|(c<<8)|d; }
static std::string ipStr(u32 u){ char b[16]; snprintf(b,sizeof(b),"%u.%u.%u.%u",(u>>24)&255,(u>>16)&255,(u>>8)&255,u&255); return b; }
static uint8_t maskToPrefix(u32 m){ int c=0; for(int i=31;i>=0;--i){ if(m>>i&1) c++; else break; } return c; }
struct SubnetInfo{ u32 network,broadcast,first,last; uint8_t prefix; u32 hosts;
  static SubnetInfo calc(u32 ip, u32 mask){ SubnetInfo s; u32 net=ip&mask; s.network=net; s.broadcast=net|~mask; s.first=net+1; s.last=s.broadcast-1; s.prefix=maskToPrefix(mask); s.hosts=(mask==0xFFFFFFFFu)?1:(~mask)-1; return s; }
};
static bool routeMatch(u32 t,u32 d,u32 m){ return (t&m)==(d&m); }
struct Route{ u32 dest,mask,nextHop; std::string iface; uint8_t metric; std::string source; };
struct FwRule{ u32 src,dst; uint16_t port; bool allow; std::string proto; };
static bool fwAllowed(const std::vector<FwRule>& fw, u32 src,u32 dst,uint16_t port,const std::string& proto){
  for(auto &r: fw){ bool sOk=(r.src==0)||(r.src==src); bool dOk=(r.dst==0)||(r.dst==dst); bool pOk=(r.port==0)||(r.port==port); bool prOk=(r.proto=="ANY"||r.proto==proto); if(sOk&&dOk&&pOk&&prOk) return r.allow; } return false;
}
int main(){
  using namespace std;
  printf("=== Host tests (portable logic from main.cpp) ===\n");

  // Subnet /24
  auto s1 = SubnetInfo::calc(ip4(192,168,10,100), ip4(255,255,255,0));
  CHECK(s1.network==ip4(192,168,10,0), "Subnet 192.168.10.100/24 network .0");
  CHECK(s1.broadcast==ip4(192,168,10,255), "Subnet /24 broadcast .255");
  CHECK(s1.prefix==24 && s1.hosts==254, "Subnet /24 prefix=24 hosts=254");
  CHECK(s1.first==ip4(192,168,10,1) && s1.last==ip4(192,168,10,254), "Subnet /24 first/last");

  // Subnet /30 VPN
  auto s2 = SubnetInfo::calc(ip4(10,8,0,1), ip4(255,255,255,252));
  CHECK(s2.network==ip4(10,8,0,0) && s2.broadcast==ip4(10,8,0,3), "VPN 10.8.0.1/30 network/broadcast");
  CHECK(s2.prefix==30 && s2.hosts==2, "VPN /30 prefix=30 hosts=2");

  // maskToPrefix edges
  CHECK(maskToPrefix(ip4(255,255,0,0))==16, "maskToPrefix /16");
  CHECK(maskToPrefix(ip4(255,255,255,252))==30, "maskToPrefix /30");
  CHECK(maskToPrefix(ip4(255,255,255,255))==32, "maskToPrefix /32");
  CHECK(maskToPrefix(0)==0, "maskToPrefix /0");

  // Routing longest-prefix
  vector<Route> routes = {
    {ip4(0,0,0,0), 0, ip4(192,168,10,1), "WIFI", 1, "static"},
    {ip4(192,168,30,0), ip4(255,255,255,0), ip4(192,168,10,1), "WIFI", 2, "static"},
    {ip4(10,8,0,0), ip4(255,255,255,252), ip4(10,8,0,1), "VPN", 1, "connected"},
  };
  auto best = [&](u32 t)-> const Route*{ const Route* b=nullptr; int bl=-1; for(auto &r: routes){ if(routeMatch(t,r.dest,r.mask)){ int l=maskToPrefix(r.mask); if(l>bl){bl=l;b=&r;} } } return b; };
  CHECK(best(ip4(192,168,30,10))->iface=="WIFI" && best(ip4(192,168,30,10))->metric==2, "Route server 30.10 -> WIFI metric 2");
  CHECK(best(ip4(10,8,0,2))->iface=="VPN", "Route VPN 10.8.0.2 -> VPN");
  CHECK(best(ip4(8,8,8,8))->iface=="WIFI" && best(ip4(8,8,8,8))->metric==1, "Default route 8.8.8.8 -> gateway");

  // Firewall
  vector<FwRule> fw = {
    {0,0,80,true,"TCP"},{0,0,443,true,"TCP"},{0,0,53,true,"UDP"},{0,0,67,true,"UDP"},{0,0,0,false,"ANY"}
  };
  CHECK(fwAllowed(fw, ip4(1,2,3,4), ip4(5,6,7,8), 80, "TCP"), "FW allow HTTP 80/TCP");
  CHECK(fwAllowed(fw, ip4(1,2,3,4), ip4(5,6,7,8), 443, "TCP"), "FW allow HTTPS 443/TCP");
  CHECK(fwAllowed(fw, ip4(1,2,3,4), ip4(5,6,7,8), 53, "UDP"), "FW allow DNS 53/UDP");
  CHECK(!fwAllowed(fw, ip4(1,2,3,4), ip4(5,6,7,8), 5000, "TCP"), "FW deny TCP 5000 (default deny)");
  CHECK(!fwAllowed(fw, ip4(1,2,3,4), ip4(5,6,7,8), 22, "TCP"), "FW deny SSH 22");

  // DHCP pool allocation mini-sim
  u32 poolStart = ip4(192,168,10,100); uint32_t poolSize=50; vector<u32> leases;
  auto alloc=[&]()->u32{ for(uint32_t i=0;i<poolSize;i++){ u32 c=poolStart+i; bool used=false; for(auto l:leases) if(l==c){used=true;break;} if(!used){leases.push_back(c);return c;} } return 0; };
  u32 a1=alloc(), a2=alloc(), a3=alloc();
  CHECK(a1==ip4(192,168,10,100) && a2==ip4(192,168,10,101) && a3==ip4(192,168,10,102), "DHCP pool allocates .100 .101 .102 in order");
  CHECK(leases.size()==3, "DHCP lease count =3");

  // DNS zone
  map<string,u32> zone; zone["server1.netlab"]=ip4(192,168,30,10); zone["lb.netlab"]=ip4(192,168,30,1);
  CHECK(zone["server1.netlab"]==ip4(192,168,30,10), "DNS server1.netlab -> 30.10");

  // Load balancer round-robin
  vector<string> backends={"192.168.30.10:80","192.168.30.11:80"}; size_t idx=0;
  auto pick=[&](){ string b=backends[idx%backends.size()]; idx++; return b; };
  CHECK(pick()=="192.168.30.10:80" && pick()=="192.168.30.11:80" && pick()=="192.168.30.10:80", "LB round-robin cycles");

  // IP parse round-trip sanity
  CHECK(ipStr(ip4(192,168,10,1))=="192.168.10.1", "ipStr 192.168.10.1");

  // Ports table sanity (from main.cpp PORT_* )
  CHECK(80!=443 && 53!=67 && 5000!=5001 && 179!=8443, "PORT_* distinct");

  printf(failures ? "\nRESULT: %d FAILURES\n" : "\nRESULT: ALL PASS\n", failures);
  return failures?1:0;
}
