#include "hle_binding_batch.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
namespace {
void old_handler(nx::CPU&) {}
void new_handler(nx::CPU&) {}
void check(bool value,const char* message) {if(!value){std::cerr<<message<<'\n';std::exit(1);}}
}
int main() {
    using namespace nx;
    std::unordered_map<std::string,HleFn> registry{{"a",old_handler},{"b",old_handler}};
    const auto original=registry;
    const HleBinding both[]={{"a",new_handler},{"b",new_handler}};
    check(!replace_hle_batch(registry,both,false,[](auto){return false;}),"late linking cannot change cached import behavior");
    check(!replace_hle_batch(registry,both,true,[](std::string_view name){return name=="b";}),"one cached alias rejects the entire replacement");
    check(registry==original,"earlier aliases remain unchanged when a later alias is cached");
    const HleBinding duplicate[]={{"a",new_handler},{"a",old_handler}};
    check(!replace_hle_batch(registry,duplicate,true,[](auto){return false;}),"duplicate alias cannot silently override the first binding");
    const HleBinding missing_handler[]={{"a",new_handler},{"c",nullptr}};
    check(!replace_hle_batch(registry,missing_handler,true,[](auto){return false;}),"missing backend handler rejects partial activation");
    try {replace_hle_batch(registry,both,true,[](auto)->bool{throw std::runtime_error("query failed");});}
    catch(const std::runtime_error&) {}
    check(registry==original,"exception before publication leaves all defaults intact");
    const HleBinding added[]={{"a",new_handler},{"b",new_handler},{"c",new_handler}};
    check(replace_hle_batch(registry,added,true,[](auto){return false;}),"validated pre-link transaction installs all aliases");
    check(registry.size()==3&&registry.at("a")==new_handler&&registry.at("b")==new_handler&&registry.at("c")==new_handler,"raw, typed and new aliases share the backend");
    std::cout<<"HLE batch: phase, cached thunk, duplicate, missing handler and publication checks passed\n";
}
