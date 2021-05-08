#include <list>
#include <mutex>
#include <utility>
#include <boost/interprocess/sync/interprocess_semaphore.hpp>

using semaphore = boost::interprocess::interprocess_semaphore;

namespace Lodestar{
    template <class listType>
    class ManagedList{
        public:
            ManagedList(): awaitSignal(0), waitingSignal(0), continueSignal(0){};

            int nSignals;
            semaphore awaitSignal;
            semaphore waitingSignal;
            semaphore continueSignal;

            std::list<listType> list;
            std::mutex listLock;

            virtual void deletionFunction(){
                    list.remove_if([](listType item){return !item.active;});
            }

            virtual bool deletionHeuristic() = 0;

            /**
             * Removes [list] entries that have a false active property with proper
             * signaling.
             * 
             * Will send [nSignals] signals via awaitSignal so that threads operating
             * on this list are notified to wait, then wait for them to notify they are
             * waiting; once it's done, deletes entries where the active attribute is false,
             * and sends [nSignals] signals via continueSignal to notify that deletion is done.
             * Does not prevent concurrent access of said list, only signals that a cleaning
             * will take place, so it's recommended to lock a mutex before executing this
             * function.
             * */
            void cleanList(){
                if(deletionHeuristic()){
                    listLock.lock();
                    for(int n = 0; n < nSignals; n++)
                        awaitSignal.post();
                    
                    for(int n = 0; n < nSignals; n++)
                        waitingSignal.wait();

                    deletionFunction();
                    
                    for(int n = 0; n < nSignals; n++)
                        continueSignal.post();
                    listLock.unlock();
                }
            }
    };
}
