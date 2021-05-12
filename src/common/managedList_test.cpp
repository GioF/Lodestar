#include <future>
#include "doctest.h"
#include "managedList.cpp"
#include "boost/date_time/posix_time/posix_time_types.hpp"

using namespace Lodestar;
using namespace std::chrono_literals;

namespace Lodestar{
    struct dummyEntry{
        bool active = false;
        
        dummyEntry(const dummyEntry& copied){
            active = copied.active;
        }
        
        dummyEntry(){}
    };
    
    struct test_managed: public ManagedList<dummyEntry>{
            void manage(){ std::this_thread::sleep_for(50ms); }
            bool deletionHeuristic() { return true; }
            
            int heuristic = 0;
            int threadHeuristic(){
                return heuristic;
            }
            
            test_managed(const test_managed& copied){}
            
            test_managed(){}
            using ManagedList::iterate;
            using ManagedList::cleanList;
            using ManagedList::oversee;
            using ManagedList::threadLock;
            using ManagedList::stopSignal;
            using ManagedList::continueSignal;
            using ManagedList::awaitSignal;
            using ManagedList::waitingSignal;
    };
}

TEST_CASE("ManagedList"){
    test_managed mockObj;
    
    SUBCASE("iterate() - thread sincronization"){
        auto iteratingThread = std::async(&test_managed::iterate, &mockObj);
        std::this_thread::sleep_for(500ms);
        
        REQUIRE(mockObj.nThreads == 1);
        
        SUBCASE("normal stopping"){
            mockObj.stopSignal.post();
            
            REQUIRE(iteratingThread.wait_for(std::chrono::milliseconds(1500)) == std::future_status::ready);
            REQUIRE(mockObj.nThreads == 0);
        }
        
        SUBCASE("threadLock taken"){
            auto waitTime = boost::posix_time::second_clock::universal_time();
            waitTime += boost::posix_time::millisec(200);
            
            //tell thread to stop with lock taken
            mockObj.threadLock.lock();
            mockObj.stopSignal.post();
            
            //check if proper signals were sent
            mockObj.awaitSignal.post();
            mockObj.continueSignal.post();

            //sleep a bit to give thread time to do all its stuff
            std::this_thread::sleep_for(200ms);
            REQUIRE(mockObj.waitingSignal.timed_wait(waitTime));

            //unlock and sleep again
            mockObj.threadLock.unlock();
            std::this_thread::sleep_for(150ms);
            REQUIRE(mockObj.nThreads == 0);
        }
    }
    
    SUBCASE("cleanList() - entry deletion and sincronization"){
        //setting up mock object
        mockObj.nThreads = 1;
        mockObj.waitingSignal.post();
        
        //set up dummy entry
        dummyEntry dummy;
        mockObj.list.push_back(dummy);
        mockObj.list.push_back(dummy);
        REQUIRE(mockObj.list.size() == 2);
        
        //cleanList call
        mockObj.cleanList();
        
        //check that entries were deleted and
        //only 1 of each signal was sent
        REQUIRE(mockObj.list.size() == 0);
        REQUIRE(mockObj.awaitSignal.try_wait());
        REQUIRE(!mockObj.awaitSignal.try_wait());
        REQUIRE(mockObj.continueSignal.try_wait());
        REQUIRE(!mockObj.continueSignal.try_wait());
    }
    
    SUBCASE("oversee() - proper scaling"){
        //so the destructor cleans the list
        mockObj.isAsync = true;
        //so at max 3 threads can be created
        mockObj.maxThreads = 3;

        //create 3 threads
        mockObj.heuristic = 3;
        mockObj.oversee();

        std::this_thread::sleep_for(100ms);
        REQUIRE(mockObj.nThreads == 3);

        //check that no new threads were created
        mockObj.oversee();

        std::this_thread::sleep_for(100ms);
        REQUIRE(mockObj.nThreads == 3);

        //remove 3 threads
        mockObj.heuristic = 0;
        mockObj.oversee();

        std::this_thread::sleep_for(100ms);
        REQUIRE(mockObj.nThreads == 0);
        REQUIRE(!mockObj.stopSignal.try_wait());
    }
};
