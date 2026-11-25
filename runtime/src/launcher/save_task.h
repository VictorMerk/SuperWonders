#pragma once
#include "save_backup.h"
#include <atomic>
#include <future>
#include <optional>
#include <stdexcept>
namespace nx::launcher {
struct SaveResult { bool ok=false;std::string message; };
// One cooperative operation owns its captured profile paths until completion.
class SaveTask {
public:
    ~SaveTask(){cancel();if(future_.valid())future_.wait();}
    bool busy()const{return future_.valid();}
    void cancel(){cancelled_.store(true);}
    bool start(std::function<SaveResult(const SaveCheckpoint&)> operation,std::string& error){
        if(busy()){error="A save operation is already running.";return false;}
        cancelled_=false;error.clear();
        try{future_=std::async(std::launch::async,[this,operation=std::move(operation)] {
            try{return operation([this]{if(cancelled_.load())throw std::runtime_error("Cancelled by the player.");});}
            catch(const std::exception&exception){return SaveResult{false,exception.what()};}
            catch(...){return SaveResult{false,"Save operation failed unexpectedly."};}
        });return true;}
        catch(const std::exception&exception){error=exception.what();return false;}
    }
    std::optional<SaveResult> poll(){
        if(!busy()||future_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return {};
        return future_.get();
    }
private:
    std::atomic<bool> cancelled_{false};
    std::future<SaveResult> future_;
};
}
