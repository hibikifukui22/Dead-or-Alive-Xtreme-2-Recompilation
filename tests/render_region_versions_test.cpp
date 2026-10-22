#include "../sdk/rexglue-sdk/include/rex/graphics/render_region_versions.h"
#include "../sdk/rexglue-sdk/include/rex/graphics/render_composition_plan.h"
#include <cstdlib>
#include <iostream>
#include <random>
#include <set>
#include <string>
using namespace rex::graphics::render_audit;
void Check(bool v) { if (!v) { std::cerr << "region version test failed\n"; std::exit(1); } }
int main(int argc, char** argv) {
  // Offline saved-log driver; no game/GPU dependencies or execution.
  if (argc == 4 && std::string(argv[1]) == "--stream") {
    RegionVersions map(std::stoul(argv[2]), std::stoul(argv[3]));
    std::vector<ProducerRegion> producers;
    char kind; uint64_t id; Rect rect;
    while (std::cin >> kind >> id >> rect.left >> rect.top >> rect.right >> rect.bottom) {
      if (kind == 'W') {
        if (!map.Write(rect,id)) return 2;
        producers.push_back({id, rect});
      }
      else if (kind == 'I') { map.Invalidate(); producers.clear(); }
      else if (kind == 'R') {
        const auto read=map.Read(rect);
        std::cout << "read=" << id << " complete=" << read.complete << " missing=" << read.missing_pixels << " versions=";
        for (size_t i=0;i<read.versions.size();++i) std::cout << (i ? "," : "") << read.versions[i];
        std::cout << '\n';
      } else if (kind == 'P') {
        const auto plan=BuildCompositionPlan(map,rect,producers);
        std::cout << "plan=" << id << " status=" << unsigned(plan.status)
                  << " copies=" << plan.copies.size();
        for (const auto& copy : plan.copies) {
          std::cout << " [version=" << copy.version << " source="
                    << copy.source.left << ',' << copy.source.top << ','
                    << copy.source.right << ',' << copy.source.bottom
                    << " destination=" << copy.destination.left << ','
                    << copy.destination.top << ',' << copy.destination.right
                    << ',' << copy.destination.bottom << ']';
        }
        std::cout << '\n';
      } else return 3;
    }
    return std::cin.eof() ? 0 : 4;
  }
  const Rect full{0,0,1280,720};
  RegionVersions map(1280,720);
  Check(!map.Read(full).complete);
  Check(map.Write({0,0,640,720},9));
  Check(map.Read(full).missing_pixels==460800);
  Check(map.Write({640,0,1280,720},11));
  Check(map.Read(full).complete && map.Read(full).versions==std::vector<uint64_t>({9,11}));
  // Dynamic DOAX2 frame 11160: two 640-wide tiled resolves provide distinct
  // producer versions at the next 1280-wide texture read. A later unknown
  // overlapping write must remove the candidate until full coverage returns.
  RegionVersions dynamic_pair(1280,720);
  Check(dynamic_pair.Write({0,0,640,720},9));
  Check(!dynamic_pair.Read(full).complete);
  Check(dynamic_pair.Write({640,0,1280,720},11));
  Check(dynamic_pair.Read(full).complete &&
        dynamic_pair.Read(full).versions==std::vector<uint64_t>({9,11}));
  dynamic_pair.Invalidate();
  Check(!dynamic_pair.Read(full).complete);
  Check(dynamic_pair.Write(full,19) &&
        dynamic_pair.Read(full).versions==std::vector<uint64_t>({19}));
  const auto saved=map;  // immutable checkpoint must survive later writes
  const ProducerRegion producers[]={{9,{0,0,640,720}},{11,{640,0,1280,720}}};
  auto plan=BuildCompositionPlan(map,full,producers);
  Check(plan.status==PlanStatus::kReadyMetadata && plan.copies.size()==2);
  Check(plan.copies[0].version==9 && plan.copies[0].destination.left==0);
  Check(plan.copies[1].version==11 && plan.copies[1].destination.left==640);
  Check(plan.copies[1].source.left==0 && plan.copies[1].source.right==640);
  Check(BuildCompositionPlan(map,full,{},2).status==PlanStatus::kMissingProducer);
  const ProducerRegion duplicate[]={{9,{0,0,640,720}},{9,{0,0,640,720}},producers[1]};
  Check(BuildCompositionPlan(map,full,duplicate).status==PlanStatus::kAmbiguousProducer);
  const ProducerRegion undersized[]={{9,{0,0,639,720}},producers[1]};
  Check(BuildCompositionPlan(map,full,undersized).status==PlanStatus::kInvalidProducerRegion);
  Check(BuildCompositionPlan(map,full,producers,1).status==PlanStatus::kTooManyCopies);
  auto crop=BuildCompositionPlan(map,{600,100,700,200},producers);
  Check(crop.status==PlanStatus::kReadyMetadata && crop.copies.size()==2);
  Check(crop.copies[0].source.left==600 && crop.copies[0].source.top==100);
  Check(crop.copies[1].source.left==0 && crop.copies[1].source.right==60);
  Check(map.Write(full,12));
  Check(map.Read(full).versions==std::vector<uint64_t>({12}));
  Check(BuildCompositionPlan(map,full,producers).status==PlanStatus::kMissingProducer);
  const ProducerRegion full_source[]={{12,full}};
  Check(BuildCompositionPlan(map,full,full_source).copies.size()==1);
  Check(saved.Read(full).versions==std::vector<uint64_t>({9,11}));
  Check(map.Write({100,100,200,200},13));
  Check(map.Read(full).versions==std::vector<uint64_t>({12,13}));
  Check(!map.Write({0,0,1281,720},14) && !map.Read(full).complete);
  Check(BuildCompositionPlan(map,full,full_source).status==PlanStatus::kIncompleteCoverage);
  RegionVersions bounded(4,4,1);
  Check(bounded.Write({0,0,4,4},1));
  Check(!bounded.Write({1,1,3,3},2) && !bounded.Read({0,0,4,4}).complete);
  RegionVersions random_map(16,16,4096);
  uint64_t pixels[16][16]{};
  std::mt19937 rng(0xD0A2);
  for (uint64_t version=1;version<=300;++version) {
    uint32_t x=rng()%16,y=rng()%16;
    Rect r{x,y,x+1+rng()%(16-x),y+1+rng()%(16-y)};
    Check(random_map.Write(r,version));
    for (uint32_t row=r.top;row<r.bottom;++row) for(uint32_t col=r.left;col<r.right;++col) pixels[row][col]=version;
    for(uint32_t row=0;row<16;++row) for(uint32_t col=0;col<16;++col) {
      auto read=random_map.Read({col,row,col+1,row+1});
      Check(read.complete==(pixels[row][col]!=0));
      if (read.complete) Check(read.versions==std::vector<uint64_t>({pixels[row][col]}));
    }
  }
  std::cout << "region version tests passed\n";
}
