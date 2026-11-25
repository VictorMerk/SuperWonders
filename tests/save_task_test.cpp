#include "launcher/save_task.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
using namespace nx::launcher;
void check(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
int main(){
    SaveTask task;std::promise<void> entered,release;auto gate=release.get_future();std::string error;
    check(task.start([&](const SaveCheckpoint& checkpoint){entered.set_value();gate.wait();checkpoint();return SaveResult{true,"done"};},error),"save worker starts");
    entered.get_future().wait();check(task.busy()&&!task.poll(),"UI polling does not wait for a blocked save worker");
    check(!task.start([](const SaveCheckpoint&){return SaveResult{};},error),"a second operation cannot race the captured profile");
    task.cancel();release.set_value();std::optional<SaveResult> result;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(!(result=task.poll())&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(result&&!result->ok&&result->message.find("Cancelled")!=std::string::npos&&!task.busy(),"cancelled save returns an error and releases the UI lock");
    check(task.start([](const SaveCheckpoint& checkpoint){checkpoint();return SaveResult{true,"retry complete"};},error),"save operation can be retried after cancellation");
    result.reset();while(!(result=task.poll())&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(result&&result->ok,"old cancellation does not leak into a retry");std::cout<<"save task cancellation, nonblocking polls, isolation and retry passed\n";
}
